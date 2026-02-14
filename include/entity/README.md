# Entity 模块 - 业务实体定义

## 📋 目录

- [概述](#概述)
- [架构设计](#架构设计)
- [核心组件](#核心组件)
- [使用指南](#使用指南)
- [API 参考](#api-参考)
- [设计原理](#设计原理)
- [最佳实践](#最佳实践)

---

## 概述

`entity` 模块定义了系统中的**核心业务实体**，是连接数据层与业务层的桥梁。这些实体对象用于在各层之间传递数据，保持领域模型的清晰和一致性。

### 核心特性

| 特性 | 说明 |
|------|------|
| 📦 **纯数据结构** | 仅包含数据字段，不包含业务逻辑 |
| 🔄 **层间传递** | DB → Service → Handler 统一数据载体 |
| 🎯 **类型安全** | 枚举类型确保角色、状态的安全使用 |
| 🧩 **可组合** | 实体间可自由组合形成复杂结构 |
| ⚡ **轻量级** | Header-only，零运行时开销 |

### 模块组成

| 文件 | 说明 |
|------|------|
| `user_entity.h` | 用户实体 + 角色枚举 |
| `token.h` | Token 会话实体 |
| `page.h` | 分页参数与结果 |

---

## 架构设计

```
┌─────────────────────────────────────────────────────────────────────┐
│                          Entity 在系统中的位置                        │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   ┌─────────────┐     ┌─────────────┐     ┌─────────────┐          │
│   │   Handler   │ ←── │   Service   │ ←── │     DB      │          │
│   │   (gRPC)    │     │   (业务)     │     │   (数据)    │          │
│   └─────────────┘     └─────────────┘     └─────────────┘          │
│          │                   │                   │                   │
│          │                   │                   │                   │
│          ▼                   ▼                   ▼                   │
│   ┌─────────────────────────────────────────────────────────┐       │
│   │                       Entity 实体层                      │       │
│   │                                                          │       │
│   │   ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │       │
│   │   │  UserEntity  │  │ TokenSession │  │  PageParams  │  │       │
│   │   │              │  │              │  │  PageResult  │  │       │
│   │   │  + UserRole  │  │              │  │              │  │       │
│   │   └──────────────┘  └──────────────┘  └──────────────┘  │       │
│   │                                                          │       │
│   └─────────────────────────────────────────────────────────┘       │
│                                                                      │
│   数据流向:                                                          │
│   ─────────                                                          │
│   DB 层: MySQL Row → ParseRow() → UserEntity                        │
│   Service 层: UserEntity → 业务处理 → UserEntity                     │
│   Handler 层: UserEntity → ToProto() → pb_user::User                │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### 实体关系图

```
┌─────────────────────────────────────────────────────────────────────┐
│                           实体关系                                   │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   ┌──────────────────┐                                              │
│   │    UserEntity    │                                              │
│   ├──────────────────┤         ┌──────────────────┐                 │
│   │ id: int64_t      │ 1     N │   TokenSession   │                 │
│   │ uuid: string     │────────►├──────────────────┤                 │
│   │ mobile: string   │         │ id: int64_t      │                 │
│   │ display_name     │         │ user_id: int64_t │                 │
│   │ password_hash    │         │ token_hash       │                 │
│   │ role: UserRole   │         │ expires_at       │                 │
│   │ disabled: bool   │         │ created_at       │                 │
│   │ created_at       │         └──────────────────┘                 │
│   │ updated_at       │                                              │
│   └──────────────────┘                                              │
│            │                                                         │
│            │ 枚举类型                                                 │
│            ▼                                                         │
│   ┌──────────────────┐         ┌──────────────────┐                 │
│   │    UserRole      │         │   分页结构        │                 │
│   ├──────────────────┤         ├──────────────────┤                 │
│   │ User = 0         │         │   PageParams     │                 │
│   │ Admin = 1        │         │   ├─ page        │                 │
│   │ SuperAdmin = 2   │         │   └─ page_size   │                 │
│   └──────────────────┘         │                  │                 │
│                                │   PageResult     │                 │
│                                │   ├─ total_records│                │
│                                │   ├─ total_pages │                 │
│                                │   ├─ page        │                 │
│                                │   └─ page_size   │                 │
│                                └──────────────────┘                 │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 核心组件

### 1. UserEntity - 用户实体

```cpp
// include/entity/user_entity.h

enum class UserRole {
    User = 0,           // 普通用户
    Admin = 1,          // 管理员
    SuperAdmin = 2      // 超级管理员
};

struct UserEntity {
    int64_t id = 0;              // 数据库自增主键（内部使用）
    std::string uuid;            // UUID（对外暴露的用户标识）
    std::string mobile;          // 手机号
    std::string display_name;    // 显示名称
    std::string password_hash;   // 密码哈希（敏感字段）
    UserRole role = UserRole::User;  // 用户角色
    bool disabled = false;       // 是否禁用
    std::string created_at;      // 创建时间
    std::string updated_at;      // 更新时间
};
```

### 2. TokenSession - 会话实体

```cpp
// include/entity/token.h

struct TokenSession {
    int64_t id = 0;              // 会话ID
    int64_t user_id = 0;         // 关联的用户ID
    std::string token_hash;      // Token 哈希值（非明文）
    std::string expires_at;      // 过期时间（MySQL DATETIME）
    std::string created_at;      // 创建时间
};
```

### 3. Page - 分页结构

```cpp
// include/entity/page.h

// 分页请求参数
struct PageParams {
    int page = 1;           // 当前页码（从 1 开始）
    int page_size = 20;     // 每页记录数

    int Offset() const;     // 计算 SQL OFFSET
    void Validate();        // 参数校验与修正
};

// 分页响应结果
struct PageResult {
    int total_records = 0;  // 总记录数
    int total_pages = 0;    // 总页数
    int page = 1;           // 当前页码
    int page_size = 20;     // 每页记录数

    static PageResult Create(int page, int page_size, int total_records);
};
```

---

## 使用指南

### UserEntity 使用示例

#### 创建用户

```cpp
#include "entity/user_entity.h"

// 构建新用户
UserEntity user;
user.mobile = "13800138000";
user.display_name = "张三";
user.password_hash = PasswordHelper::Hash("password123");
user.role = UserRole::User;

// 传递给 DB 层创建
auto result = user_db->Create(user);
if (result.IsOk()) {
    auto& created_user = result.Value();
    LOG_INFO("用户创建成功: uuid={}", created_user.uuid);
}
```

#### 角色判断

```cpp
#include "entity/user_entity.h"

void CheckPermission(const UserEntity& user) {
    // 使用枚举比较
    if (user.role == UserRole::SuperAdmin) {
        // 超级管理员权限
    }
    
    // 使用辅助函数
    if (IsAdmin(user.role)) {
        // 管理员权限（Admin 或 SuperAdmin）
    }
    
    if (IsSuperAdmin(user.role)) {
        // 仅超级管理员
    }
}
```

#### 角色转换

```cpp
// 枚举 → 字符串（用于 JWT Claims）
std::string role_str = UserRoleToString(user.role);  // "0", "1", "2"

// 字符串 → 枚举（从 JWT Claims 解析）
UserRole role = StringToUserRole("1");  // UserRole::Admin

// 枚举 → 整数（用于数据库存储）
int role_int = UserRoleToInt(user.role);  // 0, 1, 2

// 整数 → 枚举（从数据库读取）
UserRole role = IntToUserRole(1);  // UserRole::Admin
```

#### 敏感信息处理

```cpp
// 返回给客户端前清除敏感字段
UserEntity SafeUser(const UserEntity& user) {
    UserEntity safe = user;
    safe.password_hash.clear();  // 清除密码哈希
    return safe;
}

// 在 Service 层使用
Result<UserEntity> UserService::GetCurrentUser(const std::string& uuid) {
    auto result = user_db_->FindByUUID(uuid);
    if (result.IsOk()) {
        result.Value().password_hash.clear();  // 清除敏感信息
    }
    return result;
}
```

---

### TokenSession 使用示例

```cpp
#include "entity/token.h"

// 在 TokenRepository 中使用
Result<TokenSession> TokenRepository::FindByTokenHash(const std::string& hash) {
    auto conn = pool_->CreateConnection();
    
    auto res = conn->Query(
        "SELECT id, user_id, token_hash, expires_at, created_at "
        "FROM user_sessions WHERE token_hash = ?",
        {hash}
    );
    
    if (res.Next()) {
        TokenSession session;
        session.id = res.GetInt("id").value_or(0);
        session.user_id = res.GetInt("user_id").value_or(0);
        session.token_hash = res.GetString("token_hash").value_or("");
        session.expires_at = res.GetString("expires_at").value_or("");
        session.created_at = res.GetString("created_at").value_or("");
        return Result<TokenSession>::Ok(session);
    }
    
    return Result<TokenSession>::Fail(ErrorCode::TokenInvalid);
}
```

---

### Page 分页使用示例

#### 分页查询

```cpp
#include "entity/page.h"

// 接收分页参数
PageParams params;
params.page = request->page();
params.page_size = request->page_size();
params.Validate();  // 自动修正非法值

// 计算 OFFSET
int offset = params.Offset();  // (page - 1) * page_size

// 执行分页查询
std::string sql = "SELECT * FROM users ORDER BY id DESC LIMIT ?, ?";
auto res = conn->Query(sql, {offset, params.page_size});
```

#### 构建分页结果

```cpp
// 查询总数
auto count_res = conn->Query("SELECT COUNT(*) FROM users");
int64_t total = 0;
if (count_res.Next()) {
    total = count_res.GetInt(0).value_or(0);
}

// 构建分页结果
PageResult page_result = PageResult::Create(
    params.page,       // 当前页
    params.page_size,  // 每页大小
    total              // 总记录数
);

// 访问分页信息
LOG_INFO("第 {}/{} 页，共 {} 条记录", 
    page_result.page, 
    page_result.total_pages, 
    page_result.total_records
);
```

#### 完整分页查询示例

```cpp
Result<ListUsersResult> UserService::ListUsers(
    std::optional<std::string> mobile_filter,
    std::optional<bool> disabled_filter,
    int32_t page,
    int32_t page_size) 
{
    // 1. 构建并校验分页参数
    PageParams params;
    params.page = page > 0 ? page : 1;
    params.page_size = std::clamp(page_size, 1, 100);  // 限制 1-100
    
    // 2. 构建查询条件
    UserQueryParams query;
    query.page_params = params;
    query.mobile_like = mobile_filter;
    query.disabled = disabled_filter;
    
    // 3. 查询总数
    auto count_res = user_db_->Count(query);
    if (!count_res.IsOk()) {
        return Result<ListUsersResult>::Fail(count_res.code);
    }
    int64_t total = count_res.Value();
    
    // 4. 查询数据
    auto list_res = user_db_->FindAll(query);
    if (!list_res.IsOk()) {
        return Result<ListUsersResult>::Fail(list_res.code);
    }
    
    // 5. 组装结果
    ListUsersResult result;
    result.users = std::move(list_res.Value());
    result.page_res = PageResult::Create(params.page, params.page_size, total);
    
    // 6. 清除敏感信息
    for (auto& user : result.users) {
        user.password_hash.clear();
    }
    
    return Result<ListUsersResult>::Ok(result);
}
```

---

## API 参考

### UserRole 枚举

| 值 | 整数 | 说明 |
|----|------|------|
| `User` | 0 | 普通用户 |
| `Admin` | 1 | 管理员 |
| `SuperAdmin` | 2 | 超级管理员 |

### UserRole 辅助函数

| 函数 | 说明 |
|------|------|
| `UserRoleToString(UserRole)` | 枚举转字符串 `"0"/"1"/"2"` |
| `StringToUserRole(string)` | 字符串转枚举 |
| `UserRoleToInt(UserRole)` | 枚举转整数 |
| `IntToUserRole(int)` | 整数转枚举 |
| `IsAdmin(UserRole)` | 是否管理员（Admin 或 SuperAdmin） |
| `IsSuperAdmin(UserRole)` | 是否超级管理员 |

### UserEntity 字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | `int64_t` | 数据库自增主键 |
| `uuid` | `string` | 对外暴露的唯一标识 |
| `mobile` | `string` | 手机号 |
| `display_name` | `string` | 显示名称 |
| `password_hash` | `string` | 密码哈希（敏感） |
| `role` | `UserRole` | 用户角色 |
| `disabled` | `bool` | 是否禁用 |
| `created_at` | `string` | 创建时间 |
| `updated_at` | `string` | 更新时间 |

### TokenSession 字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | `int64_t` | 会话ID |
| `user_id` | `int64_t` | 关联用户ID |
| `token_hash` | `string` | Token SHA256 哈希 |
| `expires_at` | `string` | 过期时间 |
| `created_at` | `string` | 创建时间 |

### PageParams 方法

| 方法 | 说明 |
|------|------|
| `Offset()` | 返回 `(page - 1) * page_size` |
| `Validate()` | 校验并修正参数（page >= 1, 1 <= page_size <= 100） |

### PageResult 方法

| 方法 | 说明 |
|------|------|
| `Create(page, page_size, total)` | 静态工厂方法，自动计算 total_pages |

---

## 设计原理

### 1. ID 双轨制设计

```
┌─────────────────────────────────────────────────────────────────────┐
│                          ID 双轨制                                   │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   ┌──────────────────────────────────────────────────────────────┐  │
│   │                       UserEntity                              │  │
│   │                                                               │  │
│   │   id: int64_t (内部)          uuid: string (外部)             │  │
│   │   ───────────────────         ────────────────────            │  │
│   │   • 数据库自增主键              • 全局唯一标识                  │  │
│   │   • 高性能索引                  • 对外暴露给客户端              │  │
│   │   • 内部关联使用                • URL/API 中使用                │  │
│   │   • 外键引用                    • 防止 ID 枚举攻击              │  │
│   │                                                               │  │
│   └──────────────────────────────────────────────────────────────┘  │
│                                                                      │
│   使用场景:                                                          │
│   ─────────                                                          │
│   • token_session.user_id = user.id       (内部关联用 id)           │
│   • response.user.id = user.uuid          (返回客户端用 uuid)       │
│   • /api/users/{uuid}                     (URL 中用 uuid)           │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### 2. 时间字段使用字符串

```cpp
// 为什么用 string 而不是 time_point？
struct UserEntity {
    std::string created_at;  // "2024-01-15 10:30:00"
    std::string updated_at;
};

// 原因：
// 1. 直接映射 MySQL DATETIME 类型，无需转换
// 2. 序列化到 JSON/Proto 更简单
// 3. 日志输出更直观
// 4. 避免时区处理的复杂性

// 如需时间计算，使用 time_utils.h 中的转换函数
auto tp = StringToTimePoint(user.created_at);
if (std::chrono::system_clock::now() - tp > std::chrono::hours(24)) {
    // 超过 24 小时
}
```

### 3. 分页参数自动校验

```cpp
void PageParams::Validate() {
    // 页码至少为 1
    if (page < 1) page = 1;
    
    // 每页大小限制
    if (page_size < 1) page_size = 20;
    if (page_size > 100) page_size = 100;  // 防止一次查太多
}

// 自动调用示例
PageParams params{-1, 500};  // 非法参数
params.Validate();
// 结果: params.page = 1, params.page_size = 100
```

### 4. 枚举类型安全

```cpp
// ✅ 使用 enum class（强类型枚举）
enum class UserRole {
    User = 0,
    Admin = 1,
    SuperAdmin = 2
};

// 优势：
// 1. 不会隐式转换为 int
UserRole role = UserRole::Admin;
// int x = role;  // 编译错误！

// 2. 必须使用作用域限定符
// role = Admin;  // 编译错误！
role = UserRole::Admin;  // 正确

// 3. 不同枚举不会冲突
enum class Status { Active = 1 };
// UserRole 和 Status 的值 1 不会混淆
```

---

## 最佳实践

### ✅ 推荐做法

```cpp
// 1. 返回前清除敏感字段
UserEntity user = user_db_->FindById(id).Value();
user.password_hash.clear();  // 永远不返回密码哈希
return user;

// 2. 使用辅助函数判断角色
if (IsAdmin(user.role)) {  // ✅ 可读性好
    // ...
}

// 3. 分页参数先校验
PageParams params{request.page(), request.page_size()};
params.Validate();  // ✅ 防止非法输入

// 4. 使用工厂方法创建 PageResult
auto result = PageResult::Create(page, size, total);  // ✅ 自动计算 total_pages

// 5. 内部用 id，外部用 uuid
token_session.user_id = user.id;        // ✅ 内部关联
response.set_user_id(user.uuid);        // ✅ 返回给客户端
```

### ❌ 避免的做法

```cpp
// 1. 不要在实体中添加业务逻辑
struct UserEntity {
    bool IsExpired() const { ... }  // ❌ 应放到 Service 层
};

// 2. 不要直接比较枚举和整数
if (user.role == 1) { ... }  // ❌ 类型不安全
if (user.role == UserRole::Admin) { ... }  // ✅

// 3. 不要忽略分页参数校验
int offset = (page - 1) * size;  // ❌ page=0 时 offset=-size
PageParams params{page, size};
params.Validate();               // ✅
int offset = params.Offset();

// 4. 不要返回带密码的实体
return user;  // ❌ 可能泄露 password_hash
user.password_hash.clear();
return user;  // ✅

// 5. 不要在实体中保存连接/资源
struct UserEntity {
    MySQLConnection* conn;  // ❌ 实体应该是纯数据
};
```

---

## 文件结构

```
include/entity/
├── user_entity.h    # 用户实体 + UserRole 枚举 + 转换函数
├── token.h          # TokenSession 会话实体
└── page.h           # PageParams + PageResult 分页结构
```

---

## 依赖关系

```
entity/
├── user_entity.h
│   └── <string>
│
├── token.h
│   └── <string>
│
└── page.h
    └── (无外部依赖)

被依赖方:
├── db/user_db.h          # 数据层使用
├── service/user_service.h # 业务层使用
├── handlers/user_handler.h # 接口层使用
└── common/proto_converter.h # Proto 转换使用
```

---

## 与 Proto 的映射关系

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Entity ↔ Proto 映射                               │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   UserEntity                      pb_user::User                     │
│   ───────────                     ─────────────                     │
│   uuid           ──────────────►  id                                │
│   mobile         ──────────────►  mobile                            │
│   display_name   ──────────────►  display_name                      │
│   role           ──► ToProto ──►  role (pb_auth::UserRole)         │
│   disabled       ──────────────►  disabled                          │
│   created_at     ──► ToProto ──►  created_at (Timestamp)           │
│   updated_at     ──► ToProto ──►  updated_at (Timestamp)           │
│   password_hash  ──────────────►  (不映射，永不返回)                 │
│   id             ──────────────►  (不映射，内部使用)                 │
│                                                                      │
│   PageParams                      pb_user::PageRequest              │
│   ──────────                      ────────────────────              │
│   page           ◄──────────────  page                              │
│   page_size      ◄──────────────  page_size                         │
│                                                                      │
│   PageResult                      pb_user::PageResponse             │
│   ──────────                      ─────────────────────             │
│   total_records  ──────────────►  total_records                     │
│   total_pages    ──────────────►  total_pages                       │
│   page           ──────────────►  page                              │
│   page_size      ──────────────►  page_size                         │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘

转换函数（定义在 common/proto_converter.h）:
• ToProtoUser(UserEntity, pb_user::User*)
• ToProtoUserInfo(UserEntity, pb_auth::UserInfo*)
• ToProtoUserRole(UserRole) → pb_auth::UserRole
• FromProtoPageRequest(pb_user::PageRequest) → PageParams
• ToProtoPageResponse(PageResult, pb_user::PageResponse*)
```

