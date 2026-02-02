#include "db/mysql_connection.h"
#include "exception/exception.h"

#include <chrono>
#include <vector>
#include <thread>

namespace user_service {

// ==================== 构造与析构 ====================

MySQLConnection::MySQLConnection(const MySQLConfig& config) {
    /*
     * 连接流程：
     * 1. 初始化句柄并设置选项
     * 2. 尝试首次连接
     * 3. 失败且可重试 → 进入重试逻辑
     * 4. 失败且不可重试 → 直接抛异常
     */
    try {
        InitAndSetOptions(config);
    } catch (...) {
        // 确保异常时资源被释放（RAII 补充）
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
                            nullptr,    // unix_socket: 使用 TCP 连接时传 nullptr
                            0)) {       // client_flag: 默认选项
        // 重要：必须在 mysql_close 之前保存错误信息！
        // mysql_close 后 mysql_ 指向的内存被释放，再调用 mysql_error 是未定义行为
        unsigned int err_code = mysql_errno(mysql_);
        std::string err_msg = mysql_error(mysql_);

        mysql_close(mysql_);
        mysql_ = nullptr;

        // 判断是否需要重试
        MySQLException ex(err_code, err_msg);
        bool should_retry = config.auto_reconnect.value_or(false) &&
                            config.max_retries > 0 &&
                            ex.IsRetryable();

        if (!should_retry) {
            throw ex;
        }

        // 进入重试逻辑
        ConnectWithRetry(config);
    }
}

MySQLConnection::~MySQLConnection() {
    if (mysql_) {
        mysql_close(mysql_);
        mysql_ = nullptr;
    }
}

// ==================== 查询与执行 ====================

MySQLResult MySQLConnection::Query(const std::string& sql, std::initializer_list<Param> params) {
    std::string new_sql = BuildSQL(sql, params.begin(),params.end());

    if (mysql_query(mysql_, new_sql.c_str()) != 0) {
        unsigned int err_code = mysql_errno(mysql_);
        std::string err_msg = mysql_error(mysql_);
        ThrowMySQLException(err_code, err_msg);
    }

    // mysql_store_result: 将整个结果集拉取到客户端内存
    // 优点：可随机访问、可获取总行数
    // 缺点：大结果集占用内存多（大结果集应使用 mysql_use_result 流式获取）
    MYSQL_RES* res = mysql_store_result(mysql_);

    /*
     * res == nullptr 的三种情况：
     * 1. SELECT 返回空集 → field_count > 0，这是正常的空结果
     * 2. 非 SELECT 语句（如 UPDATE）→ field_count == 0，正常
     * 3. 发生错误 → field_count > 0 但 res 为空，需要抛异常
     * 
     * 因此判断条件是：res == nullptr && field_count > 0
     */
    if (res == nullptr && mysql_field_count(mysql_) > 0) {
        unsigned int err_code = mysql_errno(mysql_);
        std::string err_msg = mysql_error(mysql_);
        ThrowMySQLException(err_code, err_msg);
    }

    return MySQLResult(res);
}

MySQLResult MySQLConnection::Query(const std::string& sql, const std::vector<Param>& params) {
    std::string new_sql = BuildSQL(sql, params.begin(),params.end());

    if (mysql_query(mysql_, new_sql.c_str()) != 0) {
        unsigned int err_code = mysql_errno(mysql_);
        std::string err_msg = mysql_error(mysql_);
        ThrowMySQLException(err_code, err_msg);
    }

    // mysql_store_result: 将整个结果集拉取到客户端内存
    // 优点：可随机访问、可获取总行数
    // 缺点：大结果集占用内存多（大结果集应使用 mysql_use_result 流式获取）
    MYSQL_RES* res = mysql_store_result(mysql_);

    /*
     * res == nullptr 的三种情况：
     * 1. SELECT 返回空集 → field_count > 0，这是正常的空结果
     * 2. 非 SELECT 语句（如 UPDATE）→ field_count == 0，正常
     * 3. 发生错误 → field_count > 0 但 res 为空，需要抛异常
     * 
     * 因此判断条件是：res == nullptr && field_count > 0
     */
    if (res == nullptr && mysql_field_count(mysql_) > 0) {
        unsigned int err_code = mysql_errno(mysql_);
        std::string err_msg = mysql_error(mysql_);
        ThrowMySQLException(err_code, err_msg);
    }

    return MySQLResult(res);
}

uint64_t MySQLConnection::Execute(const std::string& sql, std::initializer_list<Param> params) {
    std::string new_sql = BuildSQL(sql, params.begin(),params.end());

    if (mysql_query(mysql_, new_sql.c_str()) != 0) {
        unsigned int err_code = mysql_errno(mysql_);
        std::string err_msg = mysql_error(mysql_);
        // INSERT 可能触发唯一键冲突（错误码 1062）
        // ThrowMySQLException 会根据错误码自动选择异常类型
        ThrowMySQLException(err_code, err_msg);
    }

    // 返回受影响的行数
    // 注意：UPDATE 即使值没变化也可能返回 0（取决于 CLIENT_FOUND_ROWS 标志）
    return mysql_affected_rows(mysql_);
}

uint64_t MySQLConnection::Execute(const std::string& sql, const std::vector<Param>& params) {
    std::string new_sql = BuildSQL(sql, params.begin(),params.end());

    if (mysql_query(mysql_, new_sql.c_str()) != 0) {
        unsigned int err_code = mysql_errno(mysql_);
        std::string err_msg = mysql_error(mysql_);
        // INSERT 可能触发唯一键冲突（错误码 1062）
        // ThrowMySQLException 会根据错误码自动选择异常类型
        ThrowMySQLException(err_code, err_msg);
    }

    // 返回受影响的行数
    // 注意：UPDATE 即使值没变化也可能返回 0（取决于 CLIENT_FOUND_ROWS 标志）
    return mysql_affected_rows(mysql_);
}

uint64_t MySQLConnection::LastInsertId() {
    // 返回最近一次 INSERT 生成的 AUTO_INCREMENT 值
    // 注意：必须在同一连接上、INSERT 之后立即调用
    return mysql_insert_id(mysql_);
}

// ==================== 初始化与连接 ====================

void MySQLConnection::InitAndSetOptions(const MySQLConfig& config) {
    mysql_ = mysql_init(nullptr);
    if (!mysql_) {
        // mysql_init 失败通常是内存不足
        // 此时 mysql_ 为空，不能调用 mysql_errno/mysql_error
        throw MySQLException(0, "mysql_init failed: out of memory");
    }

    // 设置超时参数（单位转换：ms → s，MySQL C API 只支持秒级精度）
    if (config.connection_timeout_ms.has_value()) {
        unsigned int timeout_sec = config.connection_timeout_ms.value() / 1000;
        mysql_options(mysql_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout_sec);
    }
    if (config.read_timeout_ms.has_value()) {
        unsigned int timeout_sec = config.read_timeout_ms.value() / 1000;
        mysql_options(mysql_, MYSQL_OPT_READ_TIMEOUT, &timeout_sec);
    }
    if (config.write_timeout_ms.has_value()) {
        unsigned int timeout_sec = config.write_timeout_ms.value() / 1000;
        mysql_options(mysql_, MYSQL_OPT_WRITE_TIMEOUT, &timeout_sec);
    }
    
    // 自动重连设置
    // 注意：MySQL C API 用 unsigned int 表示 bool（0=false, 非0=true）
    if (config.auto_reconnect.has_value()) {
        unsigned int mybool = config.auto_reconnect.value() ? 1 : 0;
        mysql_options(mysql_, MYSQL_OPT_RECONNECT, &mybool);
    }

    // 字符集设置（必须在连接前设置）
    mysql_options(mysql_, MYSQL_SET_CHARSET_NAME, config.charset.c_str());
}

void MySQLConnection::ConnectWithRetry(const MySQLConfig& config) {
    unsigned int err_code = 0;
    std::string err_msg;

    for (unsigned int attempt = 1; attempt <= config.max_retries; ++attempt) {
        try {
            // 每次重试都需要重新初始化（上一次的句柄已被关闭）
            InitAndSetOptions(config);

            if (mysql_real_connect(mysql_,
                                   config.host.c_str(),
                                   config.username.c_str(),
                                   config.password.c_str(),
                                   config.database.c_str(),
                                   config.port,
                                   nullptr,
                                   0)) {
                return;  // 连接成功，直接返回
            }

            // 保存错误信息（同样要在 close 之前）
            err_code = mysql_errno(mysql_);
            err_msg = mysql_error(mysql_);

            mysql_close(mysql_);
            mysql_ = nullptr;

            // 判断是否值得继续重试
            if (!MySQLException(err_code, err_msg).IsRetryable()) {
                break;  // 不可重试的错误（如认证失败），立即退出
            }

            // 等待后重试（避免频繁请求加重服务器负担）
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config.retry_interval_ms));

        } catch (...) {
            if (mysql_) {
                mysql_close(mysql_);
                mysql_ = nullptr;
            }
            throw;
        }
    }

    // 重试耗尽，抛出最后一次的错误
    throw MySQLException(err_code,
        "Failed after " + std::to_string(config.max_retries) +
        " retries: " + err_msg);
}

// ==================== SQL 构建辅助 ====================

std::string MySQLConnection::Escape(const std::string& str) {
    if (!mysql_) {
        throw MySQLException(0, "Connection not established");
    }

    // mysql_real_escape_string 官方建议：缓冲区大小 = 2 * 原长度 + 1
    // 最坏情况：每个字符都需要转义（如 ' → \'）
    std::vector<char> buffer(str.size() * 2 + 1);
    
    // 返回值是转义后的实际长度
    unsigned long len = mysql_real_escape_string(
        mysql_, 
        buffer.data(), 
        str.c_str(),
        static_cast<unsigned long>(str.size())
    );

    return std::string(buffer.data(), len);
}

// std::string MySQLConnection::BuildSQL(const std::string& sql, std::initializer_list<Param> params) {
//     // 预分配内存，减少扩容次数（经验值：每个参数平均 32 字符）
//     std::string result;
//     result.reserve(sql.size() + params.size() * 32);

//     auto param = params.begin();
    
//     for (const char& c : sql) {
//         if (c == '?') {
//             if (param == params.end()) {
//                 throw MySQLBuildException("Not enough parameters for SQL placeholders");
//             }

//             // std::visit + if constexpr：编译期类型分发
//             std::visit([&](auto&& val) {
//                 using T = std::decay_t<decltype(val)>;
                
//                 if constexpr (std::is_same_v<T, nullptr_t>) {
//                     result += "NULL";
//                 } else if constexpr (std::is_same_v<T, std::string>) {
//                     // 字符串：转义 + 加引号（防 SQL 注入的核心）
//                     result += '\'';
//                     result += Escape(val);
//                     result += '\'';
//                 } else if constexpr (std::is_same_v<T, bool>) {
//                     // MySQL 没有原生 bool，用 0/1 表示
//                     result += val ? "1" : "0";
//                 } else {
//                     // 数值类型：直接转字符串（无注入风险）
//                     result += std::to_string(val);
//                 }
//             }, *param);

//             ++param;
//         } else {
//             result.push_back(c);
//         }
//     }

//     if (param != params.end()) {
//         throw MySQLBuildException("Too many parameters for SQL placeholders");
//     }

//     return result;
// }

// 模板实现（放在 cpp 文件中，因为只在本文件内使用）
template<typename Iter>
std::string MySQLConnection::BuildSQL(const std::string& sql, Iter begin, Iter end) {
    std::string result;
    result.reserve(sql.size() + std::distance(begin, end) * 32);

    auto param = begin;
    
    for (const char& c : sql) {
        if (c == '?') {
            if (param == end) {
                throw MySQLBuildException("Not enough parameters for SQL placeholders");
            }

            std::visit([&](auto&& val) {
                using T = std::decay_t<decltype(val)>;
                
                if constexpr (std::is_same_v<T, nullptr_t>) {
                    result += "NULL";
                } else if constexpr (std::is_same_v<T, std::string>) {
                    result += '\'';
                    result += Escape(val);
                    result += '\'';
                } else if constexpr (std::is_same_v<T, bool>) {
                    result += val ? "1" : "0";
                } else {
                    result += std::to_string(val);
                }
            }, *param);

            ++param;
        } else {
            result.push_back(c);
        }
    }

    if (param != end) {
        throw MySQLBuildException("Too many parameters for SQL placeholders");
    }

    return result;
}

} // namespace user_service
