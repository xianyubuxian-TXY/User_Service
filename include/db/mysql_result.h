#pragma once
#include <mysql/mysql.h>
#include <string>
#include <cstdint>
#include <stdexcept>
#include <optional>
namespace user_service{

// // 单行数据的轻量包装（可用于流式查询回调）
// /*注意：不要把MySQLRow作为MySQLResult的数据成员：MySQLRow 本质是个临时视图，不拥有数据
//   它们职责各自不同*/
// class MySQLRow {
// public:
//     MySQLRow(MYSQL_ROW row, unsigned long* lengths, unsigned int field_count)
//         : row_(row), lengths_(lengths), field_count_(field_count) {}
    
//     bool IsNull(size_t col) const;
//     std::optional<std::string> GetString(size_t col) const;
//     std::optional<int64_t> GetInt(size_t col) const;
//     std::optional<double> GetDouble(size_t col) const;
    
//     size_t FieldCount() const { return field_count_; }

// private:
//     void CheckColumn(size_t col) const;
    
//     MYSQL_ROW row_;
//     unsigned long* lengths_;
//     unsigned int field_count_;
// };


//管理结果集
class MySQLResult {
public:
    explicit MySQLResult(MYSQL_RES* res);
    ~MySQLResult();

    // 禁用拷贝，允许移动
    MySQLResult(const MySQLResult&) = delete;
    MySQLResult& operator=(const MySQLResult&) = delete;
    //移动操作加 noexcept 可以让 STL 容器更好地优化。
    MySQLResult(MySQLResult&& other) noexcept;
    MySQLResult& operator=(MySQLResult&& other) noexcept;

    // 遍历：获取下一行存储在current_row_中，无数据则返回false
    bool Next();
    
    // 元信息
    size_t RowCount() const;
    size_t FieldCount() const;
    
    // 取值（列索引从 0 开始）
    bool IsNull(size_t col) const;
    std::optional<std::string> GetString(size_t col) const;
    std::optional<int64_t> GetInt(size_t col) const;
    std::optional<double> GetDouble(size_t col) const;
    
    // 空结果集判断
    bool Empty() const { return result_ == nullptr || RowCount() == 0; }

private:
    //检查访问的列是否合法，不合法抛出异常
    void CheckColumn(size_t col) const;

    MYSQL_RES* result_ = nullptr;       //结果集
    MYSQL_ROW current_row_ = nullptr;   //当前行
    unsigned long* lengths_ = nullptr;  //当前行各列字段值的实际长度数组
    unsigned int field_count_ = 0;      //结果集的列数
    // bool unbufferd_ =false;             //是否为流式查询结果
};
}