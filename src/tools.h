// 工具调用框架：工具注册表 + 内置工具。
#ifndef CHAT_BOT_TOOLS_H
#define CHAT_BOT_TOOLS_H

#include <functional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

// 单个工具的定义
struct Tool {
    std::string name;                              // 工具名（模型调用时使用）
    std::string description;                       // 工具用途说明
    std::string args_hint;                         // 参数说明（给模型看的示例）
    std::function<std::string(const json&)> run;   // 执行函数，入参为 arguments 对象
};

// 工具注册表
class ToolRegistry {
public:
    void add(const Tool& tool);
    bool has(const std::string& name) const;

    // 执行工具，捕获异常并返回字符串结果（失败时返回错误描述）
    std::string call(const std::string& name, const json& args) const;

    // 生成给模型看的工具清单文本（用于 system 提示词）
    std::string prompt_spec() const;

    // 简短列表（用于 /tools 命令）
    std::string list_for_user() const;

    bool empty() const { return tools_.empty(); }

private:
    std::vector<Tool> tools_;
};

// 注册所有内置工具
void register_builtin_tools(ToolRegistry& registry);

#endif  // CHAT_BOT_TOOLS_H
