#pragma once

#include <stdexcept>
#include <string>

namespace user_service {

// MySQL 异常基类（带错误码）
class MySQLException : public std::runtime_error {
public:
    MySQLException(int mysql_errno, const std::string& msg)
        : std::runtime_error(msg), errno_(mysql_errno)
    {}

    int mysql_errno() const { return errno_; }

    // 是否可重试
    bool IsRetryable() const {
        return errno_ == 1213 ||    // 死锁
               errno_ == 2002 ||    // socket 错误
               errno_ == 2003 ||    // 无法连接主机
               errno_ == 2006 ||    // 服务断开
               errno_ == 2013;      // 连接丢失
    }

private:
    int errno_;
};

// 唯一键冲突异常（继承自 MySQLException）
class MySQLDuplicateKeyException : public MySQLException {
public:
    MySQLDuplicateKeyException(int mysql_errno, const std::string& msg)
        : MySQLException(mysql_errno, msg), key_name_(ParseKeyName(msg))
    {}

    // 获取冲突的索引名（如 "username"）
    const std::string& key_name() const { return key_name_; }

private:
    std::string key_name_;

    // 解析错误信息中的索引名（index_name）
    // MySQL 5.7+ 格式固定: "... for key 'table.index_name'"
    // 如："Duplicate entry 'alice' for key 'test.name'"
    static std::string ParseKeyName(const std::string& msg) {
        auto dot = msg.rfind('.');
        auto end = msg.rfind('\'');
        
        if (dot == std::string::npos || end == std::string::npos || dot >= end) {
            return "";  // 格式异常
        }
        
        return msg.substr(dot + 1, end - dot - 1);
    }
};


// SQL 构建异常（参数不匹配等，非 MySQL 错误）
class MySQLBuildException : public std::logic_error {
public:
    explicit MySQLBuildException(const std::string& msg)
        : std::logic_error(msg)
    {}
};

// SQL 结果集使用错误(MySQLResult中，非MySQL错误)
class MySQLResultException : public std::runtime_error {
public:
    explicit MySQLResultException(const std::string& msg)
        : std::runtime_error(msg) {}
};


// 辅助函数：根据错误码抛出对应异常
inline void ThrowMySQLException(unsigned int err_code, const std::string& err_msg) {
    if (err_code == 1062) {  // ER_DUP_ENTRY - 唯一键冲突
        throw MySQLDuplicateKeyException(static_cast<int>(err_code), err_msg);
    }
    throw MySQLException(static_cast<int>(err_code), err_msg);
}

}  // namespace user_service
