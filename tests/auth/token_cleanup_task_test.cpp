#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "auth/token_cleanup_task.h"
#include "mock_auth_deps.h"

using namespace user_service;
using namespace user_service::testing;
using ::testing::_;
using ::testing::Return;
using ::testing::AtLeast;

class TokenCleanupTaskTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_token_repo_ = std::make_shared<MockTokenRepository>();
    }
    
    std::shared_ptr<MockTokenRepository> mock_token_repo_;
};

TEST_F(TokenCleanupTaskTest, StartAndStop) {
    // 清理任务会被调用至少一次
    EXPECT_CALL(*mock_token_repo_, CleanExpiredTokens())
        .WillRepeatedly(Return(Result<int64_t>::Ok(5)));
    
    // 创建任务，间隔 1 分钟（但我们会很快停止）
    TokenCleanupTask task(mock_token_repo_, 1);
    
    task.Start();
    
    // 等待任务执行一次
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    task.Stop();
    
    // 验证没有崩溃
    SUCCEED();
}

TEST_F(TokenCleanupTaskTest, MultipleStartStop) {
    EXPECT_CALL(*mock_token_repo_, CleanExpiredTokens())
        .WillRepeatedly(Return(Result<int64_t>::Ok(0)));
    
    TokenCleanupTask task(mock_token_repo_, 1);
    
    // 多次启动停止不应崩溃
    task.Start();
    task.Start();  // 重复启动应被忽略
    task.Stop();
    task.Stop();   // 重复停止应被忽略
    task.Start();
    task.Stop();
    
    SUCCEED();
}

TEST_F(TokenCleanupTaskTest, CleanupError_ContinuesRunning) {
    // 模拟清理失败
    EXPECT_CALL(*mock_token_repo_, CleanExpiredTokens())
        .WillRepeatedly(Return(Result<int64_t>::Fail(ErrorCode::ServiceUnavailable, "DB error")));
    
    TokenCleanupTask task(mock_token_repo_, 1);
    
    task.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    task.Stop();
    
    // 任务应该继续运行，不因错误而崩溃
    SUCCEED();
}

TEST_F(TokenCleanupTaskTest, DestructorStopsTask) {
    EXPECT_CALL(*mock_token_repo_, CleanExpiredTokens())
        .WillRepeatedly(Return(Result<int64_t>::Ok(0)));
    
    {
        TokenCleanupTask task(mock_token_repo_, 1);
        task.Start();
        // 作用域结束，析构函数应该停止任务
    }
    
    // 没有崩溃或死锁
    SUCCEED();
}
