#include "contract.hpp"

#include <limits>

namespace votesc {

UserNotFoundError::UserNotFoundError(const std::string& username)
    : ContractError("the UserVote " + username + " does not exist"), username_(username) {}

VoteContract::VoteContract(Ledger& ledger) : ledger_(ledger) {}

void VoteContract::initLedger() {
    // 编号从账本推导（见 nextVoteId），没有需要重置的内存状态。
    // 保留该方法是为了与 Go 版 SmartContract.InitLedger 的接口一致。
}

Vote VoteContract::voteUser(const std::string& username) {
    if (username.empty()) {
        throw ContractError("username cannot be empty");
    }

    Vote vote;

    const auto raw = ledger_.getState(username);
    if (!raw) {
        // 新用户：编号由账本中现有记录的最大 id 推导。
        vote = Vote{nextVoteId(), username, 1};
    } else {
        // 已有用户：恢复资产后票数加一，编号保持不变。
        vote = voteFromJson(*raw);
        if (vote.votes == std::numeric_limits<int>::max()) {
            throw ContractError("vote count overflow");
        }
        ++vote.votes;
    }

    // 先完成全部校验和编码，再写账本，避免部分更新。
    ledger_.putState(username, voteToJson(vote));
    return vote;
}

Vote VoteContract::getUserVote(const std::string& username) const {
    const auto raw = ledger_.getState(username);
    if (!raw) {
        throw UserNotFoundError(username);
    }
    return voteFromJson(*raw);
}

std::vector<Vote> VoteContract::getAllVotes() const {
    return loadAllVotes();
}

std::vector<Vote> VoteContract::loadAllVotes() const {
    std::vector<Vote> votes;
    for (const StateEntry& entry : ledger_.getStateByRange("", "")) {
        votes.push_back(voteFromJson(entry.value));
    }
    return votes;
}

int VoteContract::nextVoteId() const {
    int next = 0;
    for (const Vote& vote : loadAllVotes()) {
        if (vote.id >= next) {
            if (vote.id == std::numeric_limits<int>::max()) {
                throw ContractError("vote id overflow");
            }
            next = vote.id + 1;
        }
    }
    return next;
}

} // namespace votesc
