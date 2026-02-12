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

struct CompilerArgs {
    bool object_only = false;
    bool lib_file = false;
    bool no_runtime = false;
    bool no_libc = false;
    bool emit_ir = false;
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

std::optional<std::filesystem::path> resolve_module_file(const ASTNode& module_path,
                                                         const std::filesystem::path& current_file) {
    std::vector<std::string> parts;
    if (!flatten_module_path(module_path, &parts)) {
        return std::nullopt;
    }

    const std::filesystem::path rel_path = path_from_module_parts(parts);
    const std::vector<std::filesystem::path> candidates = {
        current_file.parent_path() / rel_path,
        std::filesystem::current_path() / rel_path,
        std::filesystem::path("/mnt/ssd/Coding/mt-lang/compiler") / rel_path,
    };

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::absolute(candidate);
        }
    }
    return std::nullopt;
}

void expand_imports_in_program(ASTNode* ast,
                               const std::filesystem::path& current_file,
                               std::unordered_set<std::string>* loaded_modules) {
    if (ast == nullptr || loaded_modules == nullptr || !is_node<Program>(*ast)) {
        return;
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
            const auto resolved = resolve_module_file(from.module_path, current_file);
            if (!resolved.has_value()) {
                expanded_statements.push_back(std::move(statement));
                continue;
            }

            std::string key;
            try {
                key = std::filesystem::weakly_canonical(*resolved).string();
            } catch (...) {
                key = resolved->string();
            }
            if (loaded_modules->count(key) > 0) {
                continue;
            }
            loaded_modules->insert(key);

            ASTNode module_ast;
            std::string module_error;
            if (!parse_file_to_ast(*resolved, &module_ast, &module_error)) {
                expanded_statements.push_back(std::move(statement));
                continue;
            }

            expand_imports_in_program(&module_ast, *resolved, loaded_modules);

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
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Error: Unknown flag '" << arg << "'\n";
            return std::nullopt;
        }

        positional.push_back(arg);
        ++i;
    }

    if (positional.size() != 2) {
        return std::nullopt;
    }

    args.source_file = positional[0];
    args.output_path = positional[1];

    return args;
}

std::optional<std::filesystem::path> find_exceptions_file(const std::filesystem::path& source_file) {
    const std::vector<std::filesystem::path> candidates = {
        source_file.parent_path() / "stdlib" / "exceptions.mtc",
        std::filesystem::current_path() / "stdlib" / "exceptions.mtc",
        std::filesystem::path("/mnt/ssd/Coding/mt-lang/compiler/stdlib/exceptions.mtc"),
    };

    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
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

}  // namespace

int main(int argc, char** argv) {
    const auto parsed = parse_args(argc, argv);
    if (!parsed.has_value()) {
        print_usage();
        return 1;
    }

    const CompilerArgs args = *parsed;

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

    std::unordered_set<std::string> loaded_modules;
    expand_imports_in_program(&ast, source_path, &loaded_modules);

    if (!args.no_runtime) {
        const auto exceptions_path = find_exceptions_file(source_path);
        if (exceptions_path.has_value() && !same_canonical_file(source_path, *exceptions_path)) {
            ASTNode exceptions_ast;
            std::string exceptions_error;
            if (parse_file_to_ast(*exceptions_path, &exceptions_ast, &exceptions_error)) {
                try {
                    expand_imports_in_program(&exceptions_ast, *exceptions_path, &loaded_modules);

                    if (is_node<Program>(ast) && is_node<Program>(exceptions_ast)) {
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

    const std::string compile_cmd =
        "clang -x ir -c " + shell_quote(ir_path.string()) + " -o " + shell_quote(obj_path.string());
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
    link_cmd << "clang " << shell_quote(obj_path.string());
    for (const auto& extra : args.extra_objects) {
        link_cmd << ' ' << shell_quote(extra);
    }
    link_cmd << " -o " << shell_quote(out_path.string()) << " -lm";

    const int link_status = run_command(link_cmd.str());
    if (link_status != 0) {
        std::cerr << "Error: linker failed\n";
        return 1;
    }

    std::filesystem::remove(obj_path);
    return 0;
}
