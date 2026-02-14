// tests/discovery/zk_integration_test.cpp

#include <gtest/gtest.h>
#include "discovery/zk_client.h"
#include "discovery/service_registry.h"
#include "discovery/service_discovery.h"
#include "common/logger.h"
#include <thread>
#include <chrono>
#include <memory>
#include <atomic>
#include <mutex>
#include <map>
#include <algorithm>

namespace user_service {
namespace testing {

/**
 * @brief ZooKeeper 集成测试
 * 
 * 这些测试需要真实的 ZooKeeper 服务器。
 * 运行前请确保 ZooKeeper 在 localhost:2181 运行。
 * 
 * 如果 ZK 不可用，测试会被跳过。
 */
class ZooKeeperIntegrationTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // 尝试连接 ZooKeeper
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
        
        // 清理测试路径
        CleanupTestPath();
    }

    void TearDown() override {
        if (zk_client_ && zk_client_->IsConnected()) {
            // ★ 先取消所有 watch，防止 CleanupTestPath 触发回调
            for (const auto& path : watched_paths_) {
                zk_client_->UnwatchChildren(path);
            }
            watched_paths_.clear();
            
            // 等待正在执行的回调完成
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            
            CleanupTestPath();
            zk_client_->Close();
        }
    }

    void CleanupTestPath() {
        // 递归删除测试路径下的所有节点
        auto children = zk_client_->GetChildren("/test-services");
        for (const auto& child : children) {
            auto grandchildren = zk_client_->GetChildren("/test-services/" + child);
            for (const auto& gc : grandchildren) {
                zk_client_->DeleteNode("/test-services/" + child + "/" + gc);
            }
            zk_client_->DeleteNode("/test-services/" + child);
        }
        zk_client_->DeleteNode("/test-services");
    }
    
    // ★ 辅助方法：注册 watch 并记录路径，便于 TearDown 清理
    void WatchAndTrack(const std::string& path, ZooKeeperClient::WatchCallback callback) {
        watched_paths_.push_back(path);
        zk_client_->WatchChildren(path, std::move(callback));
    }

    static bool zk_available_;
    std::shared_ptr<ZooKeeperClient> zk_client_;
    std::vector<std::string> watched_paths_;  // 跟踪所有 watch 的路径
};

bool ZooKeeperIntegrationTest::zk_available_ = false;

// ==================== ZooKeeperClient 集成测试 ====================

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

TEST_F(ZooKeeperIntegrationTest, WatchChildren_TriggersOnChange) {
    EXPECT_TRUE(zk_client_->CreateServicePath("/test-services/watched"));
    
    // ★ 使用 shared_ptr 确保回调数据的生命周期
    auto callback_count = std::make_shared<std::atomic<int>>(0);
    auto last_path = std::make_shared<std::string>();
    auto path_mutex = std::make_shared<std::mutex>();
    
    // ★ 通过值捕获 shared_ptr，不再捕获引用
    WatchAndTrack("/test-services/watched", 
        [callback_count, last_path, path_mutex](const std::string& path) {
            (*callback_count)++;
            std::lock_guard<std::mutex> lock(*path_mutex);
            *last_path = path;
        });
    
    // 添加子节点，应该触发回调
    EXPECT_TRUE(zk_client_->CreateNode("/test-services/watched/new-child", "", false));
    
    // 等待回调（增加等待时间以确保稳定性）
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    EXPECT_GT(callback_count->load(), 0);
    {
        std::lock_guard<std::mutex> lock(*path_mutex);
        EXPECT_EQ(*last_path, "/test-services/watched");
    }
}

TEST_F(ZooKeeperIntegrationTest, WatchChildren_MultipleChanges) {
    EXPECT_TRUE(zk_client_->CreateServicePath("/test-services/multi-watch"));
    
    auto callback_count = std::make_shared<std::atomic<int>>(0);
    
    WatchAndTrack("/test-services/multi-watch", 
        [callback_count](const std::string& path) {
            (*callback_count)++;
        });
    
    // 多次添加子节点
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(zk_client_->CreateNode(
            "/test-services/multi-watch/child" + std::to_string(i), "", false));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    // 每次添加都应该触发回调
    EXPECT_GE(callback_count->load(), 3);
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
    
    // 验证节点已创建
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
    
    // 更新权重
    instance.weight = 200;
    EXPECT_TRUE(registry->Update(instance));
    
    // 验证数据已更新
    std::string data = zk_client_->GetData("/test-services/test-service/192.168.1.100:50051");
    auto parsed = ServiceInstance::FromJson(data);
    EXPECT_EQ(parsed.weight, 200);
}

TEST_F(ZooKeeperIntegrationTest, ServiceRegistry_ReRegisterAfterExpiry) {
    // 创建短会话超时的客户端
    auto short_session_client = std::make_shared<ZooKeeperClient>("localhost:2181", 5000);
    ASSERT_TRUE(short_session_client->Connect(3000));
    
    auto registry = std::make_shared<ServiceRegistry>(short_session_client, "/test-services");
    
    ServiceInstance instance;
    instance.service_name = "short-session-service";
    instance.host = "192.168.1.200";
    instance.port = 50052;
    
    EXPECT_TRUE(registry->Register(instance));
    EXPECT_TRUE(zk_client_->Exists("/test-services/short-session-service/192.168.1.200:50052"));
    
    // 注销
    registry->Unregister();
    short_session_client->Close();
}

// ==================== ServiceDiscovery 集成测试 ====================

TEST_F(ZooKeeperIntegrationTest, ServiceDiscovery_DiscoverRegisteredService) {
    auto registry = std::make_shared<ServiceRegistry>(zk_client_, "/test-services");
    auto discovery = std::make_shared<ServiceDiscovery>(zk_client_, "/test-services");
    
    // 注册服务
    ServiceInstance instance;
    instance.service_name = "discoverable-service";
    instance.host = "192.168.1.200";
    instance.port = 8080;
    instance.weight = 100;
    
    EXPECT_TRUE(registry->Register(instance));
    
    // 订阅并发现服务
    discovery->Subscribe("discoverable-service");
    watched_paths_.push_back("/test-services/discoverable-service");
    
    auto instances = discovery->GetInstances("discoverable-service");
    ASSERT_EQ(instances.size(), 1);
    EXPECT_EQ(instances[0].host, "192.168.1.200");
    EXPECT_EQ(instances[0].port, 8080);
    
    // 清理
    discovery->Unsubscribe("discoverable-service");
}

TEST_F(ZooKeeperIntegrationTest, ServiceDiscovery_DetectsNewInstance) {
    auto discovery = std::make_shared<ServiceDiscovery>(zk_client_, "/test-services");
    
    // 先创建服务路径
    zk_client_->CreateServicePath("/test-services/dynamic-service");
    
    // ★ 使用 shared_ptr 延长生命周期
    auto callback_triggered = std::make_shared<std::atomic<bool>>(false);
    
    discovery->Subscribe("dynamic-service", 
        [callback_triggered](const std::string& name) {
            callback_triggered->store(true);
        });
    watched_paths_.push_back("/test-services/dynamic-service");
    
    EXPECT_EQ(discovery->GetInstances("dynamic-service").size(), 0);
    
    // 模拟新实例注册
    ServiceInstance instance;
    instance.service_name = "dynamic-service";
    instance.host = "192.168.1.300";
    instance.port = 9090;
    
    zk_client_->CreateNode(
        "/test-services/dynamic-service/192.168.1.300:9090",
        instance.ToJson(),
        true  // 临时节点
    );
    
    // 等待 watch 触发
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    EXPECT_TRUE(callback_triggered->load());
    EXPECT_EQ(discovery->GetInstances("dynamic-service").size(), 1);
    
    // 清理
    discovery->Unsubscribe("dynamic-service");
}

TEST_F(ZooKeeperIntegrationTest, ServiceDiscovery_DetectsInstanceRemoval) {
    auto discovery = std::make_shared<ServiceDiscovery>(zk_client_, "/test-services");
    
    // 创建服务和实例
    zk_client_->CreateServicePath("/test-services/removal-test");
    
    ServiceInstance instance;
    instance.service_name = "removal-test";
    instance.host = "192.168.1.400";
    instance.port = 7070;
    
    zk_client_->CreateNode(
        "/test-services/removal-test/192.168.1.400:7070",
        instance.ToJson(),
        false  // 持久节点，方便测试删除
    );
    
    auto removal_detected = std::make_shared<std::atomic<bool>>(false);
    
    discovery->Subscribe("removal-test", 
        [removal_detected](const std::string& name) {
            removal_detected->store(true);
        });
    watched_paths_.push_back("/test-services/removal-test");
    
    EXPECT_EQ(discovery->GetInstances("removal-test").size(), 1);
    
    // 删除实例
    zk_client_->DeleteNode("/test-services/removal-test/192.168.1.400:7070");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    EXPECT_TRUE(removal_detected->load());
    EXPECT_EQ(discovery->GetInstances("removal-test").size(), 0);
    
    // 清理
    discovery->Unsubscribe("removal-test");
}

TEST_F(ZooKeeperIntegrationTest, ServiceDiscovery_SelectInstance) {
    auto discovery = std::make_shared<ServiceDiscovery>(zk_client_, "/test-services");
    
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
    
    discovery->Subscribe("multi-instance");
    watched_paths_.push_back("/test-services/multi-instance");
    
    // 应该能选到实例
    auto selected = discovery->SelectInstance("multi-instance");
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected->port, 50051);
    
    // 清理
    discovery->Unsubscribe("multi-instance");
}

TEST_F(ZooKeeperIntegrationTest, ServiceDiscovery_SelectInstanceWeighted) {
    auto discovery = std::make_shared<ServiceDiscovery>(zk_client_, "/test-services");
    
    zk_client_->CreateServicePath("/test-services/weighted-service");
    
    // 创建不同权重的实例
    std::vector<std::pair<std::string, int>> instances_config = {
        {"192.168.1.10", 10},    // 低权重
        {"192.168.1.20", 100},   // 中权重
        {"192.168.1.30", 1000},  // 高权重
    };
    
    for (const auto& [host, weight] : instances_config) {
        ServiceInstance instance;
        instance.service_name = "weighted-service";
        instance.host = host;
        instance.port = 50051;
        instance.weight = weight;
        
        zk_client_->CreateNode(
            "/test-services/weighted-service/" + host + ":50051",
            instance.ToJson(),
            false
        );
    }
    
    discovery->Subscribe("weighted-service");
    watched_paths_.push_back("/test-services/weighted-service");
    
    // 多次选择，统计分布
    std::map<std::string, int> selection_count;
    const int iterations = 1000;
    
    for (int i = 0; i < iterations; ++i) {
        auto selected = discovery->SelectInstanceWeighted("weighted-service");
        ASSERT_NE(selected, nullptr);
        selection_count[selected->host]++;
    }
    
    // 高权重的应该被选中更多
    EXPECT_GT(selection_count["192.168.1.30"], selection_count["192.168.1.10"]);
    EXPECT_GT(selection_count["192.168.1.20"], selection_count["192.168.1.10"]);
    
    // 打印分布（调试用）
    std::cout << "Selection distribution:" << std::endl;
    for (const auto& [host, count] : selection_count) {
        std::cout << "  " << host << ": " << count << " (" 
                  << (count * 100.0 / iterations) << "%)" << std::endl;
    }
    
    // 清理
    discovery->Unsubscribe("weighted-service");
}

TEST_F(ZooKeeperIntegrationTest, ServiceDiscovery_NoInstancesReturnsNull) {
    auto discovery = std::make_shared<ServiceDiscovery>(zk_client_, "/test-services");
    
    zk_client_->CreateServicePath("/test-services/empty-service");
    
    discovery->Subscribe("empty-service");
    watched_paths_.push_back("/test-services/empty-service");
    
    auto selected = discovery->SelectInstance("empty-service");
    EXPECT_EQ(selected, nullptr);
    
    // 清理
    discovery->Unsubscribe("empty-service");
}

// ==================== 端到端测试 ====================

TEST_F(ZooKeeperIntegrationTest, EndToEnd_MultipleRegistriesAndDiscovery) {
    // 创建多个 ZK 客户端模拟多个服务实例
    std::vector<std::shared_ptr<ZooKeeperClient>> clients;
    std::vector<std::shared_ptr<ServiceRegistry>> registries;
    
    for (int i = 0; i < 3; ++i) {
        auto client = std::make_shared<ZooKeeperClient>("localhost:2181", 5000);
        ASSERT_TRUE(client->Connect(3000));
        clients.push_back(client);
        
        auto registry = std::make_shared<ServiceRegistry>(client, "/test-services");
        
        ServiceInstance instance;
        instance.service_name = "e2e-service";
        instance.host = "10.0.0." + std::to_string(i + 1);
        instance.port = 50051;
        instance.weight = 100 * (i + 1);  // 100, 200, 300
        
        EXPECT_TRUE(registry->Register(instance));
        registries.push_back(registry);
    }
    
    // 服务发现
    auto discovery = std::make_shared<ServiceDiscovery>(zk_client_, "/test-services");
    discovery->Subscribe("e2e-service");
    watched_paths_.push_back("/test-services/e2e-service");
    
    auto instances = discovery->GetInstances("e2e-service");
    EXPECT_EQ(instances.size(), 3);
    
    // 负载均衡测试
    std::map<std::string, int> counts;
    for (int i = 0; i < 600; ++i) {
        auto inst = discovery->SelectInstanceWeighted("e2e-service");
        ASSERT_NE(inst, nullptr);
        counts[inst->host]++;
    }
    
    // 权重高的应该被选中更多（300 > 200 > 100）
    EXPECT_GT(counts["10.0.0.3"], counts["10.0.0.1"]);
    EXPECT_GT(counts["10.0.0.2"], counts["10.0.0.1"]);
    
    std::cout << "E2E Selection distribution:" << std::endl;
    for (const auto& [host, count] : counts) {
        std::cout << "  " << host << ": " << count << std::endl;
    }
    
    // 清理
    discovery->Unsubscribe("e2e-service");
    for (auto& reg : registries) {
        reg->Unregister();
    }
    for (auto& client : clients) {
        client->Close();
    }
}

TEST_F(ZooKeeperIntegrationTest, EndToEnd_ServiceFailover) {
    // 创建两个实例
    auto client1 = std::make_shared<ZooKeeperClient>("localhost:2181", 5000);
    auto client2 = std::make_shared<ZooKeeperClient>("localhost:2181", 5000);
    ASSERT_TRUE(client1->Connect(3000));
    ASSERT_TRUE(client2->Connect(3000));
    
    auto registry1 = std::make_shared<ServiceRegistry>(client1, "/test-services");
    auto registry2 = std::make_shared<ServiceRegistry>(client2, "/test-services");
    
    ServiceInstance instance1, instance2;
    instance1.service_name = instance2.service_name = "failover-service";
    instance1.host = "10.0.0.1";
    instance1.port = 50051;
    instance2.host = "10.0.0.2";
    instance2.port = 50051;
    
    EXPECT_TRUE(registry1->Register(instance1));
    EXPECT_TRUE(registry2->Register(instance2));
    
    // 服务发现
    auto discovery = std::make_shared<ServiceDiscovery>(zk_client_, "/test-services");
    
    auto failover_detected = std::make_shared<std::atomic<int>>(0);
    
    discovery->Subscribe("failover-service", 
        [failover_detected](const std::string& name) {
            (*failover_detected)++;
        });
    watched_paths_.push_back("/test-services/failover-service");
    
    EXPECT_EQ(discovery->GetInstances("failover-service").size(), 2);
    
    // 模拟实例1下线
    registry1->Unregister();
    client1->Close();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // 应该检测到变化
    EXPECT_GT(failover_detected->load(), 0);
    EXPECT_EQ(discovery->GetInstances("failover-service").size(), 1);
    
    // 剩余实例应该仍然可用
    auto selected = discovery->SelectInstance("failover-service");
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected->host, "10.0.0.2");
    
    // 清理
    discovery->Unsubscribe("failover-service");
    registry2->Unregister();
    client2->Close();
}

TEST_F(ZooKeeperIntegrationTest, EndToEnd_ConcurrentRegistrations) {
    const int num_instances = 10;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    // 并发注册多个实例
    for (int i = 0; i < num_instances; ++i) {
        threads.emplace_back([i, &success_count]() {
            auto client = std::make_shared<ZooKeeperClient>("localhost:2181", 5000);
            if (!client->Connect(3000)) {
                return;
            }
            
            auto registry = std::make_shared<ServiceRegistry>(client, "/test-services");
            
            ServiceInstance instance;
            instance.service_name = "concurrent-service";
            instance.host = "10.0.1." + std::to_string(i);
            instance.port = 50051;
            
            if (registry->Register(instance)) {
                success_count++;
            }
            
            // 保持一段时间
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            registry->Unregister();
            client->Close();
        });
    }
    
    // 等待所有注册完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 验证服务发现
    auto discovery = std::make_shared<ServiceDiscovery>(zk_client_, "/test-services");
    discovery->Subscribe("concurrent-service");
    watched_paths_.push_back("/test-services/concurrent-service");
    
    auto instances = discovery->GetInstances("concurrent-service");
    std::cout << "Concurrent registration: " << instances.size() 
              << " instances discovered" << std::endl;
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(success_count.load(), num_instances);
    
    // 清理
    discovery->Unsubscribe("concurrent-service");
}

}  // namespace testing
}  // namespace user_service
