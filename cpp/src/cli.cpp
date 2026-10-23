#include "cli.hpp"

#include <istream>
#include <ostream>
#include <string>

namespace votesc {
namespace {

void printMenu(std::ostream& out) {
    out << "\n========== 投票智能合约 ==========\n"
        << "1. 投票\n"
        << "2. 查询用户投票\n"
        << "3. 查询全部投票\n"
        << "4. 退出\n";
}

// 去掉首尾的 ASCII 空白，并去掉 Windows 换行残留的 '\r'。
std::string trim(const std::string& text) {
    const char* whitespace = " \t\r\n";
    const std::size_t first = text.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return "";
    }
    const std::size_t last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

// 读取一行输入并去掉首尾空白。
// 返回 false 表示输入流结束（EOF），调用方应正常退出。
bool readLine(std::istream& in, std::ostream& out, const std::string& prompt,
              std::string& value) {
    out << prompt;
    if (!std::getline(in, value)) {
        return false;
    }
    value = trim(value);
    return true;
}

void printError(std::ostream& out, const std::string& message) {
    out << "错误：" << message << '\n';
}

} // namespace

int runCli(VoteContract& contract, std::istream& in, std::ostream& out) {
    while (true) {
        printMenu(out);

        std::string choice;
        if (!readLine(in, out, "请选择操作 [1-4]: ", choice)) {
            return 0;
        }

        if (choice == "4") {
            out << "再见。\n";
            return 0;
        }

        if (choice == "1") {
            std::string username;
            if (!readLine(in, out, "请输入用户名: ", username)) {
                return 0;
            }
            try {
                const Vote vote = contract.voteUser(username);
                out << "投票成功：" << voteToJson(vote) << '\n';
            } catch (const VoteError& error) {
                // 空用户名、账本数据损坏等错误只影响本次操作。
                printError(out, error.what());
            }
        } else if (choice == "2") {
            std::string username;
            if (!readLine(in, out, "请输入用户名: ", username)) {
                return 0;
            }
            try {
                const Vote vote = contract.getUserVote(username);
                out << "用户 " << username << " 的投票信息：" << voteToJson(vote) << '\n';
            } catch (const UserNotFoundError& error) {
                printError(out, "用户 \"" + error.username() + "\" 不存在");
            } catch (const VoteError& error) {
                printError(out, error.what());
            }
        } else if (choice == "3") {
            try {
                const std::vector<Vote> votes = contract.getAllVotes();
                if (votes.empty()) {
                    out << "当前没有投票记录。\n";
                } else {
                    out << "共 " << votes.size() << " 条投票记录（按用户名排序）：\n";
                    for (const Vote& vote : votes) {
                        out << voteToJson(vote) << '\n';
                    }
                }
            } catch (const VoteError& error) {
                printError(out, error.what());
            }
        } else {
            out << "无效的选项，请输入 1-4。\n";
        }
    }
}

} // namespace votesc
