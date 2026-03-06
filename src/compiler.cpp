#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "codegen.hpp"
#include "parser.hpp"
#include "semantic.hpp"
#include "tokenizer.hpp"

namespace {

constexpr const char* kMtcVersion = "0.4.0";

struct CompilerArgs {
    bool object_only = false;
    bool lib_file = false;
    bool no_runtime = false;
    bool no_libc = false;
    bool emit_ir = false;
    bool show_version = false;
    std::string opt_level = "2";
    std::vector<std::string> extra_objects;
    std::string source_file;
    std::string output_path;
};

void print_usage() {
    std::cerr << "Provide source file and output path\n";
    std::cerr << "Example usage:\n";
    std::cerr << "  mtc source.mtc executable\n";
    std::cerr << "  mtc --no-runtime source.mtc output\n";
    std::cerr << "  mtc --no-libc source.mtc output\n";
    std::cerr << "  mtc -o source.mtc output.o\n";
    std::cerr << "  mtc source.mtc --obj lib.o output\n";
    std::cerr << "  mtc --emit-ir source.mtc output\n";
    std::cerr << "  mtc --opt-level 0 source.mtc output\n";
    std::cerr << "  mtc --version\n";
}

std::string shell_quote(const std::string& input) {
    std::string quoted = "'";
    for (char ch : input) {
        if (ch == '\'') {
            quoted += "'\"'\"'";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

bool read_text_file(const std::filesystem::path& path, std::string* out) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }
    out->assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return true;
}

bool write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path);
    if (!output.is_open()) {
        return false;
    }
    output << text;
    return output.good();
}

bool parse_source_to_ast(const std::string& source_code,
                         const std::filesystem::path& file_path,
                         ASTNode* out_ast,
                         std::string* error_out) {
    if (out_ast == nullptr) {
        return false;
    }
    try {
        std::string file_path_str = file_path.string();
        std::string source_copy = source_code;
        Tokenizer tokenizer(source_copy, file_path_str);
        std::vector<Token> tokens = tokenizer.tokenize();
        Parser parser(std::move(tokens), file_path_str);
        *out_ast = parser.parse_program();
        return true;
    } catch (const std::exception& err) {
        if (error_out != nullptr) {
            *error_out = err.what();
        }
        return false;
    }
}

bool parse_file_to_ast(const std::filesystem::path& file_path,
                       ASTNode* out_ast,
                       std::string* error_out) {
    std::string source;
    if (!read_text_file(file_path, &source)) {
        if (error_out != nullptr) {
            *error_out = "failed to read '" + file_path.string() + "'";
        }
        return false;
    }
    return parse_source_to_ast(source, file_path, out_ast, error_out);
}

bool flatten_module_path(const ASTNode& module_path, std::vector<std::string>* parts) {
    if (!module_path || parts == nullptr) {
        return false;
    }

    if (is_node<Identifier>(module_path)) {
        parts->push_back(get_node<Identifier>(module_path).name);
        return true;
    }

    if (is_node<MemberExpression>(module_path)) {
        const auto& member = get_node<MemberExpression>(module_path);
        if (!flatten_module_path(member.object, parts)) {
            return false;
        }
        parts->push_back(member.property);
        return true;
    }

    return false;
}

std::filesystem::path path_from_module_parts(const std::vector<std::string>& parts) {
    std::filesystem::path path;
    for (const auto& part : parts) {
        path /= part;
    }
    path += ".mtc";
    return path;
}

std::string normalize_existing_path(const std::filesystem::path& path) {
    try {
        return std::filesystem::weakly_canonical(path).string();
    } catch (...) {
        return std::filesystem::absolute(path).lexically_normal().string();
    }
}

void append_unique_path(std::vector<std::filesystem::path>* paths,
                        std::unordered_set<std::string>* seen,
                        const std::filesystem::path& path) {
    if (!paths || !seen || path.empty()) {
        return;
    }
    const std::string key = path.lexically_normal().string();
    if (seen->insert(key).second) {
        paths->push_back(path.lexically_normal());
    }
}

std::vector<std::string> split_env_paths(const std::string& value) {
    std::vector<std::string> paths;
    std::string current;
    for (char ch : value) {
        if (ch == ':' || ch == ';') {
            if (!current.empty()) {
                paths.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        paths.push_back(current);
    }
    return paths;
}

constexpr const char* kSystemStdlibRoot = "/usr/lib/mtc_stdlib";

std::optional<std::filesystem::path> strip_stdlib_prefix(const std::filesystem::path& relative_path) {
    auto part_it = relative_path.begin();
    if (part_it == relative_path.end() || part_it->string() != "stdlib") {
        return std::nullopt;
    }

    ++part_it;
    if (part_it == relative_path.end()) {
        return std::nullopt;
    }

    std::filesystem::path stripped;
    for (; part_it != relative_path.end(); ++part_it) {
        stripped /= *part_it;
    }
    return stripped;
}

void append_system_stdlib_candidates(const std::filesystem::path& relative_path,
                                     std::vector<std::filesystem::path>* candidates,
                                     std::unordered_set<std::string>* seen) {
    const std::filesystem::path stdlib_root(kSystemStdlibRoot);
    const auto stripped = strip_stdlib_prefix(relative_path);
    if (stripped.has_value()) {
        append_unique_path(candidates, seen, stdlib_root / *stripped);
    }
    append_unique_path(candidates, seen, stdlib_root / relative_path);
}

std::vector<std::filesystem::path> build_module_search_roots(const std::filesystem::path& source_file) {
    std::vector<std::filesystem::path> roots;
    std::unordered_set<std::string> seen;

    std::filesystem::path parent = source_file.parent_path();
    while (!parent.empty()) {
        append_unique_path(&roots, &seen, parent);
        const std::filesystem::path next = parent.parent_path();
        if (next == parent) {
            break;
        }
        parent = next;
    }

    append_unique_path(&roots, &seen, std::filesystem::current_path());

    const char* env_paths = std::getenv("MTC_PATH");
    if (env_paths != nullptr) {
        for (const auto& raw : split_env_paths(env_paths)) {
            append_unique_path(&roots, &seen, std::filesystem::path(raw));
        }
    }

    return roots;
}

std::optional<std::filesystem::path> resolve_module_relative_path(
    const std::filesystem::path& relative_path,
    const std::filesystem::path& current_file,
    const std::vector<std::filesystem::path>& search_roots) {
    std::vector<std::filesystem::path> candidates;
    std::unordered_set<std::string> seen;

    if (relative_path.is_absolute()) {
        append_unique_path(&candidates, &seen, relative_path);
    } else {
        append_system_stdlib_candidates(relative_path, &candidates, &seen);
        append_unique_path(&candidates, &seen, current_file.parent_path() / relative_path);
        for (const auto& root : search_roots) {
            append_unique_path(&candidates, &seen, root / relative_path);
        }
    }

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
            return std::filesystem::absolute(candidate).lexically_normal();
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> resolve_module_file(const ASTNode& module_path,
                                                         const std::filesystem::path& current_file,
                                                         const std::vector<std::filesystem::path>& search_roots) {
    if (is_node<StringLiteral>(module_path)) {
        std::filesystem::path rel_path(get_node<StringLiteral>(module_path).value);
        if (rel_path.empty()) {
            return std::nullopt;
        }
        if (!rel_path.has_extension()) {
            rel_path += ".mtc";
        }
        return resolve_module_relative_path(rel_path, current_file, search_roots);
    }

    std::vector<std::string> parts;
    if (!flatten_module_path(module_path, &parts)) {
        return std::nullopt;
    }

    return resolve_module_relative_path(path_from_module_parts(parts), current_file, search_roots);
}

std::optional<std::filesystem::path> resolve_simple_module_file(
    const std::string& module_name,
    const std::filesystem::path& current_file,
    const std::vector<std::filesystem::path>& search_roots) {
    if (module_name.empty()) {
        return std::nullopt;
    }

    std::string module_path_str = module_name;
    for (char& ch : module_path_str) {
        if (ch == '.') {
            ch = '/';
        }
    }
    return resolve_module_relative_path(
        std::filesystem::path(module_path_str + ".mtc"),
        current_file,
        search_roots);
}

std::string short_path_label(const std::string& path_text) {
    const std::filesystem::path path(path_text);
    if (!path.filename().empty()) {
        return path.filename().string();
    }
    return path_text;
}

std::string format_import_cycle(const std::vector<std::string>& active_stack, const std::string& repeated) {
    std::ostringstream out;
    auto start = std::find(active_stack.begin(), active_stack.end(), repeated);
    if (start == active_stack.end()) {
        return short_path_label(repeated);
    }

    bool first = true;
    for (auto it = start; it != active_stack.end(); ++it) {
        if (!first) {
            out << " -> ";
        }
        first = false;
        out << short_path_label(*it);
    }
    out << " -> " << short_path_label(repeated);
    return out.str();
}

class ActiveModuleGuard {
public:
    ActiveModuleGuard(std::vector<std::string>* stack,
                      std::unordered_set<std::string>* active_set,
                      std::string key)
        : stack_(stack), active_set_(active_set), key_(std::move(key)), active_(false) {
        if (!stack_ || !active_set_) {
            return;
        }
        stack_->push_back(key_);
        active_set_->insert(key_);
        active_ = true;
    }

    ~ActiveModuleGuard() {
        if (!active_ || !stack_ || !active_set_) {
            return;
        }
        active_set_->erase(key_);
        if (!stack_->empty()) {
            stack_->pop_back();
        }
    }

private:
    std::vector<std::string>* stack_;
    std::unordered_set<std::string>* active_set_;
    std::string key_;
    bool active_;
};

bool expand_imports_in_program(ASTNode* ast,
                               const std::filesystem::path& current_file,
                               const std::vector<std::filesystem::path>& search_roots,
                               std::unordered_set<std::string>* loaded_modules,
                               std::vector<std::string>* active_stack,
                               std::unordered_set<std::string>* active_set,
                               std::string* error_out) {
    if (ast == nullptr || loaded_modules == nullptr || active_stack == nullptr ||
        active_set == nullptr || !is_node<Program>(*ast)) {
        return true;
    }

    auto& program = get_node<Program>(*ast);
    std::vector<ASTNode> expanded_statements;
    expanded_statements.reserve(program.statements.size());

    for (auto& statement : program.statements) {
        if (!statement) {
            continue;
        }

        if (is_node<FromImportStatement>(statement)) {
            const auto& from = get_node<FromImportStatement>(statement);
            const auto resolved = resolve_module_file(from.module_path, current_file, search_roots);
            if (!resolved.has_value()) {
                expanded_statements.push_back(std::move(statement));
                continue;
            }

            const std::string key = normalize_existing_path(*resolved);
            if (active_set->count(key) > 0) {
                if (error_out != nullptr) {
                    *error_out = "Import cycle detected: " + format_import_cycle(*active_stack, key);
                }
                return false;
            }
            if (loaded_modules->count(key) > 0) {
                continue;
            }
            loaded_modules->insert(key);

            ActiveModuleGuard guard(active_stack, active_set, key);
            ASTNode module_ast;
            std::string module_error;
            if (!parse_file_to_ast(*resolved, &module_ast, &module_error)) {
                if (error_out != nullptr) {
                    *error_out =
                        "Failed to parse imported module '" + resolved->string() + "': " + module_error;
                }
                return false;
            }

            if (!expand_imports_in_program(
                    &module_ast,
                    *resolved,
                    search_roots,
                    loaded_modules,
                    active_stack,
                    active_set,
                    error_out)) {
                return false;
            }

            if (!is_node<Program>(module_ast)) {
                continue;
            }

            auto& module_program = get_node<Program>(module_ast);
            for (auto& module_stmt : module_program.statements) {
                if (!module_stmt || is_node<FromImportStatement>(module_stmt) ||
                    is_node<SimpleImportStatement>(module_stmt)) {
                    continue;
                }
                expanded_statements.push_back(std::move(module_stmt));
            }
            continue;
        }

        if (is_node<SimpleImportStatement>(statement)) {
            const auto& simple = get_node<SimpleImportStatement>(statement);
            const auto resolved = resolve_simple_module_file(simple.module_name, current_file, search_roots);
            if (!resolved.has_value()) {
                expanded_statements.push_back(std::move(statement));
                continue;
            }

            const std::string key = normalize_existing_path(*resolved);
            if (active_set->count(key) > 0) {
                if (error_out != nullptr) {
                    *error_out = "Import cycle detected: " + format_import_cycle(*active_stack, key);
                }
                return false;
            }
            if (loaded_modules->count(key) > 0) {
                continue;
            }
            loaded_modules->insert(key);

            ActiveModuleGuard guard(active_stack, active_set, key);
            ASTNode module_ast;
            std::string module_error;
            if (!parse_file_to_ast(*resolved, &module_ast, &module_error)) {
                if (error_out != nullptr) {
                    *error_out =
                        "Failed to parse imported module '" + resolved->string() + "': " + module_error;
                }
                return false;
            }

            if (!expand_imports_in_program(
                    &module_ast,
                    *resolved,
                    search_roots,
                    loaded_modules,
                    active_stack,
                    active_set,
                    error_out)) {
                return false;
            }
            if (!is_node<Program>(module_ast)) {
                continue;
            }

            auto& module_program = get_node<Program>(module_ast);
            for (auto& module_stmt : module_program.statements) {
                if (!module_stmt || is_node<FromImportStatement>(module_stmt) ||
                    is_node<SimpleImportStatement>(module_stmt)) {
                    continue;
                }
                expanded_statements.push_back(std::move(module_stmt));
            }
            continue;
        }

        expanded_statements.push_back(std::move(statement));
    }

    program.statements = std::move(expanded_statements);
    return true;
}

std::optional<CompilerArgs> parse_args(int argc, char** argv) {
    CompilerArgs args;
    std::vector<std::string> positional;

    int i = 1;
    while (i < argc) {
        const std::string arg = argv[i];
        if (arg == "--obj") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --obj requires a file path\n";
                return std::nullopt;
            }
            args.extra_objects.push_back(argv[i + 1]);
            i += 2;
            continue;
        }
        if (arg == "--no-runtime") {
            args.no_runtime = true;
            ++i;
            continue;
        }
        if (arg == "--no-libc") {
            args.no_libc = true;
            ++i;
            continue;
        }
        if (arg == "-o") {
            args.object_only = true;
            ++i;
            continue;
        }
        if (arg == "--lib") {
            args.lib_file = true;
            ++i;
            continue;
        }
        if (arg == "--emit-ir") {
            args.emit_ir = true;
            ++i;
            continue;
        }
        if (arg == "--version") {
            args.show_version = true;
            ++i;
            continue;
        }
        if (arg == "--opt-level") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --opt-level requires one of 0,1,2,3\n";
                return std::nullopt;
            }
            const std::string level = argv[i + 1];
            if (level != "0" && level != "1" && level != "2" && level != "3") {
                std::cerr << "Error: invalid --opt-level '" << level << "' (expected 0,1,2,3)\n";
                return std::nullopt;
            }
            args.opt_level = level;
            i += 2;
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Error: Unknown flag '" << arg << "'\n";
            return std::nullopt;
        }

        positional.push_back(arg);
        ++i;
    }

    if (args.show_version && positional.empty()) {
        return args;
    }

    if (positional.size() != 2) {
        return std::nullopt;
    }

    args.source_file = positional[0];
    args.output_path = positional[1];

    return args;
}

std::optional<std::filesystem::path> find_exceptions_file(
    const std::filesystem::path& source_file,
    const std::vector<std::filesystem::path>& search_roots) {
    std::vector<std::filesystem::path> candidates;
    std::unordered_set<std::string> seen;

    append_unique_path(&candidates, &seen, source_file.parent_path() / "stdlib" / "exceptions.mtc");
    for (const auto& root : search_roots) {
        append_unique_path(&candidates, &seen, root / "stdlib" / "exceptions.mtc");
    }

    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return std::filesystem::absolute(path).lexically_normal();
        }
    }

    return std::nullopt;
}

bool same_canonical_file(const std::filesystem::path& a, const std::filesystem::path& b) {
    try {
        return std::filesystem::weakly_canonical(a) == std::filesystem::weakly_canonical(b);
    } catch (...) {
        return std::filesystem::absolute(a) == std::filesystem::absolute(b);
    }
}

int run_command(const std::string& cmd) {
    return std::system(cmd.c_str());
}

std::string build_codegen_flags(const CompilerArgs& args) {
    std::ostringstream flags;
    flags << "-O" << args.opt_level;

    // Bare-metal/no-libc targets (PHOBOS, kernels, freestanding apps) must avoid
    // SSE/vector instructions unless the OS explicitly enables FP/SIMD context.
    if (args.no_libc || args.no_runtime) {
        flags << " -target x86_64-elf"
              << " -ffreestanding -fno-builtin"
              << " -fno-vectorize -fno-slp-vectorize"
              << " -mno-mmx -mno-sse -mno-sse2 -msoft-float";
    }

    return flags.str();
}

}  // namespace

int main(int argc, char** argv) {
    const auto parsed = parse_args(argc, argv);
    if (!parsed.has_value()) {
        print_usage();
        return 1;
    }

    const CompilerArgs args = *parsed;
    if (args.show_version && args.source_file.empty()) {
        std::cout << "mtc " << kMtcVersion << "\n";
        return 0;
    }

    const std::filesystem::path source_path = std::filesystem::absolute(args.source_file);
    std::string source_code;
    if (!read_text_file(source_path, &source_code)) {
        std::cerr << "Error: failed to read source file '" << source_path.string() << "'\n";
        return 1;
    }

    ASTNode ast;
    std::string parse_error;
    if (!parse_source_to_ast(source_code, source_path, &ast, &parse_error)) {
        std::cerr << "Error: " << parse_error << "\n";
        return 1;
    }

    const std::vector<std::filesystem::path> search_roots = build_module_search_roots(source_path);
    std::unordered_set<std::string> loaded_modules;
    std::vector<std::string> active_stack;
    std::unordered_set<std::string> active_set;
    const std::string source_key = normalize_existing_path(source_path);
    loaded_modules.insert(source_key);
    active_stack.push_back(source_key);
    active_set.insert(source_key);

    std::string import_error;
    if (!expand_imports_in_program(
            &ast,
            source_path,
            search_roots,
            &loaded_modules,
            &active_stack,
            &active_set,
            &import_error)) {
        std::cerr << "Error: " << import_error << "\n";
        return 1;
    }

    if (!args.no_runtime) {
        const auto exceptions_path = find_exceptions_file(source_path, search_roots);
        if (exceptions_path.has_value() && !same_canonical_file(source_path, *exceptions_path)) {
            ASTNode exceptions_ast;
            std::string exceptions_error;
            if (!parse_file_to_ast(*exceptions_path, &exceptions_ast, &exceptions_error)) {
                std::cerr << "Error: failed to parse runtime exceptions module '"
                          << exceptions_path->string() << "': " << exceptions_error << "\n";
                return 1;
            }
            try {
                const std::string exceptions_key = normalize_existing_path(*exceptions_path);
                bool include_runtime_module = false;
                if (!loaded_modules.count(exceptions_key)) {
                    loaded_modules.insert(exceptions_key);
                    active_stack.push_back(exceptions_key);
                    active_set.insert(exceptions_key);
                    const bool expanded = expand_imports_in_program(
                        &exceptions_ast,
                        *exceptions_path,
                        search_roots,
                        &loaded_modules,
                        &active_stack,
                        &active_set,
                        &import_error);
                    active_set.erase(exceptions_key);
                    active_stack.pop_back();
                    if (!expanded) {
                        std::cerr << "Error: " << import_error << "\n";
                        return 1;
                    }
                    include_runtime_module = true;
                }

                if (include_runtime_module && is_node<Program>(ast) && is_node<Program>(exceptions_ast)) {
                    auto& program = get_node<Program>(ast);
                    auto& ex_program = get_node<Program>(exceptions_ast);

                    std::vector<ASTNode> merged;
                    merged.reserve(ex_program.statements.size() + program.statements.size());
                    for (auto& stmt : ex_program.statements) {
                        merged.push_back(std::move(stmt));
                    }
                    for (auto& stmt : program.statements) {
                        merged.push_back(std::move(stmt));
                    }
                    program.statements = std::move(merged);
                }
            } catch (const std::exception& err) {
                std::cerr << "Error: failed to load runtime exceptions module: "
                          << err.what() << "\n";
                return 1;
            }
        }
    }

    SemanticAnalyzer analyzer(source_path.string());
    analyzer.analyze(ast);

    if (!analyzer.get_errors().empty()) {
        for (const auto& error : analyzer.get_errors()) {
            std::cerr << "Error: " << error << "\n";
        }
        return 1;
    }

    std::string llvm_ir;
    try {
        const bool emit_main = !(args.no_runtime || args.lib_file);
        const bool emit_builtin_decls = !args.no_libc;
        CodeGenerator codegen(emit_main, emit_builtin_decls);
        llvm_ir = codegen.generate(ast);
    } catch (const std::exception& err) {
        std::cerr << "Error: code generation failed: " << err.what() << "\n";
        return 1;
    }

    if (args.emit_ir) {
        std::cout << llvm_ir;
    }

    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    const std::filesystem::path ir_path =
        std::filesystem::temp_directory_path() / ("mtc_" + std::to_string(now) + ".ll");

    if (!write_text_file(ir_path, llvm_ir)) {
        std::cerr << "Error: failed to write temporary LLVM IR file\n";
        return 1;
    }

    const std::filesystem::path out_path = args.output_path;
    const std::filesystem::path obj_path = args.object_only
        ? out_path
        : std::filesystem::path(out_path.string() + ".o");

    const std::string codegen_flags = build_codegen_flags(args);
    const std::string compile_cmd =
        "clang " + codegen_flags + " -x ir -c " + shell_quote(ir_path.string()) +
        " -o " + shell_quote(obj_path.string());
    const int compile_status = run_command(compile_cmd);

    if (compile_status != 0) {
        std::cerr << "Error: clang failed to compile generated IR\n";
        std::filesystem::remove(ir_path);
        return 1;
    }

    std::filesystem::remove(ir_path);

    if (args.object_only) {
        return 0;
    }

    std::ostringstream link_cmd;
    link_cmd << "clang " << codegen_flags << " " << shell_quote(obj_path.string());
    for (const auto& extra : args.extra_objects) {
        link_cmd << ' ' << shell_quote(extra);
    }
    link_cmd << " -o " << shell_quote(out_path.string());
    if (!args.no_libc) {
        link_cmd << " -lm";
    }

    const int link_status = run_command(link_cmd.str());
    if (link_status != 0) {
        std::cerr << "Error: linker failed\n";
        return 1;
    }

    std::filesystem::remove(obj_path);
    return 0;
}
