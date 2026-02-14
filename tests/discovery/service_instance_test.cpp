// tests/discovery/zk_integration_test.cpp

#include <gtest/gtest.h>
#include "discovery/zk_client.h"
#include "discovery/service_registry.h"
#include "discovery/service_discovery.h"
#include "common/logger.h"
#include <thread>
#include <chrono>
#include <condition_variable>

namespace user_service {
namespace testing {

class ZooKeeperIntegrationTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        auto test_client = std::make_shared<ZooKeeperClient>("localhost:2181", 5000);
        zk_available_ = test_client->Connect(3000);
        
        if (!zk_available_) {
            std::cout << "WARNING: ZooKeeper not available at localhost:2181, "
                      << "integration tests will be skipped." << std::endl;
        }
    }

    void SetUp() override {
        if (!zk_available_) {
            GTEST_SKIP() << "ZooKeeper not available";
        }
        
        zk_client_ = std::make_shared<ZooKeeperClient>("localhost:2181", 10000);
        ASSERT_TRUE(zk_client_->Connect(5000));
        
        CleanupTestPath();
    }

    void TearDown() override {
        if (zk_client_ && zk_client_->IsConnected()) {
            CleanupTestPath();
            zk_client_->Close();
        }
    }

    void CleanupTestPath() {
        DeleteRecursive("/test-services");
    }
    
    // 递归删除节点
    void DeleteRecursive(const std::string& path) {
        if (!zk_client_ || !zk_client_->IsConnected()) return;
        
        auto children = zk_client_->GetChildren(path);
        for (const auto& child : children) {
            DeleteRecursive(path + "/" + child);
        }
        zk_client_->DeleteNode(path);
    }

    static bool zk_available_;
    std::shared_ptr<ZooKeeperClient> zk_client_;
};

bool ZooKeeperIntegrationTest::zk_available_ = false;

// ==================== 基本连接测试 ====================

TEST_F(ZooKeeperIntegrationTest, Connect_Success) {
    EXPECT_TRUE(zk_client_->IsConnected());
}

TEST_F(ZooKeeperIntegrationTest, CreateAndDeleteNode) {
    EXPECT_TRUE(zk_client_->CreateServicePath("/test-services"));
    EXPECT_TRUE(zk_client_->CreateNode("/test-services/test-node", "test data", false));
    
    EXPECT_TRUE(zk_client_->Exists("/test-services/test-node"));
    EXPECT_EQ(zk_client_->GetData("/test-services/test-node"), "test data");
    
    EXPECT_TRUE(zk_client_->DeleteNode("/test-services/test-node"));
    EXPECT_FALSE(zk_client_->Exists("/test-services/test-node"));
}

TEST_F(ZooKeeperIntegrationTest, CreateEphemeralNode) {
    EXPECT_TRUE(zk_client_->CreateServicePath("/test-services"));
    EXPECT_TRUE(zk_client_->CreateNode("/test-services/ephemeral", "data", true));
    
    EXPECT_TRUE(zk_client_->Exists("/test-services/ephemeral"));
    
    // 关闭连接，临时节点应该自动消失
    zk_client_->Close();
    
    // 重新连接检查
    auto new_client = std::make_shared<ZooKeeperClient>("localhost:2181", 5000);
    ASSERT_TRUE(new_client->Connect(3000));
    
    // 等待会话过期
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    EXPECT_FALSE(new_client->Exists("/test-services/ephemeral"));
    
    new_client->Close();
    
    // 重新连接主客户端
    zk_client_ = std::make_shared<ZooKeeperClient>("localhost:2181", 10000);
    ASSERT_TRUE(zk_client_->Connect(5000));
}

TEST_F(ZooKeeperIntegrationTest, SetAndGetData) {
    EXPECT_TRUE(zk_client_->CreateServicePath("/test-services"));
    EXPECT_TRUE(zk_client_->CreateNode("/test-services/data-node", "initial", false));
    
    EXPECT_EQ(zk_client_->GetData("/test-services/data-node"), "initial");
    
    EXPECT_TRUE(zk_client_->SetData("/test-services/data-node", "updated"));
    EXPECT_EQ(zk_client_->GetData("/test-services/data-node"), "updated");
}

TEST_F(ZooKeeperIntegrationTest, GetChildren) {
    EXPECT_TRUE(zk_client_->CreateServicePath("/test-services/parent"));
    EXPECT_TRUE(zk_client_->CreateNode("/test-services/parent/child1", "", false));
    EXPECT_TRUE(zk_client_->CreateNode("/test-services/parent/child2", "", false));
    EXPECT_TRUE(zk_client_->CreateNode("/test-services/parent/child3", "", false));
    
    auto children = zk_client_->GetChildren("/test-services/parent");
    
    EXPECT_EQ(children.size(), 3);
    EXPECT_NE(std::find(children.begin(), children.end(), "child1"), children.end());
    EXPECT_NE(std::find(children.begin(), children.end(), "child2"), children.end());
    EXPECT_NE(std::find(children.begin(), children.end(), "child3"), children.end());
}

// ==================== Watch 测试（修复版）====================

TEST_F(ZooKeeperIntegrationTest, WatchChildren_TriggersOnChange) {
    EXPECT_TRUE(zk_client_->CreateServicePath("/test-services/watched"));
    
    // 使用共享指针管理状态，确保回调安全
    struct WatchState {
        std::atomic<int> callback_count{0};
        std::mutex mutex;
        std::string last_path;
        std::condition_variable cv;
        std::atomic<bool> done{false};
    };
    
    auto state = std::make_shared<WatchState>();
    
    // 捕获 shared_ptr，确保生命周期
    zk_client_->WatchChildren("/test-services/watched", 
        [state](const std::string& path) {
            if (state->done.load()) return;  // 测试已结束，忽略回调
            
            std::lock_guard<std::mutex> lock(state->mutex);
            state->callback_count++;
            state->last_path = path;
            state->cv.notify_all();
        });
    
    // 添加子节点，应该触发回调
    EXPECT_TRUE(zk_client_->CreateNode("/test-services/watched/new-child", "", false));
    
    // 等待回调（使用条件变量，更可靠）
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        bool received = state->cv.wait_for(lock, std::chrono::seconds(5), 
            [&state]() { return state->callback_count > 0; });
        
        EXPECT_TRUE(received) << "Watch callback not triggered within timeout";
    }
    
    // 标记测试结束
    state->done = true;
    
    // 取消 watch（重要！）
    zk_client_->UnwatchChildren("/test-services/watched");
    
    // 等待一小段时间，让可能还在执行的回调完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 验证结果
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        EXPECT_GT(state->callback_count, 0);
        EXPECT_EQ(state->last_path, "/test-services/watched");
    }
}

// ==================== ServiceRegistry 集成测试 ====================

TEST_F(ZooKeeperIntegrationTest, ServiceRegistry_RegisterAndUnregister) {
    auto registry = std::make_shared<ServiceRegistry>(zk_client_, "/test-services");
    
    ServiceInstance instance;
    instance.service_name = "test-service";
    instance.host = "192.168.1.100";
    instance.port = 50051;
    instance.weight = 100;
    
    EXPECT_TRUE(registry->Register(instance));
    EXPECT_TRUE(registry->IsRegistered());
    
    EXPECT_TRUE(zk_client_->Exists("/test-services/test-service/192.168.1.100:50051"));
    
    EXPECT_TRUE(registry->Unregister());
    EXPECT_FALSE(registry->IsRegistered());
}

TEST_F(ZooKeeperIntegrationTest, ServiceRegistry_Update) {
    auto registry = std::make_shared<ServiceRegistry>(zk_client_, "/test-services");
    
    ServiceInstance instance;
    instance.service_name = "test-service";
    instance.host = "192.168.1.100";
    instance.port = 50051;
    instance.weight = 100;
    
    EXPECT_TRUE(registry->Register(instance));
    
    instance.weight = 200;
    EXPECT_TRUE(registry->Update(instance));
    
    std::string data = zk_client_->GetData("/test-services/test-service/192.168.1.100:50051");
    auto parsed = ServiceInstance::FromJson(data);
    EXPECT_EQ(parsed.weight, 200);
    
    registry->Unregister();
}

// ==================== ServiceDiscovery 集成测试 ====================

TEST_F(ZooKeeperIntegrationTest, ServiceDiscovery_DiscoverRegisteredService) {
    auto registry = std::make_shared<ServiceRegistry>(zk_client_, "/test-services");
    
    ServiceInstance instance;
    instance.service_name = "discoverable-service";
    instance.host = "192.168.1.200";
    instance.port = 8080;
    instance.weight = 100;
    
    EXPECT_TRUE(registry->Register(instance));
    
    // 创建新的 ZK 客户端用于发现（模拟不同进程）
    auto discovery_client = std::make_shared<ZooKeeperClient>("localhost:2181", 5000);
    ASSERT_TRUE(discovery_client->Connect(3000));
    
    auto discovery = std::make_shared<ServiceDiscovery>(discovery_client, "/test-services");
    
    discovery->Subscribe("discoverable-service");
    
    auto instances = discovery->GetInstances("discoverable-service");
    ASSERT_EQ(instances.size(), 1);
    EXPECT_EQ(instances[0].host, "192.168.1.200");
    EXPECT_EQ(instances[0].port, 8080);
    
    // 清理
    discovery->Unsubscribe("discoverable-service");
    discovery_client->Close();
    registry->Unregister();
}

TEST_F(ZooKeeperIntegrationTest, ServiceDiscovery_DetectsNewInstance) {
    // 创建服务路径
    zk_client_->CreateServicePath("/test-services/dynamic-service");
    
    // 使用共享状态
    struct DiscoveryState {
        std::atomic<bool> callback_triggered{false};
        std::mutex mutex;
        std::condition_variable cv;
    };
    
    auto state = std::make_shared<DiscoveryState>();
    
    // 创建发现客户端
    auto discovery_client = std::make_shared<ZooKeeperClient>("localhost:2181", 5000);
    ASSERT_TRUE(discovery_client->Connect(3000));
    
    auto discovery = std::make_shared<ServiceDiscovery>(discovery_client, "/test-services");
    
    discovery->Subscribe("dynamic-service", [state](const std::string& name) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->callback_triggered = true;
        state->cv.notify_all();
    });
    
    EXPECT_EQ(discovery->GetInstances("dynamic-service").size(), 0);
    
    // 模拟新实例注册
    ServiceInstance instance;
    instance.service_name = "dynamic-service";
    instance.host = "192.168.1.300";
    instance.port = 9090;
    
    zk_client_->CreateNode(
        "/test-services/dynamic-service/192.168.1.300:9090",
        instance.ToJson(),
        false  // 使用持久节点便于测试
    );
    
    // 等待回调
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        bool received = state->cv.wait_for(lock, std::chrono::seconds(5),
            [&state]() { return state->callback_triggered.load(); });
        
        EXPECT_TRUE(received) << "Discovery callback not triggered";
    }
    
    EXPECT_EQ(discovery->GetInstances("dynamic-service").size(), 1);
    
    // 清理
    discovery->Unsubscribe("dynamic-service");
    discovery_client->Close();
}

TEST_F(ZooKeeperIntegrationTest, ServiceDiscovery_SelectInstance) {
    // 创建多个实例
    zk_client_->CreateServicePath("/test-services/multi-instance");
    
    for (int i = 1; i <= 3; ++i) {
        ServiceInstance instance;
        instance.service_name = "multi-instance";
        instance.host = "192.168.1." + std::to_string(i);
        instance.port = 50051;
        instance.weight = 100;
        
        zk_client_->CreateNode(
            "/test-services/multi-instance/192.168.1." + std::to_string(i) + ":50051",
            instance.ToJson(),
            false
        );
    }
    
    auto discovery_client = std::make_shared<ZooKeeperClient>("localhost:2181", 5000);
    ASSERT_TRUE(discovery_client->Connect(3000));
    
    auto discovery = std::make_shared<ServiceDiscovery>(discovery_client, "/test-services");
    discovery->Subscribe("multi-instance");
    
    auto selected = discovery->SelectInstance("multi-instance");
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected->port, 50051);
    
    // 清理
    discovery->Unsubscribe("multi-instance");
    discovery_client->Close();
}

}  // namespace testing
}  // namespace user_service
