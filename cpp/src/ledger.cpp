#include "ledger.hpp"

namespace votesc {

std::optional<std::string> InMemoryLedger::getState(const std::string& key) const {
    const auto it = state_.find(key);
    if (it == state_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void InMemoryLedger::putState(const std::string& key, const std::string& value) {
    state_[key] = value;
}

std::vector<StateEntry> InMemoryLedger::getStateByRange(const std::string& startKey,
                                                        const std::string& endKey) const {
    std::vector<StateEntry> result;

    // Fabric 要求 startKey <= endKey；非法范围返回空结果。
    // 必须先判断，否则 first 可能越过 last，迭代器永远无法到达 last。
    if (!startKey.empty() && !endKey.empty() && startKey > endKey) {
        return result;
    }

    const auto first = startKey.empty() ? state_.begin() : state_.lower_bound(startKey);
    const auto last = endKey.empty() ? state_.end() : state_.lower_bound(endKey);

    for (auto it = first; it != last; ++it) {
        result.push_back(StateEntry{it->first, it->second});
    }
    return result;
}

} // namespace votesc
