#include "db/mysql_result.h"
#include "exception/exception.h"

#include <cstdlib>

namespace user_service {

MySQLResult::MySQLResult(MYSQL_RES* res)
    : result_(res),
      field_count_(res ? mysql_num_fields(res) : 0)
{}

MySQLResult::~MySQLResult() {
    if (result_) {
        mysql_free_result(result_);
        result_ = nullptr;
    }
}

MySQLResult::MySQLResult(MySQLResult&& other) noexcept
    : result_(other.result_),
      current_row_(other.current_row_),
      lengths_(other.lengths_),
      field_count_(other.field_count_) {
    other.result_ = nullptr;
    other.current_row_ = nullptr;
    other.lengths_ = nullptr;
    other.field_count_ = 0;
}

MySQLResult& MySQLResult::operator=(MySQLResult&& other) noexcept {
    // 注意检查&other!=this
    if (&other != this) {
        if (result_) {
            // 释放自己的资源
            mysql_free_result(result_);
        }
        result_ = other.result_;
        current_row_ = other.current_row_;
        lengths_ = other.lengths_;
        field_count_ = other.field_count_;

        other.result_ = nullptr;
        other.current_row_ = nullptr;
        other.lengths_ = nullptr;
        other.field_count_ = 0;
    }
    return *this;
}

// 获取下一行数据，并返回是否有下一行
bool MySQLResult::Next() {
    if (!result_) return false;
    // 获取一行数据（游标自动后移）
    current_row_ = mysql_fetch_row(result_);
    if (current_row_) {
        // 获取各字段长度
        lengths_ = mysql_fetch_lengths(result_);
        return true;
    }
    return false;
}

// 行数
size_t MySQLResult::RowCount() const {
    return result_ ? static_cast<size_t>(mysql_num_rows(result_)) : 0;
}

// 字段数
size_t MySQLResult::FieldCount() const {
    return field_count_;
}

// 下标获取时，col是否合理
void MySQLResult::CheckColumn(size_t col) const {
    if (!current_row_) {
        throw MySQLQueryException("No current row, call Next() first");
    } else if (col >= field_count_) {
        throw MySQLQueryException("Column index " + std::to_string(col) +
                                  " out of range, max is " + std::to_string(field_count_ - 1));
    }
}

bool MySQLResult::IsNull(size_t col) const {
    CheckColumn(col);
    return current_row_[col] == nullptr;
}

// 获取目前行第col列字段（string类型）
std::optional<std::string> MySQLResult::GetString(size_t col) const {
    CheckColumn(col);
    // mysql中，可能存的是NULL
    if (current_row_[col] == nullptr) {
        return std::nullopt;
    }
    // current_row_可能包含的是二进制，不可以直接返回，避免被"截断"
    if (lengths_) {
        return std::string(current_row_[col], lengths_[col]);
    }
    return current_row_[col];
}

// 获取目前行第col列字段（int类型）
std::optional<int64_t> MySQLResult::GetInt(size_t col) const {
    CheckColumn(col);
    // mysql中，可能存的是NULL
    if (current_row_[col] == nullptr) {
        return std::nullopt;
    }
    return std::strtoll(current_row_[col], nullptr, 10);
}

// 获取目前行第col列字段（double类型）
std::optional<double> MySQLResult::GetDouble(size_t col) const {
    CheckColumn(col);
    // mysql中，可能存的是NULL
    if (current_row_[col] == nullptr) {
        return std::nullopt;
    }
    return std::strtod(current_row_[col], nullptr);
}

} // namespace user
