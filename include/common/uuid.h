#pragma once

#include <uuid/uuid.h>
#include <string>
#include <cstring>

namespace user_service{

/**
 * UUID 工具类 - 针对用户管理系统
 * 
 * 对应 proto 中的使用场景：
 * - User.id         → UserId()
 * - Token           → Token()
 * - 通用场景        → Generate()
 */
class UUIDHelper {
public:
    // ==================== 基础生成 ====================
    
    /// 生成标准格式 UUID: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    static std::string Generate() {
        uuid_t uuid;
        char str[37];
        uuid_generate_random(uuid);
        uuid_unparse_lower(uuid, str);
        return std::string(str);
    }
    
    /// 生成紧凑格式（无连字符）: xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    static std::string GenerateCompact() {
        uuid_t uuid;
        char str[37];
        uuid_generate_random(uuid);
        uuid_unparse_lower(uuid, str);
        
        // 移除连字符
        std::string result;
        result.reserve(32);
        for (int i = 0; i < 36; ++i) {
            if (str[i] != '-') {
                result += str[i];
            }
        }
        return result;
    }
    
    // ==================== 业务专用（对应 proto 字段）====================
    
    /// 用户 ID（对应 User.id）
    /// 格式: usr_xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    static std::string UserId() {
        return "usr_" + Generate();
    }
    
    /// 认证 Token（对应 AuthenticateResponse.token）
    /// 格式: 紧凑型，便于传输
    static std::string Token() {
        return GenerateCompact();
    }
    
    /// Session ID（如需会话管理）
    /// 格式: sess_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    static std::string SessionId() {
        return "sess_" + GenerateCompact();
    }
    
    // ==================== 工具方法 ====================
    
    /// 验证 UUID 格式是否正确
    static bool IsValid(const std::string& str) {
        std::string uuid_part = ExtractUUID(str);
        
        // 标准格式 36 字符
        if (uuid_part.length() == 36) {
            uuid_t tmp;
            return uuid_parse(uuid_part.c_str(), tmp) == 0;
        }
        
        // 紧凑格式 32 字符
        if (uuid_part.length() == 32) {
            for (char c : uuid_part) {
                if (!std::isxdigit(static_cast<unsigned char>(c))) {
                    return false;
                }
            }
            return true;
        }
        
        return false;
    }
    
    /// 从带前缀的 ID 中提取纯 UUID 部分
    /// "usr_xxxx-xxxx" → "xxxx-xxxx"
    static std::string ExtractUUID(const std::string& prefixed_id) {
        size_t pos = prefixed_id.find('_');
        if (pos != std::string::npos && pos + 1 < prefixed_id.length()) {
            return prefixed_id.substr(pos + 1);
        }
        return prefixed_id;
    }
    
    /// 判断 ID 类型
    enum class IDType { USER, SESSION, TOKEN, UNKNOWN };
    
    static IDType GetIDType(const std::string& id) {
        if (id.compare(0, 4, "usr_") == 0)  return IDType::USER;
        if (id.compare(0, 5, "sess_") == 0) return IDType::SESSION;
        if (id.length() == 32 && IsValid(id)) return IDType::TOKEN;
        return IDType::UNKNOWN;
    }
};

} // namespace user_servic