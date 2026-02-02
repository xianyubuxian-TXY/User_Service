#pragma once


#include <string>
#include <grpcpp/grpcpp.h>

namespace user_service{
enum class ErrorCode{
    // ========== 成功 ==========
    Ok = 0,

    // ==================== 通用错误（100~999） ====================
    // 系统错误（1xx）
    Unknown             = 100,  // 未知错误
    Internal            = 101,  // 内部服务器异常
    NotImplemented      = 102,  // 功能未实现
    ServiceUnavailable  = 103,  // 服务不可用
    Timeout             = 104,  // 请求超时

    // 参数错误（2xx）
    InvalidArgument     = 200,  // 参数无效
    InvalidPage         = 210,  // 无效的分页参数
    InvalidPageSize     = 211,  // 无效的分页大小

    // 限流（3xx）
    RateLimited         = 300,  // 请求过于频繁
    QuotaExceeded       = 301,  // 配额超限

    // ==================== 认证错误（1000~1999） ====================
    // Token 相关（100x）
    Unauthenticated     = 1000, // 未认证（通用）
    TokenMissing        = 1001, // Token 缺失
    TokenInvalid        = 1002, // Token 无效
    TokenExpired        = 1003, // Token 已过期
    TokenRevoked        = 1004, // Token 已注销

    // 登录相关（101x~102x）
    LoginFailed         = 1010, // 登录失败（通用）
    WrongPassword       = 1011, // 密码错误
    AccountLocked       = 1012, // 账号已锁定（多次密码错误）

    // 验证码相关
    CaptchaWrong        = 1021, // 验证码错误
    CaptchaExpired      = 1022, // 验证码已过期


    // ==================== 用户错误（2000~2999） ====================
    // 用户查询（200x）
    UserNotFound        = 2000, // 用户不存在
    UserDeleted         = 2001, // 用户已删除

    // 用户创建（201x）
    UserAlreadyExists   = 2010, // 用户已存在（通用）
    MobileTaken         = 2013, // 手机号已被占用

    // 用户状态（202x）
    UserDisabled        = 2020, // 用户已禁用
    UserNotVerified     = 2021, // 用户未验证

    // ==================== 权限错误（3000~3999） ====================
    PermissionDenied    = 3000, // 无权限（通用）
    AdminRequired       = 3001, // 需要管理员权限
    OwnerRequired       = 3002, // 需要是资源所有者
};

// ============================================================================
// 错误码工具函数
// ============================================================================

// 获取错误码对应的消息（用户友好）
inline std::string GetErrorMessage(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:                  return "成功";
        
        // 通用错误
        case ErrorCode::Unknown:             return "未知错误";
        case ErrorCode::Internal:            return "服务器内部错误";
        case ErrorCode::NotImplemented:      return "功能暂未实现";
        case ErrorCode::ServiceUnavailable:  return "服务暂不可用";
        case ErrorCode::Timeout:             return "请求超时";
        case ErrorCode::InvalidArgument:     return "参数无效";
        case ErrorCode::InvalidPage:         return "无效的页码";
        case ErrorCode::InvalidPageSize:     return "无效的每页大小";
        case ErrorCode::RateLimited:         return "请求过于频繁，请稍后再试";
        case ErrorCode::QuotaExceeded:       return "配额已超限";

        // 认证错误
        case ErrorCode::Unauthenticated:     return "请先登录";
        case ErrorCode::TokenMissing:        return "缺少认证信息";
        case ErrorCode::TokenInvalid:        return "认证信息无效";
        case ErrorCode::TokenExpired:        return "登录已过期，请重新登录";
        case ErrorCode::TokenRevoked:        return "登录已失效";
        case ErrorCode::LoginFailed:         return "登录失败";
        case ErrorCode::WrongPassword:       return "用户名或密码错误";  // 故意模糊
        case ErrorCode::AccountLocked:       return "账号已锁定，请稍后再试";
        case ErrorCode::CaptchaWrong:        return "验证码错误";
        case ErrorCode::CaptchaExpired:      return "验证码已过期，请重新获取";

        // 用户错误
        case ErrorCode::UserNotFound:        return "用户不存在";
        case ErrorCode::UserDeleted:         return "用户已注销";
        case ErrorCode::UserAlreadyExists:   return "用户已存在";
        case ErrorCode::MobileTaken:         return "手机号已被使用";
        case ErrorCode::UserDisabled:        return "账号已被禁用";
        case ErrorCode::UserNotVerified:     return "账号未验证";

        // 权限错误
        case ErrorCode::PermissionDenied:    return "无权限执行此操作";
        case ErrorCode::AdminRequired:       return "需要管理员权限";
        case ErrorCode::OwnerRequired:       return "只有所有者可执行此操作";

        default:                             return "未知错误";
    }
}

// 错误码 → gRPC 状态码映射
inline constexpr grpc::StatusCode ToGrpcStatus(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:
            return grpc::StatusCode::OK;

        // 参数错误 → INVALID_ARGUMENT
        case ErrorCode::InvalidArgument:
        case ErrorCode::InvalidPage:
        case ErrorCode::InvalidPageSize:
            return grpc::StatusCode::INVALID_ARGUMENT;

        // 认证错误 → UNAUTHENTICATED
        case ErrorCode::Unauthenticated:
        case ErrorCode::TokenMissing:
        case ErrorCode::TokenInvalid:
        case ErrorCode::TokenExpired:
        case ErrorCode::TokenRevoked:
        case ErrorCode::LoginFailed:
        case ErrorCode::WrongPassword:
        case ErrorCode::CaptchaWrong:
        case ErrorCode::CaptchaExpired:
            return grpc::StatusCode::UNAUTHENTICATED;

        // 未找到 → NOT_FOUND
        case ErrorCode::UserNotFound:
        case ErrorCode::UserDeleted:
            return grpc::StatusCode::NOT_FOUND;

        // 已存在 → ALREADY_EXISTS
        case ErrorCode::UserAlreadyExists:
        case ErrorCode::MobileTaken:
            return grpc::StatusCode::ALREADY_EXISTS;

        // 权限/状态错误 → PERMISSION_DENIED
        case ErrorCode::PermissionDenied:
        case ErrorCode::AdminRequired:
        case ErrorCode::OwnerRequired:
        case ErrorCode::UserDisabled:
        case ErrorCode::AccountLocked:
            return grpc::StatusCode::PERMISSION_DENIED;

        // 限流 → RESOURCE_EXHAUSTED
        case ErrorCode::RateLimited:
        case ErrorCode::QuotaExceeded:
            return grpc::StatusCode::RESOURCE_EXHAUSTED;

        // 未实现 → UNIMPLEMENTED
        case ErrorCode::NotImplemented:
            return grpc::StatusCode::UNIMPLEMENTED;

        // 服务不可用 → UNAVAILABLE
        case ErrorCode::ServiceUnavailable:
        case ErrorCode::Timeout:
            return grpc::StatusCode::UNAVAILABLE;

        // 其他 → INTERNAL
        default:
            return grpc::StatusCode::INTERNAL;
    }
}

// 错误码 → HTTP 状态码映射（如果需要 REST 网关）
inline constexpr int ToHttpStatus(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:                  return 200;
        
        case ErrorCode::InvalidArgument:
        case ErrorCode::InvalidPage:
        case ErrorCode::InvalidPageSize:     return 400;  // Bad Request

        case ErrorCode::Unauthenticated:
        case ErrorCode::TokenMissing:
        case ErrorCode::TokenInvalid:
        case ErrorCode::TokenExpired:
        case ErrorCode::TokenRevoked:
        case ErrorCode::LoginFailed:
        case ErrorCode::WrongPassword:
        case ErrorCode::CaptchaWrong:
        case ErrorCode::CaptchaExpired:      return 401;  // Unauthorized

        case ErrorCode::PermissionDenied:
        case ErrorCode::AdminRequired:
        case ErrorCode::OwnerRequired:
        case ErrorCode::UserDisabled:
        case ErrorCode::AccountLocked:       return 403;  // Forbidden

        case ErrorCode::UserNotFound:
        case ErrorCode::UserDeleted:         return 404;  // Not Found

        case ErrorCode::UserAlreadyExists:
        case ErrorCode::MobileTaken:         return 409;  // Conflict

        case ErrorCode::RateLimited:
        case ErrorCode::QuotaExceeded:       return 429;  // Too Many Requests

        case ErrorCode::NotImplemented:      return 501;  // Not Implemented
        case ErrorCode::ServiceUnavailable:  return 503;  // Service Unavailable

        default:                             return 500;  // Internal Server Error
    }
}

}
