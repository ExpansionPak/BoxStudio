static std::string ToUtf8(const fs::path& path)
{
    return path.u8string();
}

static std::string ToUtf8(const std::wstring& value)
{
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(0, size - 1)), '\0');
    if (size > 1) WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
}

static std::string EscapeJson(const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

static std::string Slurp(const fs::path& path);
static std::vector<std::string> SplitArgs(const std::string& args);

struct MacroCall {
    size_t start = 0;
    size_t end = 0;
    std::string name;
    std::string args;
};

static std::vector<MacroCall> FindObjectMacros(const std::string& text);

static std::string SanitizeFolderName(std::string value)
{
    for (char& c : value) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ';
        if (!ok) c = '_';
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value.empty() ? "Untitled BoxStudio Project" : value;
}

static std::string ToLevelEnumName(const std::string& levelName)
{
    std::string value = "LEVEL_";
    for (char c : levelName) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            value += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        } else {
            value += '_';
        }
    }
    return value;
}

static std::string SanitizeLevelName(std::string value)
{
    for (char& c : value) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            c = '_';
        }
    }
    while (value.find("__") != std::string::npos) {
        value = std::regex_replace(value, std::regex("__+"), "_");
    }
    while (!value.empty() && value.front() == '_') value.erase(value.begin());
    while (!value.empty() && value.back() == '_') value.pop_back();
    return value.empty() ? "boxstudio_level" : value;
}
