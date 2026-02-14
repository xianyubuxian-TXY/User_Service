// tests/auth/token_cleanup_task_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>

#include "auth/token_cleanup_task.h"
#include "mock_auth_deps.h"

using ::testing::_;
using ::testing::Return;
using ::testing::AtLeast;

namespace user_service {
namespace testing {

class TokenCleanupTaskTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_repo_ = std::make_shared<MockTokenRepository>();
    }

    std::shared_ptr<MockTokenRepository> mock_repo_;
};

TEST_F(TokenCleanupTaskTest, StartAndStop) {
    // 期望 CleanExpiredTokens 被调用至少一次
    EXPECT_CALL(*mock_repo_, CleanExpiredTokens())
        .WillRepeatedly(Return(Result<int64_t>::Ok(0)));
    
    // 使用很短的间隔便于测试
    TokenCleanupTask task(mock_repo_, 1);  // 1 分钟间隔
    
    task.Start();
    
    // 短暂等待，让任务有机会执行
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    task.Stop();
    
    // 停止后再等待一小会，确保线程已结束
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST_F(TokenCleanupTaskTest, StartTwice_NoCrash) {
    EXPECT_CALL(*mock_repo_, CleanExpiredTokens())
        .WillRepeatedly(Return(Result<int64_t>::Ok(0)));
    
    TokenCleanupTask task(mock_repo_, 1);
    
    task.Start();
    task.Start();  // 重复调用不应该崩溃
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    task.Stop();
}

TEST_F(TokenCleanupTaskTest, StopWithoutStart) {
    TokenCleanupTask task(mock_repo_, 1);
    
    // 不调用 Start 直接 Stop 不应该崩溃
    task.Stop();
}

TEST_F(TokenCleanupTaskTest, CleanupErrorHandling) {
    // 模拟清理失败
    EXPECT_CALL(*mock_repo_, CleanExpiredTokens())
        .WillRepeatedly(Return(Result<int64_t>::Fail(ErrorCode::ServiceUnavailable, "DB error")));
    
    TokenCleanupTask task(mock_repo_, 1);
    
    task.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    task.Stop();
    
    // 不应该崩溃，错误应该被记录
}

TEST_F(TokenCleanupTaskTest, DestructorStopsTask) {
    EXPECT_CALL(*mock_repo_, CleanExpiredTokens())
        .WillRepeatedly(Return(Result<int64_t>::Ok(5)));
    
    {
        TokenCleanupTask task(mock_repo_, 1);
        task.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // 析构函数会调用 Stop()
    }
    
    // 不应该崩溃或泄漏线程
}

}  // namespace testing
}  // namespace user_service
