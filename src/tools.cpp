#include "tools.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <string>

// ---------------------------------------------------------------------------
// 工具注册表实现
// ---------------------------------------------------------------------------

void ToolRegistry::add(const Tool& tool) { tools_.push_back(tool); }

bool ToolRegistry::has(const std::string& name) const {
    for (const auto& t : tools_) {
        if (t.name == name) return true;
    }
    return false;
}

std::string ToolRegistry::call(const std::string& name, const json& args) const {
    for (const auto& t : tools_) {
        if (t.name == name) {
            try {
                return t.run(args);
            } catch (const std::exception& e) {
                return std::string("[工具执行出错] ") + e.what();
            }
        }
    }
    return "[未知工具] " + name;
}

std::string ToolRegistry::prompt_spec() const {
    std::string s;
    for (const auto& t : tools_) {
        s += "- " + t.name + ": " + t.description +
             " 参数: " + t.args_hint + "\n";
    }
    return s;
}

std::string ToolRegistry::list_for_user() const {
    if (tools_.empty()) return "（无可用工具）\n";
    std::string s;
    for (const auto& t : tools_) {
        s += "  " + t.name + " - " + t.description + "\n";
    }
    return s;
}

// ---------------------------------------------------------------------------
// 工具辅助函数
// ---------------------------------------------------------------------------

namespace {

// 对字符串做 URL 百分号编码（UTF-8 逐字节）。
// 只保留 RFC3986 unreserved 字符，其余一律 %XX，确保拼进 shell 命令时无注入风险。
std::string url_encode(const std::string& value) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

// 去掉首尾空白
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 执行命令并捕获标准输出（命令本身必须已是安全字符串）。
std::string run_command_capture(const std::string& cmd) {
    std::array<char, 512> buf{};
    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return "[无法执行命令]";
    }
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        out += buf.data();
    }
    pclose(pipe);
    return out;
}

// 内置工具：查询天气（调用 wttr.in，无需 API key）
std::string tool_get_weather(const json& args) {
    if (!args.is_object() || !args.contains("location")) {
        return "[参数缺失] 需要 location，例如 {\"location\":\"北京\"}";
    }
    std::string location;
    if (args["location"].is_string()) {
        location = args["location"].get<std::string>();
    } else {
        location = args["location"].dump();
    }
    location = trim(location);
    if (location.empty()) {
        return "[参数错误] location 不能为空";
    }

    // 因为对 location 做了严格 URL 编码，整条 URL 只含安全字符，拼入命令无注入风险。
    std::string url = "https://wttr.in/" + url_encode(location) +
                      "?format=3&m&lang=zh";
    std::string cmd = "curl -s --max-time 15 \"" + url + "\"";

    std::string result = trim(run_command_capture(cmd));
    if (result.empty()) {
        return "[查询失败] 无法获取 \"" + location +
               "\" 的天气，可能是网络不可用。";
    }
    // wttr.in 对未知地点会返回 "Unknown location" 之类的提示，原样返回即可。
    return result;
}

}  // namespace

void register_builtin_tools(ToolRegistry& registry) {
    registry.add(Tool{
        "get_weather",
        "查询指定地点的实时天气",
        "{\"location\": \"城市名，如 北京 / Shanghai\"}",
        tool_get_weather,
    });
}
