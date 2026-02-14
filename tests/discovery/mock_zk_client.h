// tests/discovery/mock_zk_client.h

#pragma once

#include <gmock/gmock.h>
#include "discovery/zk_client.h"

namespace user_service {
namespace testing {

/**
 * @brief ZooKeeperClient 的 Mock 实现
 * 
 * 用于单元测试 ServiceRegistry 和 ServiceDiscovery，
 * 无需连接真实的 ZooKeeper 服务器。
 */
class MockZooKeeperClient : public ZooKeeperClient {
public:
    // 使用默认构造函数，避免真实连接
    MockZooKeeperClient() : ZooKeeperClient("mock:2181", 1000) {}

    // ==================== 通用接口 Mock ====================
    MOCK_METHOD(bool, Connect, (int timeout_ms), (override));
    MOCK_METHOD(void, Close, (), (override));
    MOCK_METHOD(bool, IsConnected, (), (const, override));

    // ==================== 服务端接口 Mock ====================
    MOCK_METHOD(bool, CreateNode, (const std::string& path, 
                                   const std::string& data, 
                                   bool ephemeral), (override));
    MOCK_METHOD(bool, CreateServicePath, (const std::string& path), (override));
    MOCK_METHOD(bool, DeleteNode, (const std::string& path), (override));
    MOCK_METHOD(bool, Exists, (const std::string& path), (override));
    MOCK_METHOD(bool, SetData, (const std::string& path, 
                                const std::string& data), (override));

    // ==================== 客户端接口 Mock ====================
    MOCK_METHOD(std::string, GetData, (const std::string& path), (override));
    MOCK_METHOD(std::vector<std::string>, GetChildren, 
                (const std::string& path), (override));
    MOCK_METHOD(void, WatchChildren, (const std::string& path, 
                                      WatchCallback callback), (override));
    MOCK_METHOD(void, UnwatchChildren, (const std::string& path), (override));
};

/**
 * @brief 创建一个已连接状态的 Mock 客户端
 */
inline std::shared_ptr<MockZooKeeperClient> CreateConnectedMockZkClient() {
    auto mock = std::make_shared<MockZooKeeperClient>();
    ON_CALL(*mock, IsConnected()).WillByDefault(::testing::Return(true));
    ON_CALL(*mock, Connect(::testing::_)).WillByDefault(::testing::Return(true));
    return mock;
}

/**
 * @brief 创建一个断开连接状态的 Mock 客户端
 */
inline std::shared_ptr<MockZooKeeperClient> CreateDisconnectedMockZkClient() {
    auto mock = std::make_shared<MockZooKeeperClient>();
    ON_CALL(*mock, IsConnected()).WillByDefault(::testing::Return(false));
    ON_CALL(*mock, Connect(::testing::_)).WillByDefault(::testing::Return(false));
    return mock;
}

}  // namespace testing
}  // namespace user_service
