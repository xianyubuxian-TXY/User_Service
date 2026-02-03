#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "auth/jwt_service.h"
#include "common/error_codes.h"

using namespace user_service;

// ============================================================================
// 测试夹具
// ============================================================================
class JwtServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        SecurityConfig config;
        config.jwt_secret = "test-secret-key-32-bytes-long!!!";
        config.jwt_issuer = "test-issuer";
        config.access_token_ttl_seconds = 3600;      // 1 小时
        config.refresh_token_ttl_seconds = 604800;   // 7 天
        
        jwt_service_ = std::make_unique<JwtService>(config);
    }
    
    UserEntity MakeUser(int64_t id = 123,
                        const std::string& uuid = "test-uuid-123",
                        const std::string& mobile = "13800138000") {
        UserEntity user;
        user.id = id;
        user.uuid = uuid;
        user.mobile = mobile;
        user.role = UserRole::User;
        return user;
    }
    
    std::unique_ptr<JwtService> jwt_service_;
};

// ============================================================================
// GenerateTokenPair 测试
// ============================================================================

TEST_F(JwtServiceTest, GenerateTokenPair_Success) {
    auto user = MakeUser();
    auto tokens = jwt_service_->GenerateTokenPair(user);
    
    EXPECT_FALSE(tokens.access_token.empty());
    EXPECT_FALSE(tokens.refresh_token.empty());
    EXPECT_EQ(tokens.expires_in, 3600);
    
    // Access Token 和 Refresh Token 应该不同
    EXPECT_NE(tokens.access_token, tokens.refresh_token);
}

TEST_F(JwtServiceTest, GenerateTokenPair_JwtFormat) {
    auto user = MakeUser();
    auto tokens = jwt_service_->GenerateTokenPair(user);
    
    // JWT 格式：Header.Payload.Signature（两个点）
    auto check_jwt_format = [](const std::string& token) {
        size_t dot1 = token.find('.');
        size_t dot2 = token.find('.', dot1 + 1);
        return dot1 != std::string::npos && 
               dot2 != std::string::npos &&
               dot1 > 0 &&
               dot2 > dot1 + 1 &&
               token.size() > dot2 + 1;
    };
    
    EXPECT_TRUE(check_jwt_format(tokens.access_token));
    EXPECT_TRUE(check_jwt_format(tokens.refresh_token));
}

TEST_F(JwtServiceTest, GenerateTokenPair_Unique) {
    auto user = MakeUser();
    
    // 连续生成多个 token pair，应该不同（因为有 jti 随机数）
    auto tokens1 = jwt_service_->GenerateTokenPair(user);
    auto tokens2 = jwt_service_->GenerateTokenPair(user);
    
    EXPECT_NE(tokens1.access_token, tokens2.access_token);
    EXPECT_NE(tokens1.refresh_token, tokens2.refresh_token);
}

TEST_F(JwtServiceTest, GenerateTokenPair_DifferentUsers) {
    auto user1 = MakeUser(1, "uuid-1", "13800000001");
    auto user2 = MakeUser(2, "uuid-2", "13800000002");
    
    auto tokens1 = jwt_service_->GenerateTokenPair(user1);
    auto tokens2 = jwt_service_->GenerateTokenPair(user2);
    
    // 不同用户的 token 应该不同
    EXPECT_NE(tokens1.access_token, tokens2.access_token);
    EXPECT_NE(tokens1.refresh_token, tokens2.refresh_token);
}

// ============================================================================
// VerifyAccessToken 测试
// ============================================================================

TEST_F(JwtServiceTest, VerifyAccessToken_Success) {
    auto user = MakeUser(123, "uuid-abc", "13800138000");
    user.role = UserRole::Admin;
    auto tokens = jwt_service_->GenerateTokenPair(user);
    
    auto result = jwt_service_->VerifyAccessToken(tokens.access_token);
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().user_id, 123);
    EXPECT_EQ(result.Value().user_uuid, "uuid-abc");
    EXPECT_EQ(result.Value().mobile, "13800138000");
    EXPECT_EQ(result.Value().role, UserRole::Admin);
}

TEST_F(JwtServiceTest, VerifyAccessToken_AllRoles) {
    std::vector<UserRole> roles = {
        UserRole::User, 
        UserRole::Admin, 
        UserRole::SuperAdmin
    };
    
    for (auto role : roles) {
        auto user = MakeUser();
        user.role = role;
        auto tokens = jwt_service_->GenerateTokenPair(user);
        
        auto result = jwt_service_->VerifyAccessToken(tokens.access_token);
        
        EXPECT_TRUE(result.IsOk());
        EXPECT_EQ(result.Value().role, role);
    }
}

TEST_F(JwtServiceTest, VerifyAccessToken_EmptyToken) {
    auto result = jwt_service_->VerifyAccessToken("");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenMissing);
}

TEST_F(JwtServiceTest, VerifyAccessToken_InvalidFormat_NoDelimiter) {
    auto result = jwt_service_->VerifyAccessToken("not-a-jwt");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, VerifyAccessToken_InvalidFormat_OnlyOneDelimiter) {
    auto result = jwt_service_->VerifyAccessToken("header.payload");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, VerifyAccessToken_InvalidSignature) {
    auto user = MakeUser();
    auto tokens = jwt_service_->GenerateTokenPair(user);
    
    // 篡改 token（修改最后一个字符）
    std::string tampered = tokens.access_token;
    tampered.back() = (tampered.back() == 'a') ? 'b' : 'a';
    
    auto result = jwt_service_->VerifyAccessToken(tampered);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, VerifyAccessToken_WrongSecret) {
    // 使用不同密钥的 JwtService 验证
    SecurityConfig other_config;
    other_config.jwt_secret = "different-secret-key-32-bytes!!";
    other_config.jwt_issuer = "test-issuer";
    other_config.access_token_ttl_seconds = 3600;
    other_config.refresh_token_ttl_seconds = 604800;
    
    JwtService other_service(other_config);
    
    auto user = MakeUser();
    auto tokens = jwt_service_->GenerateTokenPair(user);
    
    // 用不同密钥验证
    auto result = other_service.VerifyAccessToken(tokens.access_token);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, VerifyAccessToken_WrongIssuer) {
    SecurityConfig other_config;
    other_config.jwt_secret = "test-secret-key-32-bytes-long!!!";
    other_config.jwt_issuer = "different-issuer";
    other_config.access_token_ttl_seconds = 3600;
    other_config.refresh_token_ttl_seconds = 604800;
    
    JwtService other_service(other_config);
    
    auto user = MakeUser();
    auto tokens = jwt_service_->GenerateTokenPair(user);
    
    auto result = other_service.VerifyAccessToken(tokens.access_token);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, VerifyAccessToken_ExpiredToken) {
    // 创建一个过期时间很短的服务
    SecurityConfig config;
    config.jwt_secret = "test-secret-key-32-bytes-long!!!";
    config.jwt_issuer = "test-issuer";
    config.access_token_ttl_seconds = 0;  // 立即过期
    config.refresh_token_ttl_seconds = 604800;
    
    JwtService short_lived_service(config);
    
    auto user = MakeUser();
    auto tokens = short_lived_service.GenerateTokenPair(user);
    
    // 等待一秒确保过期
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    auto result = short_lived_service.VerifyAccessToken(tokens.access_token);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenExpired);
}

TEST_F(JwtServiceTest, VerifyAccessToken_RefreshTokenRejected) {
    auto user = MakeUser();
    auto tokens = jwt_service_->GenerateTokenPair(user);
    
    // 用 VerifyAccessToken 验证 Refresh Token，应该失败（类型不匹配）
    auto result = jwt_service_->VerifyAccessToken(tokens.refresh_token);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

// ============================================================================
// ParseRefreshToken 测试
// ============================================================================

TEST_F(JwtServiceTest, ParseRefreshToken_Success) {
    auto user = MakeUser(456, "uuid-xyz", "13900139000");
    auto tokens = jwt_service_->GenerateTokenPair(user);
    
    auto result = jwt_service_->ParseRefreshToken(tokens.refresh_token);
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value(), "456");  // 返回的是数据库 ID（字符串）
}

TEST_F(JwtServiceTest, ParseRefreshToken_EmptyToken) {
    auto result = jwt_service_->ParseRefreshToken("");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenMissing);
}

TEST_F(JwtServiceTest, ParseRefreshToken_AccessTokenRejected) {
    auto user = MakeUser();
    auto tokens = jwt_service_->GenerateTokenPair(user);
    
    // 用 ParseRefreshToken 解析 Access Token，应该失败（类型不匹配）
    auto result = jwt_service_->ParseRefreshToken(tokens.access_token);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, ParseRefreshToken_ExpiredToken) {
    SecurityConfig config;
    config.jwt_secret = "test-secret-key-32-bytes-long!!!";
    config.jwt_issuer = "test-issuer";
    config.access_token_ttl_seconds = 3600;
    config.refresh_token_ttl_seconds = 0;  // 立即过期
    
    JwtService short_lived_service(config);
    
    auto user = MakeUser();
    auto tokens = short_lived_service.GenerateTokenPair(user);
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    auto result = short_lived_service.ParseRefreshToken(tokens.refresh_token);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenExpired);
}

TEST_F(JwtServiceTest, ParseRefreshToken_InvalidSignature) {
    auto user = MakeUser();
    auto tokens = jwt_service_->GenerateTokenPair(user);
    
    std::string tampered = tokens.refresh_token;
    tampered.back() = (tampered.back() == 'x') ? 'y' : 'x';
    
    auto result = jwt_service_->ParseRefreshToken(tampered);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

// ============================================================================
// HashToken 测试
// ============================================================================

TEST_F(JwtServiceTest, HashToken_Deterministic) {
    std::string token = "some-token-string";
    
    auto hash1 = jwt_service_->HashToken(token);
    auto hash2 = jwt_service_->HashToken(token);
    
    EXPECT_EQ(hash1, hash2);  // 相同输入，相同输出
}

TEST_F(JwtServiceTest, HashToken_Different) {
    std::string token1 = "token-1";
    std::string token2 = "token-2";
    
    auto hash1 = jwt_service_->HashToken(token1);
    auto hash2 = jwt_service_->HashToken(token2);
    
    EXPECT_NE(hash1, hash2);  // 不同输入，不同输出
}

TEST_F(JwtServiceTest, HashToken_Length) {
    auto hash = jwt_service_->HashToken("any-token");
    
    // SHA256 = 32 字节 = 64 hex 字符
    EXPECT_EQ(hash.length(), 64u);
}

TEST_F(JwtServiceTest, HashToken_HexFormat) {
    auto hash = jwt_service_->HashToken("test");
    
    // 应该只包含十六进制字符
    for (char c : hash) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            << "Invalid hex character: " << c;
    }
}

TEST_F(JwtServiceTest, HashToken_EmptyInput) {
    auto hash = jwt_service_->HashToken("");
    
    // 空字符串也应该能哈希
    EXPECT_EQ(hash.length(), 64u);
}

TEST_F(JwtServiceTest, HashToken_RealToken) {
    auto user = MakeUser();
    auto tokens = jwt_service_->GenerateTokenPair(user);
    
    // 对真实 token 进行哈希
    auto hash = jwt_service_->HashToken(tokens.refresh_token);
    
    EXPECT_EQ(hash.length(), 64u);
    
    // 相同 token 哈希结果相同
    auto hash2 = jwt_service_->HashToken(tokens.refresh_token);
    EXPECT_EQ(hash, hash2);
}

// ============================================================================
// 端到端场景测试
// ============================================================================

TEST_F(JwtServiceTest, E2E_GenerateVerifyAccessToken) {
    auto user = MakeUser(999, "user-uuid-999", "13912345678");
    user.role = UserRole::SuperAdmin;
    
    // 生成
    auto tokens = jwt_service_->GenerateTokenPair(user);
    
    // 验证
    auto result = jwt_service_->VerifyAccessToken(tokens.access_token);
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().user_id, 999);
    EXPECT_EQ(result.Value().user_uuid, "user-uuid-999");
    EXPECT_EQ(result.Value().mobile, "13912345678");
    EXPECT_EQ(result.Value().role, UserRole::SuperAdmin);
}

TEST_F(JwtServiceTest, E2E_GenerateParseRefreshToken) {
    auto user = MakeUser(888, "user-uuid-888", "13888888888");
    
    // 生成
    auto tokens = jwt_service_->GenerateTokenPair(user);
    
    // 解析
    auto result = jwt_service_->ParseRefreshToken(tokens.refresh_token);
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value(), "888");  // 返回用户 ID
}

TEST_F(JwtServiceTest, E2E_TokenTypeValidation) {
    auto user = MakeUser();
    auto tokens = jwt_service_->GenerateTokenPair(user);
    
    // Access Token 只能用 VerifyAccessToken 验证
    EXPECT_TRUE(jwt_service_->VerifyAccessToken(tokens.access_token).IsOk());
    EXPECT_FALSE(jwt_service_->ParseRefreshToken(tokens.access_token).IsOk());
    
    // Refresh Token 只能用 ParseRefreshToken 解析
    EXPECT_TRUE(jwt_service_->ParseRefreshToken(tokens.refresh_token).IsOk());
    EXPECT_FALSE(jwt_service_->VerifyAccessToken(tokens.refresh_token).IsOk());
}

// ============================================================================
// 边界情况测试
// ============================================================================

TEST_F(JwtServiceTest, UserWithSpecialCharacters) {
    auto user = MakeUser();
    user.uuid = "uuid-with-special-\"chars\"";
    user.mobile = "138\"001\\380'00";  // 包含特殊字符
    
    auto tokens = jwt_service_->GenerateTokenPair(user);
    auto result = jwt_service_->VerifyAccessToken(tokens.access_token);
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().mobile, user.mobile);
    EXPECT_EQ(result.Value().user_uuid, user.uuid);
}

TEST_F(JwtServiceTest, UserWithEmptyMobile) {
    auto user = MakeUser();
    user.mobile = "";
    
    auto tokens = jwt_service_->GenerateTokenPair(user);
    auto result = jwt_service_->VerifyAccessToken(tokens.access_token);
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().mobile, "");
}

TEST_F(JwtServiceTest, UserWithLongUuid) {
    auto user = MakeUser();
    user.uuid = std::string(256, 'a');  // 很长的 UUID
    
    auto tokens = jwt_service_->GenerateTokenPair(user);
    auto result = jwt_service_->VerifyAccessToken(tokens.access_token);
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().user_uuid, user.uuid);
}

TEST_F(JwtServiceTest, UserWithUnicodeCharacters) {
    auto user = MakeUser();
    user.uuid = "用户-测试-🚀";  // Unicode 字符
    
    auto tokens = jwt_service_->GenerateTokenPair(user);
    auto result = jwt_service_->VerifyAccessToken(tokens.access_token);
    
    EXPECT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().user_uuid, user.uuid);
}
