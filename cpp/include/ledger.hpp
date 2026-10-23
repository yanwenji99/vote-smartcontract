// 账本抽象。
//
// Ledger 对应 Fabric ChaincodeStubInterface 中合约会用到的三个方法：
//   getState(key)                  <-> GetState(key)
//   putState(key, value)           <-> PutState(key, value)
//   getStateByRange(start, end)    <-> GetStateByRange(start, end)
//
// 把账本抽象成接口后，VoteContract 不依赖具体存储：
// 当前使用 InMemoryLedger 做本地演示，将来也可以换成真实的 Fabric Stub 适配层。
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace votesc {

// 范围查询返回的一条键值记录。
struct StateEntry {
    std::string key;
    std::string value;
};

class Ledger {
public:
    virtual ~Ledger() = default;

    // 读取 Key 对应的值；Key 不存在时返回 std::nullopt。
    // std::nullopt 对应 Go 中 GetState 返回 nil 的情况。
    virtual std::optional<std::string> getState(const std::string& key) const = 0;

    // 写入或覆盖 Key 的值。
    virtual void putState(const std::string& key, const std::string& value) = 0;

    // 范围查询：startKey 包含，endKey 不包含。
    // startKey 为空表示从头开始，endKey 为空表示到末尾，两者都为空表示全部。
    virtual std::vector<StateEntry> getStateByRange(const std::string& startKey,
                                                    const std::string& endKey) const = 0;
};

// 内存账本：用 std::map 模拟世界状态。
//
// 使用 std::map 而不是 std::unordered_map，是为了让遍历顺序稳定，
// 与 Fabric 范围查询按 Key 字典序返回结果的语义保持一致。
class InMemoryLedger final : public Ledger {
public:
    std::optional<std::string> getState(const std::string& key) const override;
    void putState(const std::string& key, const std::string& value) override;
    std::vector<StateEntry> getStateByRange(const std::string& startKey,
                                            const std::string& endKey) const override;

private:
    std::map<std::string, std::string> state_;
};

} // namespace votesc
