#pragma once

#include <string>
#include <google/protobuf/timestamp.pb.h>

// Proto 生成的头文件
#include "common/result.pb.h"
#include "auth/auth.pb.h"
#include "user/user.pb.h"

// 业务代码
#include "common/error_codes.h"
#include "common/time_utils.h"
#include "entity/user_entity.h"

namespace user_service {

// ==================== ErrorCode 转换 ====================

/**
 * @brief C++ ErrorCode 转 Protobuf ErrorCode
 */
inline common::ErrorCode ToPbErrorCode(ErrorCode code) {
    return static_cast<common::ErrorCode>(static_cast<int>(code));
}

// ==================== Result 填充 ====================

/**
 * @brief 填充成功 Result
 */
inline void SetResultOk(common::Result* result, const std::string& msg = "操作成功") {
    result->set_code(common::ErrorCode::OK);
    result->set_msg(msg);
}

/**
 * @brief 填充失败 Result
 */
inline void SetResultFail(common::Result* result, ErrorCode code, const std::string& msg = "") {
    result->set_code(ToPbErrorCode(code));
    result->set_msg(msg.empty() ? GetErrorMessage(code) : msg);
}

/**
 * @brief 添加字段级错误
 */
inline void AddFieldError(common::Result* result, const std::string& field, const std::string& msg) {
    auto* err = result->add_field_errors();
    err->set_field(field);
    err->set_msg(msg);
}

// ==================== UserEntity ↔ auth::UserInfo ====================

/**
 * @brief UserEntity 转 auth::UserInfo（登录/注册响应用）
 * @note auth::UserInfo 不包含敏感信息
 */
inline void EntityToAuthUserInfo(const UserEntity& entity, auth::UserInfo* user_info) {
    user_info->set_id(entity.uuid);
    user_info->set_mobile(entity.mobile);  // 可选：脱敏处理
    user_info->set_display_name(entity.display_name);
    user_info->set_disabled(entity.disabled);
    StringToTimestamp(entity.created_at, user_info->mutable_created_at());
}

// ==================== UserEntity ↔ user::User ====================

/**
 * @brief UserEntity 转 user::User（完整用户信息）
 */
inline void EntityToUserProto(const UserEntity& entity, user::User* user) {
    user->set_id(entity.uuid);
    user->set_mobile(entity.mobile);
    user->set_display_name(entity.display_name);
    user->set_disabled(entity.disabled);
    StringToTimestamp(entity.created_at, user->mutable_created_at());
    StringToTimestamp(entity.updated_at, user->mutable_updated_at());
    // 注意：password_hash 永远不返回给客户端
}

/**
 * @brief user::User 转 UserEntity（用于更新操作）
 * @note 只转换允许客户端修改的字段
 */
inline void UserProtoToEntity(const user::User& user, UserEntity& entity) {
    entity.uuid = user.id();
    entity.display_name = user.display_name();
    // mobile 不允许直接修改，需要走换绑流程
    // password_hash 不从 proto 获取
}

// ==================== 手机号脱敏 ====================

/**
 * @brief 手机号脱敏 138****1234
 */
inline std::string MaskMobile(const std::string& mobile) {
    if (mobile.length() != 11) return mobile;
    return mobile.substr(0, 3) + "****" + mobile.substr(7);
}

// ==================== 分页转换 ====================

/**
 * @brief 填充分页响应
 */
inline void FillPageResponse(user::PageResponse* resp, 
                             int64_t total_records, 
                             int32_t total_pages,
                             int32_t page, 
                             int32_t page_size) {
    resp->set_total_records(total_records);
    resp->set_total_pages(total_pages);
    resp->set_page(page);
    resp->set_page_size(page_size);
}

}  // namespace user_service
