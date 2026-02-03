#pragma once

#include <string>
#include <optional>
#include <chrono>
#include <map>
#include "common/auth_type.h"
#include "common/result.h"
#include "config/config.h"

// 认证模块命名空间
namespace user_service {
// JWT核心服务类：提供双令牌生成、验证、解析、哈希等核心功能
class JwtService {
public:
    virtual ~JwtService()=default; 

    // 构造函数：传入JWT配置初始化服务
    explicit JwtService(const SecurityConfig& config);
    
    // 生成双令牌对：根据用户ID，生成access_token+refresh_token
    virtual TokenPair GenerateTokenPair(const UserEntity& user);
    
    // 验证访问令牌：校验token有效性，有效则返回解析后的载荷，无效返回错误
    virtual Result<AccessTokenPayload> VerifyAccessToken(const std::string& token);
    
    // 解析刷新令牌：校验refresh_token有效性，有效则返回关联的用户ID，无效返回错误
    virtual Result<std::string> ParseRefreshToken(const std::string& token);
    
    // 令牌哈希：对原始令牌做哈希处理（用于服务端存储，避免明文保存令牌）
    static std::string HashToken(const std::string& token);

private:
    SecurityConfig config_; // 私有配置：存储JWT全局配置
    
    // 生成访问令牌（私有）：内部调用，单独生成access_token
    std::string GenerateAccessToken(const UserEntity& user);
    // 生成刷新令牌（私有）：内部调用，单独生成refresh_token
    std::string GenerateRefreshToken(const UserEntity& user);
    
    // JWT核心工具函数（静态私有）：供内部调用，封装JWT基础操作
    static std::string Base64UrlEncode(const std::string& input); // Base64Url编码（JWT专用，兼容URL）
    static std::string Base64UrlDecode(const std::string& input); // Base64Url解码
    static std::string HmacSha256(const std::string& key, const std::string& data); // HmacSha256签名（JWT验签核心）
    static std::string CreateJwt(const std::map<std::string, std::string>& claims, 
                                  const std::string& secret); // 生成JWT令牌：传入载荷和秘钥
    
    // 验证JWT令牌结果
    struct JwtVerifyResult {
        bool success = false;
        ErrorCode error_code = ErrorCode::Ok;
        std::map<std::string, std::string> claims;
    };
    
    static JwtVerifyResult VerifyJwt(
        const std::string& token, const std::string& secret); // 验证JWT令牌：校验签名+过期，返回解析后的载荷
};

} // namespace user_service
