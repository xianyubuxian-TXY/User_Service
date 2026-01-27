#include "db/mysql_connection.h"
#include "exception/exception.h"

#include <chrono>
#include <vector>
#include <thread>

namespace user_service {

MySQLConnection::MySQLConnection(const MySQLConfig& config) {
    try {
        // 初始化并设置选项
        InitAndSetOptions(config);
    } catch (...) {
        if (mysql_) {
            mysql_close(mysql_);
            mysql_ = nullptr;
        }
        throw;
    }

    if (!mysql_real_connect(mysql_,
                            config.host.c_str(),
                            config.username.c_str(),
                            config.password.c_str(),
                            config.database.c_str(),
                            config.port,
                            nullptr,
                            0)) {
        // 一定要先保存错误信息，否则释放mysql_后就无法获取了
        unsigned int err_code = mysql_errno(mysql_);
        std::string err_msg = mysql_error(mysql_);

        // 连接失败
        mysql_close(mysql_);
        mysql_ = nullptr;

        bool should_retry = config.auto_reconnect.value_or(false) &&
                            config.max_retries > 0 &&
                            IsRetryableError(err_code);

        if (!should_retry) {
            ThrowByErrorCode(err_code, err_msg);
        }

        // 重连
        ConnectWithRetry(config);
    }
}

MySQLConnection::~MySQLConnection() {
    if (mysql_) {
        mysql_close(mysql_);
        mysql_ = nullptr;
    }
}

MySQLResult MySQLConnection::Query(const std::string& sql, std::initializer_list<Param> params) {
    std::string new_sql = BuildSQL(sql, params);

    // 执行查询
    if (mysql_query(mysql_, new_sql.c_str()) != 0) {
        unsigned int err_code = mysql_errno(mysql_);
        std::string err_msg = mysql_error(mysql_);
        ThrowByErrorCode(err_code, err_msg);
    }

    // 获取结果集
    MYSQL_RES* res = mysql_store_result(mysql_);

    // 注意：SELECT 语句执行出错时， res为nullptr（空集时不会为nullptr）
    // 但如果是非 SELECT 语句调用了 Query，res 也会是 nullptr
    if (res == nullptr && mysql_field_count(mysql_) > 0) {
        // 应该有结果但没拿到，说明出错了
        unsigned int err_code = mysql_errno(mysql_);
        std::string err_msg = mysql_error(mysql_);
        ThrowByErrorCode(err_code, err_msg);
    }

    return MySQLResult(res);
}

// Insert、Update、Delete
uint64_t MySQLConnection::Execute(const std::string& sql, std::initializer_list<Param> params) {
    std::string new_sql = BuildSQL(sql, params);

    if (mysql_query(mysql_, new_sql.c_str()) != 0) {
        unsigned int err_code = mysql_errno(mysql_);
        std::string err_msg = mysql_error(mysql_);
        ThrowByErrorCode(err_code, err_msg);
    }

    return mysql_affected_rows(mysql_);
}

// 获取Insert后的id
uint64_t MySQLConnection::LastInsertId() {
    return mysql_insert_id(mysql_);
}

void MySQLConnection::InitAndSetOptions(const MySQLConfig& config) {
    // 初始化连接句柄
    mysql_ = mysql_init(nullptr);
    if (!mysql_) {
        // 注意：不可以throw MySQLException(mysql_errno(mysql_),mysql_error(mysql_));
        // 因为mysql_ 是 nullptr，mysql_errno(nullptr) 行为未定义
        throw MySQLConnectionException("mysql_init failed: out of memory");
    }

    // 设置参数
    if (config.connection_timeout_ms.has_value()) {
        unsigned int connection_timeout = config.connection_timeout_ms.value() / 1000; // ms—>s
        mysql_options(mysql_, MYSQL_OPT_CONNECT_TIMEOUT, &connection_timeout);
    }
    if (config.read_timeout_ms.has_value()) {
        unsigned int read_timeout = config.read_timeout_ms.value() / 1000;
        mysql_options(mysql_, MYSQL_OPT_READ_TIMEOUT, &read_timeout);
    }
    if (config.write_timeout_ms.has_value()) {
        unsigned int write_timeout = config.write_timeout_ms.value() / 1000;
        mysql_options(mysql_, MYSQL_OPT_WRITE_TIMEOUT, &write_timeout);
    }
    if (config.auto_reconnect.has_value()) {
        /* MySQL C API 底层不直接支持 bool 类型，而是用 unsigned int（无符号整数）来表示布尔值，
           0 代表 false，非 0 代表 true。*/
        unsigned int mybool = config.auto_reconnect.value() ? 1 : 0;
        mysql_options(mysql_, MYSQL_OPT_RECONNECT, &mybool);
    }

    mysql_options(mysql_, MYSQL_SET_CHARSET_NAME, config.charset.c_str());
}

// 判断错误是否值得重试
bool MySQLConnection::IsRetryableError(unsigned int err_code) {
    switch (err_code) {
        // 可重试的错误（网络/临时性问题）
        case 2002:  // CR_CONNECTION_ERROR - socket 错误
        case 2003:  // CR_CONN_HOST_ERROR - 无法连接主机
        case 2006:  // CR_SERVER_GONE_ERROR - 服务器断开
        case 2013:  // CR_SERVER_LOST - 连接丢失
            return true;

        // 不可重试的错误（配置/认证问题）
        case 1045:  // ER_ACCESS_DENIED_ERROR - 认证失败
        case 1044:  // ER_DBACCESS_DENIED_ERROR - 无权限
        case 1049:  // ER_BAD_DB_ERROR - 数据库不存在
        case 2005:  // CR_UNKNOWN_HOST - 未知主机
        default:
            return false;
    }
}

void MySQLConnection::ConnectWithRetry(const MySQLConfig& config) {
    unsigned int err_code = 0;
    std::string err_msg;

    for (unsigned int attempt = 1; attempt <= config.max_retries; ++attempt) {
        try {
            InitAndSetOptions(config);

            // 尝试连接
            if (mysql_real_connect(mysql_,
                                   config.host.c_str(),
                                   config.username.c_str(),
                                   config.password.c_str(),
                                   config.database.c_str(),
                                   config.port,
                                   nullptr,
                                   0)) {
                // 连接成功
                return;
            }

            // 连接失败
            err_code = mysql_errno(mysql_);
            err_msg = mysql_error(mysql_);

            // 释放当前句柄
            mysql_close(mysql_);
            mysql_ = nullptr;

            if (!IsRetryableError(err_code)) {
                break;  // 不可重试的错误，直接退出
            }

            // 尝试重连（等待一段时间）
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config.retry_interval_ms));

        } catch (...) {
            // 捕捉InitAndSetOptions等抛出的异常
            if (mysql_) {
                mysql_close(mysql_);
                mysql_ = nullptr;
            }
            throw;  // 继续向上传递
        }
    }

    // 所有重试都失败
    ThrowByErrorCode(err_code,
        "Failed after " + std::to_string(config.max_retries) +
        " retries: " + err_msg);
}

// 转义方法（只能用于转义"参数值"）（防SQL注入的简单处理）
std::string MySQLConnection::Escape(const std::string& str) {
    if (!mysql_) {
        throw MySQLConnectionException("Connection not established");
    }

    // 最坏情况"都需要转义"：所以将空间设置为 2*str.size()+1（官方推荐长度）
    // 使用vector更安全，也避免手动管理空间
    std::vector<char> buffer(str.size() * 2 + 1);
    unsigned long len = mysql_real_escape_string(mysql_, buffer.data(), str.c_str(),
                                                 static_cast<unsigned long>(str.size()));

    return std::string(buffer.data(), len);
}

/// @brief SQL语句包装器： "？为占位符 ——>sql语句中不可以包含非占位符 ？"
/// @param sql sql语句，用"?"占位
/// @param params 参数
/// @return 完整的sql语句
/// @note 对于不在Param中的类型，可以先转为string类型，在作为参数传入
std::string MySQLConnection::BuildSQL(const std::string& sql, std::initializer_list<Param> params) {
    // 预分配内存：初始容量为SQL长度 + 参数平均长度*参数数量（减少内存重分配）
    std::string result;
    result.reserve(sql.size() + params.size() * 32);

    auto param = params.begin();
    // 解析sql中的'?'占位符，并用params中的参数填入
    for (const char& c : sql) {
        if (c == '?') {
            if (param == params.end()) {
                // 参数不足
                throw MySQLBuildException("Not enough parameters for SQL placeholders");
            }

            std::visit([&](auto&& val) {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, nullptr_t>) {
                    result += "NULL";
                } else if constexpr (std::is_same_v<T, std::string>) {
                    // 字符串要加"转义单引号"
                    result += '\'';
                    result += Escape(val);  // 进行转义：防sql注入
                    result += '\'';
                } else if constexpr (std::is_same_v<T, bool>) {
                    result += val ? "1" : "0";
                } else {
                    result += std::to_string(val);
                }
            }, *param);

            ++param;  // 参数后移
        } else {
            // 普通字符
            result.push_back(c);
        }
    }

    if (param != params.end()) {
        // 参数过多
        throw MySQLBuildException("Too many parameters for SQL placeholders");
    }

    return result;
}

void MySQLConnection::ThrowByErrorCode(unsigned int code, const std::string& msg) {
    switch (code) {
        // ========== 连接类 ==========
        case 2002:  // CR_CONNECTION_ERROR - Socket 错误
        case 2003:  // CR_CONN_HOST_ERROR - 无法连接主机
        case 2005:  // CR_UNKNOWN_HOST - 未知主机
            throw MySQLConnectionException(msg);

        case 2006:  // CR_SERVER_GONE_ERROR - 服务器断开
        case 2013:  // CR_SERVER_LOST - 连接丢失
        case 2055:  // CR_SERVER_LOST_EXTENDED
            throw MySQLServerLostException(msg);

        // ========== 认证/权限类（致命） ==========
        case 1044:  // ER_DBACCESS_DENIED_ERROR - 无数据库权限
        case 1045:  // ER_ACCESS_DENIED_ERROR - 认证失败
        case 1049:  // ER_BAD_DB_ERROR - 数据库不存在
        case 1142:  // ER_TABLEACCESS_DENIED_ERROR
        case 1143:  // ER_COLUMNACCESS_DENIED_ERROR
            throw MySQLAuthException(msg);

        // ========== 锁相关（可重试） ==========
        case 1213:  // ER_LOCK_DEADLOCK - 死锁
            throw MySQLDeadlockException(msg);

        case 1205:  // ER_LOCK_WAIT_TIMEOUT - 锁超时
            throw MySQLLockTimeoutException(msg);

        // ========== 约束类 ==========
        case 1062:  // ER_DUP_ENTRY - 唯一键重复
            throw MySQLDuplicateException(msg);

        case 1451:  // ER_ROW_IS_REFERENCED_2 - 外键约束（删除）
        case 1452:  // ER_NO_REFERENCED_ROW_2 - 外键约束（插入）
            throw MySQLForeignKeyException(msg);

        // ========== 查询类 ==========
        case 1064:  // ER_PARSE_ERROR - SQL 语法错误
        case 1054:  // ER_BAD_FIELD_ERROR - 未知列
        case 1146:  // ER_NO_SUCH_TABLE - 表不存在
        case 1109:  // ER_UNKNOWN_TABLE
        case 1048:  // ER_BAD_NULL_ERROR - NOT NULL 约束
        case 1406:  // ER_DATA_TOO_LONG - 数据过长
            throw MySQLQueryException(msg);

        // ========== 兜底 ==========
        default:
            throw MySQLUnknowException("[" + std::to_string(code) + "] " + msg);
    }
}

} // namespace user
