#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../src/parser.hpp"
#include "../src/semantic.hpp"
#include "../src/tokenizer.hpp"

namespace {

bool read_file_text(const std::string& path, std::string* out) {
    if (!out) {
        return false;
    }
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }
    out->assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: mtc_fuzz_parser <source-file>\n";
        return 2;
    }

    const std::string file_path = argv[1];
    std::string source;
    if (!read_file_text(file_path, &source)) {
        std::cerr << "failed to read source file: " << file_path << "\n";
        return 2;
    }

    try {
        std::string mutable_file_path = file_path;
        Tokenizer tokenizer(source, mutable_file_path);
        std::vector<Token> tokens = tokenizer.tokenize();

        Parser parser(std::move(tokens), mutable_file_path);
        ASTNode ast = parser.parse_program();

        SemanticAnalyzer analyzer(mutable_file_path);
        analyzer.analyze(ast);
    } catch (const CompilerError&) {
        // Parser/tokenizer diagnostics are expected for random fuzz input.
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "unhandled std::exception: " << ex.what() << "\n";
        return 3;
    } catch (...) {
        std::cerr << "unhandled unknown exception\n";
        return 4;
    }

    return 0;
}
