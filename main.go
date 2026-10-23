// Package main 是投票智能合约的链码入口程序。
//
// 与 Fabric-samples 中的 asset-transfer-basic 示例相同，本文件只负责：
//  1. 把 chaincode 包中实现了业务方法的 SmartContract 注册为链码；
//  2. 启动链码进程，等待 Peer 节点通过 gRPC 调用 Init / Invoke。
//
// 真正的业务逻辑（资产模型与 VoteUser / GetUserVote / GetAllVotes）
// 全部位于 chaincode/smartcontract.go 中。
package main

import (
	"log"

	"github.com/hyperledger/fabric-contract-api-go/contractapi"

	"vote/chaincode"
)

func main() {
	// contractapi.NewChaincode 会解析 SmartContract 上所有导出的方法，
	// 生成链码元数据（合约名、函数名、参数 schema），供 Peer 校验和调用。
	voteChaincode, err := contractapi.NewChaincode(&chaincode.SmartContract{})
	if err != nil {
		log.Panicf("Error creating vote chaincode: %v", err)
	}

	// Start 启动链码的 gRPC 服务，并与 Peer 节点完成注册握手。
	// 该方法会阻塞运行，直到链码进程退出；正常运行时不会返回。
	if err := voteChaincode.Start(); err != nil {
		log.Panicf("Error starting vote chaincode: %v", err)
	}
}
