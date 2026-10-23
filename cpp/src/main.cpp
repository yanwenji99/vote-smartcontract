#include <exception>
#include <iostream>

#include "cli.hpp"
#include "contract.hpp"
#include "ledger.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

// Windows 控制台默认使用本地代码页（中文系统通常是 GBK），
// 直接输出 UTF-8 会乱码，因此启动时切换到 UTF-8。
// 重定向到文件时该调用不影响字节内容，输出仍然是 UTF-8。
void enableUtf8Console() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

} // namespace

int main() {
    enableUtf8Console();

    try {
        votesc::InMemoryLedger ledger;
        votesc::VoteContract contract(ledger);
        contract.initLedger();
        return votesc::runCli(contract, std::cin, std::cout);
    } catch (const std::exception& error) {
        // 单个命令的错误已在 CLI 内处理；这里只兜底未预期的异常。
        std::cerr << "运行错误：" << error.what() << '\n';
        return 1;
    }
}
