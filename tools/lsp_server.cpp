#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../src/parser.hpp"
#include "../src/semantic.hpp"
#include "../src/tokenizer.hpp"

namespace {

struct Diagnostic {
    int line = 0;
    int character = 0;
    std::string message;
};

struct SymbolDef {
    std::string name;
    std::string type;
    int line = -1;
    int character = -1;
};

struct SymbolOccurrence {
    std::string name;
    int line = -1;
    int character = -1;
};

struct SymbolInfoEntry {
    std::string name;
    std::string type;
    int kind = 13;
    int line = -1;
    int character = -1;
    std::string container_name;
};

struct ImportLink {
    std::string target_uri;
    int line = -1;
    int start_character = -1;
    int end_character = -1;
};

struct TextChange {
    bool has_range = false;
    int start_line = 0;
    int start_character = 0;
    int end_line = 0;
    int end_character = 0;
    std::string text;
};

struct AnalysisResult {
    std::vector<Diagnostic> diagnostics;
    std::set<std::string> completion_symbols;
    std::vector<SymbolDef> definitions;
    std::vector<SymbolOccurrence> occurrences;
    std::vector<SymbolInfoEntry> symbols;
    std::vector<ImportLink> import_links;
    std::unordered_map<std::string, std::string> import_targets;
};

struct DocumentState {
    std::string uri;
    std::string path;
    std::string text;
    int version = 0;
    std::vector<Diagnostic> diagnostics;
    std::set<std::string> completion_symbols;
    std::vector<SymbolDef> definitions;
    std::vector<SymbolOccurrence> occurrences;
    std::vector<SymbolInfoEntry> symbols;
    std::vector<ImportLink> import_links;
    std::unordered_map<std::string, std::string> import_targets;
};

std::string trim(const std::string& input) {
    std::size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        ++start;
    }
    std::size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    return input.substr(start, end - start);
}

std::string json_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 16);
    for (char ch : input) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    std::ostringstream escaped;
                    escaped << "\\u00";
                    static const char* kHex = "0123456789abcdef";
                    escaped << kHex[(ch >> 4) & 0x0F] << kHex[ch & 0x0F];
                    out += escaped.str();
                } else {
                    out.push_back(ch);
                }
                break;
        }
    }
    return out;
}

bool parse_json_string_at(const std::string& json,
                          std::size_t start,
                          std::string* value_out,
                          std::size_t* end_out) {
    if (!value_out || !end_out || start >= json.size() || json[start] != '"') {
        return false;
    }

    std::string value;
    std::size_t i = start + 1;
    while (i < json.size()) {
        char ch = json[i++];
        if (ch == '"') {
            *value_out = value;
            *end_out = i;
            return true;
        }
        if (ch == '\\') {
            if (i >= json.size()) {
                return false;
            }
            char escaped = json[i++];
            switch (escaped) {
                case '"':
                    value.push_back('"');
                    break;
                case '\\':
                    value.push_back('\\');
                    break;
                case '/':
                    value.push_back('/');
                    break;
                case 'b':
                    value.push_back('\b');
                    break;
                case 'f':
                    value.push_back('\f');
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                case 'u': {
                    if (i + 4 > json.size()) {
                        return false;
                    }
                    std::string hex = json.substr(i, 4);
                    i += 4;
                    char* end_ptr = nullptr;
                    long code = std::strtol(hex.c_str(), &end_ptr, 16);
                    if (!end_ptr || *end_ptr != '\0') {
                        return false;
                    }
                    if (code >= 0 && code <= 0x7F) {
                        value.push_back(static_cast<char>(code));
                    } else {
                        value.push_back('?');
                    }
                    break;
                }
                default:
                    return false;
            }
            continue;
        }
        value.push_back(ch);
    }
    return false;
}

std::optional<std::size_t> find_key_position(const std::string& json,
                                             const std::string& key,
                                             std::size_t start = 0) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t key_pos = json.find(needle, start);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t colon_pos = json.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }
    std::size_t value_pos = colon_pos + 1;
    while (value_pos < json.size() && std::isspace(static_cast<unsigned char>(json[value_pos]))) {
        ++value_pos;
    }
    if (value_pos >= json.size()) {
        return std::nullopt;
    }
    return value_pos;
}

std::optional<std::string> json_get_string(const std::string& json,
                                           const std::string& key,
                                           std::size_t start = 0) {
    auto pos_opt = find_key_position(json, key, start);
    if (!pos_opt.has_value()) {
        return std::nullopt;
    }
    const std::size_t pos = pos_opt.value();
    if (json[pos] != '"') {
        return std::nullopt;
    }

    std::string value;
    std::size_t end = pos;
    if (!parse_json_string_at(json, pos, &value, &end)) {
        return std::nullopt;
    }
    return value;
}

std::optional<int> json_get_int(const std::string& json,
                                const std::string& key,
                                std::size_t start = 0) {
    auto pos_opt = find_key_position(json, key, start);
    if (!pos_opt.has_value()) {
        return std::nullopt;
    }
    const std::size_t pos = pos_opt.value();
    std::size_t end = pos;
    if (json[pos] == '-') {
        ++end;
    }
    while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end]))) {
        ++end;
    }
    if (end == pos || (json[pos] == '-' && end == pos + 1)) {
        return std::nullopt;
    }
    try {
        return std::stoi(json.substr(pos, end - pos));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<bool> json_get_bool(const std::string& json,
                                  const std::string& key,
                                  std::size_t start = 0) {
    auto pos_opt = find_key_position(json, key, start);
    if (!pos_opt.has_value()) {
        return std::nullopt;
    }
    const std::size_t pos = pos_opt.value();
    if (json.compare(pos, 4, "true") == 0) {
        return true;
    }
    if (json.compare(pos, 5, "false") == 0) {
        return false;
    }
    return std::nullopt;
}

std::optional<std::string> json_get_raw_value(const std::string& json, const std::string& key) {
    auto pos_opt = find_key_position(json, key);
    if (!pos_opt.has_value()) {
        return std::nullopt;
    }
    std::size_t pos = pos_opt.value();
    if (json[pos] == '"') {
        std::string ignored;
        std::size_t end = pos;
        if (!parse_json_string_at(json, pos, &ignored, &end)) {
            return std::nullopt;
        }
        return json.substr(pos, end - pos);
    }
    std::size_t end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']') {
        ++end;
    }
    return trim(json.substr(pos, end - pos));
}

std::optional<std::pair<int, int>> extract_line_col(const std::string& message) {
    static const std::regex kLineColRegex(R"(at line (\d+)(?:,\s*column (\d+))?)");
    std::smatch match;
    if (!std::regex_search(message, match, kLineColRegex)) {
        return std::nullopt;
    }
    int line = 1;
    int column = 1;
    try {
        line = std::stoi(match[1].str());
        if (match.size() >= 3 && match[2].matched) {
            column = std::stoi(match[2].str());
        }
    } catch (...) {
        return std::nullopt;
    }
    line = std::max(1, line);
    column = std::max(1, column);
    return std::make_pair(line - 1, column - 1);
}

std::string uri_to_path(const std::string& uri) {
    if (uri.rfind("file://", 0) != 0) {
        return uri;
    }

    std::string encoded = uri.substr(7);
    std::string decoded;
    decoded.reserve(encoded.size());
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            const std::string hex = encoded.substr(i + 1, 2);
            char* end_ptr = nullptr;
            long value = std::strtol(hex.c_str(), &end_ptr, 16);
            if (end_ptr && *end_ptr == '\0') {
                decoded.push_back(static_cast<char>(value));
                i += 2;
                continue;
            }
        }
        decoded.push_back(encoded[i]);
    }
    return decoded;
}

std::string path_to_uri(const std::string& path) {
    std::ostringstream out;
    out << "file://";
    const char* hex = "0123456789ABCDEF";
    for (unsigned char ch : path) {
        if (std::isalnum(ch) || ch == '/' || ch == '.' || ch == '-' || ch == '_' || ch == '~') {
            out << static_cast<char>(ch);
        } else {
            out << '%' << hex[(ch >> 4) & 0xF] << hex[ch & 0xF];
        }
    }
    return out.str();
}

std::string to_lower_copy(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool contains_case_insensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    const std::string lowered_haystack = to_lower_copy(haystack);
    const std::string lowered_needle = to_lower_copy(needle);
    return lowered_haystack.find(lowered_needle) != std::string::npos;
}

bool read_file_text(const std::string& path, std::string* out_text) {
    if (!out_text) {
        return false;
    }
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }
    out_text->assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return true;
}

std::vector<std::string> list_workspace_mtc_files(const std::string& root_path) {
    std::vector<std::string> paths;
    if (root_path.empty()) {
        return paths;
    }

    std::error_code ec;
    std::filesystem::path root(root_path);
    if (!std::filesystem::exists(root, ec)) {
        return paths;
    }

    std::filesystem::recursive_directory_iterator it(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        ec);
    std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            continue;
        }
        const auto& entry = *it;
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        if (entry.path().extension() == ".mtc") {
            paths.push_back(entry.path().string());
        }
    }
    return paths;
}

bool token_is_keyword(const Token& token, const std::string& keyword) {
    return token.type == T_KEYWORD && token.value == keyword;
}

bool token_is_symbol(const Token& token, const std::string& symbol) {
    return token.type == T_SYMBOL && token.value == symbol;
}

std::size_t parse_qualified_module_name(const std::vector<Token>& tokens,
                                        std::size_t start_index,
                                        std::string* out_module_name,
                                        std::vector<std::size_t>* out_name_token_indices) {
    if (!out_module_name || !out_name_token_indices || start_index >= tokens.size()) {
        return std::string::npos;
    }
    if (tokens[start_index].type != T_NAME) {
        return std::string::npos;
    }

    out_module_name->clear();
    out_name_token_indices->clear();

    std::size_t index = start_index;
    out_name_token_indices->push_back(index);
    *out_module_name = tokens[index].value;

    while (index + 2 < tokens.size() &&
           token_is_symbol(tokens[index + 1], ".") &&
           tokens[index + 2].type == T_NAME) {
        index += 2;
        out_name_token_indices->push_back(index);
        *out_module_name += ".";
        *out_module_name += tokens[index].value;
    }

    return index;
}

std::optional<std::string> resolve_import_module_uri(const std::string& current_file_path,
                                                     const std::string& workspace_root_path,
                                                     const std::string& module_name) {
    if (module_name.empty()) {
        return std::nullopt;
    }

    std::vector<std::string> relative_candidates;
    auto add_candidate = [&relative_candidates](const std::string& value) {
        if (value.empty()) {
            return;
        }
        if (std::find(relative_candidates.begin(), relative_candidates.end(), value) ==
            relative_candidates.end()) {
            relative_candidates.push_back(value);
        }
    };

    if (module_name.size() >= 4 && module_name.substr(module_name.size() - 4) == ".mtc") {
        add_candidate(module_name);
    } else {
        add_candidate(module_name + ".mtc");
    }

    std::string dotted_path = module_name;
    std::replace(dotted_path.begin(), dotted_path.end(), '.', '/');
    if (dotted_path.size() >= 4 && dotted_path.substr(dotted_path.size() - 4) == ".mtc") {
        add_candidate(dotted_path);
    } else {
        add_candidate(dotted_path + ".mtc");
    }

    std::vector<std::filesystem::path> bases;
    if (!current_file_path.empty()) {
        std::filesystem::path current_path(current_file_path);
        if (current_path.has_parent_path()) {
            bases.push_back(current_path.parent_path());
        }
    }
    if (!workspace_root_path.empty()) {
        std::filesystem::path workspace_path(workspace_root_path);
        const std::string workspace_string = workspace_path.lexically_normal().string();
        bool exists = false;
        for (const auto& base : bases) {
            if (base.lexically_normal().string() == workspace_string) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            bases.push_back(workspace_path);
        }
    }

    std::error_code ec;
    for (const auto& rel : relative_candidates) {
        const std::filesystem::path rel_path(rel);
        if (rel_path.is_absolute()) {
            ec.clear();
            if (std::filesystem::exists(rel_path, ec) &&
                std::filesystem::is_regular_file(rel_path, ec)) {
                return path_to_uri(rel_path.lexically_normal().string());
            }
            continue;
        }

        for (const auto& base : bases) {
            std::filesystem::path candidate = (base / rel_path).lexically_normal();
            ec.clear();
            if (std::filesystem::exists(candidate, ec) &&
                std::filesystem::is_regular_file(candidate, ec)) {
                return path_to_uri(candidate.string());
            }
        }

        if (bases.empty()) {
            ec.clear();
            if (std::filesystem::exists(rel_path, ec) &&
                std::filesystem::is_regular_file(rel_path, ec)) {
                return path_to_uri(rel_path.lexically_normal().string());
            }
        }
    }

    return std::nullopt;
}

void add_import_link_for_token(const Token& token,
                               const std::string& target_uri,
                               std::vector<ImportLink>* import_links) {
    if (!import_links || target_uri.empty() || token.value.empty() || token.line <= 0 || token.column <= 0) {
        return;
    }
    const int start_character = token.column - 1;
    const int end_character = start_character + static_cast<int>(token.value.size());
    import_links->push_back(ImportLink{
        target_uri,
        token.line - 1,
        start_character,
        end_character,
    });
}

void collect_import_links_from_tokens(const std::vector<Token>& tokens,
                                      const std::string& current_file_path,
                                      const std::string& workspace_root_path,
                                      std::vector<ImportLink>* import_links,
                                      std::unordered_map<std::string, std::string>* import_targets) {
    if (!import_links || !import_targets) {
        return;
    }

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& token = tokens[i];

        if (token_is_keyword(token, "use")) {
            std::string module_name;
            std::vector<std::size_t> module_name_tokens;
            const std::size_t module_end =
                parse_qualified_module_name(tokens, i + 1, &module_name, &module_name_tokens);
            if (module_end == std::string::npos) {
                continue;
            }

            const auto target_uri =
                resolve_import_module_uri(current_file_path, workspace_root_path, module_name);
            if (target_uri.has_value()) {
                for (const std::size_t name_idx : module_name_tokens) {
                    add_import_link_for_token(tokens[name_idx], target_uri.value(), import_links);
                }
                if (!module_name_tokens.empty()) {
                    const std::string& first_segment = tokens[module_name_tokens.front()].value;
                    if (!first_segment.empty()) {
                        (*import_targets)[first_segment] = target_uri.value();
                    }
                }
            }

            std::size_t next = module_end + 1;
            if (next + 1 < tokens.size() &&
                token_is_keyword(tokens[next], "as") &&
                tokens[next + 1].type == T_NAME) {
                if (target_uri.has_value()) {
                    add_import_link_for_token(tokens[next + 1], target_uri.value(), import_links);
                    if (!tokens[next + 1].value.empty()) {
                        (*import_targets)[tokens[next + 1].value] = target_uri.value();
                    }
                }
                i = next + 1;
            } else {
                i = module_end;
            }
            continue;
        }

        if (token_is_keyword(token, "from")) {
            std::string module_name;
            std::vector<std::size_t> module_name_tokens;
            const std::size_t module_end =
                parse_qualified_module_name(tokens, i + 1, &module_name, &module_name_tokens);
            if (module_end == std::string::npos) {
                continue;
            }

            const auto target_uri =
                resolve_import_module_uri(current_file_path, workspace_root_path, module_name);
            if (target_uri.has_value()) {
                for (const std::size_t name_idx : module_name_tokens) {
                    add_import_link_for_token(tokens[name_idx], target_uri.value(), import_links);
                }
            }

            i = module_end;
            if (i + 1 < tokens.size() && token_is_keyword(tokens[i + 1], "use")) {
                i += 1;
            }
        }
    }
}

std::vector<std::string> builtin_completion_items() {
    return {
        "print", "length", "append", "pop", "str", "int", "float", "read", "split", "range",
    };
}

std::optional<std::size_t> find_matching_delim(const std::string& text,
                                               std::size_t start,
                                               char open_ch,
                                               char close_ch) {
    if (start >= text.size() || text[start] != open_ch) {
        return std::nullopt;
    }
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t i = start; i < text.size(); ++i) {
        char ch = text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == open_ch) {
            ++depth;
        } else if (ch == close_ch) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::nullopt;
}

std::optional<TextChange> parse_text_change_object(const std::string& object_json) {
    TextChange change;
    auto text_opt = json_get_string(object_json, "text");
    if (!text_opt.has_value()) {
        return std::nullopt;
    }
    change.text = text_opt.value();

    const std::size_t range_pos = object_json.find("\"range\"");
    if (range_pos == std::string::npos) {
        return change;
    }

    const std::size_t start_pos = object_json.find("\"start\"", range_pos);
    const std::size_t end_pos = object_json.find("\"end\"", range_pos);
    if (start_pos == std::string::npos || end_pos == std::string::npos) {
        return change;
    }

    auto start_line = json_get_int(object_json, "line", start_pos);
    auto start_char = json_get_int(object_json, "character", start_pos);
    auto end_line = json_get_int(object_json, "line", end_pos);
    auto end_char = json_get_int(object_json, "character", end_pos);
    if (!start_line.has_value() || !start_char.has_value() || !end_line.has_value() || !end_char.has_value()) {
        return change;
    }

    change.has_range = true;
    change.start_line = std::max(0, start_line.value());
    change.start_character = std::max(0, start_char.value());
    change.end_line = std::max(0, end_line.value());
    change.end_character = std::max(0, end_char.value());
    return change;
}

std::vector<TextChange> parse_content_changes(const std::string& message) {
    std::vector<TextChange> changes;
    auto changes_pos_opt = find_key_position(message, "contentChanges");
    if (!changes_pos_opt.has_value()) {
        return changes;
    }

    std::size_t arr_start = message.find('[', changes_pos_opt.value());
    if (arr_start == std::string::npos) {
        return changes;
    }
    auto arr_end_opt = find_matching_delim(message, arr_start, '[', ']');
    if (!arr_end_opt.has_value()) {
        return changes;
    }
    const std::size_t arr_end = arr_end_opt.value();

    std::size_t i = arr_start + 1;
    while (i < arr_end) {
        const std::size_t obj_start = message.find('{', i);
        if (obj_start == std::string::npos || obj_start >= arr_end) {
            break;
        }
        auto obj_end_opt = find_matching_delim(message, obj_start, '{', '}');
        if (!obj_end_opt.has_value() || obj_end_opt.value() > arr_end) {
            break;
        }
        const std::size_t obj_end = obj_end_opt.value();
        std::string object_json = message.substr(obj_start, obj_end - obj_start + 1);
        auto change_opt = parse_text_change_object(object_json);
        if (change_opt.has_value()) {
            changes.push_back(std::move(change_opt.value()));
        }
        i = obj_end + 1;
    }
    return changes;
}

std::size_t offset_from_line_character(const std::string& text, int line, int character) {
    int target_line = std::max(0, line);
    int target_char = std::max(0, character);

    std::size_t offset = 0;
    int current_line = 0;
    while (offset < text.size() && current_line < target_line) {
        if (text[offset] == '\n') {
            ++current_line;
        }
        ++offset;
    }
    if (current_line < target_line) {
        return text.size();
    }

    std::size_t line_start = offset;
    while (offset < text.size() &&
           text[offset] != '\n' &&
           static_cast<int>(offset - line_start) < target_char) {
        ++offset;
    }
    return offset;
}

std::string get_line_text(const std::string& text, int line) {
    int target_line = std::max(0, line);
    std::size_t offset = 0;
    int current_line = 0;
    while (offset < text.size() && current_line < target_line) {
        if (text[offset] == '\n') {
            ++current_line;
        }
        ++offset;
    }
    if (current_line < target_line) {
        return "";
    }
    std::size_t line_start = offset;
    while (offset < text.size() && text[offset] != '\n') {
        ++offset;
    }
    return text.substr(line_start, offset - line_start);
}

enum class ImportCompletionContext {
    NONE,
    FROM_MODULE,
    LIBC_SYMBOLS,
    MODULE_PATH,
    MODULE_SYMBOLS,
};

struct ImportCompletionInfo {
    ImportCompletionContext context = ImportCompletionContext::NONE;
    std::string module_name;
    std::string partial;
    std::unordered_set<std::string> already_imported;
};

ImportCompletionInfo detect_import_context(const std::string& line_text, int character) {
    ImportCompletionInfo info;

    std::size_t len = std::min(static_cast<std::size_t>(std::max(0, character)),
                               line_text.size());
    std::string prefix = line_text.substr(0, len);

    // Skip leading whitespace, then expect "from "
    std::size_t start = 0;
    while (start < prefix.size() &&
           std::isspace(static_cast<unsigned char>(prefix[start]))) {
        ++start;
    }
    if (prefix.compare(start, 5, "from ") != 0) {
        return info;
    }
    start += 5;

    // Look for " use " to split module name from symbol list
    std::size_t use_pos = prefix.find(" use ", start);

    if (use_pos == std::string::npos) {
        // No "use" yet — classify based on what follows "from "
        std::string module_part = trim(prefix.substr(start));
        if (!module_part.empty() && module_part.back() == '.') {
            // Cursor right after a dot: suggest submodules (e.g. "from core.")
            info.context = ImportCompletionContext::MODULE_PATH;
            info.module_name = module_part.substr(0, module_part.size() - 1);
        } else {
            // Cursor at/within module name: suggest importable modules (e.g. "from ")
            info.context = ImportCompletionContext::FROM_MODULE;
            info.partial = module_part;
        }
        return info;
    }

    // "from <module> use <symbols...>"
    std::string module_name = trim(prefix.substr(start, use_pos - start));
    if (module_name.empty()) {
        return info;
    }
    info.module_name = module_name;

    // Collect already-imported names (all comma-separated segments except the last,
    // which is the partial text the user is currently typing)
    std::string symbols_text = prefix.substr(use_pos + 5);
    std::size_t last_comma = symbols_text.rfind(',');
    if (last_comma != std::string::npos) {
        std::istringstream ss(symbols_text.substr(0, last_comma));
        std::string token;
        while (std::getline(ss, token, ',')) {
            std::string name = trim(token);
            if (!name.empty()) {
                info.already_imported.insert(name);
            }
        }
    }

    info.context = (module_name == "libc")
        ? ImportCompletionContext::LIBC_SYMBOLS
        : ImportCompletionContext::MODULE_SYMBOLS;
    return info;
}

void apply_text_change(std::string* text, const TextChange& change) {
    if (!text) {
        return;
    }
    if (!change.has_range) {
        *text = change.text;
        return;
    }

    std::size_t start = offset_from_line_character(*text, change.start_line, change.start_character);
    std::size_t end = offset_from_line_character(*text, change.end_line, change.end_character);
    if (start > end) {
        std::swap(start, end);
    }
    if (start > text->size()) {
        start = text->size();
    }
    if (end > text->size()) {
        end = text->size();
    }
    text->replace(start, end - start, change.text);
}

bool is_word_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

bool is_valid_identifier_name(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    if (!(std::isalpha(static_cast<unsigned char>(value[0])) || value[0] == '_')) {
        return false;
    }
    for (char ch : value) {
        if (!is_word_char(ch)) {
            return false;
        }
    }
    return true;
}

struct WordInfo {
    std::string word;
    int line = 0;
    int start_character = 0;
    int end_character = 0;
};

std::optional<WordInfo> word_info_at_position(const std::string& text, int line, int character) {
    if (text.empty()) {
        return std::nullopt;
    }
    std::size_t offset = offset_from_line_character(text, line, character);
    if (offset >= text.size()) {
        if (text.empty()) {
            return std::nullopt;
        }
        offset = text.size() - 1;
    }

    if (!is_word_char(text[offset])) {
        if (offset > 0 && is_word_char(text[offset - 1])) {
            --offset;
        } else {
            return std::nullopt;
        }
    }

    std::size_t start = offset;
    while (start > 0 && is_word_char(text[start - 1])) {
        --start;
    }
    std::size_t end = offset + 1;
    while (end < text.size() && is_word_char(text[end])) {
        ++end;
    }
    if (start >= end) {
        return std::nullopt;
    }
    return WordInfo{
        text.substr(start, end - start),
        std::max(0, line),
        static_cast<int>(start - offset_from_line_character(text, line, 0)),
        static_cast<int>(end - offset_from_line_character(text, line, 0)),
    };
}

std::optional<std::string> word_at_position(const std::string& text, int line, int character) {
    auto info = word_info_at_position(text, line, character);
    if (!info.has_value()) {
        return std::nullopt;
    }
    return info->word;
}

std::string variable_decl_type_string(const VariableDeclaration& node) {
    if (node.type == "array") {
        std::string base = "array";
        if (!node.element_type.empty()) {
            base += "<" + node.element_type + ">";
        }
        if (node.fixed_size > 0) {
            base += "[" + std::to_string(node.fixed_size) + "]";
        }
        if (node.is_dynamic) {
            base = "dynamic " + base;
        }
        return base;
    }
    if (node.type == "dict") {
        if (!node.key_type.empty() || !node.value_type.empty()) {
            return "dict<" +
                   (node.key_type.empty() ? "any" : node.key_type) + ", " +
                   (node.value_type.empty() ? "any" : node.value_type) + ">";
        }
        return "dict";
    }
    return node.type;
}

void maybe_add_symbol(std::set<std::string>* completion_symbols,
                      std::vector<SymbolDef>* definitions,
                      std::vector<SymbolInfoEntry>* symbols,
                      const std::string& name,
                      const std::string& type,
                      int kind,
                      int line,
                      int character,
                      const std::string& container_name = "") {
    if (name.empty()) {
        return;
    }
    if (completion_symbols) {
        completion_symbols->insert(name);
    }
    if (definitions && line >= 0 && character >= 0) {
        definitions->push_back(SymbolDef{name, type.empty() ? "any" : type, line, character});
    }
    if (symbols && line >= 0 && character >= 0) {
        symbols->push_back(SymbolInfoEntry{
            name,
            type.empty() ? "any" : type,
            kind,
            line,
            character,
            container_name,
        });
    }
}

void collect_symbols_from_ast(const ASTNode& node,
                              std::set<std::string>* completion_symbols,
                              std::vector<SymbolDef>* definitions,
                              std::vector<SymbolInfoEntry>* symbols,
                              const std::string& container_name = "") {
    if (!node) {
        return;
    }

    if (is_node<Program>(node)) {
        const auto& program = get_node<Program>(node);
        for (const auto& stmt : program.statements) {
            collect_symbols_from_ast(stmt, completion_symbols, definitions, symbols, container_name);
        }
        return;
    }

    if (is_node<Block>(node)) {
        const auto& block = get_node<Block>(node);
        for (const auto& stmt : block.statements) {
            collect_symbols_from_ast(stmt, completion_symbols, definitions, symbols, container_name);
        }
        return;
    }

    if (is_node<VariableDeclaration>(node)) {
        const auto& decl = get_node<VariableDeclaration>(node);
        maybe_add_symbol(
            completion_symbols,
            definitions,
            symbols,
            decl.name,
            variable_decl_type_string(decl),
            13,
            decl.line - 1,
            decl.column - 1,
            container_name);
        if (decl.value) {
            collect_symbols_from_ast(decl.value, completion_symbols, definitions, symbols, container_name);
        }
        return;
    }

    if (is_node<FunctionDeclaration>(node)) {
        const auto& decl = get_node<FunctionDeclaration>(node);
        maybe_add_symbol(
            completion_symbols,
            definitions,
            symbols,
            decl.name,
            decl.return_type,
            12,
            decl.line - 1,
            decl.column - 1,
            container_name);
        if (decl.body) {
            collect_symbols_from_ast(decl.body, completion_symbols, definitions, symbols, decl.name);
        }
        return;
    }

    if (is_node<DynamicFunctionDeclaration>(node)) {
        const auto& decl = get_node<DynamicFunctionDeclaration>(node);
        maybe_add_symbol(
            completion_symbols,
            definitions,
            symbols,
            decl.name,
            "any",
            12,
            decl.line - 1,
            decl.column - 1,
            container_name);
        if (decl.body) {
            collect_symbols_from_ast(decl.body, completion_symbols, definitions, symbols, decl.name);
        }
        return;
    }

    if (is_node<ExternalDeclaration>(node)) {
        const auto& decl = get_node<ExternalDeclaration>(node);
        maybe_add_symbol(
            completion_symbols,
            definitions,
            symbols,
            decl.name,
            decl.return_type,
            12,
            decl.line - 1,
            decl.column - 1,
            container_name);
        return;
    }

    if (is_node<ClassDeclaration>(node)) {
        const auto& decl = get_node<ClassDeclaration>(node);
        maybe_add_symbol(
            completion_symbols,
            definitions,
            symbols,
            decl.name,
            "class",
            5,
            decl.line - 1,
            decl.column - 1,
            container_name);
        for (const auto& field : decl.fields) {
            maybe_add_symbol(
                completion_symbols,
                definitions,
                symbols,
                field.name,
                field.type,
                8,
                field.line - 1,
                field.column - 1,
                decl.name);
            if (field.initializer) {
                collect_symbols_from_ast(field.initializer, completion_symbols, definitions, symbols, decl.name);
            }
        }
        for (const auto& method : decl.methods) {
            maybe_add_symbol(
                completion_symbols,
                definitions,
                symbols,
                method.name,
                method.return_type.empty() ? "any" : method.return_type,
                6,
                method.line - 1,
                method.column - 1,
                decl.name);
            if (method.body) {
                collect_symbols_from_ast(method.body, completion_symbols, definitions, symbols, method.name);
            }
        }
        return;
    }

    if (is_node<IfStatement>(node)) {
        const auto& stmt = get_node<IfStatement>(node);
        collect_symbols_from_ast(stmt.condition, completion_symbols, definitions, symbols, container_name);
        collect_symbols_from_ast(stmt.then_body, completion_symbols, definitions, symbols, container_name);
        collect_symbols_from_ast(stmt.else_body, completion_symbols, definitions, symbols, container_name);
        return;
    }

    if (is_node<WhileStatement>(node)) {
        const auto& stmt = get_node<WhileStatement>(node);
        collect_symbols_from_ast(stmt.condition, completion_symbols, definitions, symbols, container_name);
        collect_symbols_from_ast(stmt.then_body, completion_symbols, definitions, symbols, container_name);
        return;
    }

    if (is_node<ForInStatement>(node)) {
        const auto& stmt = get_node<ForInStatement>(node);
        maybe_add_symbol(
            completion_symbols, definitions, symbols, stmt.variable, "any", 13, -1, -1, container_name);
        collect_symbols_from_ast(stmt.iterable, completion_symbols, definitions, symbols, container_name);
        collect_symbols_from_ast(stmt.body, completion_symbols, definitions, symbols, container_name);
        return;
    }

    if (is_node<TryStatement>(node)) {
        const auto& stmt = get_node<TryStatement>(node);
        collect_symbols_from_ast(stmt.try_block, completion_symbols, definitions, symbols, container_name);
        for (const auto& catch_block : stmt.catch_blocks) {
            maybe_add_symbol(
                completion_symbols,
                definitions,
                symbols,
                catch_block.identifier,
                catch_block.exception_type.empty() ? "Exception" : catch_block.exception_type,
                13,
                catch_block.line - 1,
                catch_block.column - 1,
                container_name);
            collect_symbols_from_ast(catch_block.body, completion_symbols, definitions, symbols, container_name);
        }
        return;
    }

    if (is_node<ExpressionStatement>(node)) {
        collect_symbols_from_ast(get_node<ExpressionStatement>(node).expression, completion_symbols, definitions, symbols, container_name);
        return;
    }
    if (is_node<ReturnStatement>(node)) {
        collect_symbols_from_ast(get_node<ReturnStatement>(node).value, completion_symbols, definitions, symbols, container_name);
        return;
    }
    if (is_node<SetStatement>(node)) {
        collect_symbols_from_ast(get_node<SetStatement>(node).target, completion_symbols, definitions, symbols, container_name);
        collect_symbols_from_ast(get_node<SetStatement>(node).value, completion_symbols, definitions, symbols, container_name);
        return;
    }
}

void collect_occurrences_from_tokens(const std::vector<Token>& tokens,
                                     std::vector<SymbolOccurrence>* occurrences) {
    if (!occurrences) {
        return;
    }
    for (const auto& token : tokens) {
        if (token.type != T_NAME || token.value.empty()) {
            continue;
        }
        occurrences->push_back(SymbolOccurrence{
            token.value,
            std::max(0, token.line - 1),
            std::max(0, token.column - 1),
        });
    }
}

AnalysisResult analyze_document_text(const std::string& path,
                                     const std::string& text,
                                     const std::string& workspace_root_path = "") {
    AnalysisResult result;
    try {
        std::string source = text;
        std::string file_path = path.empty() ? std::string("unknown") : path;
        Tokenizer tokenizer(source, file_path);
        std::vector<Token> tokens = tokenizer.tokenize();
        collect_occurrences_from_tokens(tokens, &result.occurrences);
        collect_import_links_from_tokens(
            tokens,
            file_path,
            workspace_root_path,
            &result.import_links,
            &result.import_targets);

        Parser parser(std::move(tokens), file_path);
        ASTNode root = parser.parse_program();

        collect_symbols_from_ast(root, &result.completion_symbols, &result.definitions, &result.symbols);

        SemanticAnalyzer analyzer(file_path);
        analyzer.analyze(root);

        const auto& scopes = analyzer.get_symbol_table().get_scopes();
        if (!scopes.empty()) {
            for (const auto& [name, symbol] : scopes.front()) {
                if (symbol.symbol_type != "builtin" && !name.empty()) {
                    result.completion_symbols.insert(name);
                }
            }
        }

        for (const auto& err : analyzer.get_errors()) {
            Diagnostic diag;
            diag.message = err;
            auto line_col = extract_line_col(err);
            if (line_col.has_value()) {
                diag.line = line_col->first;
                diag.character = line_col->second;
            }
            result.diagnostics.push_back(std::move(diag));
        }
    } catch (const std::exception& ex) {
        Diagnostic diag;
        diag.message = ex.what();
        auto line_col = extract_line_col(diag.message);
        if (line_col.has_value()) {
            diag.line = line_col->first;
            diag.character = line_col->second;
        }
        result.diagnostics.push_back(std::move(diag));
    }
    return result;
}

const SymbolDef* find_definition_for_name(const DocumentState& doc,
                                          const std::string& name,
                                          int line,
                                          int character) {
    const SymbolDef* first_match = nullptr;
    const SymbolDef* best_prior = nullptr;

    for (const auto& def : doc.definitions) {
        if (def.name != name) {
            continue;
        }
        if (!first_match) {
            first_match = &def;
        }
        if (def.line < 0 || def.character < 0) {
            continue;
        }
        if (def.line < line || (def.line == line && def.character <= character)) {
            if (!best_prior ||
                def.line > best_prior->line ||
                (def.line == best_prior->line && def.character > best_prior->character)) {
                best_prior = &def;
            }
        }
    }

    return best_prior ? best_prior : first_match;
}

bool occurrence_matches_declaration(const SymbolOccurrence& occ,
                                    const std::vector<SymbolDef>& definitions,
                                    const std::string& name) {
    for (const auto& def : definitions) {
        if (def.name != name) {
            continue;
        }
        if (def.line == occ.line && def.character == occ.character) {
            return true;
        }
    }
    return false;
}

class LspServer {
public:
    void run() {
        while (true) {
            auto message = read_message();
            if (!message.has_value()) {
                break;
            }
            handle_message(message.value());
            if (exit_requested_) {
                break;
            }
        }
    }

private:
    std::unordered_map<std::string, DocumentState> documents_;
    std::string workspace_root_path_;
    bool shutdown_requested_ = false;
    bool exit_requested_ = false;

    std::optional<std::string> read_message() {
        std::string line;
        int content_length = -1;
        while (std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                break;
            }

            constexpr const char* kLengthPrefix = "Content-Length:";
            if (line.rfind(kLengthPrefix, 0) == 0) {
                std::string value = trim(line.substr(std::strlen(kLengthPrefix)));
                try {
                    content_length = std::stoi(value);
                } catch (...) {
                    content_length = -1;
                }
            }
        }
        if (content_length <= 0) {
            return std::nullopt;
        }

        std::string body(content_length, '\0');
        std::cin.read(body.data(), content_length);
        if (std::cin.gcount() != content_length) {
            return std::nullopt;
        }
        return body;
    }

    void send_message(const std::string& json) {
        std::cout << "Content-Length: " << json.size() << "\r\n\r\n" << json;
        std::cout.flush();
    }

    void send_response(const std::string& id_raw, const std::string& result_json) {
        send_message("{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":" + result_json + "}");
    }

    void send_error_response(const std::string& id_raw, int code, const std::string& message) {
        send_message(
            "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"error\":{\"code\":" + std::to_string(code) +
            ",\"message\":\"" + json_escape(message) + "\"}}");
    }

    void send_publish_diagnostics(const DocumentState& doc) {
        std::ostringstream diagnostics_json;
        diagnostics_json << "[";
        for (std::size_t i = 0; i < doc.diagnostics.size(); ++i) {
            if (i > 0) {
                diagnostics_json << ",";
            }
            const Diagnostic& diag = doc.diagnostics[i];
            int line = std::max(0, diag.line);
            int character = std::max(0, diag.character);
            diagnostics_json << "{"
                             << "\"range\":{\"start\":{\"line\":" << line
                             << ",\"character\":" << character
                             << "},\"end\":{\"line\":" << line
                             << ",\"character\":" << (character + 1) << "}},"
                             << "\"severity\":1,"
                             << "\"source\":\"mtc\","
                             << "\"message\":\"" << json_escape(diag.message) << "\""
                             << "}";
        }
        diagnostics_json << "]";

        std::ostringstream payload;
        payload << "{"
                << "\"jsonrpc\":\"2.0\","
                << "\"method\":\"textDocument/publishDiagnostics\","
                << "\"params\":{"
                << "\"uri\":\"" << json_escape(doc.uri) << "\","
                << "\"diagnostics\":" << diagnostics_json.str()
                << "}"
                << "}";
        send_message(payload.str());
    }

    void analyze_document(DocumentState* doc) {
        if (!doc) {
            return;
        }
        AnalysisResult analysis = analyze_document_text(doc->path, doc->text, workspace_root_path_);
        doc->diagnostics = std::move(analysis.diagnostics);
        doc->completion_symbols = std::move(analysis.completion_symbols);
        doc->definitions = std::move(analysis.definitions);
        doc->occurrences = std::move(analysis.occurrences);
        doc->symbols = std::move(analysis.symbols);
        doc->import_links = std::move(analysis.import_links);
        doc->import_targets = std::move(analysis.import_targets);
        send_publish_diagnostics(*doc);
    }

    std::unordered_set<std::string> open_document_paths() const {
        std::unordered_set<std::string> paths;
        for (const auto& [uri, doc] : documents_) {
            (void)uri;
            if (!doc.path.empty()) {
                paths.insert(doc.path);
            }
        }
        return paths;
    }

    std::optional<std::string> find_import_target_at_position(const DocumentState& doc,
                                                              int line,
                                                              int character) const {
        for (const auto& link : doc.import_links) {
            if (link.target_uri.empty() || link.line < 0 || link.start_character < 0 || link.end_character < 0) {
                continue;
            }
            if (line == link.line && character >= link.start_character && character < link.end_character) {
                return link.target_uri;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> find_import_target_for_word(const DocumentState& doc,
                                                           const std::string& name) const {
        auto it = doc.import_targets.find(name);
        if (it == doc.import_targets.end() || it->second.empty()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::string build_single_location_result(const std::string& uri,
                                             int line,
                                             int start_character,
                                             int end_character) const {
        const int clamped_line = std::max(0, line);
        const int clamped_start = std::max(0, start_character);
        const int clamped_end = std::max(clamped_start, end_character);
        std::ostringstream result;
        result << "[{"
               << "\"uri\":\"" << json_escape(uri) << "\","
               << "\"range\":{\"start\":{\"line\":" << clamped_line
               << ",\"character\":" << clamped_start
               << "},\"end\":{\"line\":" << clamped_line
               << ",\"character\":" << clamped_end << "}}"
               << "}]";
        return result.str();
    }

    void append_reference_locations_from_data(const std::string& uri,
                                              const std::vector<SymbolOccurrence>& occurrences,
                                              const std::vector<SymbolDef>& definitions,
                                              const std::string& name,
                                              bool include_declarations,
                                              std::vector<std::string>* out_locations) const {
        if (!out_locations) {
            return;
        }
        for (const auto& occ : occurrences) {
            if (occ.name != name || occ.line < 0 || occ.character < 0) {
                continue;
            }
            if (!include_declarations && occurrence_matches_declaration(occ, definitions, name)) {
                continue;
            }
            int end_character = occ.character + static_cast<int>(std::max<std::size_t>(1, name.size()));
            std::ostringstream loc;
            loc << "{"
                << "\"uri\":\"" << json_escape(uri) << "\","
                << "\"range\":{\"start\":{\"line\":" << occ.line
                << ",\"character\":" << occ.character
                << "},\"end\":{\"line\":" << occ.line
                << ",\"character\":" << end_character << "}}"
                << "}";
            out_locations->push_back(loc.str());
        }
    }

    std::vector<std::string> collect_reference_locations(const std::string& name,
                                                         bool include_declarations) const {
        std::vector<std::string> locations;

        for (const auto& [uri, doc] : documents_) {
            append_reference_locations_from_data(
                uri, doc.occurrences, doc.definitions, name, include_declarations, &locations);
        }

        if (workspace_root_path_.empty()) {
            return locations;
        }

        const std::unordered_set<std::string> open_paths = open_document_paths();
        for (const auto& path : list_workspace_mtc_files(workspace_root_path_)) {
            if (open_paths.count(path) > 0) {
                continue;
            }
            std::string text;
            if (!read_file_text(path, &text)) {
                continue;
            }
            AnalysisResult analysis = analyze_document_text(path, text, workspace_root_path_);
            append_reference_locations_from_data(
                path_to_uri(path),
                analysis.occurrences,
                analysis.definitions,
                name,
                include_declarations,
                &locations);
        }

        return locations;
    }

    std::vector<std::string> collect_document_symbols_json(const std::string& uri,
                                                           const std::vector<SymbolInfoEntry>& symbols,
                                                           const std::string& query = "") const {
        std::vector<std::string> items;
        for (const auto& symbol : symbols) {
            if (symbol.name.empty() || symbol.line < 0 || symbol.character < 0) {
                continue;
            }
            if (!query.empty() &&
                !contains_case_insensitive(symbol.name, query) &&
                !contains_case_insensitive(symbol.container_name, query)) {
                continue;
            }
            const int end_character =
                symbol.character + static_cast<int>(std::max<std::size_t>(1, symbol.name.size()));
            std::ostringstream item;
            item << "{"
                 << "\"name\":\"" << json_escape(symbol.name) << "\","
                 << "\"kind\":" << symbol.kind << ","
                 << "\"location\":{\"uri\":\"" << json_escape(uri) << "\","
                 << "\"range\":{\"start\":{\"line\":" << symbol.line
                 << ",\"character\":" << symbol.character
                 << "},\"end\":{\"line\":" << symbol.line
                 << ",\"character\":" << end_character << "}}},"
                 << "\"containerName\":\"" << json_escape(symbol.container_name) << "\""
                 << "}";
            items.push_back(item.str());
        }
        return items;
    }

    std::string build_completion_result(const std::string& uri) {
        std::set<std::string> labels;
        for (const auto& kw : KEYWORDS) {
            labels.insert(kw);
        }
        for (const auto& builtin : builtin_completion_items()) {
            labels.insert(builtin);
        }

        auto it = documents_.find(uri);
        if (it != documents_.end()) {
            labels.insert(it->second.completion_symbols.begin(), it->second.completion_symbols.end());
            for (const auto& def : it->second.definitions) {
                labels.insert(def.name);
            }
        }

        std::ostringstream items;
        items << "[";
        bool first = true;
        for (const auto& label : labels) {
            if (!first) {
                items << ",";
            }
            first = false;
            const bool is_keyword = KEYWORDS.count(label) > 0;
            const int kind = is_keyword ? 14 : 6;
            items << "{"
                  << "\"label\":\"" << json_escape(label) << "\","
                  << "\"kind\":" << kind
                  << "}";
        }
        items << "]";

        return std::string("{\"isIncomplete\":false,\"items\":") + items.str() + "}";
    }

    std::string build_libc_completion_result(const ImportCompletionInfo& info) {
        std::ostringstream items;
        items << "[";
        bool first = true;
        for (const auto& [name, func] : LIBC_FUNCTIONS) {
            if (info.already_imported.count(name) > 0) {
                continue;
            }
            if (!first) {
                items << ",";
            }
            first = false;

            std::ostringstream detail;
            detail << "(";
            for (std::size_t i = 0; i < func.args.size(); ++i) {
                if (i > 0) {
                    detail << ", ";
                }
                detail << func.args[i];
            }
            if (func.var_arg) {
                if (!func.args.empty()) {
                    detail << ", ";
                }
                detail << "...";
            }
            detail << ") -> " << func.ret;

            items << "{"
                  << "\"label\":\"" << json_escape(name) << "\","
                  << "\"kind\":3,"
                  << "\"detail\":\"" << json_escape(detail.str()) << "\""
                  << "}";
        }
        items << "]";
        return std::string("{\"isIncomplete\":false,\"items\":") + items.str() + "}";
    }

    std::vector<std::filesystem::path> import_search_bases(const std::string& current_file_path) {
        std::vector<std::filesystem::path> bases;
        if (!current_file_path.empty()) {
            std::filesystem::path current_path(current_file_path);
            if (current_path.has_parent_path()) {
                bases.push_back(current_path.parent_path());
            }
        }
        if (!workspace_root_path_.empty()) {
            std::filesystem::path workspace_path(workspace_root_path_);
            const std::string ws_str = workspace_path.lexically_normal().string();
            bool exists = false;
            for (const auto& base : bases) {
                if (base.lexically_normal().string() == ws_str) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                bases.push_back(workspace_path);
            }
        }
        return bases;
    }

    std::string build_module_path_completion_result(const ImportCompletionInfo& info,
                                                     const std::string& current_file_path,
                                                     int line, int character) {
        std::string dir_rel = info.module_name;
        std::replace(dir_rel.begin(), dir_rel.end(), '.', '/');

        auto bases = import_search_bases(current_file_path);

        std::set<std::string> seen;
        std::ostringstream items;
        items << "[";
        bool first = true;
        std::error_code ec;
        for (const auto& base : bases) {
            std::filesystem::path dir_path = base / dir_rel;
            if (!std::filesystem::is_directory(dir_path, ec)) {
                continue;
            }
            std::filesystem::directory_iterator dir_it(dir_path, ec);
            if (ec) {
                continue;
            }
            for (const auto& entry : dir_it) {
                std::string entry_name;
                if (entry.is_regular_file(ec) && entry.path().extension() == ".mtc") {
                    entry_name = entry.path().stem().string();
                } else if (entry.is_directory(ec)) {
                    entry_name = entry.path().filename().string();
                }
                if (entry_name.empty() || seen.count(entry_name) > 0) {
                    continue;
                }
                seen.insert(entry_name);
                if (!first) {
                    items << ",";
                }
                first = false;
                // textEdit tells VS Code to insert at cursor, bypassing word-based filtering
                items << "{"
                      << "\"label\":\"" << json_escape(entry_name) << "\","
                      << "\"kind\":9,"
                      << "\"textEdit\":{"
                      << "\"range\":{\"start\":{\"line\":" << line << ",\"character\":" << character
                      << "},\"end\":{\"line\":" << line << ",\"character\":" << character << "}},"
                      << "\"newText\":\"" << json_escape(entry_name) << "\""
                      << "}"
                      << "}";
            }
        }
        items << "]";
        return std::string("{\"isIncomplete\":false,\"items\":") + items.str() + "}";
    }

    std::string build_from_module_completion_result(const std::string& current_file_path) {
        auto bases = import_search_bases(current_file_path);

        std::set<std::string> seen;
        seen.insert("libc");

        std::ostringstream items;
        items << "[";
        // Always suggest libc
        items << "{\"label\":\"libc\",\"kind\":9,\"detail\":\"C standard library\"}";

        std::error_code ec;
        for (const auto& base : bases) {
            if (!std::filesystem::is_directory(base, ec)) {
                continue;
            }
            std::filesystem::directory_iterator dir_it(base, ec);
            if (ec) {
                continue;
            }
            for (const auto& entry : dir_it) {
                std::string entry_name;
                if (entry.is_regular_file(ec) && entry.path().extension() == ".mtc") {
                    entry_name = entry.path().stem().string();
                } else if (entry.is_directory(ec)) {
                    entry_name = entry.path().filename().string();
                }
                if (entry_name.empty() || entry_name[0] == '.' || seen.count(entry_name) > 0) {
                    continue;
                }
                seen.insert(entry_name);
                items << ",{"
                      << "\"label\":\"" << json_escape(entry_name) << "\","
                      << "\"kind\":9"
                      << "}";
            }
        }
        items << "]";
        return std::string("{\"isIncomplete\":false,\"items\":") + items.str() + "}";
    }

    std::string build_module_symbols_completion_result(const ImportCompletionInfo& info,
                                                       const std::string& current_file_path) {
        auto resolved = resolve_import_module_uri(current_file_path, workspace_root_path_, info.module_name);
        if (!resolved.has_value()) {
            return "{\"isIncomplete\":false,\"items\":[]}";
        }

        std::set<std::string> labels;
        auto doc_it = documents_.find(resolved.value());
        if (doc_it != documents_.end()) {
            labels.insert(doc_it->second.completion_symbols.begin(),
                          doc_it->second.completion_symbols.end());
            for (const auto& def : doc_it->second.definitions) {
                labels.insert(def.name);
            }
        } else {
            std::string file_path = uri_to_path(resolved.value());
            std::string text;
            if (read_file_text(file_path, &text)) {
                AnalysisResult analysis = analyze_document_text(file_path, text, workspace_root_path_);
                labels.insert(analysis.completion_symbols.begin(),
                              analysis.completion_symbols.end());
                for (const auto& def : analysis.definitions) {
                    labels.insert(def.name);
                }
            }
        }

        std::ostringstream items;
        items << "[";
        bool first = true;
        for (const auto& label : labels) {
            if (info.already_imported.count(label) > 0) {
                continue;
            }
            if (KEYWORDS.count(label) > 0) {
                continue;
            }
            if (!first) {
                items << ",";
            }
            first = false;
            items << "{"
                  << "\"label\":\"" << json_escape(label) << "\","
                  << "\"kind\":6"
                  << "}";
        }
        items << "]";
        return std::string("{\"isIncomplete\":false,\"items\":") + items.str() + "}";
    }

    void handle_initialize(const std::string& id_raw, const std::string& message) {
        auto root_uri = json_get_string(message, "rootUri");
        if (root_uri.has_value()) {
            workspace_root_path_ = uri_to_path(root_uri.value());
        }
        const std::string capabilities =
            "{"
            "\"capabilities\":{"
            "\"textDocumentSync\":{\"openClose\":true,\"change\":2,\"save\":{\"includeText\":false}},"
            "\"completionProvider\":{\"resolveProvider\":false,\"triggerCharacters\":[\".\"]},"
            "\"definitionProvider\":true,"
            "\"hoverProvider\":true,"
            "\"referencesProvider\":true,"
            "\"documentSymbolProvider\":true,"
            "\"workspaceSymbolProvider\":true,"
            "\"renameProvider\":{\"prepareProvider\":true}"
            "},"
            "\"serverInfo\":{\"name\":\"mtc-lsp\",\"version\":\"0.3.0\"}"
            "}";
        send_response(id_raw, capabilities);
    }

    void handle_did_open(const std::string& message) {
        auto uri = json_get_string(message, "uri");
        auto text = json_get_string(message, "text");
        auto version = json_get_int(message, "version");
        if (!uri.has_value() || !text.has_value()) {
            return;
        }

        DocumentState doc;
        doc.uri = uri.value();
        doc.path = uri_to_path(doc.uri);
        doc.text = text.value();
        if (version.has_value()) {
            doc.version = version.value();
        }
        documents_[doc.uri] = std::move(doc);
        analyze_document(&documents_[uri.value()]);
    }

    void handle_did_change(const std::string& message) {
        auto uri = json_get_string(message, "uri");
        if (!uri.has_value()) {
            return;
        }

        auto doc_it = documents_.find(uri.value());
        if (doc_it == documents_.end()) {
            DocumentState doc;
            doc.uri = uri.value();
            doc.path = uri_to_path(doc.uri);
            documents_[doc.uri] = std::move(doc);
            doc_it = documents_.find(uri.value());
        }

        auto version = json_get_int(message, "version");
        if (version.has_value()) {
            doc_it->second.version = version.value();
        }

        std::vector<TextChange> changes = parse_content_changes(message);
        if (changes.empty()) {
            auto full_text = json_get_string(message, "text");
            if (full_text.has_value()) {
                doc_it->second.text = full_text.value();
            } else {
                return;
            }
        } else {
            for (const auto& change : changes) {
                apply_text_change(&doc_it->second.text, change);
            }
        }

        analyze_document(&doc_it->second);
    }

    void handle_did_save(const std::string& message) {
        auto uri = json_get_string(message, "uri");
        if (!uri.has_value()) {
            return;
        }
        auto it = documents_.find(uri.value());
        if (it != documents_.end()) {
            analyze_document(&it->second);
        }
    }

    void handle_did_close(const std::string& message) {
        auto uri = json_get_string(message, "uri");
        if (!uri.has_value()) {
            return;
        }
        documents_.erase(uri.value());
    }

    void handle_completion(const std::string& id_raw, const std::string& message) {
        auto uri = json_get_string(message, "uri");
        if (!uri.has_value()) {
            send_response(id_raw, "{\"isIncomplete\":false,\"items\":[]}");
            return;
        }

        auto line = json_get_int(message, "line");
        auto character = json_get_int(message, "character");

        if (line.has_value() && character.has_value()) {
            auto doc_it = documents_.find(uri.value());
            if (doc_it != documents_.end()) {
                std::string line_text = get_line_text(doc_it->second.text, line.value());
                ImportCompletionInfo info = detect_import_context(line_text, character.value());

                switch (info.context) {
                    case ImportCompletionContext::FROM_MODULE:
                        send_response(id_raw, build_from_module_completion_result(doc_it->second.path));
                        return;
                    case ImportCompletionContext::LIBC_SYMBOLS:
                        send_response(id_raw, build_libc_completion_result(info));
                        return;
                    case ImportCompletionContext::MODULE_PATH:
                        send_response(id_raw, build_module_path_completion_result(
                            info, doc_it->second.path, line.value(), character.value()));
                        return;
                    case ImportCompletionContext::MODULE_SYMBOLS:
                        send_response(id_raw, build_module_symbols_completion_result(info, doc_it->second.path));
                        return;
                    case ImportCompletionContext::NONE:
                        break;
                }
            }
        }

        send_response(id_raw, build_completion_result(uri.value()));
    }

    std::optional<std::pair<std::string, SymbolDef>> resolve_definition_location(
        const std::string& current_uri,
        const std::string& name,
        int line,
        int character) const {
        auto current_it = documents_.find(current_uri);
        if (current_it != documents_.end()) {
            const SymbolDef* def = find_definition_for_name(current_it->second, name, line, character);
            if (def && def->line >= 0 && def->character >= 0) {
                return std::make_pair(current_uri, *def);
            }
        }

        for (const auto& [uri, doc] : documents_) {
            if (uri == current_uri) {
                continue;
            }
            const SymbolDef* def = find_definition_for_name(doc, name, line, character);
            if (def && def->line >= 0 && def->character >= 0) {
                return std::make_pair(uri, *def);
            }
        }

        if (workspace_root_path_.empty()) {
            return std::nullopt;
        }

        const std::unordered_set<std::string> open_paths = open_document_paths();
        for (const auto& path : list_workspace_mtc_files(workspace_root_path_)) {
            if (open_paths.count(path) > 0) {
                continue;
            }
            std::string text;
            if (!read_file_text(path, &text)) {
                continue;
            }
            AnalysisResult analysis = analyze_document_text(path, text, workspace_root_path_);
            for (const auto& def : analysis.definitions) {
                if (def.name == name && def.line >= 0 && def.character >= 0) {
                    return std::make_pair(path_to_uri(path), def);
                }
            }
        }

        return std::nullopt;
    }

    void handle_definition(const std::string& id_raw, const std::string& message) {
        auto uri = json_get_string(message, "uri");
        auto line = json_get_int(message, "line");
        auto character = json_get_int(message, "character");
        if (!uri.has_value() || !line.has_value() || !character.has_value()) {
            send_response(id_raw, "[]");
            return;
        }

        auto doc_it = documents_.find(uri.value());
        if (doc_it == documents_.end()) {
            send_response(id_raw, "[]");
            return;
        }

        auto import_target_at_cursor =
            find_import_target_at_position(doc_it->second, line.value(), character.value());
        if (import_target_at_cursor.has_value()) {
            send_response(
                id_raw,
                build_single_location_result(import_target_at_cursor.value(), 0, 0, 1));
            return;
        }

        auto word = word_at_position(doc_it->second.text, line.value(), character.value());
        if (!word.has_value()) {
            send_response(id_raw, "[]");
            return;
        }

        auto resolved = resolve_definition_location(
            uri.value(), word.value(), line.value(), character.value());
        if (!resolved.has_value()) {
            auto import_target_for_word = find_import_target_for_word(doc_it->second, word.value());
            if (import_target_for_word.has_value()) {
                send_response(
                    id_raw,
                    build_single_location_result(import_target_for_word.value(), 0, 0, 1));
                return;
            }
            send_response(id_raw, "[]");
            return;
        }

        const std::string def_uri = resolved->first;
        const SymbolDef& def = resolved->second;
        const int end_character = def.character + static_cast<int>(std::max<std::size_t>(1, def.name.size()));
        send_response(
            id_raw,
            build_single_location_result(def_uri, def.line, def.character, end_character));
    }

    void handle_hover(const std::string& id_raw, const std::string& message) {
        auto uri = json_get_string(message, "uri");
        auto line = json_get_int(message, "line");
        auto character = json_get_int(message, "character");
        if (!uri.has_value() || !line.has_value() || !character.has_value()) {
            send_response(id_raw, "null");
            return;
        }

        auto doc_it = documents_.find(uri.value());
        if (doc_it == documents_.end()) {
            send_response(id_raw, "null");
            return;
        }

        auto word = word_at_position(doc_it->second.text, line.value(), character.value());
        if (!word.has_value()) {
            send_response(id_raw, "null");
            return;
        }

        auto resolved = resolve_definition_location(uri.value(), word.value(), line.value(), character.value());
        if (!resolved.has_value()) {
            if (KEYWORDS.count(word.value()) > 0) {
                send_response(
                    id_raw,
                    "{\"contents\":{\"kind\":\"plaintext\",\"value\":\"keyword " + json_escape(word.value()) + "\"}}");
            } else {
                send_response(id_raw, "null");
            }
            return;
        }

        const SymbolDef& def = resolved->second;
        const std::string hover_value = def.name + ": " + (def.type.empty() ? "any" : def.type);
        send_response(
            id_raw,
            "{\"contents\":{\"kind\":\"plaintext\",\"value\":\"" + json_escape(hover_value) + "\"}}");
    }

    void handle_references(const std::string& id_raw, const std::string& message) {
        auto uri = json_get_string(message, "uri");
        auto line = json_get_int(message, "line");
        auto character = json_get_int(message, "character");
        if (!uri.has_value() || !line.has_value() || !character.has_value()) {
            send_response(id_raw, "[]");
            return;
        }
        auto doc_it = documents_.find(uri.value());
        if (doc_it == documents_.end()) {
            send_response(id_raw, "[]");
            return;
        }

        auto word = word_at_position(doc_it->second.text, line.value(), character.value());
        if (!word.has_value()) {
            send_response(id_raw, "[]");
            return;
        }

        bool include_declarations = true;
        auto include_decl = json_get_bool(message, "includeDeclaration");
        if (include_decl.has_value()) {
            include_declarations = include_decl.value();
        }

        std::vector<std::string> locations =
            collect_reference_locations(word.value(), include_declarations);
        std::ostringstream result;
        result << "[";
        for (std::size_t i = 0; i < locations.size(); ++i) {
            if (i > 0) {
                result << ",";
            }
            result << locations[i];
        }
        result << "]";
        send_response(id_raw, result.str());
    }

    void handle_document_symbol(const std::string& id_raw, const std::string& message) {
        auto uri = json_get_string(message, "uri");
        if (!uri.has_value()) {
            send_response(id_raw, "[]");
            return;
        }
        auto doc_it = documents_.find(uri.value());
        if (doc_it == documents_.end()) {
            send_response(id_raw, "[]");
            return;
        }

        std::vector<std::string> items =
            collect_document_symbols_json(uri.value(), doc_it->second.symbols);
        std::ostringstream result;
        result << "[";
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i > 0) {
                result << ",";
            }
            result << items[i];
        }
        result << "]";
        send_response(id_raw, result.str());
    }

    void handle_workspace_symbol(const std::string& id_raw, const std::string& message) {
        const std::string query = json_get_string(message, "query").value_or("");
        std::vector<std::string> items;
        items.reserve(256);

        std::unordered_set<std::string> open_paths;
        for (const auto& [uri, doc] : documents_) {
            open_paths.insert(doc.path);
            std::vector<std::string> doc_items =
                collect_document_symbols_json(uri, doc.symbols, query);
            items.insert(items.end(), doc_items.begin(), doc_items.end());
        }

        if (!workspace_root_path_.empty()) {
            for (const auto& path : list_workspace_mtc_files(workspace_root_path_)) {
                if (open_paths.count(path) > 0) {
                    continue;
                }
                std::string text;
                if (!read_file_text(path, &text)) {
                    continue;
                }
                AnalysisResult analysis = analyze_document_text(path, text, workspace_root_path_);
                std::vector<std::string> doc_items =
                    collect_document_symbols_json(path_to_uri(path), analysis.symbols, query);
                items.insert(items.end(), doc_items.begin(), doc_items.end());
                if (items.size() >= 1000) {
                    break;
                }
            }
        }

        std::ostringstream result;
        result << "[";
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i > 0) {
                result << ",";
            }
            result << items[i];
        }
        result << "]";
        send_response(id_raw, result.str());
    }

    void handle_prepare_rename(const std::string& id_raw, const std::string& message) {
        auto uri = json_get_string(message, "uri");
        auto line = json_get_int(message, "line");
        auto character = json_get_int(message, "character");
        if (!uri.has_value() || !line.has_value() || !character.has_value()) {
            send_response(id_raw, "null");
            return;
        }
        auto doc_it = documents_.find(uri.value());
        if (doc_it == documents_.end()) {
            send_response(id_raw, "null");
            return;
        }

        auto word_info = word_info_at_position(doc_it->second.text, line.value(), character.value());
        if (!word_info.has_value()) {
            send_response(id_raw, "null");
            return;
        }
        if (KEYWORDS.count(word_info->word) > 0) {
            send_response(id_raw, "null");
            return;
        }

        std::ostringstream result;
        result << "{"
               << "\"range\":{\"start\":{\"line\":" << word_info->line
               << ",\"character\":" << word_info->start_character
               << "},\"end\":{\"line\":" << word_info->line
               << ",\"character\":" << word_info->end_character << "}},"
               << "\"placeholder\":\"" << json_escape(word_info->word) << "\""
               << "}";
        send_response(id_raw, result.str());
    }

    void handle_rename(const std::string& id_raw, const std::string& message) {
        auto uri = json_get_string(message, "uri");
        auto line = json_get_int(message, "line");
        auto character = json_get_int(message, "character");
        auto new_name = json_get_string(message, "newName");
        if (!uri.has_value() || !line.has_value() || !character.has_value() || !new_name.has_value()) {
            send_response(id_raw, "{\"changes\":{}}");
            return;
        }
        if (!is_valid_identifier_name(*new_name) || KEYWORDS.count(*new_name) > 0) {
            send_response(id_raw, "{\"changes\":{}}");
            return;
        }

        auto doc_it = documents_.find(uri.value());
        if (doc_it == documents_.end()) {
            send_response(id_raw, "{\"changes\":{}}");
            return;
        }

        auto word = word_at_position(doc_it->second.text, line.value(), character.value());
        if (!word.has_value()) {
            send_response(id_raw, "{\"changes\":{}}");
            return;
        }

        std::vector<std::string> locations = collect_reference_locations(word.value(), true);
        std::unordered_map<std::string, std::vector<std::pair<int, int>>> edits_by_uri;
        for (const auto& loc : locations) {
            auto loc_uri = json_get_string(loc, "uri");
            auto loc_line = json_get_int(loc, "line");
            auto loc_char = json_get_int(loc, "character");
            if (!loc_uri.has_value() || !loc_line.has_value() || !loc_char.has_value()) {
                continue;
            }
            edits_by_uri[loc_uri.value()].push_back({loc_line.value(), loc_char.value()});
        }

        std::ostringstream changes;
        changes << "{";
        bool first_uri = true;
        for (const auto& [edit_uri, edit_positions] : edits_by_uri) {
            if (!first_uri) {
                changes << ",";
            }
            first_uri = false;
            changes << "\"" << json_escape(edit_uri) << "\":[";
            for (std::size_t i = 0; i < edit_positions.size(); ++i) {
                if (i > 0) {
                    changes << ",";
                }
                const int edit_line = edit_positions[i].first;
                const int edit_char = edit_positions[i].second;
                changes << "{"
                        << "\"range\":{\"start\":{\"line\":" << edit_line
                        << ",\"character\":" << edit_char
                        << "},\"end\":{\"line\":" << edit_line
                        << ",\"character\":" << (edit_char + static_cast<int>(word->size()))
                        << "}},"
                        << "\"newText\":\"" << json_escape(new_name.value()) << "\""
                        << "}";
            }
            changes << "]";
        }
        changes << "}";

        send_response(id_raw, "{\"changes\":" + changes.str() + "}");
    }

    void handle_message(const std::string& message) {
        auto method = json_get_string(message, "method");
        auto id_raw = json_get_raw_value(message, "id");

        if (!method.has_value()) {
            return;
        }

        if (method.value() == "initialize") {
            if (id_raw.has_value()) {
                handle_initialize(id_raw.value(), message);
            }
            return;
        }
        if (method.value() == "initialized") {
            return;
        }
        if (method.value() == "shutdown") {
            shutdown_requested_ = true;
            if (id_raw.has_value()) {
                send_response(id_raw.value(), "null");
            }
            return;
        }
        if (method.value() == "exit") {
            exit_requested_ = true;
            return;
        }

        if (method.value() == "textDocument/didOpen") {
            handle_did_open(message);
            return;
        }
        if (method.value() == "textDocument/didChange") {
            handle_did_change(message);
            return;
        }
        if (method.value() == "textDocument/didSave") {
            handle_did_save(message);
            return;
        }
        if (method.value() == "textDocument/didClose") {
            handle_did_close(message);
            return;
        }
        if (method.value() == "textDocument/completion") {
            if (id_raw.has_value()) {
                handle_completion(id_raw.value(), message);
            }
            return;
        }
        if (method.value() == "textDocument/definition") {
            if (id_raw.has_value()) {
                handle_definition(id_raw.value(), message);
            }
            return;
        }
        if (method.value() == "textDocument/hover") {
            if (id_raw.has_value()) {
                handle_hover(id_raw.value(), message);
            }
            return;
        }
        if (method.value() == "textDocument/references") {
            if (id_raw.has_value()) {
                handle_references(id_raw.value(), message);
            }
            return;
        }
        if (method.value() == "textDocument/documentSymbol") {
            if (id_raw.has_value()) {
                handle_document_symbol(id_raw.value(), message);
            }
            return;
        }
        if (method.value() == "workspace/symbol") {
            if (id_raw.has_value()) {
                handle_workspace_symbol(id_raw.value(), message);
            }
            return;
        }
        if (method.value() == "textDocument/prepareRename") {
            if (id_raw.has_value()) {
                handle_prepare_rename(id_raw.value(), message);
            }
            return;
        }
        if (method.value() == "textDocument/rename") {
            if (id_raw.has_value()) {
                handle_rename(id_raw.value(), message);
            }
            return;
        }

        if (id_raw.has_value()) {
            if (shutdown_requested_) {
                send_error_response(id_raw.value(), -32600, "Server is shutting down");
            } else {
                send_response(id_raw.value(), "null");
            }
        }
    }
};

}  // namespace

int main() {
    LspServer server;
    server.run();
    return 0;
}
