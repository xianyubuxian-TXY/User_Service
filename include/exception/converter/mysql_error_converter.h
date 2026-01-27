// exception/mysql_error_converter.h
#pragma once

#include "common/logger.h"
#include "common/error_codes.h"
#include "exception/client_exception.h"
#include "db/mysql_error.h"
#include "exception/mysql_exception.h"
#include "db/mysql_connection.h"

namespace user_service {

// ============================================================================
// MySQLError → ErrorCode 转换
// ============================================================================
inline ErrorCode ToErrorCode(MySQLError err) {
    switch (err) {
        case MySQLError::Ok:
            return ErrorCode::Ok;
            
        // 连接相关 → 服务不可用
        case MySQLError::ConnectionFailed:
        case MySQLError::ConnectionLost:
        case MySQLError::PoolExhausted:
            return ErrorCode::ServiceUnavailable;
            
        // 认证/权限 → 内部错误（配置问题）
        case MySQLError::AuthFailed:
        case MySQLError::AccessDenied:
            return ErrorCode::Internal;
            
        // 死锁/锁超时 → 服务繁忙
        case MySQLError::Deadlock:
        case MySQLError::LockTimeout:
            return ErrorCode::ServiceUnavailable;
            
        // 重复键 → 已存在（DAO 层可覆盖为更精确的错误码）
        case MySQLError::DuplicateEntry:
            return ErrorCode::UserAlreadyExists;
            
        // 外键/查询错误 → 内部错误
        case MySQLError::ForeignKeyViolation:
        case MySQLError::SyntaxError:
        case MySQLError::QueryFailed:
        case MySQLError::BuildError:
            return ErrorCode::Internal;
            
        default:
            return ErrorCode::Internal;
    }
}

// ============================================================================
// MySQLException → ClientException 转换
// 原则：日志记详情，给客户端只返回"模糊信息"
// ============================================================================
[[noreturn]] inline void RethrowAsClientException(const MySQLException& e,
                                                   const std::string& operation = "") {
    MySQLError err = e.code();
    
    // 根据错误类型决定日志级别
    switch (err) {
        // 严重问题：需要告警
        case MySQLError::AuthFailed:
        case MySQLError::AccessDenied:
            LOG_ERROR("[MySQL] CRITICAL %s: code=%s, detail=%s",
                      operation.c_str(), MySQLErrorToString(err), e.what());
            break;
            
        // 连接问题：需要关注
        case MySQLError::ConnectionFailed:
        case MySQLError::ConnectionLost:
        case MySQLError::PoolExhausted:
            LOG_ERROR("[MySQL] %s: code=%s, detail=%s",
                      operation.c_str(), MySQLErrorToString(err), e.what());
            break;
            
        // 锁竞争：警告级别
        case MySQLError::Deadlock:
        case MySQLError::LockTimeout:
            LOG_WARN("[MySQL] %s: code=%s, detail=%s",
                     operation.c_str(), MySQLErrorToString(err), e.what());
            break;
            
        // 业务正常情况（重复键）
        case MySQLError::DuplicateEntry:
            LOG_INFO("[MySQL] %s: code=%s, detail=%s",
                     operation.c_str(), MySQLErrorToString(err), e.what());
            break;
            
        // 编程错误
        default:
            LOG_ERROR("[MySQL] %s: code=%s, detail=%s",
                      operation.c_str(), MySQLErrorToString(err), e.what());
            break;
    }
    
    // 转换为客户端异常（不暴露细节）
    throw ClientException(ToErrorCode(err));
}

// 重载：std::exception 版本（兜底）
[[noreturn]] inline void RethrowAsClientException(const std::exception& e,
                                                   const std::string& operation = "") {
    // 尝试转换为 MySQLException
    if (const auto* mysql_ex = dynamic_cast<const MySQLException*>(&e)) {
        RethrowAsClientException(*mysql_ex, operation);
    }
    
    // 非 MySQLException：未知错误
    LOG_ERROR("[MySQL] %s unexpected: %s", operation.c_str(), e.what());
    throw ClientException(ErrorCode::Internal);
}

// ============================================================================
// 连接检查辅助
// ============================================================================
inline void EnsureValidConnection(const MySQLConnection* conn) {
    if (!conn || !conn->Valid()) {
        LOG_ERROR("[MySQL] Connection invalid");
        throw ClientException(ErrorCode::ServiceUnavailable);
    }
}

}  // namespace user_service
