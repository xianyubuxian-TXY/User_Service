# Client 模块使用指南

## 📖 概述

Client 模块提供了**开箱即用**的 gRPC 客户端封装，用于调用用户服务的各种接口：

| 客户端 | 用途 | 是否需要登录 |
|--------|------|-------------|
| `AuthClient` | 认证相关（注册、登录、Token 管理） | ❌ 大部分不需要 |
| `UserClient` | 用户操作（查询、修改、删除） | ✅ 需要 Access Token |

**核心特性：**

- ✅ 统一的 `Result<T>` 返回值，清晰的错误处理
- ✅ 自动超时管理
- ✅ 支持 Bearer Token 认证
- ✅ 链式配置，简洁易用

---

## 🚀 快速开始

### 安装依赖

确保你的 CMakeLists.txt 链接了客户端库：

```cmake
target_link_libraries(your_app PRIVATE
    user_client_lib
)
```

### 最小示例：注册 + 登录

```cpp
#include "client/auth_client.h"
#include <iostream>

int main() {
    using namespace user_service;
    
    // 1. 创建客户端，连接到服务器
    AuthClient auth("localhost:50051");
    
    // 2. 发送验证码
    auto send_result = auth.SendVerifyCode("13800138000", SmsScene::Register);
    if (!send_result.IsOk()) {
        std::cerr << "发送验证码失败: " << send_result.message << std::endl;
        return 1;
    }
    std::cout << "验证码已发送，" << send_result.Value() << " 秒后可重发" << std::endl;
    
    // 3. 注册（实际场景中验证码由用户输入）
    auto reg_result = auth.Register(
        "13800138000",    // 手机号
        "123456",         // 验证码
        "MyPassword123",  // 密码
        "张三"            // 昵称（可选）
    );
    
    if (reg_result.IsOk()) {
        auto& auth_result = reg_result.Value();
        std::cout << "注册成功！" << std::endl;
        std::cout << "用户ID: " << auth_result.user.uuid << std::endl;
        std::cout << "Access Token: " << auth_result.tokens.access_token << std::endl;
    } else {
        std::cerr << "注册失败: " << reg_result.message << std::endl;
    }
    
    return 0;
}
```

---

## 📚 AuthClient API

### 创建客户端

```cpp
// 方式 1：简单创建
AuthClient auth("localhost:50051");

// 方式 2：使用配置选项
ClientOptions options;
options.target = "localhost:50051";
options.timeout = std::chrono::milliseconds(10000);  // 10 秒超时
AuthClient auth(options);

// 方式 3：使用已有 Channel（高级用法）
auto channel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
AuthClient auth(channel);
```

### 发送验证码

```cpp
// 场景枚举
enum class SmsScene {
    Register,       // 注册
    Login,          // 登录
    ResetPassword,  // 重置密码
    DeleteUser      // 注销账号
};

// 发送验证码
auto result = auth.SendVerifyCode("13800138000", SmsScene::Register);

if (result.IsOk()) {
    int retry_after = result.Value();  // 重发间隔（秒）
    std::cout << retry_after << " 秒后可重发" << std::endl;
} else {
    // 常见错误：手机号格式错误、发送过于频繁、手机号已注册等
    std::cerr << "错误: " << result.message << std::endl;
}
```

### 注册

```cpp
auto result = auth.Register(
    "13800138000",     // 手机号
    "123456",          // 验证码
    "Password123",     // 密码（8-32位，需包含字母和数字）
    "昵称"             // 昵称（可选，传空字符串则不设置）
);

if (result.IsOk()) {
    auto& data = result.Value();
    
    // 用户信息
    std::cout << "用户ID: " << data.user.uuid << std::endl;
    std::cout << "手机号: " << data.user.mobile << std::endl;
    
    // Token（需要保存，后续请求要用）
    std::cout << "Access Token: " << data.tokens.access_token << std::endl;
    std::cout << "Refresh Token: " << data.tokens.refresh_token << std::endl;
    std::cout << "过期时间: " << data.tokens.expires_in << " 秒" << std::endl;
}
```

### 密码登录

```cpp
auto result = auth.LoginByPassword("13800138000", "Password123");

if (result.IsOk()) {
    auto& data = result.Value();
    std::cout << "登录成功，欢迎 " << data.user.display_name << std::endl;
    
    // 保存 Token
    std::string access_token = data.tokens.access_token;
    std::string refresh_token = data.tokens.refresh_token;
}
```

### 验证码登录

```cpp
// 先发送验证码
auth.SendVerifyCode("13800138000", SmsScene::Login);

// 用验证码登录
auto result = auth.LoginByCode("13800138000", "123456");
```

### 刷新 Token

```cpp
// Access Token 过期后，用 Refresh Token 获取新的 Token 对
auto result = auth.RefreshToken(refresh_token);

if (result.IsOk()) {
    auto& tokens = result.Value();
    std::cout << "新的 Access Token: " << tokens.access_token << std::endl;
    // 更新保存的 Token
}
```

### 登出

```cpp
auto result = auth.Logout(refresh_token);

if (result.IsOk()) {
    std::cout << "已登出" << std::endl;
    // 清除本地保存的 Token
}
```

### 重置密码

```cpp
// 1. 发送验证码
auth.SendVerifyCode("13800138000", SmsScene::ResetPassword);

// 2. 重置密码
auto result = auth.ResetPassword(
    "13800138000",    // 手机号
    "123456",         // 验证码
    "NewPassword456"  // 新密码
);

if (result.IsOk()) {
    std::cout << "密码已重置，请重新登录" << std::endl;
}
```

### 验证 Token（内部服务使用）

```cpp
// 用于微服务之间验证 Token
auto result = auth.ValidateToken(access_token);

if (result.IsOk()) {
    auto& validation = result.Value();
    std::cout << "Token 有效" << std::endl;
    std::cout << "用户ID: " << validation.user_uuid << std::endl;
    std::cout << "用户角色: " << static_cast<int>(validation.role) << std::endl;
}
```

---

## 📚 UserClient API

### 创建客户端

```cpp
// 创建客户端
UserClient user("localhost:50051");

// ⚠️ 必须设置 Access Token，否则所有请求都会失败
user.SetAccessToken(access_token);
```

### 获取当前用户信息

```cpp
auto result = user.GetCurrentUser();

if (result.IsOk()) {
    auto& user_info = result.Value();
    std::cout << "用户ID: " << user_info.uuid << std::endl;
    std::cout << "手机号: " << user_info.mobile << std::endl;
    std::cout << "昵称: " << user_info.display_name << std::endl;
    std::cout << "角色: " << static_cast<int>(user_info.role) << std::endl;
    std::cout << "是否禁用: " << (user_info.disabled ? "是" : "否") << std::endl;
}
```

### 更新用户信息

```cpp
// 更新昵称
auto result = user.UpdateUser("新昵称");

// 也可以传 std::nullopt 不更新
auto result = user.UpdateUser(std::nullopt);
```

### 修改密码

```cpp
auto result = user.ChangePassword("旧密码", "新密码");

if (result.IsOk()) {
    std::cout << "密码修改成功" << std::endl;
} else if (result.code == ErrorCode::WrongPassword) {
    std::cout << "旧密码错误" << std::endl;
}
```

### 注销账号

```cpp
// 1. 发送验证码
AuthClient auth("localhost:50051");
auth.SendVerifyCode("13800138000", SmsScene::DeleteUser);

// 2. 注销账号
auto result = user.DeleteUser("123456");  // 验证码

if (result.IsOk()) {
    std::cout << "账号已注销" << std::endl;
}
```

### 管理员：获取指定用户

```cpp
// 需要管理员权限
auto result = user.GetUser("user-uuid-xxx");

if (result.IsOk()) {
    auto& target_user = result.Value();
    std::cout << "用户昵称: " << target_user.display_name << std::endl;
}
```

### 管理员：获取用户列表

```cpp
// 获取所有用户（分页）
auto result = user.ListUsers(
    std::nullopt,    // 手机号过滤（可选）
    std::nullopt,    // 禁用状态过滤（可选）
    1,               // 页码
    20               // 每页数量
);

if (result.IsOk()) {
    auto& [users, page_info] = result.Value();
    
    std::cout << "总用户数: " << page_info.total_records << std::endl;
    std::cout << "总页数: " << page_info.total_pages << std::endl;
    
    for (const auto& u : users) {
        std::cout << u.uuid << " - " << u.mobile << " - " << u.display_name << std::endl;
    }
}

// 按条件过滤
auto result = user.ListUsers(
    "138",           // 手机号包含 "138"
    false,           // 只查未禁用的用户
    1,
    10
);
```

---

## 🎯 常见场景

### 场景 1：完整的用户认证流程

```cpp
#include "client/auth_client.h"
#include "client/user_client.h"

class UserSession {
public:
    UserSession(const std::string& server) 
        : auth_(server), user_(server) {}
    
    // 登录
    bool Login(const std::string& mobile, const std::string& password) {
        auto result = auth_.LoginByPassword(mobile, password);
        if (result.IsOk()) {
            access_token_ = result.Value().tokens.access_token;
            refresh_token_ = result.Value().tokens.refresh_token;
            user_.SetAccessToken(access_token_);
            return true;
        }
        last_error_ = result.message;
        return false;
    }
    
    // 获取当前用户
    std::optional<UserEntity> GetCurrentUser() {
        auto result = user_.GetCurrentUser();
        if (result.IsOk()) {
            return result.Value();
        }
        
        // Token 过期，尝试刷新
        if (result.code == ErrorCode::TokenExpired) {
            if (RefreshTokens()) {
                return GetCurrentUser();  // 重试
            }
        }
        
        last_error_ = result.message;
        return std::nullopt;
    }
    
    // 刷新 Token
    bool RefreshTokens() {
        auto result = auth_.RefreshToken(refresh_token_);
        if (result.IsOk()) {
            access_token_ = result.Value().access_token;
            refresh_token_ = result.Value().refresh_token;
            user_.SetAccessToken(access_token_);
            return true;
        }
        return false;
    }
    
    // 登出
    void Logout() {
        auth_.Logout(refresh_token_);
        access_token_.clear();
        refresh_token_.clear();
    }
    
    const std::string& GetLastError() const { return last_error_; }
    
private:
    AuthClient auth_;
    UserClient user_;
    std::string access_token_;
    std::string refresh_token_;
    std::string last_error_;
};

// 使用
int main() {
    UserSession session("localhost:50051");
    
    if (session.Login("13800138000", "Password123")) {
        auto user = session.GetCurrentUser();
        if (user) {
            std::cout << "欢迎，" << user->display_name << std::endl;
        }
        
        session.Logout();
    } else {
        std::cerr << "登录失败: " << session.GetLastError() << std::endl;
    }
}
```

### 场景 2：Token 自动刷新

```cpp
class TokenManager {
public:
    TokenManager(AuthClient& auth, const std::string& refresh_token)
        : auth_(auth), refresh_token_(refresh_token) {}
    
    // 获取有效的 Access Token
    std::string GetAccessToken() {
        auto now = std::chrono::system_clock::now();
        
        // 如果 Token 即将过期（提前 1 分钟刷新）
        if (now >= expires_at_ - std::chrono::minutes(1)) {
            Refresh();
        }
        
        return access_token_;
    }
    
private:
    void Refresh() {
        auto result = auth_.RefreshToken(refresh_token_);
        if (result.IsOk()) {
            access_token_ = result.Value().access_token;
            refresh_token_ = result.Value().refresh_token;
            expires_at_ = std::chrono::system_clock::now() 
                        + std::chrono::seconds(result.Value().expires_in);
        }
    }
    
    AuthClient& auth_;
    std::string access_token_;
    std::string refresh_token_;
    std::chrono::system_clock::time_point expires_at_;
};
```

### 场景 3：错误处理最佳实践

```cpp
void HandleAuthResult(const Result<AuthResult>& result) {
    if (result.IsOk()) {
        std::cout << "操作成功" << std::endl;
        return;
    }
    
    // 根据错误码处理不同情况
    switch (result.code) {
        case ErrorCode::InvalidArgument:
            std::cout << "参数错误: " << result.message << std::endl;
            break;
            
        case ErrorCode::WrongPassword:
            std::cout << "用户名或密码错误" << std::endl;
            break;
            
        case ErrorCode::AccountLocked:
            std::cout << "账号已锁定，请稍后再试" << std::endl;
            break;
            
        case ErrorCode::UserNotFound:
            std::cout << "用户不存在" << std::endl;
            break;
            
        case ErrorCode::MobileTaken:
            std::cout << "手机号已被注册" << std::endl;
            break;
            
        case ErrorCode::CaptchaWrong:
            std::cout << "验证码错误" << std::endl;
            break;
            
        case ErrorCode::CaptchaExpired:
            std::cout << "验证码已过期，请重新获取" << std::endl;
            break;
            
        case ErrorCode::RateLimited:
            std::cout << "请求过于频繁: " << result.message << std::endl;
            break;
            
        case ErrorCode::ServiceUnavailable:
            std::cout << "服务暂不可用，请稍后重试" << std::endl;
            break;
            
        default:
            std::cout << "未知错误: " << result.message << std::endl;
    }
}
```

### 场景 4：配置超时

```cpp
// 方式 1：创建时配置
ClientOptions options;
options.target = "localhost:50051";
options.timeout = std::chrono::milliseconds(10000);  // 10 秒
options.connect_timeout = std::chrono::milliseconds(5000);  // 5 秒连接超时

AuthClient auth(options);

// 方式 2：动态设置
AuthClient auth("localhost:50051");
auth.SetTimeout(std::chrono::milliseconds(30000));  // 30 秒（适合慢网络）
```

### 场景 5：微服务间调用（Token 验证）

```cpp
// 网关服务验证用户 Token
class GatewayService {
public:
    GatewayService(const std::string& auth_service_addr)
        : auth_client_(auth_service_addr) {}
    
    // 验证请求中的 Token
    bool ValidateRequest(const std::string& auth_header, std::string& user_id) {
        // 提取 Bearer Token
        if (auth_header.substr(0, 7) != "Bearer ") {
            return false;
        }
        std::string token = auth_header.substr(7);
        
        // 调用认证服务验证
        auto result = auth_client_.ValidateToken(token);
        if (result.IsOk()) {
            user_id = result.Value().user_uuid;
            return true;
        }
        
        return false;
    }
    
private:
    AuthClient auth_client_;
};
```

---

## 📊 Result<T> 使用说明

所有客户端方法都返回 `Result<T>` 类型：

```cpp
template<typename T>
struct Result {
    ErrorCode code;         // 错误码
    std::string message;    // 错误信息
    std::optional<T> data;  // 数据（成功时有值）
    
    // 状态检查
    bool IsOk() const;      // 是否成功
    bool IsErr() const;     // 是否失败
    
    // 数据访问
    const T& Value() const; // 获取数据（成功时）
    T ValueOr(default_val); // 获取数据或默认值
};
```

**使用示例：**

```cpp
auto result = auth.LoginByPassword(mobile, password);

// 方式 1：显式检查
if (result.IsOk()) {
    auto& data = result.Value();
    // 使用 data
} else {
    std::cerr << "错误 [" << static_cast<int>(result.code) << "]: " 
              << result.message << std::endl;
}

// 方式 2：使用 operator bool
if (result) {
    // 成功
}

// 方式 3：获取可选数据
auto data = result.GetData();  // 返回 std::optional<T>
if (data.has_value()) {
    // 使用 data.value()
}
```

---

## ⚠️ 常见错误码

| 错误码 | 说明 | 建议处理方式 |
|--------|------|-------------|
| `Ok` | 成功 | - |
| `InvalidArgument` | 参数无效 | 检查输入参数 |
| `WrongPassword` | 密码错误 | 提示重新输入 |
| `AccountLocked` | 账号锁定 | 提示等待或联系客服 |
| `UserNotFound` | 用户不存在 | 引导注册 |
| `MobileTaken` | 手机号已注册 | 引导登录 |
| `CaptchaWrong` | 验证码错误 | 提示重新输入 |
| `CaptchaExpired` | 验证码过期 | 重新发送验证码 |
| `RateLimited` | 请求过于频繁 | 显示倒计时 |
| `TokenExpired` | Token 过期 | 刷新 Token |
| `TokenInvalid` | Token 无效 | 重新登录 |
| `ServiceUnavailable` | 服务不可用 | 稍后重试 |
| `AdminRequired` | 需要管理员权限 | 提示权限不足 |

---

## 🔄 完整示例：命令行登录工具

```cpp
#include "client/auth_client.h"
#include "client/user_client.h"
#include <iostream>
#include <string>

using namespace user_service;

int main() {
    AuthClient auth("localhost:50051");
    UserClient user("localhost:50051");
    
    std::string mobile, password;
    
    std::cout << "=== 用户登录 ===" << std::endl;
    std::cout << "手机号: ";
    std::cin >> mobile;
    std::cout << "密码: ";
    std::cin >> password;
    
    // 登录
    auto login_result = auth.LoginByPassword(mobile, password);
    
    if (!login_result.IsOk()) {
        std::cerr << "登录失败: " << login_result.message << std::endl;
        return 1;
    }
    
    auto& auth_data = login_result.Value();
    std::cout << "\n✓ 登录成功！" << std::endl;
    
    // 设置 Token
    user.SetAccessToken(auth_data.tokens.access_token);
    
    // 获取用户信息
    auto user_result = user.GetCurrentUser();
    
    if (user_result.IsOk()) {
        auto& info = user_result.Value();
        std::cout << "\n=== 用户信息 ===" << std::endl;
        std::cout << "ID: " << info.uuid << std::endl;
        std::cout << "手机号: " << info.mobile << std::endl;
        std::cout << "昵称: " << info.display_name << std::endl;
        std::cout << "创建时间: " << info.created_at << std::endl;
    }
    
    // 登出
    std::cout << "\n按 Enter 登出...";
    std::cin.ignore();
    std::cin.get();
    
    auth.Logout(auth_data.tokens.refresh_token);
    std::cout << "已登出" << std::endl;
    
    return 0;
}
```

---

## 📁 相关文件

```
include/client/
├── auth_client.h       # 认证客户端头文件
├── user_client.h       # 用户客户端头文件
└── client_options.h    # 配置选项

src/client/
├── auth_client.cpp     # 认证客户端实现
├── user_client.cpp     # 用户客户端实现
└── CMakeLists.txt      # 构建配置
```

---

## ❓ 常见问题

### Q1: 连接失败怎么办？

```cpp
auto result = auth.LoginByPassword(mobile, password);
if (result.code == ErrorCode::ServiceUnavailable) {
    // 检查：
    // 1. 服务器是否启动
    // 2. 地址端口是否正确
    // 3. 防火墙是否放行
    std::cerr << "无法连接服务器" << std::endl;
}
```

### Q2: 如何处理 Token 过期？

```cpp
auto result = user.GetCurrentUser();

if (result.code == ErrorCode::TokenExpired) {
    // 使用 Refresh Token 刷新
    auto refresh_result = auth.RefreshToken(refresh_token);
    if (refresh_result.IsOk()) {
        user.SetAccessToken(refresh_result.Value().access_token);
        // 重试原请求
        result = user.GetCurrentUser();
    } else {
        // Refresh Token 也失效，需要重新登录
        std::cout << "登录已过期，请重新登录" << std::endl;
    }
}
```

### Q3: 如何在多线程中使用？

```cpp
// 每个线程创建独立的客户端实例
void WorkerThread(const std::string& server, const std::string& token) {
    UserClient user(server);
    user.SetAccessToken(token);
    
    auto result = user.GetCurrentUser();
    // ...
}

// 或使用连接池（高级用法）
// gRPC Channel 是线程安全的，可以共享
auto channel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());

std::vector<std::thread> threads;
for (int i = 0; i < 10; ++i) {
    threads.emplace_back([channel, &token]() {
        UserClient user(channel);  // 共享 Channel
        user.SetAccessToken(token);
        // ...
    });
}
```

### Q4: 如何设置 TLS？

```cpp
ClientOptions options;
options.target = "user-service.example.com:443";
options.use_tls = true;
options.ca_cert_path = "/path/to/ca.crt";

AuthClient auth(options);
```

---

## 🎉 总结

| 客户端 | 主要方法 | 用途 |
|--------|---------|------|
| `AuthClient` | `SendVerifyCode` | 发送验证码 |
|  | `Register` | 用户注册 |
|  | `LoginByPassword` | 密码登录 |
|  | `LoginByCode` | 验证码登录 |
|  | `RefreshToken` | 刷新 Token |
|  | `Logout` | 登出 |
|  | `ResetPassword` | 重置密码 |
| `UserClient` | `GetCurrentUser` | 获取当前用户 |
|  | `UpdateUser` | 更新用户信息 |
|  | `ChangePassword` | 修改密码 |
|  | `DeleteUser` | 注销账号 |
|  | `GetUser` | 获取指定用户（管理员） |
|  | `ListUsers` | 获取用户列表（管理员） |

