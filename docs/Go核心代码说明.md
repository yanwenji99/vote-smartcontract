# Go 链码核心代码说明

> 源码：[main.go](../main.go)、[chaincode/smartcontract.go](../chaincode/smartcontract.go)
> 依赖：`fabric-contract-api-go v1.2.0`；环境：Fabric 2.5、Go 1.18+

本文按“调用链 → 入口 → 资产 → 交易方法 → Fabric 机制 → 验证”的顺序，说明 Go 链码的核心实现。

## 1. 代码结构与调用链

| 文件 | 职责 |
| --- | --- |
| [go.mod](../go.mod) | 模块 `vote`，直接依赖 `fabric-contract-api-go` |
| [main.go](../main.go) | 注册 `SmartContract`，启动链码 gRPC 服务 |
| [chaincode/smartcontract.go](../chaincode/smartcontract.go) | 资产模型与全部交易方法 |

一次调用的路径：

```text
peer CLI / SDK
  → Peer 发起 Proposal
  → shim 收到 ChaincodeMessage，调用 ContractChaincode.Invoke
  → contractapi 解析函数名、转换参数、创建 TransactionContext
  → SmartContract.VoteUser(ctx, "alice")
  → GetState / PutState（读写集）
  → 返回 (*Vote, error)，序列化为 JSON 或错误响应
```

函数名解析规则：

- `Args[0]` 是函数名，支持 `VoteUser` 或 `SmartContract:VoteUser`；
- 没有合约前缀时使用默认合约，本合约的默认合约名是 `SmartContract`；
- 函数名首字母会被规范化为大写，所以 `voteUser` 也能找到 `VoteUser`；
- `nextVoteID` 是未导出方法，不会被注册，外部无法调用。

## 2. 入口 main.go

```go
func main() {
	// 反射扫描 SmartContract 的导出方法，生成合约元数据。
	voteChaincode, err := contractapi.NewChaincode(&chaincode.SmartContract{})
	if err != nil {
		log.Panicf("Error creating vote chaincode: %v", err)
	}

	// 启动 gRPC 服务并连接 Peer；Start 会阻塞运行。
	if err := voteChaincode.Start(); err != nil {
		log.Panicf("Error starting vote chaincode: %v", err)
	}
}
```

`NewChaincode` 做了四件事：

1. 反射找出“导出、首参为 `TransactionContextInterface`、返回 `(T, error)` 或 `error`”的方法；
2. 把 `InitLedger`、`VoteUser`、`GetUserVote`、`GetAllVotes` 注册为交易；
3. 过滤 `GetInfo`、`GetName` 等框架接口方法，并自动加入系统合约 `org.hyperledger.fabric`；
4. 生成元数据（合约名、函数名、参数/返回值 schema），默认使用 `JSONSerializer`。

`Start` 在 `deployCC` 场景下调用 `shim.Start`，通过 `CORE_CHAINCODE_ID_NAME`、`CORE_PEER_ADDRESS` 等环境变量连接 Peer；正常运行时不会返回。启动失败用 `log.Panicf` 让进程退出，便于 Peer/容器日志定位问题。

## 3. 资产模型

```go
type Vote struct {
	Id       int    `json:"id"`       // 新用户创建时分配
	Username string `json:"username"` // 同时作为世界状态 Key
	Votes    int    `json:"votes"`    // 当前得票数
}
```

世界状态映射：

```text
Key   = username
Value = JSON(Vote)，例如 {"id":0,"username":"alice","votes":1}
```

要点：

- 用户名直接作 Key，单用户查询是 O(1)，也不会出现重复记录；
- `json` tag 决定账本字段名，字段顺序固定为 `id`、`username`、`votes`；
- `VoteUser` 拒绝空用户名，避免空 Key 污染账本；
- `maxInt = int(^uint(0) >> 1)`，用于票数和编号的溢出检查。

## 4. VoteUser：投票

```go
func (s *SmartContract) VoteUser(ctx contractapi.TransactionContextInterface, username string) (*Vote, error) {
	if username == "" {
		return nil, fmt.Errorf("username cannot be empty")
	}

	// 点查：nil 表示不存在，err 表示底层故障。
	voteBytes, err := ctx.GetStub().GetState(username)
	if err != nil {
		return nil, fmt.Errorf("failed to read from world state: %v", err)
	}

	var user Vote
	if voteBytes == nil {
		// 新用户：编号从账本推导，初始 1 票。
		id, err := s.nextVoteID(ctx)
		if err != nil {
			return nil, err
		}
		user = Vote{Id: id, Username: username, Votes: 1}
	} else {
		// 老用户：解码后票数 +1，id 不变。
		if err := json.Unmarshal(voteBytes, &user); err != nil {
			return nil, fmt.Errorf("failed to unmarshal vote for %q: %v", username, err)
		}
		if user.Votes == maxInt {
			return nil, fmt.Errorf("vote count overflow for %q", username)
		}
		user.Votes++
	}

	// 先编码，再写回；PutState 只写入本交易读写集，提交后才落盘。
	userJSON, err := json.Marshal(user)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal vote for %q: %v", username, err)
	}
	if err := ctx.GetStub().PutState(username, userJSON); err != nil {
		return nil, fmt.Errorf("failed to write vote for %q: %v", username, err)
	}

	return &user, nil
}
```

| 场景 | 行为 |
| --- | --- |
| 新用户 | 分配 `nextVoteID()`，`votes=1` |
| 老用户 | 保留 `id`，`votes++` |
| 空用户名 | 返回 `username cannot be empty` |
| 账本值损坏 | 返回解码错误，不静默跳过 |
| 票数溢出 | 返回 `vote count overflow` |

Fabric 采用“模拟执行 + 提交校验”：`GetState` 把读到的键和版本记入读集，`PutState` 把新值记入写集；提交时做 MVCC 校验。两个客户端同时给同一用户投票时，后提交的交易会因版本变化失败（通常为 `MVCC_READ_CONFLICT`），客户端应重试。因此并发投票不会丢票，但需要处理重试。

## 5. GetUserVote：查询单个用户

```go
func (s *SmartContract) GetUserVote(ctx contractapi.TransactionContextInterface, username string) (*Vote, error) {
	voteBytes, err := ctx.GetStub().GetState(username)
	if err != nil {
		return nil, fmt.Errorf("failed to read from world state: %v", err)
	}
	if voteBytes == nil {
		// GetState 对不存在的键返回 (nil, nil)。
		return nil, fmt.Errorf("the UserVote %v does not exist", username)
	}

	var user Vote
	if err := json.Unmarshal(voteBytes, &user); err != nil {
		return nil, fmt.Errorf("failed to unmarshal vote for %q: %v", username, err)
	}
	return &user, nil
}
```

用户不存在时返回错误而不是零值，便于区分“不存在”和“存在但 0 票”，错误文本与参考实现保持一致。

`peer chaincode query` 只在一个 Peer 上模拟执行，不排序、不提交；`peer chaincode invoke` 会走完整的背书、排序和提交流程。只读查询应优先使用 `query`。

## 6. GetAllVotes：查询全部

```go
func (s *SmartContract) GetAllVotes(ctx contractapi.TransactionContextInterface) ([]*Vote, error) {
	// 空起点、空终点表示扫描整个键空间。
	iter, err := ctx.GetStub().GetStateByRange("", "")
	if err != nil {
		return nil, fmt.Errorf("failed to query world state: %v", err)
	}
	defer iter.Close() // 迭代器必须关闭，释放 Peer 资源

	// 用 make 保证空账本返回 [] 而不是 nil 切片。
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
```

范围查询语义：起点包含、终点不包含；空起点/终点表示无界。Fabric 会按键排序返回，因此输出通常按用户名字典序排列。小写 ASCII 用户名在 LevelDB 与 CouchDB 下顺序一致；非 ASCII 用户名如需严格排序，应在链码内显式排序。

当前实现没有分页，记录很多时会一次性返回全部数据。生产场景可改用 `GetStateByRangeWithPagination`。

## 7. nextVoteID：从账本推导编号

```go
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
		// 取现有最大 id，下一个编号 = max(id) + 1。
		if vote.Id >= nextID {
			if vote.Id == maxInt {
				return 0, fmt.Errorf("vote id overflow")
			}
			nextID = vote.Id + 1
		}
	}
	return nextID, nil
}
```

为什么不用包级 `var NextId int`：

- 链码容器重启后计数器归零，可能与账本中已有编号重复；
- 多个背书节点各自执行，进程内状态可能不同，破坏确定性。

从账本推导的编号只取决于已提交状态，天然确定。代价是每次新建用户 O(n) 扫描；本实验规模可以接受。

并发边界：两个新用户交易若读到同一快照，可能得到相同编号。Fabric 的 MVCC/范围查询校验可能使其中一笔失败，客户端应重试；合约本身不保证编号全局唯一。生产环境可改用计数器 Key 或 UUID。

## 8. 关键机制

### 8.1 Stub 与读写集

| 方法 | 作用 |
| --- | --- |
| `GetState(key)` | 点查，不存在返回 `(nil, nil)` |
| `PutState(key, value)` | 暂存写入；提交校验通过后才落盘 |
| `GetStateByRange(start, end)` | 返回范围迭代器，必须 `Close()` |

链码不直接访问 CouchDB/LevelDB，所有读写都经过 Stub，由 Fabric 记入读写集并做背书、排序和 MVCC 校验。

### 8.2 返回值与错误

| Go 返回 | Peer 响应 |
| --- | --- |
| `(*Vote, nil)` / `([]*Vote, nil)` | `shim.Success`，JSON 载荷，status 200 |
| `(nil, error)` | `shim.Error`，错误文本，status 500 |
| `panic` | 链码进程异常，交易失败，容器可能重启 |

业务错误一律返回 `error`，只有进程无法启动时才用 `log.Panicf`。

### 8.3 确定性

链码会在多个背书节点上独立执行，必须避免：

- 进程内可变状态（如全局计数器）；
- `time.Now()`、随机数、外部网络/文件；
- Go map 遍历等不确定顺序；
- 依赖状态库差异的富查询。

本合约的编号来自账本，JSON 字段顺序固定，范围查询顺序由 Fabric 保证，满足确定性要求。

### 8.4 JSON 细节

`json.Marshal` 按结构体字段顺序输出紧凑 JSON，并默认对 `<`、`>`、`&` 做 HTML 转义，对 U+2028/U+2029 转义，对非法 UTF-8 替换为 U+FFFD。Key 是原始字符串，不经过 JSON 转义；Value 会转义。

## 9. 验证

```bash
go build ./...
go vet ./...
gofmt -l .
```

部署后依次执行：

```bash
peer chaincode invoke ... -c '{"function":"VoteUser","Args":["alice"]}'
peer chaincode invoke ... -c '{"function":"VoteUser","Args":["alice"]}'
peer chaincode invoke ... -c '{"function":"VoteUser","Args":["bob"]}'
peer chaincode query  ... -c '{"Args":["GetUserVote","alice"]}'
peer chaincode query  ... -c '{"Args":["GetAllVotes"]}'
```

预期：

```text
GetUserVote("alice") → {"id":0,"username":"alice","votes":2}
GetAllVotes()        → [{"id":0,"username":"alice","votes":2},
                        {"id":1,"username":"bob","votes":1}]
```

边界情况：

| 场景 | 预期 |
| --- | --- |
| `VoteUser("")` | `username cannot be empty` |
| `GetUserVote("nobody")` | `the UserVote nobody does not exist` |
| 空账本 `GetAllVotes()` | `[]` |
| 同一用户重复投票 | `id` 不变，`votes` 递增 |

## 10. 已知限制

| 限制 | 影响 | 改进方向 |
| --- | --- | --- |
| 编号推导 O(n) | 新建用户扫描全表 | 计数器 Key 或 UUID |
| 并发编号可能重复 | 合约不保证唯一 | 计数器 Key（接受冲突重试） |
| `GetAllVotes` 无分页 | 数据量大时响应过大 | `GetStateByRangeWithPagination` |
| 无权限控制 | 任意成员可投票 | `GetClientIdentity` / `GetCreator` |
| 无防重复投票 | 同一投票人可无限投 | 记录投票人身份 |
| 无事件通知 | 应用层需轮询 | `SetEvent` |
| 用户名直接作 Key | 超长/特殊字符受限 | 长度、字符集、规范化校验 |
| 无数据版本 | 结构变更需兼容旧记录 | schema 版本或迁移交易 |

新增交易只需在 `SmartContract` 上添加导出方法（首参为 `TransactionContextInterface`，返回 `(T, error)` 或 `error`），无需修改 `main.go`。
