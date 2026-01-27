#pragma once

#include <string>

namespace user_service {

// MySQL 层独立错误码（基础设施层）
enum class MySQLError {
    Ok = 0,
    
    // 连接相关
    ConnectionFailed,       // 连接失败
    ConnectionLost,         // 连接丢失
    PoolExhausted,          // 连接池耗尽
    
    // 认证/权限
    AuthFailed,             // 认证失败
    AccessDenied,           // 权限不足
    
    // 锁相关
    Deadlock,               // 死锁
    LockTimeout,            // 锁等待超时
    
    // 约束相关
    DuplicateEntry,         // 主键/唯一键冲突
    ForeignKeyViolation,    // 外键约束失败
    
    // 查询相关
    SyntaxError,            // SQL 语法错误
    QueryFailed,            // 查询失败（通用）
    BuildError,             // SQL 构建错误
    
    // 其他
    Unknown,
};

inline const char* MySQLErrorToString(MySQLError err) {
    switch (err) {
        case MySQLError::Ok:                  return "Ok";
        case MySQLError::ConnectionFailed:    return "ConnectionFailed";
        case MySQLError::ConnectionLost:      return "ConnectionLost";
        case MySQLError::PoolExhausted:       return "PoolExhausted";
        case MySQLError::AuthFailed:          return "AuthFailed";
        case MySQLError::AccessDenied:        return "AccessDenied";
        case MySQLError::Deadlock:            return "Deadlock";
        case MySQLError::LockTimeout:         return "LockTimeout";
        case MySQLError::DuplicateEntry:      return "DuplicateEntry";
        case MySQLError::ForeignKeyViolation: return "ForeignKeyViolation";
        case MySQLError::SyntaxError:         return "SyntaxError";
        case MySQLError::QueryFailed:         return "QueryFailed";
        case MySQLError::BuildError:          return "BuildError";
        case MySQLError::Unknown:             return "Unknown";
        default:                              return "Unknown";
    }
}

}  // namespace user_service
