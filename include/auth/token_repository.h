#pragma once
#include "pool/connection_pool.h"
#include "db/mysql_result.h"
#include "db/mysql_connection.h"
#include "common/result.h"
#include "common/error_codes.h"
#include "common/logger.h"
#include "entity/token.h"

#include <memory>
#include <string>
#include <chrono>
#include <vector>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace user_service {

// ==================== Token 仓库类 ====================
class TokenRepository {
public:
    using MySQLPool=TemplateConnectionPool<MySQLConnection>;
    explicit TokenRepository(std::shared_ptr<MySQLPool> pool);

    virtual ~TokenRepository()=default;
    // ==================== 创建 ====================
    // 保存 Refresh Token（创建会话）
    virtual Result<void> SaveRefreshToken(int64_t user_id,
                                   const std::string& token_hash,
                                   int expires_in_seconds);

    // ==================== 查询 ====================
    // 根据 Token 哈希查找会话
    virtual Result<TokenSession> FindByTokenHash(const std::string& token_hash);

    // 检查 Token 是否有效（存在且未过期）
    virtual Result<bool> IsTokenValid(const std::string& token_hash);

    // 获取用户的活跃会话数量
    virtual Result<int64_t> CountActiveSessionsByUserId(int64_t user_id);

    // ==================== 删除 ====================
    // 删除指定 Token（单设备登出）
    virtual Result<void> DeleteByTokenHash(const std::string& token_hash);

    // 删除用户的所有 Token（强制登出所有设备）
    virtual Result<void> DeleteByUserId(int64_t user_id);

    // 清理过期 Token（返回清理数量）
    virtual Result<int64_t> CleanExpiredTokens();

private:
    std::shared_ptr<MySQLPool> pool_;

    // 解析数据库行
    TokenSession ParseRow(MySQLResult& res);

    // 格式化时间戳为 MySQL DATETIME 格式
    static std::string FormatDatetime(std::chrono::system_clock::time_point tp);

    // 获取当前时间的 MySQL DATETIME 格式
    static std::string NowDatetime();
};

} // namespace user_service
