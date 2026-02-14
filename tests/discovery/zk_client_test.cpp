// tests/discovery/zk_client_test.cpp

#include <gtest/gtest.h>
#include "discovery/zk_client.h"

namespace user_service {
namespace testing {

/**
 * @brief ZooKeeperClient 的单元测试
 * 
 * 注意：这些测试主要测试客户端的逻辑，
 * 不需要真实的 ZooKeeper 服务器。
 * 需要真实 ZK 的测试请参见 zk_integration_test.cpp
 */
class ZooKeeperClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建一个不会真正连接的客户端
        // 使用无效地址，但不调用 Connect
        client_ = std::make_unique<ZooKeeperClient>("invalid:2181", 1000);
    }

    std::unique_ptr<ZooKeeperClient> client_;
};

// ==================== 构造函数测试 ====================

TEST_F(ZooKeeperClientTest, Constructor_DoesNotConnectImmediately) {
    // 构造后不应该自动连接
    EXPECT_FALSE(client_->IsConnected());
}

TEST_F(ZooKeeperClientTest, Constructor_WithEmptyHosts) {
    // 空地址应该可以创建，但连接会失败
    auto client = std::make_unique<ZooKeeperClient>("", 1000);
    EXPECT_FALSE(client->IsConnected());
}

// ==================== 连接状态测试 ====================

TEST_F(ZooKeeperClientTest, IsConnected_ReturnsFalseBeforeConnect) {
    EXPECT_FALSE(client_->IsConnected());
}

TEST_F(ZooKeeperClientTest, Connect_WithInvalidHost_ReturnsFalse) {
    // 连接到无效地址应该失败
    bool result = client_->Connect(100);  // 短超时
    EXPECT_FALSE(result);
    EXPECT_FALSE(client_->IsConnected());
}

TEST_F(ZooKeeperClientTest, Close_WhenNotConnected_DoesNotCrash) {
    // 未连接时关闭不应该崩溃
    client_->Close();
    EXPECT_FALSE(client_->IsConnected());
}

TEST_F(ZooKeeperClientTest, Close_CanBeCalledMultipleTimes) {
    client_->Close();
    client_->Close();
    client_->Close();
    // 不应该崩溃
}

// ==================== 未连接状态下的操作测试 ====================

TEST_F(ZooKeeperClientTest, CreateNode_WhenNotConnected_ReturnsFalse) {
    EXPECT_FALSE(client_->CreateNode("/test", "data", false));
}

TEST_F(ZooKeeperClientTest, CreateServicePath_WhenNotConnected_ReturnsFalse) {
    EXPECT_FALSE(client_->CreateServicePath("/services/test"));
}

TEST_F(ZooKeeperClientTest, DeleteNode_WhenNotConnected_ReturnsFalse) {
    EXPECT_FALSE(client_->DeleteNode("/test"));
}

TEST_F(ZooKeeperClientTest, Exists_WhenNotConnected_ReturnsFalse) {
    EXPECT_FALSE(client_->Exists("/test"));
}

TEST_F(ZooKeeperClientTest, SetData_WhenNotConnected_ReturnsFalse) {
    EXPECT_FALSE(client_->SetData("/test", "data"));
}

TEST_F(ZooKeeperClientTest, GetData_WhenNotConnected_ReturnsEmpty) {
    EXPECT_EQ(client_->GetData("/test"), "");
}

TEST_F(ZooKeeperClientTest, GetChildren_WhenNotConnected_ReturnsEmpty) {
    auto children = client_->GetChildren("/test");
    EXPECT_TRUE(children.empty());
}

TEST_F(ZooKeeperClientTest, WatchChildren_WhenNotConnected_DoesNotCrash) {
    // 不应该崩溃
    client_->WatchChildren("/test", [](const std::string&) {});
}

TEST_F(ZooKeeperClientTest, UnwatchChildren_WhenNotConnected_DoesNotCrash) {
    // 不应该崩溃
    client_->UnwatchChildren("/test");
}

// ==================== Watch 管理测试 ====================

TEST_F(ZooKeeperClientTest, WatchChildren_CanBeCalledMultipleTimes) {
    // 同一路径多次设置 watch 不应该崩溃
    client_->WatchChildren("/test", [](const std::string&) {});
    client_->WatchChildren("/test", [](const std::string&) {});
    client_->WatchChildren("/test", [](const std::string&) {});
}

TEST_F(ZooKeeperClientTest, UnwatchChildren_NonExistentPath_DoesNotCrash) {
    // 取消不存在的 watch 不应该崩溃
    client_->UnwatchChildren("/non-existent");
}

// ==================== 析构函数测试 ====================

TEST_F(ZooKeeperClientTest, Destructor_ClosesConnection) {
    // 创建并销毁，不应该有资源泄漏
    {
        auto temp_client = std::make_unique<ZooKeeperClient>("localhost:2181", 1000);
        // 不调用 Connect
    }
    // 析构应该正常完成
}

}  // namespace testing
}  // namespace user_service
