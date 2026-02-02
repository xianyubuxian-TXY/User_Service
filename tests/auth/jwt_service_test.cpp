// tests/auth/test_jwt_service.cpp

#include <gtest/gtest.h>
#include "auth/jwt_service.h"
#include "config/config.h"
#include "common/error_codes.h"
#include <thread>
#include <chrono>

namespace user_service {
namespace test {

// ==================== 测试夹具 ====================
class JwtServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.jwt_secret = "test-secret-key-12345";
        config_.jwt_issuer = "test-service";
        config_.access_token_ttl_seconds = 900;      // 15分钟
        config_.refresh_token_ttl_seconds = 604800;  // 7天
        
        jwt_service_ = std::make_unique<JwtService>(config_);
    }
    
    SecurityConfig config_;
    std::unique_ptr<JwtService> jwt_service_;
};

// ==================== GenerateTokenPair 测试 ====================

TEST_F(JwtServiceTest, GenerateTokenPair_ReturnsValidTokens) {
    std::string user_id = "user_123";
    
    auto token_pair = jwt_service_->GenerateTokenPair(user_id);
    
    // 验证返回的 token 不为空
    EXPECT_FALSE(token_pair.access_token.empty());
    EXPECT_FALSE(token_pair.refresh_token.empty());
    EXPECT_EQ(token_pair.expires_in, config_.access_token_ttl_seconds);
}

TEST_F(JwtServiceTest, GenerateTokenPair_DifferentUsersGetDifferentTokens) {
    auto pair1 = jwt_service_->GenerateTokenPair("user_1");
    auto pair2 = jwt_service_->GenerateTokenPair("user_2");
    
    EXPECT_NE(pair1.access_token, pair2.access_token);
    EXPECT_NE(pair1.refresh_token, pair2.refresh_token);
}

TEST_F(JwtServiceTest, GenerateTokenPair_SameUserGetsDifferentTokensEachTime) {
    auto pair1 = jwt_service_->GenerateTokenPair("user_123");
    
    // 等待1秒确保时间戳不同
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    auto pair2 = jwt_service_->GenerateTokenPair("user_123");
    
    // 即使是同一用户，不同时间生成的 token 也不同
    EXPECT_NE(pair1.access_token, pair2.access_token);
    EXPECT_NE(pair1.refresh_token, pair2.refresh_token);
}

// ==================== VerifyAccessToken 测试 ====================

TEST_F(JwtServiceTest, VerifyAccessToken_ValidToken_ReturnsPayload) {
    std::string user_id = "user_456";
    auto token_pair = jwt_service_->GenerateTokenPair(user_id);
    
    auto result = jwt_service_->VerifyAccessToken(token_pair.access_token);
    
    ASSERT_TRUE(result.Success());
    ASSERT_TRUE(result.data.has_value());
    EXPECT_EQ(result.data.value().user_id, user_id);
    
    // 验证过期时间在合理范围内
    auto now = std::chrono::system_clock::now();
    auto expected_exp = now + std::chrono::seconds(config_.access_token_ttl_seconds);
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(
        result.data.value().expires_at - expected_exp).count();
    EXPECT_LE(std::abs(diff), 2);  // 允许2秒误差
}

TEST_F(JwtServiceTest, VerifyAccessToken_InvalidToken_ReturnsFail) {
    auto result = jwt_service_->VerifyAccessToken("invalid.token.here");
    
    EXPECT_TRUE(result.Failure());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, VerifyAccessToken_TamperedToken_ReturnsFail) {
    auto token_pair = jwt_service_->GenerateTokenPair("user_123");
    
    // 篡改 token
    std::string tampered = token_pair.access_token;
    if (!tampered.empty()) {
        tampered[tampered.size() / 2] = 'X';
    }
    
    auto result = jwt_service_->VerifyAccessToken(tampered);
    
    EXPECT_TRUE(result.Failure());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, VerifyAccessToken_RefreshTokenUsedAsAccess_ReturnsFail) {
    auto token_pair = jwt_service_->GenerateTokenPair("user_123");
    
    // 用 refresh_token 当 access_token 验证，应该失败
    auto result = jwt_service_->VerifyAccessToken(token_pair.refresh_token);
    
    EXPECT_TRUE(result.Failure());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, VerifyAccessToken_WrongSecret_ReturnsFail) {
    auto token_pair = jwt_service_->GenerateTokenPair("user_123");
    
    // 创建使用不同密钥的 JwtService
    SecurityConfig other_config = config_;
    other_config.jwt_secret = "different-secret-key";
    JwtService other_service(other_config);
    
    auto result = other_service.VerifyAccessToken(token_pair.access_token);
    
    EXPECT_TRUE(result.Failure());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, VerifyAccessToken_WrongIssuer_ReturnsFail) {
    auto token_pair = jwt_service_->GenerateTokenPair("user_123");
    
    // 创建使用不同 issuer 的 JwtService
    SecurityConfig other_config = config_;
    other_config.jwt_issuer = "other-service";
    JwtService other_service(other_config);
    
    auto result = other_service.VerifyAccessToken(token_pair.access_token);
    
    EXPECT_TRUE(result.Failure());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, VerifyAccessToken_EmptyToken_ReturnsFail) {
    auto result = jwt_service_->VerifyAccessToken("");
    
    EXPECT_TRUE(result.Failure());
    EXPECT_EQ(result.code, ErrorCode::TokenMissing);
}

TEST_F(JwtServiceTest, VerifyAccessToken_MalformedToken_ReturnsFail) {
    // 缺少分隔符
    auto result1 = jwt_service_->VerifyAccessToken("nodots");
    EXPECT_TRUE(result1.Failure());
    EXPECT_EQ(result1.code, ErrorCode::TokenInvalid);
    
    // 只有一个分隔符
    auto result2 = jwt_service_->VerifyAccessToken("one.dot");
    EXPECT_TRUE(result2.Failure());
    EXPECT_EQ(result2.code, ErrorCode::TokenInvalid);
    
    // 空段
    auto result3 = jwt_service_->VerifyAccessToken("..");
    EXPECT_TRUE(result3.Failure());
    EXPECT_EQ(result3.code, ErrorCode::TokenInvalid);
}

// ==================== ParseRefreshToken 测试 ====================

TEST_F(JwtServiceTest, ParseRefreshToken_ValidToken_ReturnsUserId) {
    std::string user_id = "user_789";
    auto token_pair = jwt_service_->GenerateTokenPair(user_id);
    
    auto result = jwt_service_->ParseRefreshToken(token_pair.refresh_token);
    
    ASSERT_TRUE(result.Success());
    ASSERT_TRUE(result.data.has_value());
    EXPECT_EQ(result.data.value(), user_id);
}

TEST_F(JwtServiceTest, ParseRefreshToken_InvalidToken_ReturnsFail) {
    auto result = jwt_service_->ParseRefreshToken("invalid.refresh.token");
    
    EXPECT_TRUE(result.Failure());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, ParseRefreshToken_AccessTokenUsedAsRefresh_ReturnsFail) {
    auto token_pair = jwt_service_->GenerateTokenPair("user_123");
    
    // 用 access_token 当 refresh_token 解析，应该失败
    auto result = jwt_service_->ParseRefreshToken(token_pair.access_token);
    
    EXPECT_TRUE(result.Failure());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, ParseRefreshToken_EmptyToken_ReturnsFail) {
    auto result = jwt_service_->ParseRefreshToken("");
    
    EXPECT_TRUE(result.Failure());
    EXPECT_EQ(result.code, ErrorCode::TokenMissing);
}

// ==================== HashToken 测试 ====================

TEST_F(JwtServiceTest, HashToken_ProducesSha256Hex) {
    std::string token = "some-token-value";
    
    auto hash = JwtService::HashToken(token);
    
    // SHA256 哈希为 64 个十六进制字符
    EXPECT_EQ(hash.size(), 64);
    
    // 验证是有效的十六进制字符串
    for (char c : hash) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TEST_F(JwtServiceTest, HashToken_SameInput_SameOutput) {
    std::string token = "test-token";
    
    auto hash1 = JwtService::HashToken(token);
    auto hash2 = JwtService::HashToken(token);
    
    EXPECT_EQ(hash1, hash2);
}

TEST_F(JwtServiceTest, HashToken_DifferentInput_DifferentOutput) {
    auto hash1 = JwtService::HashToken("token1");
    auto hash2 = JwtService::HashToken("token2");
    
    EXPECT_NE(hash1, hash2);
}

TEST_F(JwtServiceTest, HashToken_EmptyInput) {
    auto hash = JwtService::HashToken("");
    
    EXPECT_EQ(hash.size(), 64);
    // 空字符串的 SHA256 哈希是已知的
    EXPECT_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

// ==================== 过期测试（使用短 TTL）====================

class JwtServiceExpirationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.jwt_secret = "test-secret";
        config_.jwt_issuer = "test-service";
        config_.access_token_ttl_seconds = 1;   // 1秒过期
        config_.refresh_token_ttl_seconds = 2;  // 2秒过期
        
        jwt_service_ = std::make_unique<JwtService>(config_);
    }
    
    SecurityConfig config_;
    std::unique_ptr<JwtService> jwt_service_;
};

TEST_F(JwtServiceExpirationTest, VerifyAccessToken_ExpiredToken_ReturnsFail) {
    auto token_pair = jwt_service_->GenerateTokenPair("user_123");
    
    // 验证 token 当前有效
    auto result_before = jwt_service_->VerifyAccessToken(token_pair.access_token);
    EXPECT_TRUE(result_before.Success());
    
    // 等待过期
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 验证 token 已过期
    auto result_after = jwt_service_->VerifyAccessToken(token_pair.access_token);
    EXPECT_TRUE(result_after.Failure());
    EXPECT_EQ(result_after.code, ErrorCode::TokenExpired);
}

TEST_F(JwtServiceExpirationTest, ParseRefreshToken_ExpiredToken_ReturnsFail) {
    auto token_pair = jwt_service_->GenerateTokenPair("user_123");
    
    // 验证 token 当前有效
    auto result_before = jwt_service_->ParseRefreshToken(token_pair.refresh_token);
    EXPECT_TRUE(result_before.Success());
    
    // 等待过期
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // 验证 token 已过期
    auto result_after = jwt_service_->ParseRefreshToken(token_pair.refresh_token);
    EXPECT_TRUE(result_after.Failure());
    EXPECT_EQ(result_after.code, ErrorCode::TokenExpired);
}

// ==================== 边界情况测试 ====================

TEST_F(JwtServiceTest, GenerateTokenPair_SpecialCharactersInUserId) {
    std::string user_id = "user-with_special.chars@123";
    
    auto token_pair = jwt_service_->GenerateTokenPair(user_id);
    auto result = jwt_service_->VerifyAccessToken(token_pair.access_token);
    
    ASSERT_TRUE(result.Success());
    EXPECT_EQ(result.data.value().user_id, user_id);
}

TEST_F(JwtServiceTest, GenerateTokenPair_UnicodeUserId) {
    std::string user_id = "用户_123";
    
    auto token_pair = jwt_service_->GenerateTokenPair(user_id);
    auto result = jwt_service_->VerifyAccessToken(token_pair.access_token);
    
    ASSERT_TRUE(result.Success());
    EXPECT_EQ(result.data.value().user_id, user_id);
}

TEST_F(JwtServiceTest, GenerateTokenPair_LongUserId) {
    std::string user_id(1000, 'x');  // 1000字符
    
    auto token_pair = jwt_service_->GenerateTokenPair(user_id);
    auto result = jwt_service_->VerifyAccessToken(token_pair.access_token);
    
    ASSERT_TRUE(result.Success());
    EXPECT_EQ(result.data.value().user_id, user_id);
}

TEST_F(JwtServiceTest, GenerateTokenPair_EmptyUserId) {
    std::string user_id = "";
    
    auto token_pair = jwt_service_->GenerateTokenPair(user_id);
    auto result = jwt_service_->VerifyAccessToken(token_pair.access_token);
    
    // 空 user_id 生成的 token 验证时应该失败（缺少用户标识）
    EXPECT_TRUE(result.Failure());
    EXPECT_EQ(result.code, ErrorCode::TokenInvalid);
}

// ==================== 错误码验证测试 ====================

TEST_F(JwtServiceTest, VerifyAccessToken_ReturnsCorrectErrorCodes) {
    // TokenMissing: 空 token
    auto empty_result = jwt_service_->VerifyAccessToken("");
    EXPECT_EQ(empty_result.code, ErrorCode::TokenMissing);
    
    // TokenInvalid: 格式错误
    auto invalid_result = jwt_service_->VerifyAccessToken("not.valid.jwt");
    EXPECT_EQ(invalid_result.code, ErrorCode::TokenInvalid);
    
    // TokenInvalid: 类型不匹配（用 refresh token 验证）
    auto token_pair = jwt_service_->GenerateTokenPair("user_123");
    auto type_mismatch = jwt_service_->VerifyAccessToken(token_pair.refresh_token);
    EXPECT_EQ(type_mismatch.code, ErrorCode::TokenInvalid);
}

TEST_F(JwtServiceTest, ParseRefreshToken_ReturnsCorrectErrorCodes) {
    // TokenMissing: 空 token
    auto empty_result = jwt_service_->ParseRefreshToken("");
    EXPECT_EQ(empty_result.code, ErrorCode::TokenMissing);
    
    // TokenInvalid: 格式错误
    auto invalid_result = jwt_service_->ParseRefreshToken("not.valid.jwt");
    EXPECT_EQ(invalid_result.code, ErrorCode::TokenInvalid);
    
    // TokenInvalid: 类型不匹配（用 access token 解析）
    auto token_pair = jwt_service_->GenerateTokenPair("user_123");
    auto type_mismatch = jwt_service_->ParseRefreshToken(token_pair.access_token);
    EXPECT_EQ(type_mismatch.code, ErrorCode::TokenInvalid);
}

// ==================== Result 接口测试 ====================

TEST_F(JwtServiceTest, Result_BoolOperator_WorksCorrectly) {
    auto token_pair = jwt_service_->GenerateTokenPair("user_123");
    
    // 成功的 Result 应该转换为 true
    auto success_result = jwt_service_->VerifyAccessToken(token_pair.access_token);
    EXPECT_TRUE(static_cast<bool>(success_result));
    
    // 失败的 Result 应该转换为 false
    auto fail_result = jwt_service_->VerifyAccessToken("invalid");
    EXPECT_FALSE(static_cast<bool>(fail_result));
}

TEST_F(JwtServiceTest, Result_ErrorMessage_NotEmpty) {
    // 失败时应该有错误消息
    auto result = jwt_service_->VerifyAccessToken("");
    
    EXPECT_TRUE(result.Failure());
    EXPECT_FALSE(result.message.empty());
}

}  // namespace test
}  // namespace user_service
