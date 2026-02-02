#pragma once

#include "config/config.h"
#include "mysql_result.h"
#include <mysql/mysql.h>
#include <string>
#include <variant>

namespace user_service {

/**
 * @brief MySQL 数据库连接封装类
 * 
 * 提供安全的数据库操作接口，支持参数化查询以防止 SQL 注入。
 * 内部管理 MYSQL* 连接资源，自动处理连接失败重试。
 * 
 * @note 禁止拷贝和移动，生命周期由连接池统一管理
 * @note 所有 SQL 语句使用 '?' 作为参数占位符
 * 
 * @example
 * @code
 *   MySQLConnection conn(config);
 *   
 *   // 查询
 *   auto res = conn.Query("SELECT * FROM users WHERE id = ?", {123});
 *   
 *   // 插入
 *   conn.Execute("INSERT INTO users (name, age) VALUES (?, ?)", {"Tom", 25});
 *   auto id = conn.LastInsertId();
 * @endcode
 * 
 * @warning SQL 语句中不能包含非占位符的 '?' 字符
 */
class MySQLConnection {
public:
    /**
     * @brief 参数类型定义
     * 
     * 支持的参数类型：
     * - nullptr_t: 对应 SQL NULL
     * - int64_t/uint64_t: 整数类型
     * - double: 浮点类型
     * - std::string: 字符串（会自动转义）
     * - bool: 布尔类型（转为 0/1）
     * 
     * @note 其他类型请先转为 string 再传入
     */
    using Param = std::variant<nullptr_t, int64_t, uint64_t, double, std::string, bool>;

    /**
     * @brief 构造函数，建立数据库连接
     * @param config MySQL 配置（host, port, user, password, database 等）
     * @throws MySQLException 连接失败且重试耗尽时抛出
     */
    explicit MySQLConnection(const MySQLConfig& config);
    
    /**
     * @brief 析构函数，关闭数据库连接
     */
    ~MySQLConnection();

    /// @name 禁用拷贝和移动
    /// @{
    MySQLConnection(const MySQLConnection&) = delete;
    MySQLConnection& operator=(const MySQLConnection&) = delete;
    MySQLConnection(MySQLConnection&& other) = delete;
    MySQLConnection& operator=(MySQLConnection&& other) = delete;
    /// @}

    /**
     * @brief 检查连接是否有效
     * 
     * 内部使用 mysql_ping 检测连接状态，会自动尝试重连。
     * 
     * @return true 连接有效
     * @return false 连接无效或已断开
     */
    bool Valid() const { return mysql_ && mysql_ping(mysql_) == 0; }

    /**
     * @brief 获取原生 MySQL 连接指针
     * @return MYSQL* 原生连接指针
     * @warning 谨慎使用，避免破坏内部状态
     */
    MYSQL* Get() { return mysql_; }

    /**
     * @brief 执行 SELECT 查询
     * 
     * @param sql SQL 语句，使用 '?' 作为参数占位符
     * @param params 参数列表，按顺序替换 '?'
     * @return MySQLResult 查询结果集
     * 
     * @throws MySQLException 查询执行失败
     * @throws std::invalid_argument 参数数量与占位符不匹配
     * 
     * @example
     * @code
     *   // 无参数查询
     *   auto res = conn.Query("SELECT * FROM users");
     *   
     *   // 带参数查询（防 SQL 注入）
     *   auto res = conn.Query("SELECT * FROM users WHERE name = ? AND age > ?", 
     *                         {"Tom", 18});
     * @endcode
     * 
     * @warning SQL 中不能包含非占位符的 '?' 字符
     */
    MySQLResult Query(const std::string& sql, std::initializer_list<Param> params = {});

    MySQLResult Query(const std::string& sql, const std::vector<Param>& params);

    /**
     * @brief 执行 INSERT/UPDATE/DELETE 操作
     * 
     * @param sql SQL 语句，使用 '?' 作为参数占位符
     * @param params 参数列表，按顺序替换 '?'
     * @return uint64_t 受影响的行数
     * 
     * @throws MySQLException 执行失败
     * @throws MySQLDuplicateKeyException 唯一键冲突（INSERT 时）
     * @throws std::invalid_argument 参数数量与占位符不匹配
     * 
     * @example
     * @code
     *   // INSERT
     *   conn.Execute("INSERT INTO users (name) VALUES (?)", {"Tom"});
     *   
     *   // UPDATE
     *   uint64_t affected = conn.Execute("UPDATE users SET age = ? WHERE id = ?", {30, 1});
     *   
     *   // DELETE  
     *   uint64_t deleted = conn.Execute("DELETE FROM users WHERE id = ?", {1});
     * @endcode
     */
    uint64_t Execute(const std::string& sql, std::initializer_list<Param> params = {});

    uint64_t Execute(const std::string& sql, const std::vector<Param>& params);
    // /**
    //  * @brief 流式查询（逐行获取，适合大结果集）
    //  * @param sql SQL 语句
    //  * @return MySQLResult 流式结果集
    //  * @warning 遍历期间不能执行其他查询！
    //  */
    // MySQLResult QueryUnbuffered(const std::string& sql);

    /**
     * @brief 获取最后一次 INSERT 操作生成的自增 ID
     * @return uint64_t 自增 ID，无自增列时返回 0
     * @note 必须在 INSERT 之后立即调用
     */
    uint64_t LastInsertId();

private:
    /**
     * @brief 初始化连接并设置选项
     * @param config MySQL 配置
     * @note 设置字符集、超时、自动重连等选项
     */
    void InitAndSetOptions(const MySQLConfig& config);

    /**
     * @brief 判断错误是否值得重试
     * @param err_code MySQL 错误码
     * @return true 可重试（如网络超时、连接丢失）
     * @return false 不可重试（如语法错误、权限不足）
     */
    bool IsRetryableError(unsigned int err_code);

    /**
     * @brief 带重试机制的连接方法
     * @param config MySQL 配置
     * @throws MySQLException 重试次数耗尽仍失败
     */
    void ConnectWithRetry(const MySQLConfig& config);

    /**
     * @brief 转义字符串（防 SQL 注入）
     * @param str 原始字符串
     * @return std::string 转义后的字符串
     * @note 仅用于转义参数值，不要用于表名/列名
     */
    std::string Escape(const std::string& str);

    // /**
    //  * @brief 构建完整的 SQL 语句
    //  * 
    //  * 将参数化 SQL 中的 '?' 占位符替换为实际值。
    //  * 
    //  * @param sql 带占位符的 SQL 模板
    //  * @param params 参数列表
    //  * @return std::string 完整的可执行 SQL
    //  * 
    //  * @throws std::invalid_argument 参数数量与 '?' 数量不匹配
    //  * 
    //  * @note 字符串参数会自动转义并加引号
    //  * @note nullptr 会转换为 SQL 的 NULL
    //  * @note bool 会转换为 0 或 1
    //  */
    // std::string BuildSQL(const std::string& sql, std::initializer_list<Param> params);

    // 模板方法：接受任意迭代器
    template<typename Iter>
    std::string BuildSQL(const std::string& sql, Iter begin, Iter end);

    /**
     * @brief 根据错误码抛出对应异常
     * @param code MySQL 错误码
     * @param msg 错误描述信息
     * @throws MySQLDuplicateKeyException 唯一键冲突 (ER_DUP_ENTRY)
     * @throws MySQLException 其他错误
     */
    void ThrowByErrorCode(unsigned int code, const std::string& msg);

private:
    
    MYSQL* mysql_ = nullptr;  ///< MySQL 原生连接句柄
};

} // namespace user_service
