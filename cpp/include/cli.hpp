// 交互式命令行界面。
//
// 把输入/输出流作为参数传入，便于在测试中替换为字符串流；
// 同时避免在 CLI 内部直接依赖 std::cin / std::cout。
#pragma once

#include <iosfwd>

#include "contract.hpp"

namespace votesc {

// 运行菜单循环。
// 返回 0 表示正常退出（选择“退出”或输入流结束），不会因为单条命令失败而终止。
int runCli(VoteContract& contract, std::istream& in, std::ostream& out);

} // namespace votesc
