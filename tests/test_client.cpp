#include <iostream>
#include <memory>
#include <string>
#include <iomanip>

#include <grpcpp/grpcpp.h>

// 引入生成的 proto 头文件
#include "pb_common/result.pb.h"
#include "pb_auth/auth.grpc.pb.h"
#include "pb_user/user.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

// ==================== 辅助函数 ====================

std::string ErrorCodeToString(pb_common::ErrorCode code) {
    switch (code) {
        case pb_common::OK: return "OK";
        case pb_common::UNKNOWN: return "UNKNOWN";
        case pb_common::INTERNAL: return "INTERNAL";
        case pb_common::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case pb_common::RATE_LIMITED: return "RATE_LIMITED";
        case pb_common::UNAUTHENTICATED: return "UNAUTHENTICATED";
        case pb_common::TOKEN_INVALID: return "TOKEN_INVALID";
        case pb_common::TOKEN_EXPIRED: return "TOKEN_EXPIRED";
        case pb_common::CAPTCHA_WRONG: return "CAPTCHA_WRONG";
        case pb_common::CAPTCHA_EXPIRED: return "CAPTCHA_EXPIRED";
        case pb_common::USER_NOT_FOUND: return "USER_NOT_FOUND";
        case pb_common::USER_ALREADY_EXISTS: return "USER_ALREADY_EXISTS";
        case pb_common::MOBILE_TAKEN: return "MOBILE_TAKEN";
        case pb_common::WRONG_PASSWORD: return "WRONG_PASSWORD";
        case pb_common::PERMISSION_DENIED: return "PERMISSION_DENIED";
        default: return "CODE_" + std::to_string(code);
    }
}

std::string RoleToString(pb_auth::UserRole role) {
    switch (role) {
        case pb_auth::USER_ROLE_USER: return "普通用户";
        case pb_auth::USER_ROLE_ADMIN: return "管理员";
        case pb_auth::USER_ROLE_SUPER_ADMIN: return "超级管理员";
        default: return "未知角色";
    }
}

std::string SmsSceneToString(pb_auth::SmsScene scene) {
    switch (scene) {
        case pb_auth::SMS_SCENE_REGISTER: return "注册";
        case pb_auth::SMS_SCENE_LOGIN: return "登录";
        case pb_auth::SMS_SCENE_RESET_PASSWORD: return "重置密码";
        case pb_auth::SMS_SCENE_DELETE_USER: return "注销账号";
        default: return "未知场景";
    }
}

void PrintResult(const pb_common::Result& result) {
    if (result.code() == pb_common::OK) {
        std::cout << "  ✓ 结果: 成功" << std::endl;
    } else {
        std::cout << "  ✗ 结果: 失败" << std::endl;
        std::cout << "    错误码: " << ErrorCodeToString(result.code()) 
                  << " (" << result.code() << ")" << std::endl;
        std::cout << "    错误信息: " << result.msg() << std::endl;
    }
}

void PrintUserInfo(const pb_auth::UserInfo& user) {
    std::cout << "  用户信息:" << std::endl;
    std::cout << "    ID: " << user.id() << std::endl;
    std::cout << "    手机号: " << user.mobile() << std::endl;
    std::cout << "    昵称: " << user.display_name() << std::endl;
    std::cout << "    角色: " << RoleToString(user.role()) << std::endl;
    std::cout << "    禁用: " << (user.disabled() ? "是" : "否") << std::endl;
}

void PrintUser(const pb_user::User& user) {
    std::cout << "  用户信息:" << std::endl;
    std::cout << "    ID: " << user.id() << std::endl;
    std::cout << "    手机号: " << user.mobile() << std::endl;
    std::cout << "    昵称: " << user.display_name() << std::endl;
    std::cout << "    角色: " << RoleToString(user.role()) << std::endl;
    std::cout << "    禁用: " << (user.disabled() ? "是" : "否") << std::endl;
}

void PrintTokenPair(const pb_auth::TokenPair& tokens) {
    std::cout << "  令牌信息:" << std::endl;
    std::cout << "    access_token: " << tokens.access_token().substr(0, 50) << "..." << std::endl;
    std::cout << "    refresh_token: " << tokens.refresh_token().substr(0, 50) << "..." << std::endl;
    std::cout << "    expires_in: " << tokens.expires_in() << " 秒" << std::endl;
}

// ==================== 测试客户端类 ====================

class TestClient {
public:
    TestClient(std::shared_ptr<Channel> channel)
        : auth_stub_(pb_auth::AuthService::NewStub(channel)),
          user_stub_(pb_user::UserService::NewStub(channel)) {}

    // ==================== AuthService 测试 ====================

    // 发送验证码
    void TestSendVerifyCode(const std::string& mobile, pb_auth::SmsScene scene) {
        std::cout << "\n========== 测试: 发送验证码 ==========" << std::endl;
        std::cout << "  手机号: " << mobile << std::endl;
        std::cout << "  场景: " << SmsSceneToString(scene) << std::endl;

        pb_auth::SendVerifyCodeRequest request;
        request.set_mobile(mobile);
        request.set_scene(scene);

        pb_auth::SendVerifyCodeResponse response;
        ClientContext context;

        Status status = auth_stub_->SendVerifyCode(&context, request, &response);

        if (status.ok()) {
            PrintResult(response.result());
            if (response.result().code() == pb_common::OK) {
                std::cout << "  重发冷却时间: " << response.retry_after() << " 秒" << std::endl;
            }
        } else {
            std::cerr << "  ✗ gRPC 调用失败!" << std::endl;
            std::cerr << "    错误码: " << status.error_code() << std::endl;
            std::cerr << "    错误信息: " << status.error_message() << std::endl;
        }
    }

    // 注册
    std::pair<std::string, std::string> TestRegister(const std::string& mobile, 
                                                      const std::string& verify_code,
                                                      const std::string& password,
                                                      const std::string& display_name = "") {
        std::cout << "\n========== 测试: 用户注册 ==========" << std::endl;
        std::cout << "  手机号: " << mobile << std::endl;
        std::cout << "  验证码: " << verify_code << std::endl;
        std::cout << "  昵称: " << (display_name.empty() ? "(未设置)" : display_name) << std::endl;

        pb_auth::RegisterRequest request;
        request.set_mobile(mobile);
        request.set_verify_code(verify_code);
        request.set_password(password);
        if (!display_name.empty()) {
            request.set_display_name(display_name);
        }

        pb_auth::RegisterResponse response;
        ClientContext context;

        Status status = auth_stub_->Register(&context, request, &response);

        if (status.ok()) {
            PrintResult(response.result());
            if (response.result().code() == pb_common::OK) {
                PrintUserInfo(response.user());
                PrintTokenPair(response.tokens());
                return {response.tokens().access_token(), response.tokens().refresh_token()};
            }
        } else {
            std::cerr << "  ✗ gRPC 调用失败: " << status.error_message() << std::endl;
        }
        return {"", ""};
    }

    // 密码登录
    std::pair<std::string, std::string> TestLoginByPassword(const std::string& mobile, 
                                                             const std::string& password) {
        std::cout << "\n========== 测试: 密码登录 ==========" << std::endl;
        std::cout << "  手机号: " << mobile << std::endl;

        pb_auth::LoginByPasswordRequest request;
        request.set_mobile(mobile);
        request.set_password(password);

        pb_auth::LoginByPasswordResponse response;
        ClientContext context;

        Status status = auth_stub_->LoginByPassword(&context, request, &response);

        if (status.ok()) {
            PrintResult(response.result());
            if (response.result().code() == pb_common::OK) {
                PrintUserInfo(response.user());
                PrintTokenPair(response.tokens());
                return {response.tokens().access_token(), response.tokens().refresh_token()};
            }
        } else {
            std::cerr << "  ✗ gRPC 调用失败: " << status.error_message() << std::endl;
        }
        return {"", ""};
    }

    // 验证码登录
    std::pair<std::string, std::string> TestLoginByCode(const std::string& mobile, 
                                                         const std::string& verify_code) {
        std::cout << "\n========== 测试: 验证码登录 ==========" << std::endl;
        std::cout << "  手机号: " << mobile << std::endl;
        std::cout << "  验证码: " << verify_code << std::endl;

        pb_auth::LoginByCodeRequest request;
        request.set_mobile(mobile);
        request.set_verify_code(verify_code);

        pb_auth::LoginByCodeResponse response;
        ClientContext context;

        Status status = auth_stub_->LoginByCode(&context, request, &response);

        if (status.ok()) {
            PrintResult(response.result());
            if (response.result().code() == pb_common::OK) {
                PrintUserInfo(response.user());
                PrintTokenPair(response.tokens());
                return {response.tokens().access_token(), response.tokens().refresh_token()};
            }
        } else {
            std::cerr << "  ✗ gRPC 调用失败: " << status.error_message() << std::endl;
        }
        return {"", ""};
    }

    // 刷新令牌
    std::pair<std::string, std::string> TestRefreshToken(const std::string& refresh_token) {
        std::cout << "\n========== 测试: 刷新令牌 ==========" << std::endl;

        pb_auth::RefreshTokenRequest request;
        request.set_refresh_token(refresh_token);

        pb_auth::RefreshTokenResponse response;
        ClientContext context;

        Status status = auth_stub_->RefreshToken(&context, request, &response);

        if (status.ok()) {
            PrintResult(response.result());
            if (response.result().code() == pb_common::OK) {
                PrintTokenPair(response.tokens());
                return {response.tokens().access_token(), response.tokens().refresh_token()};
            }
        } else {
            std::cerr << "  ✗ gRPC 调用失败: " << status.error_message() << std::endl;
        }
        return {"", ""};
    }

    // 登出
    void TestLogout(const std::string& refresh_token) {
        std::cout << "\n========== 测试: 登出 ==========" << std::endl;

        pb_auth::LogoutRequest request;
        request.set_refresh_token(refresh_token);

        pb_auth::LogoutResponse response;
        ClientContext context;

        Status status = auth_stub_->Logout(&context, request, &response);

        if (status.ok()) {
            PrintResult(response.result());
        } else {
            std::cerr << "  ✗ gRPC 调用失败: " << status.error_message() << std::endl;
        }
    }

    // 重置密码
    void TestResetPassword(const std::string& mobile, 
                           const std::string& verify_code,
                           const std::string& new_password) {
        std::cout << "\n========== 测试: 重置密码 ==========" << std::endl;
        std::cout << "  手机号: " << mobile << std::endl;

        pb_auth::ResetPasswordRequest request;
        request.set_mobile(mobile);
        request.set_verify_code(verify_code);
        request.set_new_password(new_password);

        pb_auth::ResetPasswordResponse response;
        ClientContext context;

        Status status = auth_stub_->ResetPassword(&context, request, &response);

        if (status.ok()) {
            PrintResult(response.result());
        } else {
            std::cerr << "  ✗ gRPC 调用失败: " << status.error_message() << std::endl;
        }
    }

    // 验证令牌（内部接口）
    void TestValidateToken(const std::string& access_token) {
        std::cout << "\n========== 测试: 验证令牌 ==========" << std::endl;

        pb_auth::ValidateTokenRequest request;
        request.set_access_token(access_token);

        pb_auth::ValidateTokenResponse response;
        ClientContext context;

        Status status = auth_stub_->ValidateToken(&context, request, &response);

        if (status.ok()) {
            PrintResult(response.result());
            if (response.result().code() == pb_common::OK) {
                std::cout << "  用户ID: " << response.user_id() << std::endl;
                std::cout << "  用户UUID: " << response.user_uuid() << std::endl;
                std::cout << "  手机号: " << response.mobile() << std::endl;
                std::cout << "  角色: " << RoleToString(response.role()) << std::endl;
            }
        } else {
            std::cerr << "  ✗ gRPC 调用失败: " << status.error_message() << std::endl;
        }
    }

    // ==================== UserService 测试 ====================

    // 获取当前用户
    void TestGetCurrentUser(const std::string& access_token) {
        std::cout << "\n========== 测试: 获取当前用户 ==========" << std::endl;

        pb_user::GetCurrentUserRequest request;
        pb_user::GetCurrentUserResponse response;
        ClientContext context;
        context.AddMetadata("authorization", "Bearer " + access_token);

        Status status = user_stub_->GetCurrentUser(&context, request, &response);

        if (status.ok()) {
            PrintResult(response.result());
            if (response.result().code() == pb_common::OK) {
                PrintUser(response.user());
            }
        } else {
            std::cerr << "  ✗ gRPC 调用失败: " << status.error_message() << std::endl;
        }
    }

    // 获取指定用户（管理员）
    void TestGetUser(const std::string& access_token, const std::string& user_id) {
        std::cout << "\n========== 测试: 获取指定用户 ==========" << std::endl;
        std::cout << "  目标用户ID: " << user_id << std::endl;

        pb_user::GetUserRequest request;
        request.set_id(user_id);

        pb_user::GetUserResponse response;
        ClientContext context;
        context.AddMetadata("authorization", "Bearer " + access_token);

        Status status = user_stub_->GetUser(&context, request, &response);

        if (status.ok()) {
            PrintResult(response.result());
            if (response.result().code() == pb_common::OK) {
                PrintUser(response.user());
            }
        } else {
            std::cerr << "  ✗ gRPC 调用失败: " << status.error_message() << std::endl;
        }
    }

    // 更新用户信息
    void TestUpdateUser(const std::string& access_token, const std::string& display_name) {
        std::cout << "\n========== 测试: 更新用户信息 ==========" << std::endl;
        std::cout << "  新昵称: " << display_name << std::endl;

        pb_user::UpdateUserRequest request;
        request.mutable_display_name()->set_value(display_name);

        pb_user::UpdateUserResponse response;
        ClientContext context;
        context.AddMetadata("authorization", "Bearer " + access_token);

        Status status = user_stub_->UpdateUser(&context, request, &response);

        if (status.ok()) {
            PrintResult(response.result());
            if (response.result().code() == pb_common::OK) {
                PrintUser(response.user());
            }
        } else {
            std::cerr << "  ✗ gRPC 调用失败: " << status.error_message() << std::endl;
        }
    }

    // 修改密码
    void TestChangePassword(const std::string& access_token,
                            const std::string& old_password,
                            const std::string& new_password) {
        std::cout << "\n========== 测试: 修改密码 ==========" << std::endl;

        pb_user::ChangePasswordRequest request;
        request.set_old_password(old_password);
        request.set_new_password(new_password);

        pb_user::ChangePasswordResponse response;
        ClientContext context;
        context.AddMetadata("authorization", "Bearer " + access_token);

        Status status = user_stub_->ChangePassword(&context, request, &response);

        if (status.ok()) {
            PrintResult(response.result());
        } else {
            std::cerr << "  ✗ gRPC 调用失败: " << status.error_message() << std::endl;
        }
    }

    // 删除用户（注销）
    void TestDeleteUser(const std::string& access_token, const std::string& verify_code) {
        std::cout << "\n========== 测试: 删除用户（注销） ==========" << std::endl;

        pb_user::DeleteUserRequest request;
        request.set_verify_code(verify_code);

        pb_user::DeleteUserResponse response;
        ClientContext context;
        context.AddMetadata("authorization", "Bearer " + access_token);

        Status status = user_stub_->DeleteUser(&context, request, &response);

        if (status.ok()) {
            PrintResult(response.result());
        } else {
            std::cerr << "  ✗ gRPC 调用失败: " << status.error_message() << std::endl;
        }
    }

    // 用户列表（管理员）
    void TestListUsers(const std::string& access_token, 
                       int32_t page = 1, 
                       int32_t page_size = 10,
                       const std::string& mobile_filter = "") {
        std::cout << "\n========== 测试: 用户列表（管理员） ==========" << std::endl;
        std::cout << "  页码: " << page << ", 每页: " << page_size << std::endl;

        pb_user::ListUsersRequest request;
        request.mutable_page()->set_page(page);
        request.mutable_page()->set_page_size(page_size);
        if (!mobile_filter.empty()) {
            request.set_mobile_filter(mobile_filter);
            std::cout << "  手机号过滤: " << mobile_filter << std::endl;
        }

        pb_user::ListUsersResponse response;
        ClientContext context;
        context.AddMetadata("authorization", "Bearer " + access_token);

        Status status = user_stub_->ListUsers(&context, request, &response);

        if (status.ok()) {
            PrintResult(response.result());
            if (response.result().code() == pb_common::OK) {
                std::cout << "  分页信息:" << std::endl;
                std::cout << "    总记录数: " << response.page_info().total_records() << std::endl;
                std::cout << "    总页数: " << response.page_info().total_pages() << std::endl;
                std::cout << "    当前页: " << response.page_info().page() << std::endl;
                std::cout << "  用户列表 (" << response.users_size() << " 条):" << std::endl;
                for (int i = 0; i < response.users_size(); i++) {
                    const auto& user = response.users(i);
                    std::cout << "    [" << i+1 << "] ID:" << user.id() 
                              << " | 手机:" << user.mobile()
                              << " | 昵称:" << user.display_name() << std::endl;
                }
            }
        } else {
            std::cerr << "  ✗ gRPC 调用失败: " << status.error_message() << std::endl;
        }
    }

private:
    std::unique_ptr<pb_auth::AuthService::Stub> auth_stub_;
    std::unique_ptr<pb_user::UserService::Stub> user_stub_;
};

// ==================== 使用说明 ====================

void PrintUsage(const char* program) {
    std::cout << "用法: " << program << " <命令> [参数...]" << std::endl;
    std::cout << std::endl;
    std::cout << "========== 认证服务 (AuthService) ==========" << std::endl;
    std::cout << "  send_code <mobile> <scene>              发送验证码" << std::endl;
    std::cout << "      scene: register|login|reset|delete" << std::endl;
    std::cout << "  register <mobile> <code> <password> [name]  注册" << std::endl;
    std::cout << "  login_pwd <mobile> <password>           密码登录" << std::endl;
    std::cout << "  login_code <mobile> <code>              验证码登录" << std::endl;
    std::cout << "  refresh <refresh_token>                 刷新令牌" << std::endl;
    std::cout << "  logout <refresh_token>                  登出" << std::endl;
    std::cout << "  reset_pwd <mobile> <code> <new_pwd>     重置密码" << std::endl;
    std::cout << "  validate <access_token>                 验证令牌" << std::endl;
    std::cout << std::endl;
    std::cout << "========== 用户服务 (UserService) ==========" << std::endl;
    std::cout << "  me <access_token>                       获取当前用户" << std::endl;
    std::cout << "  get_user <access_token> <user_id>       获取指定用户" << std::endl;
    std::cout << "  update <access_token> <display_name>    更新昵称" << std::endl;
    std::cout << "  change_pwd <token> <old_pwd> <new_pwd>  修改密码" << std::endl;
    std::cout << "  delete <access_token> <verify_code>     删除账号" << std::endl;
    std::cout << "  list <access_token> [page] [size]       用户列表" << std::endl;
    std::cout << std::endl;
    std::cout << "========== 完整流程测试 ==========" << std::endl;
    std::cout << "  flow_register <mobile>                  注册流程" << std::endl;
    std::cout << "  flow_login <mobile> <password>          登录流程" << std::endl;
    std::cout << "  flow_full <mobile>                      完整流程" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program << " send_code 13800138000 login" << std::endl;
    std::cout << "  " << program << " login_pwd 13800138000 123456" << std::endl;
    std::cout << "  " << program << " flow_full 13800138000" << std::endl;
}

pb_auth::SmsScene ParseSmsScene(const std::string& scene) {
    if (scene == "register") return pb_auth::SMS_SCENE_REGISTER;
    if (scene == "login") return pb_auth::SMS_SCENE_LOGIN;
    if (scene == "reset") return pb_auth::SMS_SCENE_RESET_PASSWORD;
    if (scene == "delete") return pb_auth::SMS_SCENE_DELETE_USER;
    return pb_auth::SMS_SCENE_UNKNOWN;
}

// ==================== 主函数 ====================

int main(int argc, char** argv) {
    std::string server_address = "localhost:50051";
    
    std::cout << "========================================" << std::endl;
    std::cout << "  User Service 测试客户端" << std::endl;
    std::cout << "  连接到: " << server_address << std::endl;
    std::cout << "========================================" << std::endl;

    auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
    TestClient client(channel);

    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::string cmd = argv[1];

    // ========== 认证服务命令 ==========
    if (cmd == "send_code" && argc >= 4) {
        client.TestSendVerifyCode(argv[2], ParseSmsScene(argv[3]));
    }
    else if (cmd == "register" && argc >= 5) {
        std::string name = (argc >= 6) ? argv[5] : "";
        client.TestRegister(argv[2], argv[3], argv[4], name);
    }
    else if (cmd == "login_pwd" && argc >= 4) {
        client.TestLoginByPassword(argv[2], argv[3]);
    }
    else if (cmd == "login_code" && argc >= 4) {
        client.TestLoginByCode(argv[2], argv[3]);
    }
    else if (cmd == "refresh" && argc >= 3) {
        client.TestRefreshToken(argv[2]);
    }
    else if (cmd == "logout" && argc >= 3) {
        client.TestLogout(argv[2]);
    }
    else if (cmd == "reset_pwd" && argc >= 5) {
        client.TestResetPassword(argv[2], argv[3], argv[4]);
    }
    else if (cmd == "validate" && argc >= 3) {
        client.TestValidateToken(argv[2]);
    }
    // ========== 用户服务命令 ==========
    else if (cmd == "me" && argc >= 3) {
        client.TestGetCurrentUser(argv[2]);
    }
    else if (cmd == "get_user" && argc >= 4) {
        client.TestGetUser(argv[2], argv[3]);
    }
    else if (cmd == "update" && argc >= 4) {
        client.TestUpdateUser(argv[2], argv[3]);
    }
    else if (cmd == "change_pwd" && argc >= 5) {
        client.TestChangePassword(argv[2], argv[3], argv[4]);
    }
    else if (cmd == "delete" && argc >= 4) {
        client.TestDeleteUser(argv[2], argv[3]);
    }
    else if (cmd == "list" && argc >= 3) {
        int page = (argc >= 4) ? std::stoi(argv[3]) : 1;
        int size = (argc >= 5) ? std::stoi(argv[4]) : 10;
        client.TestListUsers(argv[2], page, size);
    }
    // ========== 流程测试 ==========
    else if (cmd == "flow_register" && argc >= 3) {
        std::string mobile = argv[2];
        std::cout << "\n######### 注册流程测试 #########\n" << std::endl;

        // 1. 发送注册验证码
        client.TestSendVerifyCode(mobile, pb_auth::SMS_SCENE_REGISTER);

        // 2. 输入验证码
        std::cout << "\n请输入验证码: ";
        std::string code;
        std::cin >> code;

        // 3. 输入密码
        std::cout << "请输入密码: ";
        std::string password;
        std::cin >> password;

        // 4. 注册
        auto [access_token, refresh_token] = client.TestRegister(mobile, code, password, "新用户");

        if (!access_token.empty()) {
            // 5. 获取当前用户信息
            client.TestGetCurrentUser(access_token);
        }

        std::cout << "\n######### 注册流程完成 #########\n" << std::endl;
    }
    else if (cmd == "flow_login" && argc >= 4) {
        std::string mobile = argv[2];
        std::string password = argv[3];
        std::cout << "\n######### 登录流程测试 #########\n" << std::endl;

        // 1. 密码登录
        auto [access_token, refresh_token] = client.TestLoginByPassword(mobile, password);

        if (!access_token.empty()) {
            // 2. 获取当前用户
            client.TestGetCurrentUser(access_token);

            // 3. 验证令牌
            client.TestValidateToken(access_token);

            // 4. 刷新令牌
            auto [new_access, new_refresh] = client.TestRefreshToken(refresh_token);

            // 5. 登出
            client.TestLogout(new_refresh.empty() ? refresh_token : new_refresh);
        }

        std::cout << "\n######### 登录流程完成 #########\n" << std::endl;
    }
    else if (cmd == "flow_full" && argc >= 3) {
        std::string mobile = argv[2];
        std::cout << "\n######### 完整流程测试 #########\n" << std::endl;

        // 1. 发送登录验证码
        client.TestSendVerifyCode(mobile, pb_auth::SMS_SCENE_LOGIN);

        std::cout << "\n请输入验证码 (测试环境可能是 123456): ";
        std::string code;
        std::cin >> code;

        // 2. 验证码登录
        auto [access_token, refresh_token] = client.TestLoginByCode(mobile, code);

        if (!access_token.empty()) {
            // 3. 获取当前用户
            client.TestGetCurrentUser(access_token);

            // 4. 更新昵称
            std::string new_name = "测试用户_" + std::to_string(time(nullptr));
            client.TestUpdateUser(access_token, new_name);

            // 5. 再次获取确认更新
            client.TestGetCurrentUser(access_token);

            // 6. 验证令牌
            client.TestValidateToken(access_token);

            // 7. 刷新令牌
            auto [new_access, new_refresh] = client.TestRefreshToken(refresh_token);

            // 8. 登出
            client.TestLogout(new_refresh.empty() ? refresh_token : new_refresh);

            // 9. 验证登出后令牌失效
            std::cout << "\n>>> 验证登出后令牌是否失效:" << std::endl;
            client.TestValidateToken(new_access.empty() ? access_token : new_access);
        }

        std::cout << "\n######### 完整流程测试完成 #########\n" << std::endl;
    }else if (cmd == "test_all" || cmd == "all") {
        std::string mobile = (argc >= 3) ? argv[2] : "13800138000";
        std::string password = (argc >= 4) ? argv[3] : "Test123456";
        
        std::cout << "\n##################################################" << std::endl;
        std::cout << "#           一键运行所有测试                      #" << std::endl;
        std::cout << "##################################################" << std::endl;
        std::cout << "测试手机号: " << mobile << std::endl;
        std::cout << "测试密码: " << password << std::endl;
    
        std::string access_token, refresh_token;
        std::string user_id;
    
        // ==================== 1. 发送验证码测试 ====================
        std::cout << "\n\n【1/12】发送验证码测试" << std::endl;
        client.TestSendVerifyCode(mobile, pb_auth::SMS_SCENE_REGISTER);
        client.TestSendVerifyCode(mobile, pb_auth::SMS_SCENE_LOGIN);
    
        // ==================== 2. 注册测试 ====================
        std::cout << "\n\n【2/12】注册测试" << std::endl;
        // 注意：测试环境验证码通常是 123456
        auto [reg_access, reg_refresh] = client.TestRegister(
            mobile, "123456", password, "测试用户"
        );
        
        // 如果注册失败（用户已存在），改用登录
        if (reg_access.empty()) {
            std::cout << "  >>> 用户可能已存在，尝试登录..." << std::endl;
        } else {
            access_token = reg_access;
            refresh_token = reg_refresh;
        }
    
        // ==================== 3. 密码登录测试 ====================
        std::cout << "\n\n【3/12】密码登录测试" << std::endl;
        auto [pwd_access, pwd_refresh] = client.TestLoginByPassword(mobile, password);
        if (!pwd_access.empty()) {
            access_token = pwd_access;
            refresh_token = pwd_refresh;
        }
    
        // ==================== 4. 验证码登录测试 ====================
        std::cout << "\n\n【4/12】验证码登录测试" << std::endl;
        client.TestSendVerifyCode(mobile, pb_auth::SMS_SCENE_LOGIN);
        auto [code_access, code_refresh] = client.TestLoginByCode(mobile, "123456");
        if (!code_access.empty()) {
            access_token = code_access;
            refresh_token = code_refresh;
        }
    
        if (access_token.empty()) {
            std::cerr << "\n✗ 无法获取有效 Token，后续测试跳过" << std::endl;
            return 1;
        }
    
        std::cout << "\n  >>> 使用 Token 进行后续测试..." << std::endl;
    
        // ==================== 5. 验证令牌测试 ====================
        std::cout << "\n\n【5/12】验证令牌测试" << std::endl;
        client.TestValidateToken(access_token);
    
        // ==================== 6. 获取当前用户测试 ====================
        std::cout << "\n\n【6/12】获取当前用户测试" << std::endl;
        client.TestGetCurrentUser(access_token);
    
        // ==================== 7. 更新用户信息测试 ====================
        std::cout << "\n\n【7/12】更新用户信息测试" << std::endl;
        std::string new_nickname = "自动测试_" + std::to_string(time(nullptr));
        client.TestUpdateUser(access_token, new_nickname);
    
        // ==================== 8. 确认更新成功 ====================
        std::cout << "\n\n【8/12】确认更新成功" << std::endl;
        client.TestGetCurrentUser(access_token);
    
        // ==================== 9. 刷新令牌测试 ====================
        std::cout << "\n\n【9/12】刷新令牌测试" << std::endl;
        auto [new_access, new_refresh] = client.TestRefreshToken(refresh_token);
        if (!new_access.empty()) {
            access_token = new_access;
            refresh_token = new_refresh;
        }
    
        // ==================== 10. 修改密码测试 ====================
        std::cout << "\n\n【10/12】修改密码测试" << std::endl;
        std::string new_password = password + "_new";
        client.TestChangePassword(access_token, password, new_password);
        // 改回原密码
        client.TestChangePassword(access_token, new_password, password);
    
        // ==================== 11. 用户列表测试（需要管理员权限） ====================
        std::cout << "\n\n【11/12】用户列表测试（可能需要管理员权限）" << std::endl;
        client.TestListUsers(access_token, 1, 5);
    
        // ==================== 12. 登出测试 ====================
        std::cout << "\n\n【12/12】登出测试" << std::endl;
        client.TestLogout(refresh_token);
    
        // ==================== 验证登出后令牌失效 ====================
        std::cout << "\n\n【附加】验证登出后令牌失效" << std::endl;
        client.TestValidateToken(access_token);
    
        // ==================== 测试报告 ====================
        std::cout << "\n\n##################################################" << std::endl;
        std::cout << "#                测试完成                         #" << std::endl;
        std::cout << "##################################################" << std::endl;
        std::cout << "请检查上方输出，确认各测试结果是否符合预期" << std::endl;
    }
    else {
        std::cerr << "未知命令或参数不足: " << cmd << std::endl;
        PrintUsage(argv[0]);
        return 1;
    }

    return 0;
}
