#pragma once
#include <mysql/mysql.h>
#include <string>
#include <cstdint>
#include <stdexcept>
#include <optional>
#include <unordered_map>

namespace user_service {

/**
 * @brief MySQL 查询结果集封装类
 * 
 * 封装 MYSQL_RES，提供安全的结果集遍历和字段值获取功能。
 * 支持按索引或列名获取字段，自动管理底层资源生命周期。
 * 
 * @note 禁止拷贝，仅支持移动语义
 * @note 使用前必须先调用 Next() 获取第一行数据
 * 
 * @example
 * @code
 *   auto res = conn->Query("SELECT id, username FROM users");
 *   while (res.Next()) {
 *       auto id = res.GetInt("id");
 *       auto name = res.GetString("username");
 *   }
 * @endcode
 */
class MySQLResult {
public:
    /**
     * @brief 构造函数
     * @param res MySQL 原生结果集指针，可为 nullptr（表示空结果）
     * @note 构造时会自动构建列名索引映射
     */
    explicit MySQLResult(MYSQL_RES* res);
    
    /**
     * @brief 析构函数，自动释放 MYSQL_RES 资源
     */
    ~MySQLResult();

    /// @name 禁用拷贝，允许移动
    /// @{
    MySQLResult(const MySQLResult&) = delete;
    MySQLResult& operator=(const MySQLResult&) = delete;
    
    /**
     * @brief 移动构造函数
     * @param other 被移动的对象
     * @note noexcept 保证 STL 容器可以安全优化
     */
    MySQLResult(MySQLResult&& other) noexcept;
    
    /**
     * @brief 移动赋值运算符
     * @param other 被移动的对象
     * @return 当前对象的引用
     */
    MySQLResult& operator=(MySQLResult&& other) noexcept;
    /// @}

    // ==================== 遍历接口 ====================
    
    /**
     * @brief 获取下一行数据
     * 
     * 将游标移动到下一行，并将数据存储在内部缓冲区中。
     * 必须在调用 Get* 系列方法前调用此方法。
     * 
     * @return true 成功获取下一行
     * @return false 没有更多数据
     */
    bool Next();

    // ==================== 元信息接口 ====================
    
    /**
     * @brief 获取结果集总行数
     * @return 总行数，空结果返回 0
     * @warning 对于 unbuffered 查询，此值可能不准确
     */
    size_t RowCount() const;
    
    /**
     * @brief 获取结果集列数
     * @return 列数
     */
    size_t FieldCount() const;
    
    /**
     * @brief 判断结果集是否为空
     * @return true 结果集为空或无数据
     */
    bool Empty() const { return result_ == nullptr || RowCount() == 0; }

    // ==================== 按索引获取字段值 ====================
    
    /**
     * @brief 判断指定列是否为 NULL
     * @param col 列索引（从 0 开始）
     * @return true 该列值为 NULL
     * @throws MySQLResultException 未调用 Next() 或索引越界
     */
    bool IsNull(size_t col) const;
    
    /**
     * @brief 获取字符串类型字段值
     * @param col 列索引（从 0 开始）
     * @return 字段值，NULL 时返回 std::nullopt
     * @throws MySQLResultException 未调用 Next() 或索引越界
     */
    std::optional<std::string> GetString(size_t col) const;
    
    /**
     * @brief 获取整数类型字段值
     * @param col 列索引（从 0 开始）
     * @return 字段值（int64_t），NULL 时返回 std::nullopt
     * @throws MySQLResultException 未调用 Next() 或索引越界
     */
    std::optional<int64_t> GetInt(size_t col) const;
    
    /**
     * @brief 获取浮点类型字段值
     * @param col 列索引（从 0 开始）
     * @return 字段值（double），NULL 时返回 std::nullopt
     * @throws MySQLResultException 未调用 Next() 或索引越界
     */
    std::optional<double> GetDouble(size_t col) const;

    // ==================== 按列名获取字段值 ====================
    
    /**
     * @brief 判断指定列是否为 NULL
     * @param col_name 列名
     * @return true 该列值为 NULL
     * @throws std::out_of_range 列名不存在
     * @throws MySQLResultException 未调用 Next()
     */
    bool IsNull(const std::string& col_name) const;
    
    /**
     * @brief 获取字符串类型字段值
     * @param col_name 列名
     * @return 字段值，NULL 时返回 std::nullopt
     * @throws std::out_of_range 列名不存在
     */
    std::optional<std::string> GetString(const std::string& col_name) const;
    
    /**
     * @brief 获取整数类型字段值
     * @param col_name 列名
     * @return 字段值（int64_t），NULL 时返回 std::nullopt
     * @throws std::out_of_range 列名不存在
     */
    std::optional<int64_t> GetInt(const std::string& col_name) const;
    
    /**
     * @brief 获取浮点类型字段值
     * @param col_name 列名
     * @return 字段值（double），NULL 时返回 std::nullopt
     * @throws std::out_of_range 列名不存在
     */
    std::optional<double> GetDouble(const std::string& col_name) const;

private:
    /**
     * @brief 检查列索引是否合法
     * @param col 列索引
     * @throws MySQLResultException 未调用 Next() 或索引越界
     */
    void CheckColumn(size_t col) const;
    
    /**
     * @brief 构建列名到索引的映射表
     * @note 在构造函数中自动调用
     */
    void BuildColumnMap();
    
    /**
     * @brief 根据列名获取列索引
     * @param col_name 列名
     * @return 对应的列索引
     * @throws std::out_of_range 列名不存在
     */
    size_t GetColumnIndex(const std::string& col_name) const;

    MYSQL_RES* result_ = nullptr;           ///< MySQL 原生结果集
    MYSQL_ROW current_row_ = nullptr;       ///< 当前行数据指针
    unsigned long* lengths_ = nullptr;      ///< 当前行各字段的实际长度
    unsigned int field_count_ = 0;          ///< 结果集列数

    /// 列名到索引的映射，用于按列名快速查找
    std::unordered_map<std::string, size_t> col_name_map_;
};

} // namespace user_service
