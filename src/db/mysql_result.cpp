#include "db/mysql_result.h"
#include "exception/exception.h"

#include <cstdlib>

namespace user_service {

// ==================== 构造与析构 ====================

MySQLResult::MySQLResult(MYSQL_RES* res)
    : result_(res),
      field_count_(res ? mysql_num_fields(res) : 0) {
    // 构造时立即构建列名映射，支持后续按列名查询
    BuildColumnMap(); 
}

MySQLResult::~MySQLResult() {
    // RAII: 确保 MYSQL_RES 资源被正确释放
    if (result_) {
        mysql_free_result(result_);
        result_ = nullptr;
    }
}

// ==================== 移动语义 ====================

MySQLResult::MySQLResult(MySQLResult&& other) noexcept
    : result_(other.result_),
      current_row_(other.current_row_),
      lengths_(other.lengths_),
      field_count_(other.field_count_),
      col_name_map_(std::move(other.col_name_map_)) {
    // 将源对象置为"空壳"状态，防止其析构时释放资源
    other.result_ = nullptr;
    other.current_row_ = nullptr;
    other.lengths_ = nullptr;
    other.field_count_ = 0;
    // col_name_map_ 已被 move，自动变为空
}

MySQLResult& MySQLResult::operator=(MySQLResult&& other) noexcept {
    // 自赋值检查：防止 a = std::move(a) 导致资源丢失
    if (&other != this) {
        // 先释放自己持有的资源
        if (result_) {
            mysql_free_result(result_);
        }
        
        // 接管 other 的资源
        result_ = other.result_;
        current_row_ = other.current_row_;
        lengths_ = other.lengths_;
        field_count_ = other.field_count_;
        col_name_map_ = std::move(other.col_name_map_);

        // 将 other 置空
        other.result_ = nullptr;
        other.current_row_ = nullptr;
        other.lengths_ = nullptr;
        other.field_count_ = 0;
    }
    return *this;
}

// ==================== 遍历接口 ====================

bool MySQLResult::Next() {
    if (!result_) return false;
    
    // mysql_fetch_row: 获取当前行并自动将游标后移
    // 返回 NULL 表示没有更多数据或发生错误
    current_row_ = mysql_fetch_row(result_);
    
    if (current_row_) {
        // mysql_fetch_lengths: 获取当前行各字段的实际字节长度
        // 对于二进制数据和含 '\0' 的字符串，必须使用长度而非 strlen
        lengths_ = mysql_fetch_lengths(result_);
        return true;
    }
    return false;
}

// ==================== 元信息接口 ====================

size_t MySQLResult::RowCount() const {
    // 注意：对于 unbuffered 查询，mysql_num_rows 可能返回 0
    return result_ ? static_cast<size_t>(mysql_num_rows(result_)) : 0;
}

size_t MySQLResult::FieldCount() const {
    return field_count_;
}

// ==================== 按索引获取字段值 ====================

bool MySQLResult::IsNull(size_t col) const {
    CheckColumn(col);
    return current_row_[col] == nullptr;
}

std::optional<std::string> MySQLResult::GetString(size_t col) const {
    CheckColumn(col);
    
    // MySQL 中 NULL 值需要特殊处理
    if (current_row_[col] == nullptr) {
        return std::nullopt;
    }
    
    // 重要：使用 lengths_ 构造 string，而非直接用 char*
    // 原因：1. 二进制数据可能包含 '\0'
    //       2. BLOB 类型不以 '\0' 结尾
    if (lengths_) {
        return std::string(current_row_[col], lengths_[col]);
    }
    
    // 降级处理（lengths_ 为空的罕见情况）
    return current_row_[col];
}

std::optional<int64_t> MySQLResult::GetInt(size_t col) const {
    CheckColumn(col);
    
    if (current_row_[col] == nullptr) {
        return std::nullopt;
    }
    
    // strtoll: 字符串转 long long，比 atoi 更安全
    // 参数：源字符串, 未转换部分指针(不需要), 进制(10进制)
    return std::strtoll(current_row_[col], nullptr, 10);
}

std::optional<double> MySQLResult::GetDouble(size_t col) const {
    CheckColumn(col);
    
    if (current_row_[col] == nullptr) {
        return std::nullopt;
    }
    
    return std::strtod(current_row_[col], nullptr);
}

// ==================== 按列名获取（委托给按索引版本） ====================

bool MySQLResult::IsNull(const std::string& col_name) const {
    return IsNull(GetColumnIndex(col_name));
}

std::optional<std::string> MySQLResult::GetString(const std::string& col_name) const {
    return GetString(GetColumnIndex(col_name));
}

std::optional<int64_t> MySQLResult::GetInt(const std::string& col_name) const {
    return GetInt(GetColumnIndex(col_name));
}

std::optional<double> MySQLResult::GetDouble(const std::string& col_name) const {
    return GetDouble(GetColumnIndex(col_name));
}

// ==================== 私有辅助方法 ====================

void MySQLResult::CheckColumn(size_t col) const {
    // 检查1：是否已调用 Next() 获取行数据
    if (!current_row_) {
        throw MySQLResultException("No current row, call Next() first");
    }
    
    // 检查2：列索引是否越界
    if (col >= field_count_) {
        throw MySQLResultException(
            "Column index " + std::to_string(col) +
            " out of range, max is " + std::to_string(field_count_ - 1));
    }
}

void MySQLResult::BuildColumnMap() {
    if (!result_) return;

    // mysql_fetch_fields: 一次性获取所有字段的元信息数组
    // 注意：不是 mysql_fetch_field（单数），那个是逐个获取
    MYSQL_FIELD* fields = mysql_fetch_fields(result_);
    
    // 构建 列名 -> 索引 的映射,方便按列名查找
    for (size_t i = 0; i < field_count_; ++i) {
        col_name_map_[fields[i].name] = i;
    }
}

size_t MySQLResult::GetColumnIndex(const std::string& col_name) const {
    auto it = col_name_map_.find(col_name);
    
    if (it == col_name_map_.end()) {
        // 列名不存在，抛出异常（常见原因：拼写错误、表结构变更）
        throw std::out_of_range("Column not found: " + col_name);
    }
    
    return it->second;
}

} // namespace user_service
