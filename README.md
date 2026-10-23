# 投票智能合约（Vote Smart Contract）

Hyperledger Fabric 2.5 课程实验中的投票智能合约，包含两套等价实现：

- **Go 链码（根目录）**：可部署到 Fabric 测试网络，完成实验要求的部署、命令行调用与应用程序调用。
- **C++17 独立实现（`cpp/`）**：复刻同一套合约语义，带 Fabric Stub 风格的账本接口和交互式 CLI，用于回答实验手册 4.4 的选做题“能否用其他编程语言实现该合约”。

> **关于 C++ 部署**：Fabric 的 `deployCC` 只支持 Go、Java、JavaScript、TypeScript，主线 Fabric 没有 C++ 链码 shim，因此 `cpp/` 版本不能作为链码部署到 Fabric。原因与替代方案见 [实现文档](docs/实现文档.md) 第 10 节。

## 合约设计

资产：

```text
Vote(id, username, votes)   // username 获得了 votes 票
```

交易：

| 交易 | 参数 | 行为 |
| --- | --- | --- |
| `VoteUser` | `username` | 给指定用户投一票；新用户自动创建，票数为 1 |
| `GetUserVote` | `username` | 查询指定用户的投票信息，用户不存在时报错 |
| `GetAllVotes` | 无 | 查询所有得票人的投票信息，按用户名排序 |

世界状态存储方式：

```text
Key   = username
Value = {"id":0,"username":"alice","votes":1}
```

## 目录结构

```text
vote-smartcontract/
├── go.mod                      # Go 模块定义（module vote）
├── go.sum                      # 依赖校验和
├── main.go                     # Go 链码入口：注册并启动合约
├── chaincode/
│   └── smartcontract.go        # 投票合约业务逻辑
├── cpp/                        # C++17 等价实现（不依赖 Fabric）
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── vote.hpp            # Vote 资产与 JSON 编解码
│   │   ├── ledger.hpp          # Fabric Stub 风格的账本接口
│   │   ├── contract.hpp        # 合约方法与异常
│   │   └── cli.hpp             # 交互式 CLI
│   ├── src/
│   │   ├── vote.cpp
│   │   ├── ledger.cpp
│   │   ├── contract.cpp
│   │   ├── cli.cpp
│   │   └── main.cpp
│   └── tests/
│       └── test_main.cpp       # 无第三方依赖的断言测试
├── docs/
│   └── 实现文档.md              # 设计、流程图、部署调用与测试说明
└── README.md
```

## 快速开始

### 1. 部署 Go 链码（Fabric 2.5 测试网络）

前置条件：已完成实验手册第 2 章环境搭建，`fabric-samples/test-network` 可用。

```bash
cd ~/hyperledger/fabric-samples/test-network

# 启动测试网络并创建通道
./network.sh up createChannel -ca -c mychannel -s couchdb

# 部署本仓库的 Go 链码
./network.sh deployCC \
  -c mychannel \
  -ccn vote \
  -ccl go \
  -ccp /path/to/vote-smartcontract
```

`deployCC` 会在链码目录执行 `go mod vendor` 并下载依赖，首次部署需要网络。

调用示例（Org1 Admin 环境变量见实验手册第 6 章）：

```bash
# 投票（写账本）
peer chaincode invoke -o localhost:7050 \
  --ordererTLSHostnameOverride orderer.example.com \
  --tls --cafile "${PWD}/organizations/ordererOrganizations/example.com/orderers/orderer.example.com/msp/tlscacerts/tlsca.example.com-cert.pem" \
  -C mychannel -n vote \
  --peerAddresses localhost:7051 --tlsRootCertFiles "${PWD}/organizations/peerOrganizations/org1.example.com/peers/peer0.org1.example.com/tls/ca.crt" \
  --peerAddresses localhost:9051 --tlsRootCertFiles "${PWD}/organizations/peerOrganizations/org2.example.com/peers/peer0.org2.example.com/tls/ca.crt" \
  -c '{"function":"VoteUser","Args":["alice"]}'

# 查询单个用户
peer chaincode query -C mychannel -n vote \
  -c '{"Args":["GetUserVote","alice"]}'

# 查询全部
peer chaincode query -C mychannel -n vote \
  -c '{"Args":["GetAllVotes"]}'
```

### 2. 编译并运行 C++ 版本（无需 Fabric）

要求：CMake ≥ 3.20、支持 C++17 的编译器（MinGW g++ / GCC / MSVC）。

```bash
cmake -S cpp -B cpp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build
ctest --test-dir cpp/build --output-on-failure

# Windows
./cpp/build/vote_app.exe
# Linux
./cpp/build/vote_app
```

如果 CMake 选中了错误的编译器，可显式指定：

```bash
cmake -S cpp -B cpp/build -G Ninja \
  -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
```

交互界面：

```text
========== 投票智能合约 ==========
1. 投票
2. 查询用户投票
3. 查询全部投票
4. 退出
请选择操作 [1-4]:
```

也可以直接用一条 `g++` 命令编译：

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -finput-charset=UTF-8 -fexec-charset=UTF-8 -Icpp/include \
  cpp/src/vote.cpp cpp/src/ledger.cpp cpp/src/contract.cpp \
  cpp/src/cli.cpp cpp/src/main.cpp -o vote_app
```

## 与参考实现的差异

参考实现见 [HUST-blockchain-exp/vote-smartcontract](https://github.com/sakura-ysy/HUST-blockchain-exp/tree/main/vote-smartcontract)。本仓库在保持对外行为一致的前提下做了三处改进：

1. **资产编号改为从账本推导**：不再使用包级全局变量 `NextId`，避免链码容器重启后编号重复，以及多个背书节点内存状态不一致的问题。
2. **查询迭代器显式关闭**：`GetAllVotes` 和编号推导都使用 `defer iter.Close()`，避免占用 Peer 资源。
3. **参数与溢出检查**：拒绝空用户名，并在票数、编号自增前做溢出检查。

## 文档

- [实现文档](docs/实现文档.md)：需求分析、总体架构、Go/C++ 实现细节、流程图、部署与调用步骤、测试用例、设计不足。

## 实验信息

- 课程：华中科技大学《区块链技术及应用》
- 平台：Hyperledger Fabric 2.5.4，测试网络（1 Orderer + 2 Peer + 3 CA + CouchDB）
- 通道：`mychannel`，链码名：`vote`
