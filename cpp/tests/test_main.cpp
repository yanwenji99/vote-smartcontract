// 无第三方依赖的断言测试。
//
// 测试输出使用英文，避免终端代码页影响失败信息的可读性。
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "contract.hpp"
#include "ledger.hpp"
#include "vote.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        std::cerr << file << ":" << line << ": CHECK failed: " << expression << '\n';
        ++failures;
    }
}

template <typename Actual, typename Expected>
void checkEqual(const Actual& actual, const Expected& expected,
                const char* actualExpression, const char* expectedExpression,
                const char* file, int line) {
    if (!(actual == expected)) {
        std::cerr << file << ":" << line << ": CHECK_EQ failed: "
                  << actualExpression << " == " << expectedExpression << '\n';
        ++failures;
    }
}

template <typename Exception, typename Function>
void checkThrowsAs(Function&& function, const char* expression,
                   const char* file, int line) {
    bool threwExpected = false;
    try {
        function();
    } catch (const Exception&) {
        threwExpected = true;
    } catch (const std::exception& error) {
        std::cerr << file << ":" << line << ": CHECK_THROWS_AS(" << expression
                  << ") caught wrong exception: " << error.what() << '\n';
        ++failures;
        return;
    }

    if (!threwExpected) {
        std::cerr << file << ":" << line << ": CHECK_THROWS_AS(" << expression
                  << ") did not throw\n";
        ++failures;
    }
}

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

#define CHECK_EQ(actual, expected) \
    checkEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)

#define CHECK_THROWS_AS(expression, exceptionType)                       \
    checkThrowsAs<exceptionType>([&] { (void)(expression); }, #expression, \
                                 __FILE__, __LINE__)

// ---------------------------------------------------------------- 资产与 JSON

void testJsonExactBytes() {
    const votesc::Vote vote{1, "a\"b\\c\n<&>", 2};
    const std::string expected =
        "{\"id\":1,\"username\":\"a\\\"b\\\\c\\n\\u003c\\u0026\\u003e\",\"votes\":2}";
    CHECK_EQ(votesc::voteToJson(vote), expected);
}

void testJsonRoundTrip() {
    const votesc::Vote original{7, "张三 \"quoted\" \\ slash\n<&>", 42};
    const std::string json = votesc::voteToJson(original);
    const votesc::Vote decoded = votesc::voteFromJson(json);

    CHECK_EQ(decoded.id, 7);
    CHECK_EQ(decoded.username, original.username);
    CHECK_EQ(decoded.votes, 42);
    CHECK(json.find("张三") != std::string::npos);
}

void testJsonFieldOrderAndUnknownFields() {
    const votesc::Vote vote = votesc::voteFromJson(
        "{\"votes\":3,\"unknown\":{\"a\":[1,true,null]},"
        "\"username\":\"alice\",\"id\":5}");
    CHECK_EQ(vote.id, 5);
    CHECK_EQ(vote.username, std::string("alice"));
    CHECK_EQ(vote.votes, 3);
}

void testJsonDefaults() {
    const votesc::Vote vote = votesc::voteFromJson("{}");
    CHECK_EQ(vote.id, 0);
    CHECK_EQ(vote.username, std::string(""));
    CHECK_EQ(vote.votes, 0);
}

void testJsonUnicodeEscapes() {
    // \uXXXX 基本多文种平面字符
    const votesc::Vote chinese = votesc::voteFromJson(
        "{\"id\":1,\"username\":\"\\u4f60\\u597d\",\"votes\":1}");
    CHECK_EQ(chinese.username, std::string("你好"));

    // 代理对：U+1F600
    const votesc::Vote emoji = votesc::voteFromJson(
        "{\"id\":1,\"username\":\"\\ud83d\\ude00\",\"votes\":1}");
    CHECK_EQ(emoji.username, std::string("😀"));

    // U+2028 / U+2029 必须被编码器转义
    const votesc::Vote lineSeparator{
        0, std::string("a") + "\xE2\x80\xA8" + "b" + "\xE2\x80\xA9", 0};
    const std::string json = votesc::voteToJson(lineSeparator);
    CHECK(json.find("\\u2028") != std::string::npos);
    CHECK(json.find("\\u2029") != std::string::npos);
    CHECK_EQ(votesc::voteFromJson(json).username, lineSeparator.username);
}

void testJsonErrors() {
    // 未闭合的对象
    CHECK_THROWS_AS(votesc::voteFromJson("{\"id\":1"), votesc::JsonError);
    // 未闭合的字符串
    CHECK_THROWS_AS(votesc::voteFromJson("{\"username\":\"abc}"), votesc::JsonError);
    // 小数不是整数
    CHECK_THROWS_AS(
        votesc::voteFromJson("{\"id\":1.5,\"username\":\"a\",\"votes\":1}"),
        votesc::JsonError);
    // 整数溢出
    CHECK_THROWS_AS(
        votesc::voteFromJson("{\"id\":99999999999,\"username\":\"a\",\"votes\":1}"),
        votesc::JsonError);
    // 尾随垃圾
    CHECK_THROWS_AS(
        votesc::voteFromJson("{\"id\":1,\"username\":\"a\",\"votes\":1} extra"),
        votesc::JsonError);
    // 顶层不是对象
    CHECK_THROWS_AS(votesc::voteFromJson("[1,2]"), votesc::JsonError);
    // 非法转义
    CHECK_THROWS_AS(
        votesc::voteFromJson("{\"id\":1,\"username\":\"\\x\",\"votes\":1}"),
        votesc::JsonError);
    // 前导零
    CHECK_THROWS_AS(
        votesc::voteFromJson("{\"id\":01,\"username\":\"a\",\"votes\":1}"),
        votesc::JsonError);
}

// ---------------------------------------------------------------- 合约行为

void testFirstVote() {
    votesc::InMemoryLedger ledger;
    votesc::VoteContract contract(ledger);
    contract.initLedger();

    const votesc::Vote vote = contract.voteUser("alice");
    CHECK_EQ(vote.id, 0);
    CHECK_EQ(vote.username, std::string("alice"));
    CHECK_EQ(vote.votes, 1);

    const auto stored = ledger.getState("alice");
    CHECK(stored.has_value());
    CHECK_EQ(*stored, std::string("{\"id\":0,\"username\":\"alice\",\"votes\":1}"));
}

void testRepeatVote() {
    votesc::InMemoryLedger ledger;
    votesc::VoteContract contract(ledger);
    contract.initLedger();

    contract.voteUser("alice");
    const votesc::Vote second = contract.voteUser("alice");

    CHECK_EQ(second.id, 0);
    CHECK_EQ(second.votes, 2);
    CHECK_EQ(contract.getAllVotes().size(), static_cast<std::size_t>(1));
}

void testSecondUserGetsNextId() {
    votesc::InMemoryLedger ledger;
    votesc::VoteContract contract(ledger);
    contract.initLedger();

    contract.voteUser("alice");
    const votesc::Vote bob = contract.voteUser("bob");

    CHECK_EQ(bob.id, 1);
    CHECK_EQ(bob.votes, 1);
}

void testGetUserVote() {
    votesc::InMemoryLedger ledger;
    votesc::VoteContract contract(ledger);
    contract.initLedger();

    contract.voteUser("alice");
    const votesc::Vote vote = contract.getUserVote("alice");
    CHECK_EQ(vote.id, 0);
    CHECK_EQ(vote.votes, 1);
}

void testMissingUser() {
    votesc::InMemoryLedger ledger;
    votesc::VoteContract contract(ledger);
    contract.initLedger();

    CHECK_THROWS_AS(contract.getUserVote("carol"), votesc::UserNotFoundError);
}

void testEmptyUsername() {
    votesc::InMemoryLedger ledger;
    votesc::VoteContract contract(ledger);
    contract.initLedger();

    CHECK_THROWS_AS(contract.voteUser(""), votesc::ContractError);
}

void testEmptyLedger() {
    votesc::InMemoryLedger ledger;
    votesc::VoteContract contract(ledger);
    contract.initLedger();

    CHECK(contract.getAllVotes().empty());
}

void testGetAllVotesOrdering() {
    votesc::InMemoryLedger ledger;
    votesc::VoteContract contract(ledger);
    contract.initLedger();

    // 故意按倒序插入，验证输出按用户名升序。
    contract.voteUser("bob");
    contract.voteUser("alice");

    const std::vector<votesc::Vote> votes = contract.getAllVotes();
    CHECK_EQ(votes.size(), static_cast<std::size_t>(2));
    CHECK_EQ(votes[0].username, std::string("alice"));
    CHECK_EQ(votes[1].username, std::string("bob"));
}

void testCorruptedState() {
    votesc::InMemoryLedger ledger;
    ledger.putState("alice", "not json");
    votesc::VoteContract contract(ledger);

    CHECK_THROWS_AS(contract.getUserVote("alice"), votesc::JsonError);
    CHECK_THROWS_AS(contract.getAllVotes(), votesc::JsonError);
}

// ---------------------------------------------------------------- 账本范围查询

void testRangeQuery() {
    votesc::InMemoryLedger ledger;
    ledger.putState("a", "1");
    ledger.putState("b", "2");
    ledger.putState("c", "3");

    CHECK_EQ(ledger.getStateByRange("a", "c").size(), static_cast<std::size_t>(2));
    CHECK_EQ(ledger.getStateByRange("", "").size(), static_cast<std::size_t>(3));
    CHECK_EQ(ledger.getStateByRange("b", "b").size(), static_cast<std::size_t>(0));
    CHECK_EQ(ledger.getStateByRange("c", "a").size(), static_cast<std::size_t>(0));
    CHECK_EQ(ledger.getStateByRange("", "b").size(), static_cast<std::size_t>(1));
    CHECK_EQ(ledger.getStateByRange("b", "").size(), static_cast<std::size_t>(2));

    const auto entries = ledger.getStateByRange("a", "c");
    CHECK_EQ(entries[0].key, std::string("a"));
    CHECK_EQ(entries[1].key, std::string("b"));
}

} // namespace

int main() {
    testJsonExactBytes();
    testJsonRoundTrip();
    testJsonFieldOrderAndUnknownFields();
    testJsonDefaults();
    testJsonUnicodeEscapes();
    testJsonErrors();

    testFirstVote();
    testRepeatVote();
    testSecondUserGetsNextId();
    testGetUserVote();
    testMissingUser();
    testEmptyUsername();
    testEmptyLedger();
    testGetAllVotesOrdering();
    testCorruptedState();

    testRangeQuery();

    if (failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test(s) failed.\n";
    return 1;
}
