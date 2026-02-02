#pragma once

#include "error_codes.h"
#include <optional>

namespace user_service {

// 通用版本：有返回值
template<typename T>
struct Result {
    ErrorCode code;
    std::string message;
    std::optional<T> data;

    // ==================== 构造方法 ====================
    
    // 成功：有返回值
    static Result Ok(const T& value) {
        return {ErrorCode::Ok, GetErrorMessage(ErrorCode::Ok), value};
    }
    
    // 成功：移动语义
    static Result Ok(T&& value) {
        return {ErrorCode::Ok, GetErrorMessage(ErrorCode::Ok), std::move(value)};
    }

    // 失败
    static Result Fail(ErrorCode c, const std::string& msg = "") {
        return {c, msg.empty() ? GetErrorMessage(c) : msg, std::nullopt};
    }

    // ==================== 状态检查 ====================
    
    bool Success() const { return code == ErrorCode::Ok; }
    bool Failure() const { return !Success(); }
    
    // 别名方法（兼容不同命名习惯）
    bool IsOk() const { return Success(); }
    bool IsErr() const { return Failure(); }

    explicit operator bool() const {
        return code == ErrorCode::Ok;
    }
    
    // ==================== 数据访问 ====================
    
    // 获取值（如果成功）
    const T& Value() const& { return *data; }
    T& Value() & { return *data; }
    T&& Value() && { return std::move(*data); }
    
    // 获取值或默认值
    T ValueOr(const T& default_value) const {
        return data.value_or(default_value);
    }
    
    // ==================== 错误访问 ====================
    
    // 别名（兼容不同命名习惯）
    std::optional<T> GetData() const { return data;}
    ErrorCode GetErrCode() const { return code; }
    const std::string& GetErrMessage() const { return message; }
};

// ==================== 特化版本：无返回值 ====================

template<>
struct Result<void> {
    ErrorCode code;
    std::string message;

    // 成功
    static Result Ok() {
        return {ErrorCode::Ok, GetErrorMessage(ErrorCode::Ok)};
    }

    // 失败
    static Result Fail(ErrorCode c, const std::string& msg = "") {
        return {c, msg.empty() ? GetErrorMessage(c) : msg};
    }

    // 状态检查
    bool Success() const { return code == ErrorCode::Ok; }
    bool Failure() const { return !Success(); }
    
    // 别名方法
    bool IsOk() const { return Success(); }
    bool IsErr() const { return Failure(); }

    explicit operator bool() const {
        return code == ErrorCode::Ok;
    }
    
    // 错误访问别名
    user_service::ErrorCode GetErrCode() const { return code; }
    const std::string& GetErrMessage() const { return message; }
};

}  // namespace user_service
