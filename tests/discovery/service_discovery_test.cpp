// tests/discovery/service_discovery_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "discovery/service_discovery.h"
#include "mock_zk_client.h"
#include <thread>
#include <chrono>

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::SaveArg;
using ::testing::DoAll;

namespace user_service {
namespace testing {

class ServiceDiscoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_zk_ = std::make_shared<NiceMock<MockZooKeeperClient>>();
        
        // 默认设置为已连接状态
        ON_CALL(*mock_zk_, IsConnected()).WillByDefault(Return(true));
        
        discovery_ = std::make_unique<ServiceDiscovery>(mock_zk_, "/services");
    }

    // 创建测试用的服务实例 JSON
    std::string CreateInstanceJson(const std::string& host, int port, int weight = 100) {
        ServiceInstance inst;
        inst.service_name = "user-service";
        inst.host = host;
        inst.port = port;
        inst.weight = weight;
        return inst.ToJson();
    }

    std::shared_ptr<NiceMock<MockZooKeeperClient>> mock_zk_;
    std::unique_ptr<ServiceDiscovery> discovery_;
};

// ==================== 订阅测试 ====================

TEST_F(ServiceDiscoveryTest, Subscribe_SetsWatch) {
    EXPECT_CALL(*mock_zk_, WatchChildren("/services/user-service", _))
        .Times(1);
    EXPECT_CALL(*mock_zk_, GetChildren("/services/user-service"))
        .WillOnce(Return(std::vector<std::string>{}));
    
    discovery_->Subscribe("user-service");
}

TEST_F(ServiceDiscoveryTest, Subscribe_WithCallback_StoresCallback) {
    bool callback_invoked = false;
    
    EXPECT_CALL(*mock_zk_, GetChildren(_))
        .WillRepeatedly(Return(std::vector<std::string>{}));
    
    discovery_->Subscribe("user-service", [&](const std::string& name) {
        callback_invoked = true;
        EXPECT_EQ(name, "user-service");
    });
    
    // 回调应该在实例变化时触发（这里暂时不测试）
}

TEST_F(ServiceDiscoveryTest, Subscribe_WhenNotConnected_DoesNotCrash) {
    ON_CALL(*mock_zk_, IsConnected()).WillByDefault(Return(false));
    
    // 不应该崩溃
    discovery_->Subscribe("user-service");
}

TEST_F(ServiceDiscoveryTest, Subscribe_FetchesInstancesImmediately) {
    std::vector<std::string> children = {"192.168.1.10:50051"};
    
    EXPECT_CALL(*mock_zk_, GetChildren("/services/user-service"))
        .WillOnce(Return(children));
    EXPECT_CALL(*mock_zk_, GetData("/services/user-service/192.168.1.10:50051"))
        .WillOnce(Return(CreateInstanceJson("192.168.1.10", 50051)));
    
    discovery_->Subscribe("user-service");
    
    auto instances = discovery_->GetInstances("user-service");
    ASSERT_EQ(instances.size(), 1);
    EXPECT_EQ(instances[0].host, "192.168.1.10");
}

// ==================== 取消订阅测试 ====================

TEST_F(ServiceDiscoveryTest, Unsubscribe_RemovesWatch) {
    EXPECT_CALL(*mock_zk_, GetChildren(_))
        .WillRepeatedly(Return(std::vector<std::string>{}));
    
    discovery_->Subscribe("user-service");
    
    EXPECT_CALL(*mock_zk_, UnwatchChildren("/services/user-service"))
        .Times(1);
    
    discovery_->Unsubscribe("user-service");
}

TEST_F(ServiceDiscoveryTest, Unsubscribe_ClearsCache) {
    std::vector<std::string> children = {"192.168.1.10:50051"};
    
    EXPECT_CALL(*mock_zk_, GetChildren(_)).WillRepeatedly(Return(children));
    EXPECT_CALL(*mock_zk_, GetData(_))
        .WillRepeatedly(Return(CreateInstanceJson("192.168.1.10", 50051)));
    
    discovery_->Subscribe("user-service");
    EXPECT_EQ(discovery_->GetInstances("user-service").size(), 1);
    
    discovery_->Unsubscribe("user-service");
    EXPECT_EQ(discovery_->GetInstances("user-service").size(), 0);
}

TEST_F(ServiceDiscoveryTest, Unsubscribe_NonExistentService_DoesNotCrash) {
    discovery_->Unsubscribe("non-existent-service");
}

// ==================== GetInstances 测试 ====================

TEST_F(ServiceDiscoveryTest, GetInstances_ReturnsEmptyForUnsubscribedService) {
    auto instances = discovery_->GetInstances("unknown-service");
    EXPECT_TRUE(instances.empty());
}

TEST_F(ServiceDiscoveryTest, GetInstances_ReturnsAllInstances) {
    std::vector<std::string> children = {
        "192.168.1.10:50051",
        "192.168.1.11:50051",
        "192.168.1.12:50051"
    };
    
    EXPECT_CALL(*mock_zk_, GetChildren(_)).WillOnce(Return(children));
    EXPECT_CALL(*mock_zk_, GetData("/services/user-service/192.168.1.10:50051"))
        .WillOnce(Return(CreateInstanceJson("192.168.1.10", 50051)));
    EXPECT_CALL(*mock_zk_, GetData("/services/user-service/192.168.1.11:50051"))
        .WillOnce(Return(CreateInstanceJson("192.168.1.11", 50051)));
    EXPECT_CALL(*mock_zk_, GetData("/services/user-service/192.168.1.12:50051"))
        .WillOnce(Return(CreateInstanceJson("192.168.1.12", 50051)));
    
    discovery_->Subscribe("user-service");
    
    auto instances = discovery_->GetInstances("user-service");
    EXPECT_EQ(instances.size(), 3);
}

TEST_F(ServiceDiscoveryTest, GetInstances_SkipsInvalidInstances) {
    std::vector<std::string> children = {"valid", "invalid"};
    
    EXPECT_CALL(*mock_zk_, GetChildren(_)).WillOnce(Return(children));
    EXPECT_CALL(*mock_zk_, GetData("/services/user-service/valid"))
        .WillOnce(Return(CreateInstanceJson("192.168.1.10", 50051)));
    EXPECT_CALL(*mock_zk_, GetData("/services/user-service/invalid"))
        .WillOnce(Return("invalid json"));  // 无效数据
    
    discovery_->Subscribe("user-service");
    
    auto instances = discovery_->GetInstances("user-service");
    EXPECT_EQ(instances.size(), 1);
}

TEST_F(ServiceDiscoveryTest, GetInstances_SkipsEmptyData) {
    std::vector<std::string> children = {"node1", "node2"};
    
    EXPECT_CALL(*mock_zk_, GetChildren(_)).WillOnce(Return(children));
    EXPECT_CALL(*mock_zk_, GetData("/services/user-service/node1"))
        .WillOnce(Return(CreateInstanceJson("192.168.1.10", 50051)));
    EXPECT_CALL(*mock_zk_, GetData("/services/user-service/node2"))
        .WillOnce(Return(""));  // 空数据
    
    discovery_->Subscribe("user-service");
    
    auto instances = discovery_->GetInstances("user-service");
    EXPECT_EQ(instances.size(), 1);
}

// ==================== SelectInstance 测试 ====================

TEST_F(ServiceDiscoveryTest, SelectInstance_ReturnsNullForEmptyService) {
    auto instance = discovery_->SelectInstance("unknown-service");
    EXPECT_EQ(instance, nullptr);
}

TEST_F(ServiceDiscoveryTest, SelectInstance_ReturnsSingleInstance) {
    std::vector<std::string> children = {"192.168.1.10:50051"};
    
    EXPECT_CALL(*mock_zk_, GetChildren(_)).WillOnce(Return(children));
    EXPECT_CALL(*mock_zk_, GetData(_))
        .WillOnce(Return(CreateInstanceJson("192.168.1.10", 50051)));
    
    discovery_->Subscribe("user-service");
    
    auto instance = discovery_->SelectInstance("user-service");
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(instance->host, "192.168.1.10");
    EXPECT_EQ(instance->port, 50051);
}

TEST_F(ServiceDiscoveryTest, SelectInstance_RandomlySelectsFromMultiple) {
    std::vector<std::string> children = {
        "192.168.1.10:50051",
        "192.168.1.11:50051",
        "192.168.1.12:50051"
    };
    
    EXPECT_CALL(*mock_zk_, GetChildren(_)).WillOnce(Return(children));
    EXPECT_CALL(*mock_zk_, GetData(_))
        .WillRepeatedly(Invoke([this](const std::string& path) {
            if (path.find("192.168.1.10") != std::string::npos)
                return CreateInstanceJson("192.168.1.10", 50051);
            if (path.find("192.168.1.11") != std::string::npos)
                return CreateInstanceJson("192.168.1.11", 50051);
            return CreateInstanceJson("192.168.1.12", 50051);
        }));
    
    discovery_->Subscribe("user-service");
    
    // 多次选择，统计分布
    std::map<std::string, int> selection_count;
    for (int i = 0; i < 100; ++i) {
        auto instance = discovery_->SelectInstance("user-service");
        ASSERT_NE(instance, nullptr);
        selection_count[instance->host]++;
    }
    
    // 每个实例都应该被选中过（概率上）
    EXPECT_GT(selection_count["192.168.1.10"], 0);
    EXPECT_GT(selection_count["192.168.1.11"], 0);
    EXPECT_GT(selection_count["192.168.1.12"], 0);
}

// ==================== SelectInstanceWeighted 测试 ====================

TEST_F(ServiceDiscoveryTest, SelectInstanceWeighted_ReturnsNullForEmptyService) {
    auto instance = discovery_->SelectInstanceWeighted("unknown-service");
    EXPECT_EQ(instance, nullptr);
}

TEST_F(ServiceDiscoveryTest, SelectInstanceWeighted_RespectWeight) {
    std::vector<std::string> children = {"high", "low"};
    
    EXPECT_CALL(*mock_zk_, GetChildren(_)).WillOnce(Return(children));
    EXPECT_CALL(*mock_zk_, GetData("/services/user-service/high"))
        .WillOnce(Return(CreateInstanceJson("192.168.1.10", 50051, 900)));  // 高权重
    EXPECT_CALL(*mock_zk_, GetData("/services/user-service/low"))
        .WillOnce(Return(CreateInstanceJson("192.168.1.11", 50051, 100)));  // 低权重
    
    discovery_->Subscribe("user-service");
    
    // 多次选择，统计分布
    std::map<std::string, int> selection_count;
    for (int i = 0; i < 1000; ++i) {
        auto instance = discovery_->SelectInstanceWeighted("user-service");
        ASSERT_NE(instance, nullptr);
        selection_count[instance->host]++;
    }
    
    // 高权重实例应该被选中更多次（约 90%）
    double high_ratio = static_cast<double>(selection_count["192.168.1.10"]) / 1000;
    EXPECT_GT(high_ratio, 0.8);
    EXPECT_LT(high_ratio, 0.95);
}

TEST_F(ServiceDiscoveryTest, SelectInstanceWeighted_WithZeroWeight_FallsBackToRandom) {
    std::vector<std::string> children = {"node1", "node2"};
    
    EXPECT_CALL(*mock_zk_, GetChildren(_)).WillOnce(Return(children));
    EXPECT_CALL(*mock_zk_, GetData("/services/user-service/node1"))
        .WillOnce(Return(CreateInstanceJson("192.168.1.10", 50051, 0)));  // 零权重
    EXPECT_CALL(*mock_zk_, GetData("/services/user-service/node2"))
        .WillOnce(Return(CreateInstanceJson("192.168.1.11", 50051, 0)));  // 零权重
    
    discovery_->Subscribe("user-service");
    
    // 应该退化为随机选择
    auto instance = discovery_->SelectInstanceWeighted("user-service");
    EXPECT_NE(instance, nullptr);
}

// ==================== Watch 回调测试 ====================

TEST_F(ServiceDiscoveryTest, WatchCallback_UpdatesInstancesOnChange) {
    ZooKeeperClient::WatchCallback captured_callback;
    
    // 捕获 watch 回调
    EXPECT_CALL(*mock_zk_, WatchChildren(_, _))
        .WillOnce(SaveArg<1>(&captured_callback));
    
    // 初始状态：1 个实例
    EXPECT_CALL(*mock_zk_, GetChildren(_))
        .WillOnce(Return(std::vector<std::string>{"node1"}))
        .WillOnce(Return(std::vector<std::string>{"node1", "node2"}));  // 变化后
    
    EXPECT_CALL(*mock_zk_, GetData(_))
        .WillRepeatedly(Invoke([this](const std::string& path) {
            if (path.find("node1") != std::string::npos)
                return CreateInstanceJson("192.168.1.10", 50051);
            return CreateInstanceJson("192.168.1.11", 50051);
        }));
    
    discovery_->Subscribe("user-service");
    EXPECT_EQ(discovery_->GetInstances("user-service").size(), 1);
    
    // 模拟 ZK 触发 watch 回调
    if (captured_callback) {
        captured_callback("/services/user-service");
    }
    
    // 实例数量应该更新
    EXPECT_EQ(discovery_->GetInstances("user-service").size(), 2);
}

TEST_F(ServiceDiscoveryTest, WatchCallback_InvokesUserCallback) {
    bool user_callback_invoked = false;
    std::string received_service_name;
    ZooKeeperClient::WatchCallback captured_callback;
    
    EXPECT_CALL(*mock_zk_, WatchChildren(_, _))
        .WillOnce(SaveArg<1>(&captured_callback));
    EXPECT_CALL(*mock_zk_, GetChildren(_))
        .WillRepeatedly(Return(std::vector<std::string>{}));
    
    discovery_->Subscribe("user-service", [&](const std::string& name) {
        user_callback_invoked = true;
        received_service_name = name;
    });
    
    // 模拟 ZK 触发 watch 回调
    if (captured_callback) {
        captured_callback("/services/user-service");
    }
    
    EXPECT_TRUE(user_callback_invoked);
    EXPECT_EQ(received_service_name, "user-service");
}

// ==================== 并发安全测试 ====================

TEST_F(ServiceDiscoveryTest, ConcurrentGetInstances) {
    std::vector<std::string> children = {"node1"};
    
    EXPECT_CALL(*mock_zk_, GetChildren(_)).WillOnce(Return(children));
    EXPECT_CALL(*mock_zk_, GetData(_))
        .WillOnce(Return(CreateInstanceJson("192.168.1.10", 50051)));
    
    discovery_->Subscribe("user-service");
    
    // 多线程并发获取实例
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([this, &success_count]() {
            for (int j = 0; j < 100; ++j) {
                auto instances = discovery_->GetInstances("user-service");
                if (instances.size() == 1) {
                    success_count++;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(success_count, 1000);
}

TEST_F(ServiceDiscoveryTest, ConcurrentSelectInstance) {
    std::vector<std::string> children = {"node1", "node2"};
    
    EXPECT_CALL(*mock_zk_, GetChildren(_)).WillOnce(Return(children));
    EXPECT_CALL(*mock_zk_, GetData(_))
        .WillRepeatedly(Invoke([this](const std::string& path) {
            if (path.find("node1") != std::string::npos)
                return CreateInstanceJson("192.168.1.10", 50051);
            return CreateInstanceJson("192.168.1.11", 50051);
        }));
    
    discovery_->Subscribe("user-service");
    
    // 多线程并发选择实例
    std::vector<std::thread> threads;
    std::atomic<int> null_count{0};
    
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([this, &null_count]() {
            for (int j = 0; j < 100; ++j) {
                auto instance = discovery_->SelectInstance("user-service");
                if (!instance) {
                    null_count++;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(null_count, 0);  // 不应该返回 nullptr
}

// ==================== 边界情况测试 ====================

TEST_F(ServiceDiscoveryTest, MultipleServices) {
    EXPECT_CALL(*mock_zk_, GetChildren("/services/service-a"))
        .WillOnce(Return(std::vector<std::string>{"a1"}));
    EXPECT_CALL(*mock_zk_, GetChildren("/services/service-b"))
        .WillOnce(Return(std::vector<std::string>{"b1", "b2"}));
    
    EXPECT_CALL(*mock_zk_, GetData("/services/service-a/a1"))
        .WillOnce(Return(CreateInstanceJson("10.0.0.1", 8080)));
    EXPECT_CALL(*mock_zk_, GetData("/services/service-b/b1"))
        .WillOnce(Return(CreateInstanceJson("10.0.0.2", 8080)));
    EXPECT_CALL(*mock_zk_, GetData("/services/service-b/b2"))
        .WillOnce(Return(CreateInstanceJson("10.0.0.3", 8080)));
    
    discovery_->Subscribe("service-a");
    discovery_->Subscribe("service-b");
    
    EXPECT_EQ(discovery_->GetInstances("service-a").size(), 1);
    EXPECT_EQ(discovery_->GetInstances("service-b").size(), 2);
}

TEST_F(ServiceDiscoveryTest, DestructorCleansUpWatches) {
    EXPECT_CALL(*mock_zk_, GetChildren(_))
        .WillRepeatedly(Return(std::vector<std::string>{}));
    
    discovery_->Subscribe("service-a");
    discovery_->Subscribe("service-b");
    
    // 析构时应该取消所有 watch
    EXPECT_CALL(*mock_zk_, UnwatchChildren("/services/service-a")).Times(1);
    EXPECT_CALL(*mock_zk_, UnwatchChildren("/services/service-b")).Times(1);
    
    discovery_.reset();
}

}  // namespace testing
}  // namespace user_service
