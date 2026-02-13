const cp = require("child_process");
const path = require("path");
const vscode = require("vscode");

class MtcLspClient {
  constructor(context) {
    this.context = context;
    this.process = null;
    this.nextId = 1;
    this.pending = new Map();
    this.buffer = Buffer.alloc(0);
    this.output = vscode.window.createOutputChannel("MTC Language Server");
    this.diagnostics = vscode.languages.createDiagnosticCollection("mtc");
    this.pendingChanges = new Map();
    this.changeTimers = new Map();
    context.subscriptions.push(this.output, this.diagnostics);
  }

  resolveServerPath() {
    const configured = vscode.workspace
      .getConfiguration("mtc")
      .get("languageServer.path", "dist/mtc_lsp");

    if (path.isAbsolute(configured)) {
      return configured;
    }

    const workspaceFolders = vscode.workspace.workspaceFolders || [];
    if (workspaceFolders.length > 0) {
      return path.join(workspaceFolders[0].uri.fsPath, configured);
    }

    return path.resolve(__dirname, "..", configured);
  }

  start() {
    const serverPath = this.resolveServerPath();
    this.output.appendLine(`Starting mtc LSP server: ${serverPath}`);

    this.process = cp.spawn(serverPath, [], { stdio: ["pipe", "pipe", "pipe"] });

    this.process.on("error", (err) => {
      this.output.appendLine(`Failed to start server: ${err.message}`);
      vscode.window.showErrorMessage(
        `MTC language server failed to start: ${err.message}`
      );
    });

    this.process.on("exit", (code, signal) => {
      this.output.appendLine(`Server exited (code=${code}, signal=${signal || "none"})`);
      for (const [, pending] of this.pending) {
        pending.resolve(null);
      }
      this.pending.clear();
      this.process = null;
    });

    if (this.process.stderr) {
      this.process.stderr.on("data", (chunk) => {
        this.output.append(chunk.toString("utf8"));
      });
    }
    if (this.process.stdout) {
      this.process.stdout.on("data", (chunk) => this.handleStdout(chunk));
    }

    const rootUri =
      vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders.length > 0
        ? vscode.workspace.workspaceFolders[0].uri.toString()
        : null;

    this.request("initialize", {
      processId: process.pid,
      rootUri,
      capabilities: {},
    }).then(() => {
      this.notify("initialized", {});
      this.pushOpenDocuments();
    });

    this.context.subscriptions.push(
      vscode.workspace.onDidOpenTextDocument((doc) => this.onDidOpen(doc)),
      vscode.workspace.onDidChangeTextDocument((evt) => this.onDidChange(evt)),
      vscode.workspace.onDidSaveTextDocument((doc) => this.onDidSave(doc)),
      vscode.workspace.onDidCloseTextDocument((doc) => this.onDidClose(doc)),
      vscode.languages.registerCompletionItemProvider(
        { language: "mtc" },
        {
          provideCompletionItems: async (document, position) => {
            this.flushDocumentChanges(document.uri.toString());
            const result = await this.request("textDocument/completion", {
              textDocument: { uri: document.uri.toString() },
              position: { line: position.line, character: position.character },
            });
            return this.toCompletionItems(result);
          },
        },
        "."
      ),
      vscode.languages.registerHoverProvider({ language: "mtc" }, {
        provideHover: async (document, position) => {
          this.flushDocumentChanges(document.uri.toString());
          const result = await this.request("textDocument/hover", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character },
          });
          return this.toHover(result);
        },
      }),
      vscode.languages.registerDefinitionProvider({ language: "mtc" }, {
        provideDefinition: async (document, position) => {
          this.flushDocumentChanges(document.uri.toString());
          const result = await this.request("textDocument/definition", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character },
          });
          return this.toDefinitions(result);
        },
      }),
      vscode.languages.registerReferenceProvider({ language: "mtc" }, {
        provideReferences: async (document, position, context) => {
          this.flushDocumentChanges(document.uri.toString());
          const result = await this.request("textDocument/references", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character },
            context: { includeDeclaration: !!context.includeDeclaration },
          });
          return this.toLocations(result);
        },
      }),
      vscode.languages.registerRenameProvider({ language: "mtc" }, {
        provideRenameEdits: async (document, position, newName) => {
          this.flushDocumentChanges(document.uri.toString());
          const result = await this.request("textDocument/rename", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character },
            newName,
          });
          return this.toWorkspaceEdit(result);
        },
        prepareRename: async (document, position) => {
          this.flushDocumentChanges(document.uri.toString());
          const result = await this.request("textDocument/prepareRename", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character },
          });
          if (!result) {
            return null;
          }
          const range = this.toRange(result.range);
          if (!range) {
            return null;
          }
          if (typeof result.placeholder === "string") {
            return { range, placeholder: result.placeholder };
          }
          return range;
        },
      }),
      vscode.languages.registerDocumentSymbolProvider({ language: "mtc" }, {
        provideDocumentSymbols: async (document) => {
          this.flushDocumentChanges(document.uri.toString());
          const result = await this.request("textDocument/documentSymbol", {
            textDocument: { uri: document.uri.toString() },
          });
          return this.toSymbolInformation(result);
        },
      }),
      vscode.languages.registerWorkspaceSymbolProvider({
        provideWorkspaceSymbols: async (query) => {
          this.flushAllChanges();
          const result = await this.request("workspace/symbol", { query: query || "" });
          return this.toSymbolInformation(result);
        },
      })
    );
  }

  stop() {
    this.flushAllChanges();
    for (const [, timer] of this.changeTimers) {
      clearTimeout(timer);
    }
    this.changeTimers.clear();
    this.pendingChanges.clear();

    if (!this.process) {
      return;
    }
    this.notify("shutdown", {});
    this.notify("exit", {});
    this.process.kill();
    this.process = null;
  }

  pushOpenDocuments() {
    for (const doc of vscode.workspace.textDocuments) {
      this.onDidOpen(doc);
    }
  }

  isMtcDocument(doc) {
    return doc && doc.languageId === "mtc";
  }

  onDidOpen(doc) {
    if (!this.isMtcDocument(doc)) {
      return;
    }
    this.notify("textDocument/didOpen", {
      textDocument: {
        uri: doc.uri.toString(),
        languageId: "mtc",
        version: doc.version,
        text: doc.getText(),
      },
    });
  }

  onDidChange(evt) {
    if (!this.isMtcDocument(evt.document)) {
      return;
    }

    const uri = evt.document.uri.toString();
    const existing = this.pendingChanges.get(uri) || {
      version: evt.document.version,
      changes: [],
    };
    existing.version = evt.document.version;

    for (const change of evt.contentChanges) {
      const out = { text: change.text };
      if (change.range) {
        out.range = {
          start: {
            line: change.range.start.line,
            character: change.range.start.character,
          },
          end: {
            line: change.range.end.line,
            character: change.range.end.character,
          },
        };
      }
      if (typeof change.rangeLength === "number") {
        out.rangeLength = change.rangeLength;
      }
      existing.changes.push(out);
    }
    this.pendingChanges.set(uri, existing);

    const oldTimer = this.changeTimers.get(uri);
    if (oldTimer) {
      clearTimeout(oldTimer);
    }
    const timer = setTimeout(() => this.flushDocumentChanges(uri), 120);
    this.changeTimers.set(uri, timer);
  }

  flushDocumentChanges(uri) {
    const pending = this.pendingChanges.get(uri);
    this.pendingChanges.delete(uri);

    const timer = this.changeTimers.get(uri);
    if (timer) {
      clearTimeout(timer);
      this.changeTimers.delete(uri);
    }

    if (!pending || !pending.changes || pending.changes.length === 0) {
      return;
    }

    this.notify("textDocument/didChange", {
      textDocument: {
        uri,
        version: pending.version,
      },
      contentChanges: pending.changes,
    });
  }

  flushAllChanges() {
    for (const uri of Array.from(this.pendingChanges.keys())) {
      this.flushDocumentChanges(uri);
    }
  }

  onDidSave(doc) {
    if (!this.isMtcDocument(doc)) {
      return;
    }
    this.flushDocumentChanges(doc.uri.toString());
    this.notify("textDocument/didSave", {
      textDocument: { uri: doc.uri.toString() },
    });
  }

  onDidClose(doc) {
    if (!this.isMtcDocument(doc)) {
      return;
    }
    const uri = doc.uri.toString();
    this.flushDocumentChanges(uri);
    this.pendingChanges.delete(uri);
    const timer = this.changeTimers.get(uri);
    if (timer) {
      clearTimeout(timer);
      this.changeTimers.delete(uri);
    }
    this.notify("textDocument/didClose", {
      textDocument: { uri },
    });
    this.diagnostics.delete(doc.uri);
  }

  toCompletionItems(result) {
    if (!result) {
      return [];
    }
    const rawItems = Array.isArray(result)
      ? result
      : Array.isArray(result.items)
      ? result.items
      : [];

    return rawItems.map((item) => {
      const completion = new vscode.CompletionItem(
        item.label || "",
        this.mapCompletionKind(item.kind)
      );
      if (item.detail) {
        completion.detail = item.detail;
      }
      return completion;
    });
  }

  toHover(result) {
    if (!result || !result.contents) {
      return null;
    }

    let text = null;
    if (typeof result.contents === "string") {
      text = result.contents;
    } else if (Array.isArray(result.contents) && result.contents.length > 0) {
      const first = result.contents[0];
      if (typeof first === "string") {
        text = first;
      } else if (first && typeof first.value === "string") {
        text = first.value;
      }
    } else if (result.contents && typeof result.contents.value === "string") {
      text = result.contents.value;
    }
    return text ? new vscode.Hover(text) : null;
  }

  toDefinitions(result) {
    if (!result) {
      return [];
    }
    const locations = Array.isArray(result) ? result : [result];
    const out = [];
    for (const loc of locations) {
      if (!loc || !loc.uri || !loc.range || !loc.range.start || !loc.range.end) {
        continue;
      }
      const uri = vscode.Uri.parse(loc.uri);
      const start = new vscode.Position(
        loc.range.start.line || 0,
        loc.range.start.character || 0
      );
      const end = new vscode.Position(
        loc.range.end.line || start.line,
        loc.range.end.character || start.character
      );
      out.push(new vscode.Location(uri, new vscode.Range(start, end)));
    }
    return out;
  }

  toLocations(result) {
    if (!result) {
      return [];
    }
    const locations = Array.isArray(result) ? result : [result];
    const out = [];
    for (const loc of locations) {
      if (!loc || !loc.uri || !loc.range) {
        continue;
      }
      const range = this.toRange(loc.range);
      if (!range) {
        continue;
      }
      out.push(new vscode.Location(vscode.Uri.parse(loc.uri), range));
    }
    return out;
  }

  toRange(rawRange) {
    if (!rawRange || !rawRange.start || !rawRange.end) {
      return null;
    }
    const start = new vscode.Position(
      rawRange.start.line || 0,
      rawRange.start.character || 0
    );
    const end = new vscode.Position(
      rawRange.end.line || start.line,
      rawRange.end.character || start.character
    );
    return new vscode.Range(start, end);
  }

  toWorkspaceEdit(result) {
    if (!result || !result.changes) {
      return null;
    }
    const edit = new vscode.WorkspaceEdit();
    for (const [uri, edits] of Object.entries(result.changes)) {
      const targetUri = vscode.Uri.parse(uri);
      for (const textEdit of edits || []) {
        const range = this.toRange(textEdit.range);
        if (!range) {
          continue;
        }
        edit.replace(targetUri, range, textEdit.newText || "");
      }
    }
    return edit;
  }

  toSymbolInformation(result) {
    if (!result) {
      return [];
    }
    const symbols = Array.isArray(result) ? result : [];
    const out = [];
    for (const sym of symbols) {
      if (!sym || !sym.name || !sym.location || !sym.location.uri || !sym.location.range) {
        continue;
      }
      const range = this.toRange(sym.location.range);
      if (!range) {
        continue;
      }
      const location = new vscode.Location(vscode.Uri.parse(sym.location.uri), range);
      out.push(
        new vscode.SymbolInformation(
          sym.name,
          this.mapSymbolKind(sym.kind),
          sym.containerName || "",
          location
        )
      );
    }
    return out;
  }

  mapSymbolKind(kind) {
    switch (kind) {
      case 5:
        return vscode.SymbolKind.Class;
      case 6:
        return vscode.SymbolKind.Method;
      case 8:
        return vscode.SymbolKind.Field;
      case 12:
        return vscode.SymbolKind.Function;
      case 13:
        return vscode.SymbolKind.Variable;
      default:
        return vscode.SymbolKind.Variable;
    }
  }

  mapCompletionKind(kind) {
    switch (kind) {
      case 3:
        return vscode.CompletionItemKind.Function;
      case 6:
        return vscode.CompletionItemKind.Variable;
      case 7:
        return vscode.CompletionItemKind.Class;
      case 14:
        return vscode.CompletionItemKind.Keyword;
      default:
        return vscode.CompletionItemKind.Text;
    }
  }

  handleStdout(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);

    while (true) {
      const headerEnd = this.buffer.indexOf("\r\n\r\n");
      if (headerEnd < 0) {
        return;
      }

      const headerText = this.buffer.slice(0, headerEnd).toString("utf8");
      const headers = headerText.split("\r\n");
      let contentLength = -1;
      for (const line of headers) {
        const idx = line.indexOf(":");
        if (idx < 0) {
          continue;
        }
        const key = line.slice(0, idx).trim().toLowerCase();
        const value = line.slice(idx + 1).trim();
        if (key === "content-length") {
          contentLength = Number(value);
        }
      }

      if (!Number.isFinite(contentLength) || contentLength < 0) {
        this.output.appendLine("Invalid LSP header (missing content-length)");
        this.buffer = Buffer.alloc(0);
        return;
      }

      const totalLen = headerEnd + 4 + contentLength;
      if (this.buffer.length < totalLen) {
        return;
      }

      const body = this.buffer.slice(headerEnd + 4, totalLen).toString("utf8");
      this.buffer = this.buffer.slice(totalLen);
      this.handleServerMessage(body);
    }
  }

  handleServerMessage(body) {
    let msg;
    try {
      msg = JSON.parse(body);
    } catch (err) {
      this.output.appendLine(`Invalid server JSON: ${err.message}`);
      return;
    }

    if (Object.prototype.hasOwnProperty.call(msg, "id")) {
      const pending = this.pending.get(msg.id);
      if (pending) {
        this.pending.delete(msg.id);
        pending.resolve(msg.error ? null : msg.result);
      }
      return;
    }

    if (msg.method === "textDocument/publishDiagnostics" && msg.params) {
      this.applyDiagnostics(msg.params);
    }
  }

  applyDiagnostics(params) {
    if (!params || !params.uri) {
      return;
    }
    const uri = vscode.Uri.parse(params.uri);
    const diagnostics = (params.diagnostics || []).map((diag) => {
      const start = new vscode.Position(
        diag.range?.start?.line || 0,
        diag.range?.start?.character || 0
      );
      const end = new vscode.Position(
        diag.range?.end?.line || start.line,
        diag.range?.end?.character || start.character + 1
      );
      const range = new vscode.Range(start, end);
      const severity = this.mapDiagnosticSeverity(diag.severity);
      const out = new vscode.Diagnostic(range, diag.message || "Error", severity);
      out.source = diag.source || "mtc";
      return out;
    });
    this.diagnostics.set(uri, diagnostics);
  }

  mapDiagnosticSeverity(severity) {
    switch (severity) {
      case 1:
        return vscode.DiagnosticSeverity.Error;
      case 2:
        return vscode.DiagnosticSeverity.Warning;
      case 3:
        return vscode.DiagnosticSeverity.Information;
      case 4:
        return vscode.DiagnosticSeverity.Hint;
      default:
        return vscode.DiagnosticSeverity.Error;
    }
  }

  request(method, params) {
    if (!this.process || !this.process.stdin) {
      return Promise.resolve(null);
    }
    const id = this.nextId++;
    this.send({ jsonrpc: "2.0", id, method, params });
    return new Promise((resolve) => {
      this.pending.set(id, { resolve });
    });
  }

  notify(method, params) {
    if (!this.process || !this.process.stdin) {
      return;
    }
    this.send({ jsonrpc: "2.0", method, params });
  }

  send(payload) {
    if (!this.process || !this.process.stdin) {
      return;
    }
    const body = JSON.stringify(payload);
    const header = `Content-Length: ${Buffer.byteLength(body, "utf8")}\r\n\r\n`;
    this.process.stdin.write(header + body);
  }
}

let client = null;

function activate(context) {
  client = new MtcLspClient(context);
  client.start();
}

function deactivate() {
  if (client) {
    client.stop();
    client = null;
  }
}

module.exports = {
  activate,
  deactivate,
};
