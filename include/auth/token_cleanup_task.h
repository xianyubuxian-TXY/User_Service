// include/auth/token_cleanup_task.h

#pragma once

#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include "common/logger.h"
#include "auth/token_repository.h"

namespace user_service {

class TokenCleanupTask {
public:
    TokenCleanupTask(std::shared_ptr<TokenRepository> token_repo, int interval_minutes = 60);
    ~TokenCleanupTask();  // 添加析构函数
    
    // 禁止拷贝和移动
    TokenCleanupTask(const TokenCleanupTask&) = delete;
    TokenCleanupTask& operator=(const TokenCleanupTask&) = delete;
    TokenCleanupTask(TokenCleanupTask&&) = delete;
    TokenCleanupTask& operator=(TokenCleanupTask&&) = delete;
    
    void Start();
    void Stop();

private:
    std::shared_ptr<TokenRepository> token_repo_;
    int interval_;
    std::atomic<bool> running_;
    std::thread thread_;
    std::mutex mutex_;  // 保护 Start/Stop 的并发调用
};

}  // namespace user_service
