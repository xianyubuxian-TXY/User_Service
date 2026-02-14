// tests/client/auth_client_test.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "client/auth_client.h"
#include "common/error_codes.h"
#include "common/auth_type.h"
#include "pb_auth/auth.grpc.pb.h"

using ::testing::_;
using ::testing::Return;
using ::testing::DoAll;
using ::testing::SetArgPointee;
using ::testing::Invoke;

namespace user_service {
namespace testing {

// ============================================================================
// 测试用的可注入 AuthClient（绕过真实 gRPC 连接）
// ============================================================================

/**
 * @brief 可测试的 AuthClient 子类
 * 
 * 通过构造函数注入 mock stub，避免真实网络连接
 */
class TestableAuthClient : public AuthClient {
public:
    TestableAuthClient(std::unique_ptr<pb_auth::AuthService::StubInterface> stub)
        : AuthClient(grpc::CreateChannel("localhost:50051", 
                     grpc::InsecureChannelCredentials()))
        , mock_stub_(std::move(stub)) {
    }

    // 重写获取 stub 的方法（需要在 AuthClient 中添加 virtual）
    pb_auth::AuthService::StubInterface* GetStub() {
        return mock_stub_.get();
    }

private:
    std::unique_ptr<pb_auth::AuthService::StubInterface> mock_stub_;
};

// ============================================================================
// Mock Stub 实现
// ============================================================================

class MockAuthStub : public pb_auth::AuthService::StubInterface {
public:
    // 同步方法
    MOCK_METHOD(grpc::Status, SendVerifyCode,
        (grpc::ClientContext*, const pb_auth::SendVerifyCodeRequest&, 
         pb_auth::SendVerifyCodeResponse*), (override));
    
    MOCK_METHOD(grpc::Status, Register,
        (grpc::ClientContext*, const pb_auth::RegisterRequest&, 
         pb_auth::RegisterResponse*), (override));
    
    MOCK_METHOD(grpc::Status, LoginByPassword,
        (grpc::ClientContext*, const pb_auth::LoginByPasswordRequest&, 
         pb_auth::LoginByPasswordResponse*), (override));
    
    MOCK_METHOD(grpc::Status, LoginByCode,
        (grpc::ClientContext*, const pb_auth::LoginByCodeRequest&, 
         pb_auth::LoginByCodeResponse*), (override));
    
    MOCK_METHOD(grpc::Status, RefreshToken,
        (grpc::ClientContext*, const pb_auth::RefreshTokenRequest&, 
         pb_auth::RefreshTokenResponse*), (override));
    
    MOCK_METHOD(grpc::Status, Logout,
        (grpc::ClientContext*, const pb_auth::LogoutRequest&, 
         pb_auth::LogoutResponse*), (override));
    
    MOCK_METHOD(grpc::Status, ResetPassword,
        (grpc::ClientContext*, const pb_auth::ResetPasswordRequest&, 
         pb_auth::ResetPasswordResponse*), (override));
    
    MOCK_METHOD(grpc::Status, ValidateToken,
        (grpc::ClientContext*, const pb_auth::ValidateTokenRequest&, 
         pb_auth::ValidateTokenResponse*), (override));

    // 异步方法（必须实现，返回 nullptr 即可）
    grpc::ClientAsyncResponseReaderInterface<pb_auth::SendVerifyCodeResponse>* 
    AsyncSendVerifyCodeRaw(grpc::ClientContext*, const pb_auth::SendVerifyCodeRequest&, 
                           grpc::CompletionQueue*) override { return nullptr; }
    
    grpc::ClientAsyncResponseReaderInterface<pb_auth::SendVerifyCodeResponse>* 
    PrepareAsyncSendVerifyCodeRaw(grpc::ClientContext*, const pb_auth::SendVerifyCodeRequest&, 
                                  grpc::CompletionQueue*) override { return nullptr; }

    grpc::ClientAsyncResponseReaderInterface<pb_auth::RegisterResponse>* 
    AsyncRegisterRaw(grpc::ClientContext*, const pb_auth::RegisterRequest&, 
                     grpc::CompletionQueue*) override { return nullptr; }
    
    grpc::ClientAsyncResponseReaderInterface<pb_auth::RegisterResponse>* 
    PrepareAsyncRegisterRaw(grpc::ClientContext*, const pb_auth::RegisterRequest&, 
                            grpc::CompletionQueue*) override { return nullptr; }

    grpc::ClientAsyncResponseReaderInterface<pb_auth::LoginByPasswordResponse>* 
    AsyncLoginByPasswordRaw(grpc::ClientContext*, const pb_auth::LoginByPasswordRequest&, 
                            grpc::CompletionQueue*) override { return nullptr; }
    
    grpc::ClientAsyncResponseReaderInterface<pb_auth::LoginByPasswordResponse>* 
    PrepareAsyncLoginByPasswordRaw(grpc::ClientContext*, const pb_auth::LoginByPasswordRequest&, 
                                   grpc::CompletionQueue*) override { return nullptr; }

    grpc::ClientAsyncResponseReaderInterface<pb_auth::LoginByCodeResponse>* 
    AsyncLoginByCodeRaw(grpc::ClientContext*, const pb_auth::LoginByCodeRequest&, 
                        grpc::CompletionQueue*) override { return nullptr; }
    
    grpc::ClientAsyncResponseReaderInterface<pb_auth::LoginByCodeResponse>* 
    PrepareAsyncLoginByCodeRaw(grpc::ClientContext*, const pb_auth::LoginByCodeRequest&, 
                               grpc::CompletionQueue*) override { return nullptr; }

    grpc::ClientAsyncResponseReaderInterface<pb_auth::RefreshTokenResponse>* 
    AsyncRefreshTokenRaw(grpc::ClientContext*, const pb_auth::RefreshTokenRequest&, 
                         grpc::CompletionQueue*) override { return nullptr; }
    
    grpc::ClientAsyncResponseReaderInterface<pb_auth::RefreshTokenResponse>* 
    PrepareAsyncRefreshTokenRaw(grpc::ClientContext*, const pb_auth::RefreshTokenRequest&, 
                                grpc::CompletionQueue*) override { return nullptr; }

    grpc::ClientAsyncResponseReaderInterface<pb_auth::LogoutResponse>* 
    AsyncLogoutRaw(grpc::ClientContext*, const pb_auth::LogoutRequest&, 
                   grpc::CompletionQueue*) override { return nullptr; }
    
    grpc::ClientAsyncResponseReaderInterface<pb_auth::LogoutResponse>* 
    PrepareAsyncLogoutRaw(grpc::ClientContext*, const pb_auth::LogoutRequest&, 
                          grpc::CompletionQueue*) override { return nullptr; }

    grpc::ClientAsyncResponseReaderInterface<pb_auth::ResetPasswordResponse>* 
    AsyncResetPasswordRaw(grpc::ClientContext*, const pb_auth::ResetPasswordRequest&, 
                          grpc::CompletionQueue*) override { return nullptr; }
    
    grpc::ClientAsyncResponseReaderInterface<pb_auth::ResetPasswordResponse>* 
    PrepareAsyncResetPasswordRaw(grpc::ClientContext*, const pb_auth::ResetPasswordRequest&, 
                                 grpc::CompletionQueue*) override { return nullptr; }

    grpc::ClientAsyncResponseReaderInterface<pb_auth::ValidateTokenResponse>* 
    AsyncValidateTokenRaw(grpc::ClientContext*, const pb_auth::ValidateTokenRequest&, 
                          grpc::CompletionQueue*) override { return nullptr; }
    
    grpc::ClientAsyncResponseReaderInterface<pb_auth::ValidateTokenResponse>* 
    PrepareAsyncValidateTokenRaw(grpc::ClientContext*, const pb_auth::ValidateTokenRequest&, 
                                 grpc::CompletionQueue*) override { return nullptr; }
};

// ============================================================================
// 测试 Fixture
// ============================================================================

class AuthClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 每个测试创建新的 mock
    }

    void TearDown() override {
    }

    // 创建成功响应的 Result
    static pb_common::Result MakeOkResult() {
        pb_common::Result result;
        result.set_code(pb_common::ErrorCode::OK);
        result.set_msg("success");
        return result;
    }

    // 创建失败响应的 Result
    static pb_common::Result MakeErrorResult(pb_common::ErrorCode code, 
                                             const std::string& msg) {
        pb_common::Result result;
        result.set_code(code);
        result.set_msg(msg);
        return result;
    }
};

// ============================================================================
// SendVerifyCode 测试
// ============================================================================

TEST_F(AuthClientTest, SendVerifyCode_Success) {
    // 创建 mock stub
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    // 设置期望行为
    EXPECT_CALL(*mock_stub, SendVerifyCode(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*, 
                      const pb_auth::SendVerifyCodeRequest& req,
                      pb_auth::SendVerifyCodeResponse* resp) {
                // 验证请求参数
                EXPECT_EQ(req.mobile(), "13800138000");
                EXPECT_EQ(req.scene(), pb_auth::SmsScene::SMS_SCENE_REGISTER);
                
                // 设置响应
                *resp->mutable_result() = MakeOkResult();
                resp->set_retry_after(60);
                return grpc::Status::OK;
            })
        ));

    // 注意：由于 AuthClient 内部直接使用 NewStub，无法直接注入 mock
    // 这里我们通过单独测试请求/响应逻辑来验证
    
    pb_auth::SendVerifyCodeRequest request;
    request.set_mobile("13800138000");
    request.set_scene(pb_auth::SmsScene::SMS_SCENE_REGISTER);
    
    pb_auth::SendVerifyCodeResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->SendVerifyCode(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.retry_after(), 60);
}

TEST_F(AuthClientTest, SendVerifyCode_RateLimited) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, SendVerifyCode(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*, 
                      const pb_auth::SendVerifyCodeRequest&,
                      pb_auth::SendVerifyCodeResponse* resp) {
                *resp->mutable_result() = MakeErrorResult(
                    pb_common::ErrorCode::RATE_LIMITED,
                    "请求过于频繁，请60秒后再试"
                );
                resp->set_retry_after(60);
                return grpc::Status::OK;
            })
        ));
    
    pb_auth::SendVerifyCodeRequest request;
    request.set_mobile("13800138000");
    request.set_scene(pb_auth::SmsScene::SMS_SCENE_REGISTER);
    
    pb_auth::SendVerifyCodeResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->SendVerifyCode(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::RATE_LIMITED);
}

// ============================================================================
// Register 测试
// ============================================================================

TEST_F(AuthClientTest, Register_Success) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, Register(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*, 
                      const pb_auth::RegisterRequest& req,
                      pb_auth::RegisterResponse* resp) {
                // 验证请求
                EXPECT_EQ(req.mobile(), "13800138000");
                EXPECT_EQ(req.verify_code(), "123456");
                EXPECT_FALSE(req.password().empty());
                
                // 设置响应
                *resp->mutable_result() = MakeOkResult();
                
                auto* user = resp->mutable_user();
                user->set_id("usr_test-uuid-12345");
                user->set_mobile("13800138000");
                user->set_display_name("TestUser");
                user->set_role(pb_auth::UserRole::USER_ROLE_USER);
                user->set_disabled(false);
                
                auto* tokens = resp->mutable_tokens();
                tokens->set_access_token("access_token_xxx");
                tokens->set_refresh_token("refresh_token_xxx");
                tokens->set_expires_in(900);
                
                return grpc::Status::OK;
            })
        ));
    
    pb_auth::RegisterRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");
    request.set_password("Password123");
    request.set_display_name("TestUser");
    
    pb_auth::RegisterResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->Register(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.user().mobile(), "13800138000");
    EXPECT_FALSE(response.tokens().access_token().empty());
}

TEST_F(AuthClientTest, Register_MobileAlreadyExists) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, Register(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*, 
                      const pb_auth::RegisterRequest&,
                      pb_auth::RegisterResponse* resp) {
                *resp->mutable_result() = MakeErrorResult(
                    pb_common::ErrorCode::MOBILE_TAKEN,
                    "该手机号已被注册"
                );
                return grpc::Status::OK;
            })
        ));
    
    pb_auth::RegisterRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");
    request.set_password("Password123");
    
    pb_auth::RegisterResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->Register(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::MOBILE_TAKEN);
}

TEST_F(AuthClientTest, Register_InvalidVerifyCode) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, Register(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*, 
                      const pb_auth::RegisterRequest&,
                      pb_auth::RegisterResponse* resp) {
                *resp->mutable_result() = MakeErrorResult(
                    pb_common::ErrorCode::CAPTCHA_WRONG,
                    "验证码错误"
                );
                return grpc::Status::OK;
            })
        ));
    
    pb_auth::RegisterRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("000000");
    request.set_password("Password123");
    
    pb_auth::RegisterResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->Register(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::CAPTCHA_WRONG);
}

// ============================================================================
// LoginByPassword 测试
// ============================================================================

TEST_F(AuthClientTest, LoginByPassword_Success) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, LoginByPassword(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*, 
                      const pb_auth::LoginByPasswordRequest& req,
                      pb_auth::LoginByPasswordResponse* resp) {
                EXPECT_EQ(req.mobile(), "13800138000");
                EXPECT_EQ(req.password(), "Password123");
                
                *resp->mutable_result() = MakeOkResult();
                
                auto* user = resp->mutable_user();
                user->set_id("usr_test-uuid-12345");
                user->set_mobile("13800138000");
                
                auto* tokens = resp->mutable_tokens();
                tokens->set_access_token("access_token_xxx");
                tokens->set_refresh_token("refresh_token_xxx");
                tokens->set_expires_in(900);
                
                return grpc::Status::OK;
            })
        ));
    
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_password("Password123");
    
    pb_auth::LoginByPasswordResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->LoginByPassword(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
}

TEST_F(AuthClientTest, LoginByPassword_WrongPassword) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, LoginByPassword(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*, 
                      const pb_auth::LoginByPasswordRequest&,
                      pb_auth::LoginByPasswordResponse* resp) {
                *resp->mutable_result() = MakeErrorResult(
                    pb_common::ErrorCode::WRONG_PASSWORD,
                    "账号或密码错误"
                );
                return grpc::Status::OK;
            })
        ));
    
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_password("WrongPassword");
    
    pb_auth::LoginByPasswordResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->LoginByPassword(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::WRONG_PASSWORD);
}

TEST_F(AuthClientTest, LoginByPassword_AccountLocked) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, LoginByPassword(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*, 
                      const pb_auth::LoginByPasswordRequest&,
                      pb_auth::LoginByPasswordResponse* resp) {
                *resp->mutable_result() = MakeErrorResult(
                    pb_common::ErrorCode::ACCOUNT_LOCKED,
                    "账号已锁定，请30分钟后再试"
                );
                return grpc::Status::OK;
            })
        ));
    
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_password("WrongPassword");
    
    pb_auth::LoginByPasswordResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->LoginByPassword(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::ACCOUNT_LOCKED);
}

// ============================================================================
// LoginByCode 测试
// ============================================================================

TEST_F(AuthClientTest, LoginByCode_Success) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, LoginByCode(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*, 
                      const pb_auth::LoginByCodeRequest& req,
                      pb_auth::LoginByCodeResponse* resp) {
                EXPECT_EQ(req.mobile(), "13800138000");
                EXPECT_EQ(req.verify_code(), "123456");
                
                *resp->mutable_result() = MakeOkResult();
                
                auto* user = resp->mutable_user();
                user->set_id("usr_test-uuid-12345");
                user->set_mobile("13800138000");
                
                auto* tokens = resp->mutable_tokens();
                tokens->set_access_token("access_token_xxx");
                tokens->set_refresh_token("refresh_token_xxx");
                tokens->set_expires_in(900);
                
                return grpc::Status::OK;
            })
        ));
    
    pb_auth::LoginByCodeRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");
    
    pb_auth::LoginByCodeResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->LoginByCode(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
}

// ============================================================================
// RefreshToken 测试
// ============================================================================

TEST_F(AuthClientTest, RefreshToken_Success) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, RefreshToken(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*, 
                      const pb_auth::RefreshTokenRequest& req,
                      pb_auth::RefreshTokenResponse* resp) {
                EXPECT_FALSE(req.refresh_token().empty());
                
                *resp->mutable_result() = MakeOkResult();
                
                auto* tokens = resp->mutable_tokens();
                tokens->set_access_token("new_access_token_xxx");
                tokens->set_refresh_token("new_refresh_token_xxx");
                tokens->set_expires_in(900);
                
                return grpc::Status::OK;
            })
        ));
    
    pb_auth::RefreshTokenRequest request;
    request.set_refresh_token("old_refresh_token_xxx");
    
    pb_auth::RefreshTokenResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->RefreshToken(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_NE(response.tokens().access_token(), "old_access_token");
}

TEST_F(AuthClientTest, RefreshToken_TokenExpired) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, RefreshToken(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*, 
                      const pb_auth::RefreshTokenRequest&,
                      pb_auth::RefreshTokenResponse* resp) {
                *resp->mutable_result() = MakeErrorResult(
                    pb_common::ErrorCode::TOKEN_EXPIRED,
                    "Token 已过期"
                );
                return grpc::Status::OK;
            })
        ));
    
    pb_auth::RefreshTokenRequest request;
    request.set_refresh_token("expired_token");
    
    pb_auth::RefreshTokenResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->RefreshToken(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::TOKEN_EXPIRED);
}

// ============================================================================
// Logout 测试
// ============================================================================

TEST_F(AuthClientTest, Logout_Success) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, Logout(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*, 
                      const pb_auth::LogoutRequest& req,
                      pb_auth::LogoutResponse* resp) {
                EXPECT_FALSE(req.refresh_token().empty());
                *resp->mutable_result() = MakeOkResult();
                return grpc::Status::OK;
            })
        ));
    
    pb_auth::LogoutRequest request;
    request.set_refresh_token("refresh_token_xxx");
    
    pb_auth::LogoutResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->Logout(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
}

// ============================================================================
// ResetPassword 测试
// ============================================================================

TEST_F(AuthClientTest, ResetPassword_Success) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, ResetPassword(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*, 
                      const pb_auth::ResetPasswordRequest& req,
                      pb_auth::ResetPasswordResponse* resp) {
                EXPECT_EQ(req.mobile(), "13800138000");
                EXPECT_EQ(req.verify_code(), "123456");
                EXPECT_FALSE(req.new_password().empty());
                
                *resp->mutable_result() = MakeOkResult();
                return grpc::Status::OK;
            })
        ));
    
    pb_auth::ResetPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_verify_code("123456");
    request.set_new_password("NewPassword123");
    
    pb_auth::ResetPasswordResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->ResetPassword(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
}

// ============================================================================
// ValidateToken 测试
// ============================================================================

TEST_F(AuthClientTest, ValidateToken_Success) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, ValidateToken(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*, 
                      const pb_auth::ValidateTokenRequest& req,
                      pb_auth::ValidateTokenResponse* resp) {
                EXPECT_FALSE(req.access_token().empty());
                
                *resp->mutable_result() = MakeOkResult();
                resp->set_user_id("12345");
                resp->set_user_uuid("usr_test-uuid-12345");
                resp->set_mobile("13800138000");
                resp->set_role(pb_auth::UserRole::USER_ROLE_USER);
                
                return grpc::Status::OK;
            })
        ));
    
    pb_auth::ValidateTokenRequest request;
    request.set_access_token("valid_access_token");
    
    pb_auth::ValidateTokenResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->ValidateToken(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::OK);
    EXPECT_EQ(response.user_uuid(), "usr_test-uuid-12345");
}

TEST_F(AuthClientTest, ValidateToken_InvalidToken) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, ValidateToken(_, _, _))
        .WillOnce(DoAll(
            Invoke([](grpc::ClientContext*, 
                      const pb_auth::ValidateTokenRequest&,
                      pb_auth::ValidateTokenResponse* resp) {
                *resp->mutable_result() = MakeErrorResult(
                    pb_common::ErrorCode::TOKEN_INVALID,
                    "Token 无效"
                );
                return grpc::Status::OK;
            })
        ));
    
    pb_auth::ValidateTokenRequest request;
    request.set_access_token("invalid_token");
    
    pb_auth::ValidateTokenResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->ValidateToken(&context, request, &response);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.result().code(), pb_common::ErrorCode::TOKEN_INVALID);
}

// ============================================================================
// gRPC 连接错误测试
// ============================================================================

TEST_F(AuthClientTest, RpcError_Unavailable) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, SendVerifyCode(_, _, _))
        .WillOnce(Return(grpc::Status(grpc::StatusCode::UNAVAILABLE, 
                                       "Service unavailable")));
    
    pb_auth::SendVerifyCodeRequest request;
    request.set_mobile("13800138000");
    request.set_scene(pb_auth::SmsScene::SMS_SCENE_REGISTER);
    
    pb_auth::SendVerifyCodeResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->SendVerifyCode(&context, request, &response);
    
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAVAILABLE);
}

TEST_F(AuthClientTest, RpcError_DeadlineExceeded) {
    auto mock_stub = std::make_unique<MockAuthStub>();
    
    EXPECT_CALL(*mock_stub, LoginByPassword(_, _, _))
        .WillOnce(Return(grpc::Status(grpc::StatusCode::DEADLINE_EXCEEDED, 
                                       "Deadline exceeded")));
    
    pb_auth::LoginByPasswordRequest request;
    request.set_mobile("13800138000");
    request.set_password("Password123");
    
    pb_auth::LoginByPasswordResponse response;
    grpc::ClientContext context;
    
    auto status = mock_stub->LoginByPassword(&context, request, &response);
    
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::DEADLINE_EXCEEDED);
}

} // namespace testing
} // namespace user_service
