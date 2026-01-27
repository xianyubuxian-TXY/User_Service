#pragma once

#include <stdexcept>
#include <string>
#include "mysql_error.h"

namespace user_service {

// ============================================================================
// MySQL 异常基类（带错误码）
// ============================================================================
class MySQLException : public std::runtime_error {
public:
    MySQLException(MySQLError code, const std::string& msg) 
        : std::runtime_error(msg), code_(code) {}
    
    MySQLError code() const { return code_; }
    
private:
    MySQLError code_;
};

// ============================================================================
// 具体异常类
// ============================================================================

class MySQLUnknowException: public MySQLException{
public:
    explicit MySQLUnknowException(const std::string& msg)
        : MySQLException(MySQLError::Unknown, msg) {}

};

// 连接异常
class MySQLConnectionException : public MySQLException {
public:
    explicit MySQLConnectionException(const std::string& msg)
        : MySQLException(MySQLError::ConnectionFailed, msg) {}
};

// 连接丢失
class MySQLServerLostException : public MySQLException {
public:
    explicit MySQLServerLostException(const std::string& msg)
        : MySQLException(MySQLError::ConnectionLost, msg) {}
};

// 连接池耗尽
class MySQLPoolExhaustedException : public MySQLException {
public:
    explicit MySQLPoolExhaustedException(const std::string& msg)
        : MySQLException(MySQLError::PoolExhausted, msg) {}
};

// 认证异常
class MySQLAuthException : public MySQLException {
public:
    explicit MySQLAuthException(const std::string& msg)
        : MySQLException(MySQLError::AuthFailed, msg) {}
};

// 死锁异常
class MySQLDeadlockException : public MySQLException {
public:
    explicit MySQLDeadlockException(const std::string& msg)
        : MySQLException(MySQLError::Deadlock, msg) {}
};

// 锁超时
class MySQLLockTimeoutException : public MySQLException {
public:
    explicit MySQLLockTimeoutException(const std::string& msg)
        : MySQLException(MySQLError::LockTimeout, msg) {}
};

// 重复键
class MySQLDuplicateException : public MySQLException {
public:
    explicit MySQLDuplicateException(const std::string& msg)
        : MySQLException(MySQLError::DuplicateEntry, msg) {}
};

// 外键约束
class MySQLForeignKeyException : public MySQLException {
public:
    explicit MySQLForeignKeyException(const std::string& msg)
        : MySQLException(MySQLError::ForeignKeyViolation, msg) {}
};

// 查询错误
class MySQLQueryException : public MySQLException {
public:
    explicit MySQLQueryException(const std::string& msg)
        : MySQLException(MySQLError::QueryFailed, msg) {}
};

// SQL 构建错误
class MySQLBuildException : public MySQLException {
public:
    explicit MySQLBuildException(const std::string& msg)
        : MySQLException(MySQLError::BuildError, msg) {}
};

}  // namespace user_service
