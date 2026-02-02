#pragma once

#include <string>
#include <regex>
#include "config/config.h"  // 引入配置

namespace user_service {

/**
 * @brief 校验手机号格式（中国大陆）
 */
inline bool IsValidMobile(const std::string& mobile, std::string& error) {
    static const std::regex pattern(R"(1[3-9]\d{9})");
    if (mobile.length() != 11 || !std::regex_match(mobile, pattern)) {
        error = "手机号格式错误";
        return false;
    }
    return true;
}

/**
 * @brief 校验邮箱格式
 */
inline bool IsValidEmail(const std::string& email, std::string& error) {
    static const std::regex pattern(R"([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,})");
    if (email.empty() || email.length() > 128 || !std::regex_match(email, pattern)) {
        error = "邮箱格式错误";
        return false;
    }
    return true;
}

/**
 * @brief 校验密码强度（默认版本：8-32位，必须包含字母+数字）
 */
inline bool IsValidPassword(const std::string& password, std::string& error) {
    if (password.length() < 8) {
        error = "密码长度至少8位";
        return false;
    }
    if (password.length() > 32) {
        error = "密码长度不能超过32位";
        return false;
    }
    
    bool has_digit = false;
    bool has_alpha = false;
    for (char c : password) {
        if (std::isdigit(c)) has_digit = true;
        if (std::isalpha(c)) has_alpha = true;
    }
    
    if (!has_digit || !has_alpha) {
        error = "密码必须包含字母和数字";
        return false;
    }
    
    return true;
}

/**
 * @brief 校验密码强度（配置版本：根据 PasswordPolicyConfig 校验）
 */
inline bool IsValidPassword(const std::string& password, 
                            std::string& error,
                            const PasswordPolicyConfig& policy) {
    // 长度检查
    if (static_cast<int>(password.length()) < policy.min_length) {
        error = "密码长度至少" + std::to_string(policy.min_length) + "位";
        return false;
    }
    if (static_cast<int>(password.length()) > policy.max_length) {
        error = "密码长度不能超过" + std::to_string(policy.max_length) + "位";
        return false;
    }
    
    // 复杂度检查
    bool has_uppercase = false;
    bool has_lowercase = false;
    bool has_digit = false;
    bool has_special = false;
    
    for (char c : password) {
        if (std::isupper(c)) has_uppercase = true;
        if (std::islower(c)) has_lowercase = true;
        if (std::isdigit(c)) has_digit = true;
        if (!std::isalnum(c)) has_special = true;
    }
    
    if (policy.require_uppercase && !has_uppercase) {
        error = "密码必须包含大写字母";
        return false;
    }
    if (policy.require_lowercase && !has_lowercase) {
        error = "密码必须包含小写字母";
        return false;
    }
    if (policy.require_digit && !has_digit) {
        error = "密码必须包含数字";
        return false;
    }
    if (policy.require_special_char && !has_special) {
        error = "密码必须包含特殊字符";
        return false;
    }
    
    return true;
}

/**
 * @brief 校验验证码格式（默认版本：6位数字）
 */
inline bool IsValidVerifyCode(const std::string& code, std::string& error) {
    if (code.length() != 6) {
        error = "验证码格式错误";
        return false;
    }
    for (char c : code) {
        if (!std::isdigit(c)) {
            error = "验证码格式错误";
            return false;
        }
    }
    return true;
}

/**
 * @brief 校验验证码格式（配置版本：根据 SmsConfig 校验）
 */
inline bool IsValidVerifyCode(const std::string& code, 
                              std::string& error,
                              const SmsConfig& config) {
    if (static_cast<int>(code.length()) != config.code_len) {
        error = "验证码应为" + std::to_string(config.code_len) + "位数字";
        return false;
    }
    for (char c : code) {
        if (!std::isdigit(c)) {
            error = "验证码应为" + std::to_string(config.code_len) + "位数字";
            return false;
        }
    }
    return true;
}

/**
 * @brief 校验昵称
 */
inline bool IsValidDisplayName(const std::string& name, std::string& error) {
    if (name.empty()) {
        error = "昵称不能为空";
        return false;
    }
    if (name.length() > 32) {
        error = "昵称长度不能超过32位";
        return false;
    }
    return true;
}

/**
 * @brief 校验用户ID（字符串形式）
 */
inline bool IsValidUserId(const std::string& user_id, std::string& error) {
    if (user_id.empty()) {
        error = "用户ID不能为空";
        return false;
    }
    for (char c : user_id) {
        if (!std::isdigit(c)) {
            error = "用户ID格式错误";
            return false;
        }
    }
    return true;
}

}  // namespace user_service
