// 投票资产与 JSON 编解码。
//
// 本文件与 chaincode/smartcontract.go 中的 Vote 类型一一对应：
//   Go:   type Vote struct { Id int `json:"id"`; Username string `json:"username"`; Votes int `json:"votes"` }
//   C++:  struct Vote { int id; std::string username; int votes; }
//
// 账本中保存的是 Vote 的紧凑 JSON，例如：
//   {"id":0,"username":"alice","votes":1}
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace votesc {

// 投票资产。
struct Vote {
    int id = 0;               // 资产编号：新用户第一次被投票时分配
    std::string username;     // 被投票人，同时作为世界状态的 Key
    int votes = 0;            // 当前得票数
};

// 所有合约与序列化错误的基类。
class VoteError : public std::runtime_error {
public:
    explicit VoteError(const std::string& message) : std::runtime_error(message) {}
};

// JSON 解析或编码错误。offset 为出错的字节位置，便于定位损坏的账本值。
class JsonError : public VoteError {
public:
    JsonError(const std::string& message, std::size_t offset)
        : VoteError(message), offset_(offset) {}

    std::size_t offset() const noexcept { return offset_; }

private:
    std::size_t offset_;
};

// 将 Vote 编码为与 Go json.Marshal 兼容的紧凑 JSON。
// 输出格式固定为 {"id":<int>,"username":"<转义后的用户名>","votes":<int>}。
std::string voteToJson(const Vote& vote);

// 解析 Vote JSON。
// 字段顺序任意，未知字段按 JSON 语法跳过；字段缺失时保留默认值。
// 解析失败抛出 JsonError。
Vote voteFromJson(const std::string& json);

} // namespace votesc
