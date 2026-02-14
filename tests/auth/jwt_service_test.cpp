// tests/auth/jwt_service_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>

#include "auth/jwt_service.h"
#include "mock_auth_deps.h"

namespace user_service {
namespace testing {

class JwtServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = CreateTestSecurityConfig();
        jwt_service_ = std::make_unique<JwtService>(config_);
    }

    SecurityConfig config_;
    std::unique_ptr<JwtService> jwt_service_;
};

// ============================================================================
// Token 生成测试
// ============================================================================

TEST_F(JwtServiceTest, GenerateTokenPair_Success) {
    UserEntity user = CreateTestUser();
    
    TokenPair tokens = jwt_service_->GenerateTokenPair(user);
    
    // 验证 token 不为空
    EXPECT_FALSE(tokens.access_token.empty());
    EXPECT_FALSE(tokens.refresh_token.empty());
    
    // 验证过期时间
    EXPECT_EQ(tokens.expires_in, config_.access_token_ttl_seconds);
    
    // 验证 JWT 格式（三段式：header.payload.signature）
    auto count_dots = [](const std::string& s) {
        return std::count(s.begin(), s.end(), '.');
    };
    EXPECT_EQ(count_dots(tokens.access_token), 2);
    EXPECT_EQ(count_dots(tokens.refresh_token), 2);
}

TEST_F(JwtServiceTest, GenerateTokenPair_DifferentUsersGetDifferentTokens) {
    UserEntity user1 = CreateTestUser(1, "uuid-1", "13800138001");
    UserEntity user2 = CreateTestUser(2, "uuid-2", "13800138002");
    
    TokenPair tokens1 = jwt_service_->GenerateTokenPair(user1);
    TokenPair tokens2 = jwt_service_->GenerateTokenPair(user2);
    
    EXPECT_NE(tokens1.access_token, tokens2.access_token);
    EXPECT_NE(tokens1.refresh_token, tokens2.refresh_token);
}

TEST_F(JwtServiceTest, GenerateTokenPair_SameUserDifferentTimes) {
    UserEntity user = CreateTestUser();
    
    TokenPair tokens1 = jwt_service_->GenerateTokenPair(user);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    TokenPair tokens2 = jwt_service_->GenerateTokenPair(user);
    
    // 即使同一用户，不同时间生成的 token 也应该不同（因为有 jti）
    EXPECT_NE(tokens1.access_token, tokens2.access_token);
    EXPECT_NE(tokens1.refresh_token, tokens2.refresh_token);
}

// ============================================================================
// Access Token 验证测试
// ============================================================================

TEST_F(JwtServiceTest, VerifyAccessToken_ValidToken) {
    UserEntity user = CreateTestUser(123, "uuid-abc-123", "13800138000");
    user.role = UserRole::Admin;
    
    TokenPair tokens = jwt_service_->GenerateTokenPair(user);
    
    auto result = jwt_service_->VerifyAccessToken(tokens.access_token);
    
    ASSERT_TRUE(result.IsOk()) << "Error: " << result.message;
    EXPECT_EQ(result.Value().user_id, user.id);
    EXPECT_EQ(result.Value().user_uuid, user.uuid);
    EXPECT_EQ(result.Value().mobile, user.mobile);
    EXPECT_EQ(result.Value().role, UserRole::Admin);
}

TEST_F(JwtServiceTest, VerifyAccessToken_EmptyToken) {
    auto result = jwt_service_->VerifyAccessToken("");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenMissing);
}

TEST_F(JwtServiceTest, VerifyAccessToken_InvalidFormat) {
    auto result = jwt_service_->VerifyAccessToken("invalid_token_without_dots");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, VerifyAccessToken_TamperedSignature) {
    UserEntity user = CreateTestUser();
    TokenPair tokens = jwt_service_->GenerateTokenPair(user);
    
    // 篡改签名（修改最后几个字符）
    std::string tampered = tokens.access_token;
    if (tampered.length() > 10) {
        tampered[tampered.length() - 5] = 'X';
        tampered[tampered.length() - 3] = 'Y';
    }
    
    auto result = jwt_service_->VerifyAccessToken(tampered);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, VerifyAccessToken_WrongSecret) {
    UserEntity user = CreateTestUser();
    TokenPair tokens = jwt_service_->GenerateTokenPair(user);
    
    // 使用不同密钥的 JwtService 验证
    SecurityConfig other_config = config_;
    other_config.jwt_secret = "different-secret-key-32-bytes!!!!!";
    JwtService other_service(other_config);
    
    auto result = other_service.VerifyAccessToken(tokens.access_token);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, VerifyAccessToken_ExpiredToken) {
    // 创建一个过期时间极短的配置
    SecurityConfig short_config = config_;
    short_config.access_token_ttl_seconds = 1;  // 1 秒
    JwtService short_service(short_config);
    
    UserEntity user = CreateTestUser();
    TokenPair tokens = short_service.GenerateTokenPair(user);
    
    // 等待 token 过期
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    auto result = short_service.VerifyAccessToken(tokens.access_token);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenExpired);
}

TEST_F(JwtServiceTest, VerifyAccessToken_RefreshTokenAsAccessToken) {
    UserEntity user = CreateTestUser();
    TokenPair tokens = jwt_service_->GenerateTokenPair(user);
    
    // 用 refresh_token 作为 access_token 验证
    auto result = jwt_service_->VerifyAccessToken(tokens.refresh_token);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

// ============================================================================
// Refresh Token 解析测试
// ============================================================================

TEST_F(JwtServiceTest, ParseRefreshToken_ValidToken) {
    UserEntity user = CreateTestUser(456, "uuid-456", "13800138000");
    
    TokenPair tokens = jwt_service_->GenerateTokenPair(user);
    
    auto result = jwt_service_->ParseRefreshToken(tokens.refresh_token);
    
    ASSERT_TRUE(result.IsOk()) << "Error: " << result.message;
    EXPECT_EQ(result.Value(), std::to_string(user.id));
}

TEST_F(JwtServiceTest, ParseRefreshToken_EmptyToken) {
    auto result = jwt_service_->ParseRefreshToken("");
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenMissing);
}

TEST_F(JwtServiceTest, ParseRefreshToken_AccessTokenAsRefreshToken) {
    UserEntity user = CreateTestUser();
    TokenPair tokens = jwt_service_->GenerateTokenPair(user);
    
    // 用 access_token 作为 refresh_token 解析
    auto result = jwt_service_->ParseRefreshToken(tokens.access_token);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, ParseRefreshToken_ExpiredToken) {
    SecurityConfig short_config = config_;
    short_config.refresh_token_ttl_seconds = 1;  // 1 秒
    JwtService short_service(short_config);
    
    UserEntity user = CreateTestUser();
    TokenPair tokens = short_service.GenerateTokenPair(user);
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    auto result = short_service.ParseRefreshToken(tokens.refresh_token);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.code, ErrorCode::TokenExpired);
}

// ============================================================================
// Token 哈希测试
// ============================================================================

TEST_F(JwtServiceTest, HashToken_Consistency) {
    std::string token = "test_token_value";
    
    std::string hash1 = JwtService::HashToken(token);
    std::string hash2 = JwtService::HashToken(token);
    
    EXPECT_EQ(hash1, hash2);
}

TEST_F(JwtServiceTest, HashToken_DifferentInputs) {
    std::string token1 = "token_1";
    std::string token2 = "token_2";
    
    std::string hash1 = JwtService::HashToken(token1);
    std::string hash2 = JwtService::HashToken(token2);
    
    EXPECT_NE(hash1, hash2);
}

TEST_F(JwtServiceTest, HashToken_OutputFormat) {
    std::string token = "test_token";
    std::string hash = JwtService::HashToken(token);
    
    // SHA256 输出为 64 个十六进制字符
    EXPECT_EQ(hash.length(), 64);
    
    // 全部为十六进制字符
    for (char c : hash) {
        EXPECT_TRUE(std::isxdigit(c));
    }
}

TEST_F(JwtServiceTest, HashToken_EmptyInput) {
    std::string hash = JwtService::HashToken("");
    
    EXPECT_EQ(hash.length(), 64);
}

// ============================================================================
// 用户角色测试
// ============================================================================

TEST_F(JwtServiceTest, TokenContainsUserRole) {
    UserEntity admin_user = CreateTestUser();
    admin_user.role = UserRole::Admin;
    
    TokenPair tokens = jwt_service_->GenerateTokenPair(admin_user);
    auto result = jwt_service_->VerifyAccessToken(tokens.access_token);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().role, UserRole::Admin);
}

TEST_F(JwtServiceTest, TokenContainsSuperAdminRole) {
    UserEntity super_admin = CreateTestUser();
    super_admin.role = UserRole::SuperAdmin;
    
    TokenPair tokens = jwt_service_->GenerateTokenPair(super_admin);
    auto result = jwt_service_->VerifyAccessToken(tokens.access_token);
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().role, UserRole::SuperAdmin);
}

}  // namespace testing
}  // namespace user_service
