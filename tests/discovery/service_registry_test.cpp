// tests/discovery/service_registry_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "discovery/service_registry.h"
#include "mock_zk_client.h"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::NiceMock;

namespace user_service {
namespace testing {

class ServiceRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_zk_ = std::make_shared<NiceMock<MockZooKeeperClient>>();
        
        // 默认设置为已连接状态
        ON_CALL(*mock_zk_, IsConnected()).WillByDefault(Return(true));
        ON_CALL(*mock_zk_, CreateServicePath(_)).WillByDefault(Return(true));
        ON_CALL(*mock_zk_, CreateNode(_, _, _)).WillByDefault(Return(true));
        ON_CALL(*mock_zk_, DeleteNode(_)).WillByDefault(Return(true));
        ON_CALL(*mock_zk_, SetData(_, _)).WillByDefault(Return(true));
        
        registry_ = std::make_unique<ServiceRegistry>(mock_zk_, "/services");
        
        // 创建测试实例
        test_instance_.service_name = "user-service";
        test_instance_.host = "192.168.1.10";
        test_instance_.port = 50051;
        test_instance_.weight = 100;
    }

    std::shared_ptr<NiceMock<MockZooKeeperClient>> mock_zk_;
    std::unique_ptr<ServiceRegistry> registry_;
    ServiceInstance test_instance_;
};

// ==================== 注册测试 ====================

TEST_F(ServiceRegistryTest, Register_WithValidInstance_ReturnsTrue) {
    EXPECT_CALL(*mock_zk_, CreateServicePath("/services/user-service"))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_zk_, CreateNode("/services/user-service/192.168.1.10:50051", 
                                       _, true))
        .WillOnce(Return(true));
    
    EXPECT_TRUE(registry_->Register(test_instance_));
    EXPECT_TRUE(registry_->IsRegistered());
}

TEST_F(ServiceRegistryTest, Register_WhenNotConnected_ReturnsFalse) {
    ON_CALL(*mock_zk_, IsConnected()).WillByDefault(Return(false));
    
    EXPECT_FALSE(registry_->Register(test_instance_));
    EXPECT_FALSE(registry_->IsRegistered());
}

TEST_F(ServiceRegistryTest, Register_WithInvalidInstance_ReturnsFalse) {
    test_instance_.host = "";  // 无效实例
    
    EXPECT_FALSE(registry_->Register(test_instance_));
    EXPECT_FALSE(registry_->IsRegistered());
}

TEST_F(ServiceRegistryTest, Register_WhenCreatePathFails_ReturnsFalse) {
    EXPECT_CALL(*mock_zk_, CreateServicePath(_))
        .WillOnce(Return(false));
    
    EXPECT_FALSE(registry_->Register(test_instance_));
    EXPECT_FALSE(registry_->IsRegistered());
}

TEST_F(ServiceRegistryTest, Register_WhenCreateNodeFails_ReturnsFalse) {
    EXPECT_CALL(*mock_zk_, CreateServicePath(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_zk_, CreateNode(_, _, _)).WillOnce(Return(false));
    
    EXPECT_FALSE(registry_->Register(test_instance_));
    EXPECT_FALSE(registry_->IsRegistered());
}

TEST_F(ServiceRegistryTest, Register_CreatesEphemeralNode) {
    // 验证创建的是临时节点（ephemeral = true）
    EXPECT_CALL(*mock_zk_, CreateNode(_, _, true))
        .WillOnce(Return(true));
    
    registry_->Register(test_instance_);
}

TEST_F(ServiceRegistryTest, Register_StoresInstanceDataAsJson) {
    std::string captured_data;
    
    EXPECT_CALL(*mock_zk_, CreateNode(_, _, _))
        .WillOnce(Invoke([&](const std::string&, 
                             const std::string& data, 
                             bool) {
            captured_data = data;
            return true;
        }));
    
    registry_->Register(test_instance_);
    
    // 验证数据是有效的 JSON
    auto parsed = ServiceInstance::FromJson(captured_data);
    EXPECT_EQ(parsed.host, test_instance_.host);
    EXPECT_EQ(parsed.port, test_instance_.port);
}

// ==================== 注销测试 ====================

TEST_F(ServiceRegistryTest, Unregister_WhenNotRegistered_ReturnsTrue) {
    // 未注册时注销应该成功（幂等性）
    EXPECT_TRUE(registry_->Unregister());
}

TEST_F(ServiceRegistryTest, Unregister_WhenRegistered_DeletesNode) {
    // 先注册
    registry_->Register(test_instance_);
    
    EXPECT_CALL(*mock_zk_, DeleteNode("/services/user-service/192.168.1.10:50051"))
        .WillOnce(Return(true));
    
    EXPECT_TRUE(registry_->Unregister());
    EXPECT_FALSE(registry_->IsRegistered());
}

TEST_F(ServiceRegistryTest, Unregister_WhenDeleteFails_StillMarksUnregistered) {
    registry_->Register(test_instance_);
    
    EXPECT_CALL(*mock_zk_, DeleteNode(_)).WillOnce(Return(false));
    
    // 即使删除失败，也应该标记为未注册
    registry_->Unregister();
    EXPECT_FALSE(registry_->IsRegistered());
}

TEST_F(ServiceRegistryTest, Unregister_CanBeCalledMultipleTimes) {
    registry_->Register(test_instance_);
    
    EXPECT_TRUE(registry_->Unregister());
    EXPECT_TRUE(registry_->Unregister());  // 第二次也应该成功
    EXPECT_TRUE(registry_->Unregister());  // 第三次也应该成功
}

// ==================== 更新测试 ====================

TEST_F(ServiceRegistryTest, Update_WhenNotRegistered_ReturnsFalse) {
    EXPECT_FALSE(registry_->Update(test_instance_));
}

TEST_F(ServiceRegistryTest, Update_WhenRegistered_UpdatesData) {
    registry_->Register(test_instance_);
    
    test_instance_.weight = 200;  // 修改权重
    
    EXPECT_CALL(*mock_zk_, SetData(_, _)).WillOnce(Return(true));
    
    EXPECT_TRUE(registry_->Update(test_instance_));
}

TEST_F(ServiceRegistryTest, Update_WhenNotConnected_ReturnsFalse) {
    registry_->Register(test_instance_);
    
    ON_CALL(*mock_zk_, IsConnected()).WillByDefault(Return(false));
    
    EXPECT_FALSE(registry_->Update(test_instance_));
}

TEST_F(ServiceRegistryTest, Update_WhenSetDataFails_ReturnsFalse) {
    registry_->Register(test_instance_);
    
    EXPECT_CALL(*mock_zk_, SetData(_, _)).WillOnce(Return(false));
    
    EXPECT_FALSE(registry_->Update(test_instance_));
}

// ==================== 路径构建测试 ====================

TEST_F(ServiceRegistryTest, Register_BuildsCorrectServicePath) {
    EXPECT_CALL(*mock_zk_, CreateServicePath("/services/user-service"))
        .WillOnce(Return(true));
    
    registry_->Register(test_instance_);
}

TEST_F(ServiceRegistryTest, Register_BuildsCorrectInstancePath) {
    EXPECT_CALL(*mock_zk_, CreateNode(
        "/services/user-service/192.168.1.10:50051", _, _))
        .WillOnce(Return(true));
    
    registry_->Register(test_instance_);
}

TEST_F(ServiceRegistryTest, Register_WithDifferentRootPath) {
    auto registry = std::make_unique<ServiceRegistry>(mock_zk_, "/my-services");
    
    EXPECT_CALL(*mock_zk_, CreateServicePath("/my-services/user-service"))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_zk_, CreateNode(
        "/my-services/user-service/192.168.1.10:50051", _, _))
        .WillOnce(Return(true));
    
    registry->Register(test_instance_);
}

// ==================== 析构函数测试 ====================

TEST_F(ServiceRegistryTest, Destructor_UnregistersIfRegistered) {
    registry_->Register(test_instance_);
    
    EXPECT_CALL(*mock_zk_, DeleteNode(_)).Times(1);
    
    registry_.reset();  // 销毁 registry
}

TEST_F(ServiceRegistryTest, Destructor_DoesNothingIfNotRegistered) {
    // 不注册，直接销毁
    EXPECT_CALL(*mock_zk_, DeleteNode(_)).Times(0);
    
    registry_.reset();
}

// ==================== 并发安全测试 ====================

TEST_F(ServiceRegistryTest, ConcurrentRegisterUnregister) {
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    // 多个线程同时尝试注册/注销
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([this, &success_count, i]() {
            if (i % 2 == 0) {
                if (registry_->Register(test_instance_)) {
                    success_count++;
                }
            } else {
                registry_->Unregister();
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // 不应该崩溃，最终状态应该一致
    // （具体的 success_count 取决于执行顺序）
}

}  // namespace testing
}  // namespace user_service
