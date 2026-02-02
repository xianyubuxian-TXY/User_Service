// src/auth/token_cleanup_task.cpp

#include "auth/token_cleanup_task.h"

namespace user_service {

TokenCleanupTask::TokenCleanupTask(std::shared_ptr<TokenRepository> token_repo, int interval_minutes)
    : token_repo_(std::move(token_repo))
    , interval_(interval_minutes)
    , running_(false) {
}

TokenCleanupTask::~TokenCleanupTask() {
    Stop();  // 确保析构时停止线程
}

void TokenCleanupTask::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 关键修复：如果已经在运行，直接返回
    if (running_.load()) {
        LOG_INFO("TokenCleanupTask already running, ignoring Start()");
        return;
    }
    
    // 关键修复：如果之前的线程还存在，先等待它结束
    if (thread_.joinable()) {
        thread_.join();
    }
    
    running_.store(true);
    
    thread_ = std::thread([this] {
        LOG_INFO("TokenCleanupTask started, interval = {} minutes", interval_);
        
        while (running_.load()) {
            // 清理过期 token
            try {
                auto result = token_repo_->CleanExpiredTokens();
                if (result.Success()) {
                    LOG_INFO("Token cleanup: removed {} expired tokens", *result.data);
                } else {
                    LOG_ERROR("Token cleanup failed: {}", result.message);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Token cleanup exception: {}", e.what());
            }
            
            // 等待下次执行（分段睡眠以便快速响应停止请求）
            for (int i = 0; i < interval_ * 60 && running_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        
        LOG_INFO("TokenCleanupTask stopped");
    });
}

void TokenCleanupTask::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 设置停止标志
    running_.store(false);
    
    // 等待线程结束
    if (thread_.joinable()) {
        thread_.join();
    }
}

}  // namespace user_service
