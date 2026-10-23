// Package chaincode 实现投票智能合约的业务逻辑。
//
// 资产模型：
//
//	Vote(id, username, votes) —— username 获得了 votes 票
//
// 世界状态（World State）的存储方式与 Fabric 示例一致：
//
//	Key   = username
//	Value = Vote 结构体的 JSON 编码，例如 {"id":0,"username":"alice","votes":1}
//
// 对外交易：
//
//	VoteUser(username)   给 username 投一票，票数加一
//	GetUserVote(username) 查询 username 的投票信息
//	GetAllVotes()        查询所有得票人的投票信息
package chaincode

import (
	"encoding/json"
	"fmt"

	"github.com/hyperledger/fabric-contract-api-go/contractapi"
)

// SmartContract 提供投票合约的调用入口。
// 嵌入 contractapi.Contract 后，本类型所有导出的方法都会被注册为链码交易函数。
type SmartContract struct {
	contractapi.Contract
}

// Vote 是投票资产。
// json tag 决定了写入账本时的字段名，必须与 C++ 版本保持一致。
type Vote struct {
	Id       int    `json:"id"`       // 资产编号，新用户创建时分配
	Username string `json:"username"` // 被投票人，同时作为世界状态的 Key
	Votes    int    `json:"votes"`    // 当前得票数
}

// maxInt 是当前平台上 int 能表示的最大值，用于票数自增前的溢出检查。
const maxInt = int(^uint(0) >> 1)

// InitLedger 保留为初始化入口，用于和示例合约保持相同的接口。
//
// 本实现不依赖进程内的全局计数器：每个新用户的 id 都由世界状态推导
// （见 nextVoteID），因此 InitLedger 不需要重置任何内存状态，返回 nil 即可。
// 注意：Fabric 2.x 的生命周期流程不会自动调用 InitLedger；需要时可显式调用：
//
//	peer chaincode invoke ... -c '{"function":"InitLedger","Args":[]}'
//
// 如果提交链码定义时使用了 --init-required，则首次调用必须加 --isInit。
func (s *SmartContract) InitLedger(ctx contractapi.TransactionContextInterface) error {
	return nil
}

// VoteUser 给指定用户投一票。
//
// 处理流程与参考实现一致：
//  1. 以 username 为 Key 读取世界状态；
//  2. 若不存在，则创建 Vote{id: 下一个编号, username, votes: 1}；
//  3. 若已存在，则反序列化后把 votes 加一，保留原 id；
//  4. 将更新后的 JSON 写回世界状态，并返回最新的 Vote。
func (s *SmartContract) VoteUser(ctx contractapi.TransactionContextInterface, username string) (*Vote, error) {
	if username == "" {
		return nil, fmt.Errorf("username cannot be empty")
	}

	voteBytes, err := ctx.GetStub().GetState(username)
	if err != nil {
		return nil, fmt.Errorf("failed to read from world state: %v", err)
	}

	var user Vote
	if voteBytes == nil {
		// 新用户：编号由账本中现有记录的最大 id 推导，而不是使用进程内全局变量。
		id, err := s.nextVoteID(ctx)
		if err != nil {
			return nil, err
		}
		user = Vote{
			Id:       id,
			Username: username,
			Votes:    1,
		}
	} else {
		// 已有用户：恢复资产并累加票数。
		if err := json.Unmarshal(voteBytes, &user); err != nil {
			return nil, fmt.Errorf("failed to unmarshal vote for %q: %v", username, err)
		}
		if user.Votes == maxInt {
			return nil, fmt.Errorf("vote count overflow for %q", username)
		}
		user.Votes++
	}

	userJSON, err := json.Marshal(user)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal vote for %q: %v", username, err)
	}
	// PutState 只把写入暂存到本交易的读写集；交易通过背书和提交校验后才会真正落盘。
	if err := ctx.GetStub().PutState(username, userJSON); err != nil {
		return nil, fmt.Errorf("failed to write vote for %q: %v", username, err)
	}

	return &user, nil
}

// GetUserVote 查询指定用户的投票信息。
// 用户不存在时返回错误，消息与参考实现保持一致。
func (s *SmartContract) GetUserVote(ctx contractapi.TransactionContextInterface, username string) (*Vote, error) {
	voteBytes, err := ctx.GetStub().GetState(username)
	if err != nil {
		return nil, fmt.Errorf("failed to read from world state: %v", err)
	}
	if voteBytes == nil {
		// GetState 对不存在的键返回 (nil, nil)，而不是空切片。
		return nil, fmt.Errorf("the UserVote %v does not exist", username)
	}

	var user Vote
	if err := json.Unmarshal(voteBytes, &user); err != nil {
		return nil, fmt.Errorf("failed to unmarshal vote for %q: %v", username, err)
	}
	return &user, nil
}

// GetAllVotes 查询所有得票人的投票信息。
//
// GetStateByRange("", "") 表示扫描整个键空间。Fabric 会按键排序返回结果，
// 因此输出按 username 的字典序排列；对实验中使用的小写 ASCII 用户名，
// LevelDB 与 CouchDB 的顺序一致。非 ASCII 用户名如需严格排序应显式排序。
func (s *SmartContract) GetAllVotes(ctx contractapi.TransactionContextInterface) ([]*Vote, error) {
	iter, err := ctx.GetStub().GetStateByRange("", "")
	if err != nil {
		return nil, fmt.Errorf("failed to query world state: %v", err)
	}
	// 查询迭代器用完必须关闭，否则会占用 Peer 的资源。
	defer iter.Close()

	// 用 make 初始化而不是 var votes []*Vote：保证空账本时返回 [] 而不是 nil 切片。
	votes := make([]*Vote, 0)
	for iter.HasNext() {
		kv, err := iter.Next()
		if err != nil {
			return nil, fmt.Errorf("failed to iterate world state: %v", err)
		}

		var user Vote
		if err := json.Unmarshal(kv.GetValue(), &user); err != nil {
			return nil, fmt.Errorf("failed to unmarshal vote %q: %v", kv.GetKey(), err)
		}
		votes = append(votes, &user)
	}

	return votes, nil
}

// nextVoteID 扫描世界状态，返回下一个可用的资产编号（现有最大 id + 1）。
//
// 说明：参考示例使用包级变量 `var NextId int` 保存计数器，这在 Fabric 中
// 存在两个问题：
//  1. 链码容器重启后计数器会归零，可能与账本中已有的 id 重复；
//  2. 内存状态不属于世界状态，多个背书节点各自执行时无法保证一致。
//
// 因此本实现改为从账本推导编号：编号只取决于已提交的世界状态，天然确定，
// 且无需额外的计数器键。代价是新建用户时需要扫描一次全部记录，对本实验
// 的数据规模完全可以接受。
//
// 并发说明：编号取决于交易执行时读到的账本快照。两个新建用户的交易若并发
// 执行并读到同一快照，会得到相同编号；合约本身不额外保证唯一性。Fabric 的
// MVCC / 范围查询校验可能使其中一笔在提交阶段失败，客户端应把这类失败视为
// 可重试。生产环境若要求强唯一编号，应改用专门的计数器键或 UUID。
func (s *SmartContract) nextVoteID(ctx contractapi.TransactionContextInterface) (int, error) {
	iter, err := ctx.GetStub().GetStateByRange("", "")
	if err != nil {
		return 0, fmt.Errorf("failed to query world state: %v", err)
	}
	defer iter.Close()

	nextID := 0
	for iter.HasNext() {
		kv, err := iter.Next()
		if err != nil {
			return 0, fmt.Errorf("failed to iterate world state: %v", err)
		}

		var vote Vote
		if err := json.Unmarshal(kv.GetValue(), &vote); err != nil {
			return 0, fmt.Errorf("failed to unmarshal vote %q: %v", kv.GetKey(), err)
		}
		// 跟踪现有最大 id；下一个可用编号 = max(id) + 1。
		if vote.Id >= nextID {
			if vote.Id == maxInt {
				return 0, fmt.Errorf("vote id overflow")
			}
			nextID = vote.Id + 1
		}
	}

	return nextID, nil
}
