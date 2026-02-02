#include "token_repository.h"
#include "exception/mysql_exception.h"

namespace user_service {

// 数据库连接校验宏：校验连接有效性，无效则返回服务不可用结果
// Conn: 数据库连接指针  ResultType: 统一返回结果类型
#define CHECK_CONN(Conn, ResultType)                                            \
    if (!Conn->Valid()) {                                              \
        LOG_ERROR("mysql_conn is invalid");                                     \
        return ResultType::Fail(ErrorCode::ServiceUnavailable,                  \
                                GetErrorMessage(ErrorCode::ServiceUnavailable));\
    }

// ==================== 构造函数 ====================
/// 构造函数：注入MySQL连接池，用于获取数据库连接
/// @param pool MySQL连接池智能指针，全局单例复用
TokenRepository::TokenRepository(std::shared_ptr<MySQLPool> pool)
    : pool_(std::move(pool))
{}

// ==================== 新增操作 ====================
/// 保存刷新令牌哈希到数据库（用户会话表）
/// @param user_id 用户唯一标识
/// @param token_hash 刷新令牌的SHA256哈希值（非明文存储）
/// @param expires_in_seconds 令牌有效期（秒）
/// @return 统一结果对象：成功/失败（含错误码+信息）
Result<void> TokenRepository::SaveRefreshToken(int64_t user_id,
                                                const std::string& token_hash,
                                                int expires_in_seconds) {
    try {
        auto conn = pool_->CreateConnection(); // 从连接池获取连接
        CHECK_CONN(conn, Result<void>);        // 校验连接有效性

        // 计算令牌过期时间点，转换为MySQL标准时间字符串（%Y-%m-%d %H:%M:%S）
        auto expires_at = std::chrono::system_clock::now() 
                        + std::chrono::seconds(expires_in_seconds);
        std::string expires_at_str = FormatDatetime(expires_at);

        // 插入用户会话SQL：防注入，使用参数化查询
        std::string sql = 
            "INSERT INTO user_sessions (user_id, token_hash, expires_at) "
            "VALUES (?, ?, ?)";

        // 执行参数化插入
        conn->Execute(sql, {
            std::to_string(user_id),
            token_hash,
            expires_at_str
        });

        LOG_INFO("Token saved for user_id={}, expires_at={}", user_id, expires_at_str);
        return Result<void>::Ok(); // 保存成功

    } catch (const MySQLDuplicateKeyException& e) {
        // 令牌哈希冲突（理论上SHA256几乎不会发生，做容错处理）
        LOG_ERROR("Duplicate token hash: {}", e.what());
        return Result<void>::Fail(ErrorCode::Internal, "Token 保存失败");

    } catch (const std::exception& e) {
        // 通用异常：数据库操作失败、连接异常等
        LOG_ERROR("SaveRefreshToken failed: {}", e.what());
        return Result<void>::Fail(ErrorCode::ServiceUnavailable,
                                   GetErrorMessage(ErrorCode::ServiceUnavailable));
    }
}

// ==================== 查询操作 ====================
/// 根据令牌哈希查询用户会话信息
/// @param token_hash 刷新令牌的SHA256哈希值
/// @return 统一结果对象：成功返回TokenSession，失败返回错误信息
Result<TokenSession> TokenRepository::FindByTokenHash(const std::string& token_hash) {
    try {
        auto conn = pool_->CreateConnection();
        CHECK_CONN(conn, Result<TokenSession>);

        // 查询会话SQL：根据哈希精准查询
        std::string sql = 
            "SELECT id, user_id, token_hash, expires_at, created_at "
            "FROM user_sessions "
            "WHERE token_hash = ?";

        // 执行参数化查询
        auto res = conn->Query(sql, {token_hash});

        // 存在则解析行数据，返回会话信息
        if (res.Next()) {
            TokenSession session = ParseRow(res);
            LOG_DEBUG("Token found for user_id={}", session.user_id);
            return Result<TokenSession>::Ok(session);
        }

        // 令牌哈希不存在
        LOG_DEBUG("Token not found: hash={}", token_hash.substr(0, 8) + "...");
        return Result<TokenSession>::Fail(ErrorCode::TokenInvalid,
                                           GetErrorMessage(ErrorCode::TokenInvalid));

    } catch (const std::exception& e) {
        LOG_ERROR("FindByTokenHash failed: {}", e.what());
        return Result<TokenSession>::Fail(ErrorCode::ServiceUnavailable,
                                           GetErrorMessage(ErrorCode::ServiceUnavailable));
    }
}

/// 校验令牌哈希是否有效（存在且未过期）
/// @param token_hash 刷新令牌的SHA256哈希值
/// @return 统一结果对象：成功返回bool（true=有效/false=无效）
Result<bool> TokenRepository::IsTokenValid(const std::string& token_hash) {
    try {
        auto conn = pool_->CreateConnection();
        CHECK_CONN(conn, Result<bool>);

        // 校验SQL：存在且过期时间大于当前时间，LIMIT 1提升查询效率
        std::string sql = 
            "SELECT 1 FROM user_sessions "
            "WHERE token_hash = ? AND expires_at > NOW() "  // expires_at > NOW() ：未过期
            "LIMIT 1";

        auto res = conn->Query(sql, {token_hash});
        bool valid = res.Next(); // 有结果则有效，无则无效

        LOG_DEBUG("Token valid check: hash={}..., valid={}", 
                  token_hash.substr(0, 8), valid);
        return Result<bool>::Ok(valid);

    } catch (const std::exception& e) {
        LOG_ERROR("IsTokenValid failed: {}", e.what());
        return Result<bool>::Fail(ErrorCode::ServiceUnavailable,
                                   GetErrorMessage(ErrorCode::ServiceUnavailable));
    }
}

/// 统计用户的有效会话数（未过期的刷新令牌数）
/// @param user_id 用户唯一标识
/// @return 统一结果对象：成功返回有效会话数
Result<int64_t> TokenRepository::CountActiveSessionsByUserId(int64_t user_id) {
    try {
        auto conn = pool_->CreateConnection();
        CHECK_CONN(conn, Result<int64_t>);

        // 统计SQL：按用户ID查询未过期会话数
        std::string sql = 
            "SELECT COUNT(*) FROM user_sessions "
            "WHERE user_id = ? AND expires_at > NOW()";

        auto res = conn->Query(sql, {std::to_string(user_id)});

        int64_t count = 0;
        if (res.Next()) {
            count = res.GetInt(0).value_or(0); // 解析统计结果，默认0
        }

        LOG_DEBUG("Active sessions for user_id={}: {}", user_id, count);
        return Result<int64_t>::Ok(count);

    } catch (const std::exception& e) {
        LOG_ERROR("CountActiveSessionsByUserId failed: {}", e.what());
        return Result<int64_t>::Fail(ErrorCode::ServiceUnavailable,
                                      GetErrorMessage(ErrorCode::ServiceUnavailable));
    }
}

// ==================== 删除操作 ====================
/// 根据令牌哈希删除会话（令牌失效/登出时调用）
/// @param token_hash 刷新令牌的SHA256哈希值
/// @return 统一结果对象：成功/失败（幂等性：删除不存在的令牌也算成功）
Result<void> TokenRepository::DeleteByTokenHash(const std::string& token_hash) {
    try {
        auto conn = pool_->CreateConnection();
        CHECK_CONN(conn, Result<void>);

        // 删除SQL：根据哈希精准删除
        std::string sql = "DELETE FROM user_sessions WHERE token_hash = ?";
        auto affected = conn->Execute(sql, {token_hash}); // 受影响行数

        if (affected == 0) {
            LOG_DEBUG("Token not found for deletion: hash={}...", 
                      token_hash.substr(0, 8));
            // 幂等性设计：删除不存在的令牌，不返回错误
        } else {
            LOG_INFO("Token deleted: hash={}...", token_hash.substr(0, 8));
        }

        return Result<void>::Ok();

    } catch (const std::exception& e) {
        LOG_ERROR("DeleteByTokenHash failed: {}", e.what());
        return Result<void>::Fail(ErrorCode::ServiceUnavailable,
                                   GetErrorMessage(ErrorCode::ServiceUnavailable));
    }
}

/// 根据用户ID删除所有会话（用户登出所有设备/账号注销时调用）
/// @param user_id 用户唯一标识
/// @return 统一结果对象：成功/失败
Result<void> TokenRepository::DeleteByUserId(int64_t user_id) {
    try {
        auto conn = pool_->CreateConnection();
        CHECK_CONN(conn, Result<void>);

        // 删除SQL：删除用户所有会话
        std::string sql = "DELETE FROM user_sessions WHERE user_id = ?";
        auto affected = conn->Execute(sql, {std::to_string(user_id)});

        LOG_INFO("Deleted {} sessions for user_id={}", affected, user_id);
        return Result<void>::Ok();

    } catch (const std::exception& e) {
        LOG_ERROR("DeleteByUserId failed: {}", e.what());
        return Result<void>::Fail(ErrorCode::ServiceUnavailable,
                                   GetErrorMessage(ErrorCode::ServiceUnavailable));
    }
}

/// 清理数据库中所有过期的令牌会话（定时任务调用，如每天凌晨）
/// @return 统一结果对象：成功返回清理的过期令牌数
Result<int64_t> TokenRepository::CleanExpiredTokens() {
    try {
        auto conn = pool_->CreateConnection();
        CHECK_CONN(conn, Result<int64_t>);

        // 清理SQL：删除过期时间小于等于当前时间的会话
        std::string sql = "DELETE FROM user_sessions WHERE expires_at <= NOW()";
        auto affected = conn->Execute(sql, {});

        LOG_INFO("Cleaned {} expired tokens", affected);
        return Result<int64_t>::Ok(static_cast<int64_t>(affected));

    } catch (const std::exception& e) {
        LOG_ERROR("CleanExpiredTokens failed: {}", e.what());
        return Result<int64_t>::Fail(ErrorCode::ServiceUnavailable,
                                      GetErrorMessage(ErrorCode::ServiceUnavailable));
    }
}

// ==================== 私有工具方法 ====================
/// 解析MySQL查询结果行，转换为TokenSession对象
/// @param res MySQL查询结果对象
/// @return 封装后的TokenSession会话对象
TokenSession TokenRepository::ParseRow(MySQLResult& res) {
    TokenSession session;
    session.id         = res.GetInt("id").value_or(0);        // 会话ID，默认0
    session.user_id    = res.GetInt("user_id").value_or(0);   // 用户ID，默认0
    session.token_hash = res.GetString("token_hash").value_or(""); // 令牌哈希，默认空
    session.expires_at = res.GetString("expires_at").value_or(""); // 过期时间字符串
    session.created_at = res.GetString("created_at").value_or(""); // 创建时间字符串
    return session;
}

/// 将系统时间戳转换为MySQL标准datetime字符串（%Y-%m-%d %H:%M:%S）
/// @param tp 系统时间点（chrono）
/// @return 格式化后的时间字符串，适配MySQL datetime类型
std::string TokenRepository::FormatDatetime(std::chrono::system_clock::time_point tp) {
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_val{};
    
    // 线程安全的本地时间转换：Windows用localtime_s，POSIX系统用localtime_r
#ifdef _WIN32
    localtime_s(&tm_val, &time_t_val);
#else
    localtime_r(&time_t_val, &tm_val);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_val, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

/// 获取当前系统时间的MySQL标准datetime字符串
/// @return 格式化后的当前时间字符串
std::string TokenRepository::NowDatetime() {
    return FormatDatetime(std::chrono::system_clock::now());
}

#undef CHECK_CONN // 释放连接校验宏

} // namespace user_service