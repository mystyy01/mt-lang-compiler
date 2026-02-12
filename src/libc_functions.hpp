#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct LibcFunctionInfo {
    std::string ret;
    std::vector<std::string> args;
    bool var_arg = false;
};

extern const std::unordered_map<std::string, LibcFunctionInfo> LIBC_FUNCTIONS;

std::string libc_return_type_to_semantic_type(const std::string& ret_type);
