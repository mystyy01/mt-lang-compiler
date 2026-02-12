#include "libc_functions.hpp"

#include <unordered_map>

const std::unordered_map<std::string, LibcFunctionInfo> LIBC_FUNCTIONS = {
    {"malloc", {"ptr", {"int"}, false}},
    {"calloc", {"ptr", {"int", "int"}, false}},
    {"realloc", {"ptr", {"ptr", "int"}, false}},
    {"free", {"void", {"ptr"}, false}},
    {"memcpy", {"ptr", {"ptr", "ptr", "int"}, false}},
    {"memset", {"ptr", {"ptr", "int", "int"}, false}},

    {"strlen", {"int", {"ptr"}, false}},
    {"strcpy", {"ptr", {"ptr", "ptr"}, false}},
    {"strncpy", {"ptr", {"ptr", "ptr", "int"}, false}},
    {"strcat", {"ptr", {"ptr", "ptr"}, false}},
    {"strcmp", {"int", {"ptr", "ptr"}, false}},
    {"strncmp", {"int", {"ptr", "ptr", "int"}, false}},
    {"strstr", {"ptr", {"ptr", "ptr"}, false}},
    {"strchr", {"ptr", {"ptr", "int"}, false}},
    {"sprintf", {"int", {"ptr", "ptr"}, true}},
    {"snprintf", {"int", {"ptr", "int", "ptr"}, true}},
    {"tolower", {"int", {"int"}, false}},
    {"toupper", {"int", {"int"}, false}},

    {"fopen", {"ptr", {"ptr", "ptr"}, false}},
    {"fclose", {"int", {"ptr"}, false}},
    {"fread", {"int", {"ptr", "int", "int", "ptr"}, false}},
    {"fwrite", {"int", {"ptr", "int", "int", "ptr"}, false}},
    {"fgets", {"ptr", {"ptr", "int", "ptr"}, false}},
    {"fputs", {"int", {"ptr", "ptr"}, false}},
    {"fseek", {"int", {"ptr", "int", "int"}, false}},
    {"ftell", {"int", {"ptr"}, false}},
    {"fflush", {"int", {"ptr"}, false}},
    {"feof", {"int", {"ptr"}, false}},
    {"ferror", {"int", {"ptr"}, false}},

    {"access", {"int", {"ptr", "int"}, false}},
    {"remove", {"int", {"ptr"}, false}},
    {"rename", {"int", {"ptr", "ptr"}, false}},
    {"stat", {"int", {"ptr", "ptr"}, false}},
    {"mkdir", {"int", {"ptr", "int"}, false}},
    {"rmdir", {"int", {"ptr"}, false}},
    {"getcwd", {"ptr", {"ptr", "int"}, false}},
    {"chdir", {"int", {"ptr"}, false}},
    {"unlink", {"int", {"ptr"}, false}},

    {"printf", {"int", {"ptr"}, true}},
    {"scanf", {"int", {"ptr"}, true}},
    {"puts", {"int", {"ptr"}, false}},
    {"getchar", {"int", {}, false}},
    {"putchar", {"int", {"int"}, false}},

    {"exit", {"void", {"int"}, false}},
    {"abort", {"void", {}, false}},
    {"system", {"int", {"ptr"}, false}},
    {"getenv", {"ptr", {"ptr"}, false}},

    {"abs", {"int", {"int"}, false}},
    {"atoi", {"int", {"ptr"}, false}},
    {"atof", {"float", {"ptr"}, false}},
    {"floor", {"float", {"float"}, false}},
    {"ceil", {"float", {"float"}, false}},
    {"round", {"float", {"float"}, false}},
    {"sqrt", {"float", {"float"}, false}},
    {"pow", {"float", {"float", "float"}, false}},
    {"fabs", {"float", {"float"}, false}},
    {"fmod", {"float", {"float", "float"}, false}},
    {"log", {"float", {"float"}, false}},
    {"log10", {"float", {"float"}, false}},
    {"exp", {"float", {"float"}, false}},
    {"sin", {"float", {"float"}, false}},
    {"cos", {"float", {"float"}, false}},
    {"tan", {"float", {"float"}, false}},
    {"asin", {"float", {"float"}, false}},
    {"acos", {"float", {"float"}, false}},
    {"atan", {"float", {"float"}, false}},

    {"rand", {"int", {}, false}},
    {"srand", {"void", {"int"}, false}},

    {"time", {"int", {"ptr"}, false}},
    {"sleep", {"int", {"int"}, false}},
};

std::string libc_return_type_to_semantic_type(const std::string& ret_type) {
    static const std::unordered_map<std::string, std::string> kTypeMap = {
        {"int", "int"},
        {"ptr", "string"},
        {"void", "void"},
        {"float", "float"},
    };

    const auto it = kTypeMap.find(ret_type);
    if (it == kTypeMap.end()) {
        return "unknown";
    }
    return it->second;
}
