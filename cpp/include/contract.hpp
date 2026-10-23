// 投票合约。
//
// VoteContract 与 chaincode/smartcontract.go 中的 SmartContract 对应，
// 对外提供同样的四个方法：
//   initLedger()             <-> InitLedger
//   voteUser(username)       <-> VoteUser
//   getUserVote(username)    <-> GetUserVote
//   getAllVotes()            <-> GetAllVotes
//
// 与 Go 版本唯一的表达差异：C++ 用异常表示失败，Go 用 error 返回值。
#pragma once

#include <string>
#include <vector>

#include "ledger.hpp"
#include "vote.hpp"

namespace votesc {

// 合约级错误，例如空用户名、票数或编号溢出。
class ContractError : public VoteError {
public:
    explicit ContractError(const std::string& message) : VoteError(message) {}
};

// 查询的用户不存在。
class UserNotFoundError : public ContractError {
public:
    explicit UserNotFoundError(const std::string& username);

    const std::string& username() const noexcept { return username_; }

private:
    std::string username_;
};

class VoteContract {
public:
    explicit VoteContract(Ledger& ledger);

    // 保留的初始化入口。编号由账本推导，因此这里是空操作。
    void initLedger();

    // 给 username 投一票，返回最新的 Vote。
    // 新用户自动创建并分配编号；已有用户票数加一、编号不变。
    Vote voteUser(const std::string& username);

    // 查询指定用户的投票信息；用户不存在时抛出 UserNotFoundError。
    Vote getUserVote(const std::string& username) const;

    // 查询全部投票信息，顺序与账本的 Key 顺序一致（按用户名升序）。
    std::vector<Vote> getAllVotes() const;

private:
    // 读取并解析全部记录。
    std::vector<Vote> loadAllVotes() const;

    // 扫描账本，返回现有最大编号 + 1；空账本返回 0。
    int nextVoteId() const;

    Ledger& ledger_;
};

} // namespace votesc
