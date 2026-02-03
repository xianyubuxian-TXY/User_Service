#pragma once

#include <string>
#include <grpcpp/grpcpp.h>
#include "pb_common/result.pb.h"
#include "common/error_codes.h"
#include "common/auth_type.h"
#include "entity/user_entity.h"
#include "entity/page.h"
#include "pb_common/result.grpc.pb.h"
#include "pb_auth/auth.grpc.pb.h"
#include "pb_user/user.grpc.pb.h"

namespace user_service {

// ============================================================================
// ErrorCode 转换函数
// ============================================================================

// 业务层 ErrorCode → Proto ErrorCode
inline pb_common::ErrorCode ToProtoErrorCode(ErrorCode code) {
    // 数值相同，直接转换
    return static_cast<pb_common::ErrorCode>(static_cast<int>(code));
}

// Proto ErrorCode → 业务层 ErrorCode
inline ErrorCode FromProtoErrorCode(pb_common::ErrorCode code) {
    return static_cast<ErrorCode>(static_cast<int>(code));
}

// ============================================================================
// 便捷函数：直接设置 Result
// ============================================================================

// 设置成功结果
inline void SetResultOk(pb_common::Result* result, const std::string& msg = "成功") {
    result->set_code(pb_common::ErrorCode::OK);
    result->set_msg(msg);
}

// 设置错误结果
inline void SetResultError(pb_common::Result* result, ErrorCode code) {
    result->set_code(ToProtoErrorCode(code));
    result->set_msg(GetErrorMessage(code));
}

// 设置错误结果（自定义消息）
inline void SetResultError(pb_common::Result* result, ErrorCode code, const std::string& msg) {
    result->set_code(ToProtoErrorCode(code));
    result->set_msg(msg);
}


// ============================================================================
// 时间转换
// ============================================================================

/// @brief MySQL DATETIME 字符串 → Protobuf Timestamp
/// @param datetime_str 格式: "2024-01-15 10:30:00"
inline google::protobuf::Timestamp ToProtoTimestamp(const std::string& datetime_str) {
    google::protobuf::Timestamp ts;
    
    if (datetime_str.empty()) {
        ts.set_seconds(0);
        ts.set_nanos(0);
        return ts;
    }
    
    // 简单解析 "YYYY-MM-DD HH:MM:SS"
    std::tm tm = {};
    if (strptime(datetime_str.c_str(), "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
        std::time_t time = std::mktime(&tm);
        ts.set_seconds(time);
        ts.set_nanos(0);
    }
    
    return ts;
}

/// @brief Protobuf Timestamp → MySQL DATETIME 字符串
inline std::string FromProtoTimestamp(const google::protobuf::Timestamp& ts) {
    std::time_t time = ts.seconds();
    std::tm* tm = std::localtime(&time);
    
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    return std::string(buf);
}

/// @brief system_clock::time_point → Protobuf Timestamp
inline google::protobuf::Timestamp ToProtoTimestamp(
    const std::chrono::system_clock::time_point& tp) {
    google::protobuf::Timestamp ts;
    auto duration = tp.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - seconds);
    ts.set_seconds(seconds.count());
    ts.set_nanos(static_cast<int32_t>(nanos.count()));
    return ts;
}

// ============================================================================
// SmsScene 枚举转换
// ============================================================================

/// @brief 业务层 SmsScene → Proto SmsScene
inline pb_auth::SmsScene ToProtoSmsScene(SmsScene scene) {
    switch (scene) {
        case SmsScene::Register:      return pb_auth::SmsScene::SMS_SCENE_REGISTER;
        case SmsScene::Login:         return pb_auth::SmsScene::SMS_SCENE_LOGIN;
        case SmsScene::ResetPassword: return pb_auth::SmsScene::SMS_SCENE_RESET_PASSWORD;
        case SmsScene::DeleteUser:    return pb_auth::SmsScene::SMS_SCENE_DELETE_USER;
        default:                      return pb_auth::SmsScene::SMS_SCENE_UNKNOWN;
    }
}

/// @brief Proto SmsScene → 业务层 SmsScene
inline SmsScene FromProtoSmsScene(pb_auth::SmsScene scene) {
    switch (scene) {
        case pb_auth::SmsScene::SMS_SCENE_REGISTER:       return SmsScene::Register;
        case pb_auth::SmsScene::SMS_SCENE_LOGIN:          return SmsScene::Login;
        case pb_auth::SmsScene::SMS_SCENE_RESET_PASSWORD: return SmsScene::ResetPassword;
        case pb_auth::SmsScene::SMS_SCENE_DELETE_USER:    return SmsScene::DeleteUser;
        default:                                          return SmsScene::UnKnow;
    }
}

// ============================================================================
// UserRole 枚举转换
// ============================================================================

/// @brief 业务层 UserRole → Proto UserRole
inline pb_auth::UserRole ToProtoUserRole(UserRole role) {
    switch (role) {
        case UserRole::User:       return pb_auth::UserRole::USER_ROLE_USER;
        case UserRole::Admin:      return pb_auth::UserRole::USER_ROLE_ADMIN;
        case UserRole::SuperAdmin: return pb_auth::UserRole::USER_ROLE_SUPER_ADMIN;
        default:                   return pb_auth::UserRole::USER_ROLE_USER;
    }
}

/// @brief Proto UserRole → 业务层 UserRole
inline UserRole FromProtoUserRole(pb_auth::UserRole role) {
    switch (role) {
        case pb_auth::UserRole::USER_ROLE_ADMIN:       return UserRole::Admin;
        case pb_auth::UserRole::USER_ROLE_SUPER_ADMIN: return UserRole::SuperAdmin;
        default:                                       return UserRole::User;
    }
}

// ============================================================================
// TokenPair 转换
// ============================================================================

/// @brief 业务层 TokenPair → Proto TokenPair
inline void ToProtoTokenPair(const TokenPair& src, pb_auth::TokenPair* dst) {
    dst->set_access_token(src.access_token);
    dst->set_refresh_token(src.refresh_token);
    dst->set_expires_in(src.expires_in);
}

/// @brief Proto TokenPair → 业务层 TokenPair
inline TokenPair FromProtoTokenPair(const pb_auth::TokenPair& src) {
    return TokenPair{
        .access_token = src.access_token(),
        .refresh_token = src.refresh_token(),
        .expires_in = src.expires_in()
    };
}

// ============================================================================
// TokenValidation 转换
// ============================================================================

/// @brief 业务层 TokenValidation → Proto ValidateTokenResponse 字段
inline void SetValidateTokenResponse(
    const TokenValidationResult& src,
    pb_auth::ValidateTokenResponse* response) 
{
    response->set_user_id(std::to_string(src.user_id));
    response->set_user_uuid(src.user_uuid);
    response->set_mobile(src.mobile);
    response->set_role(ToProtoUserRole(src.role));
    *response->mutable_expires_at() = ToProtoTimestamp(src.expires_at);
}


// ============================================================================
// UserEntity 转换
// ============================================================================

/// @brief UserEntity → pb_auth::UserInfo（登录/注册返回用）
inline void ToProtoUserInfo(const UserEntity& src, pb_auth::UserInfo* dst) {
    dst->set_id(src.uuid);
    dst->set_mobile(src.mobile);
    dst->set_display_name(src.display_name);
    dst->set_role(ToProtoUserRole(src.role));
    dst->set_disabled(src.disabled);
    *dst->mutable_created_at() = ToProtoTimestamp(src.created_at);
}

/// @brief UserEntity → pb_user::User（完整用户信息）
inline void ToProtoUser(const UserEntity& src, pb_user::User* dst) {
    dst->set_id(src.uuid);
    dst->set_mobile(src.mobile);
    dst->set_display_name(src.display_name);
    dst->set_role(ToProtoUserRole(src.role));
    dst->set_disabled(src.disabled);
    *dst->mutable_created_at() = ToProtoTimestamp(src.created_at);
    *dst->mutable_updated_at() = ToProtoTimestamp(src.updated_at);
}

/// @brief pb_user::User → UserEntity（部分字段，用于更新）
inline UserEntity FromProtoUser(const pb_user::User& src) {
    UserEntity entity;
    entity.uuid = src.id();
    entity.mobile = src.mobile();
    entity.display_name = src.display_name();
    entity.role = FromProtoUserRole(src.role());
    entity.disabled = src.disabled();
    entity.created_at = FromProtoTimestamp(src.created_at());
    entity.updated_at = FromProtoTimestamp(src.updated_at());
    return entity;
}

// ============================================================================
// AuthResult 转换（组合转换）
// ============================================================================

/// @brief 设置 RegisterResponse / LoginResponse
inline void SetAuthResponse(
    const AuthResult& result,
    pb_auth::UserInfo* user_info,
    pb_auth::TokenPair* tokens) 
{
    ToProtoUserInfo(result.user, user_info);
    ToProtoTokenPair(result.tokens, tokens);
}

// ============================================================================
// 分页转换
// ============================================================================

/// @brief Proto PageRequest → 业务层 PageParams
inline PageParams FromProtoPageRequest(const pb_user::PageRequest& src) {
    PageParams params;
    params.page = src.page();
    params.page_size = src.page_size();
    params.Validate();  // 自动校验
    return params;
}

/// @brief 业务层 PageResult → Proto PageResponse
inline void ToProtoPageResponse(const PageResult& src, pb_user::PageResponse* dst) {
    dst->set_total_records(src.total_records);
    dst->set_total_pages(src.total_pages);
    dst->set_page(src.page);
    dst->set_page_size(src.page_size);
}

// ============================================================================
// Wrapper 类型辅助函数
// ============================================================================

/// @brief 检查 StringValue 是否有值
inline bool HasValue(const google::protobuf::StringValue& wrapper) {
    return !wrapper.value().empty();
}

/// @brief 检查 BoolValue 是否有值（通过是否被设置判断）
inline bool HasValue(const google::protobuf::BoolValue& wrapper, bool* out_value) {
    // BoolValue 总是有值，需要通过 message 是否存在来判断
    *out_value = wrapper.value();
    return true;
}

/// @brief 获取 StringValue 的值，无值返回默认值
inline std::string GetValueOr(
    const google::protobuf::StringValue& wrapper, 
    const std::string& default_value = "") 
{
    return wrapper.value().empty() ? default_value : wrapper.value();
}

} // namespace user_service
