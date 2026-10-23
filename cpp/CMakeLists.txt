cmake_minimum_required(VERSION 3.20)

project(vote_smartcontract_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 合约核心：资产/JSON、账本抽象、合约逻辑。
# 单独做成静态库，便于应用和测试共用同一份实现。
add_library(vote_core STATIC
    src/vote.cpp
    src/ledger.cpp
    src/contract.cpp
)
target_include_directories(vote_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
    # /utf-8 让编译器同时按 UTF-8 解析源文件并生成 UTF-8 字符串字面量。
    target_compile_options(vote_core PUBLIC /W4 /utf-8)
else()
    # -finput-charset/-fexec-charset 保证中文字符串常量按 UTF-8 处理。
    target_compile_options(vote_core PUBLIC
        -Wall -Wextra -Wpedantic
        -finput-charset=UTF-8
        -fexec-charset=UTF-8
    )
endif()

# 交互式演示程序。
add_executable(vote_app
    src/main.cpp
    src/cli.cpp
)
target_link_libraries(vote_app PRIVATE vote_core)

if(WIN32 AND MINGW)
    # SetConsoleOutputCP / SetConsoleCP 位于 kernel32。
    target_link_libraries(vote_app PRIVATE kernel32)
endif()

# 自动化测试。
include(CTest)
if(BUILD_TESTING)
    add_executable(vote_tests tests/test_main.cpp)
    target_link_libraries(vote_tests PRIVATE vote_core)
    add_test(NAME vote_tests COMMAND vote_tests)
endif()
