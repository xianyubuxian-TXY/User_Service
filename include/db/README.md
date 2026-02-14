# DB 模块设计文档

## 目录结构

```
include/
├── db/
│   ├── mysql_connection.h    # MySQL 连接封装
│   ├── mysql_result.h        # 查询结果集封装
│   └── user_db.h             # 用户数据访问层（DAO）
├── pool/
│   └── connection_pool.h     # 泛型连接池模板
└── exception/
    └── mysql_exception.h     # MySQL 异常定义

src/db/
├── mysql_connection.cpp
├── mysql_result.cpp
└── user_db.cpp
```

---

## 1. 模块概述

DB 模块提供了完整的 MySQL 数据库访问能力，包括：

- **泛型连接池**：线程安全、自动重连、RAII 资源管理
- **连接封装**：参数化查询防 SQL 注入、自动重试
- **结果集封装**：支持按索引/列名获取、类型安全
- **异常体系**：细分错误类型，便于精确处理

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          DB 模块架构                                     │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   业务层 (UserDB / TokenRepository)                                     │
│       │                                                                 │
│       │  pool_->CreateConnection()                                      │
│       ▼                                                                 │
│   ┌─────────────────────────────────────────────────────────────────┐  │
│   │  TemplateConnectionPool<MySQLConnection>                         │  │
│   │                                                                  │  │
│   │  ┌─────────────────────────────────────────────────────────┐    │  │
│   │  │  ConnectionGuard (RAII)                                  │    │  │
│   │  │      │                                                   │    │  │
│   │  │      ▼                                                   │    │  │
│   │  │  MySQLConnection                                         │    │  │
│   │  │      │                                                   │    │  │
│   │  │      ├── Query()  ──────▶  MySQLResult                   │    │  │
│   │  │      │                         │                         │    │  │
│   │  │      │                         ├── Next()                │    │  │
│   │  │      │                         ├── GetString()           │    │  │
│   │  │      │                         └── GetInt()              │    │  │
│   │  │      │                                                   │    │  │
│   │  │      └── Execute() ──────▶  affected_rows               │    │  │
│   │  └─────────────────────────────────────────────────────────┘    │  │
│   │                                                                  │  │
│   │  析构时自动归还连接到池中                                        │  │
│   └─────────────────────────────────────────────────────────────────┘  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 泛型连接池 (TemplateConnectionPool)

### 2.1 设计特点

| 特性 | 说明 |
|------|------|
| **泛型设计** | 模板类支持 `MySQLConnection`、`RedisConnection` 等不同连接类型 |
| **编译期类型推导** | 自动从连接类型推导配置类型，无需手动指定 |
| **RAII 资源管理** | `ConnectionGuard` 自动归还连接，防止泄漏 |
| **线程安全** | 内部使用互斥锁 + 条件变量 |
| **连接健康检查** | 获取/归还时自动检测，无效连接自动重建 |
| **超时机制** | 获取连接最多等待 5 秒，避免无限阻塞 |

### 2.2 类型推导机制

```cpp
// 连接类型 → 配置类型的编译期映射
template<typename T> struct ConnectionConfig;

template<> struct ConnectionConfig<MySQLConnection> { 
    using type = MySQLConfig;   // MySQLConnection → MySQLConfig
};

template<> struct ConnectionConfig<RedisConnection> { 
    using type = RedisConfig;   // RedisConnection → RedisConfig
};

// 使用别名简化
template<typename T>
using Config_t = typename ConnectionConfig<T>::type;
```

**好处：**
- 避免 `TemplateConnectionPool<MySQLConnection, MySQLConfig>` 这样的冗余写法
- 编译期检查，类型不匹配直接报错

### 2.3 创建连接池

```cpp
#include "pool/connection_pool.h"
#include "db/mysql_connection.h"
#include "config/config.h"

// 类型别名（推荐）
using MySQLPool = TemplateConnectionPool<MySQLConnection>;

// 方式1：从全局配置创建
auto global_config = std::make_shared<Config>(Config::LoadFromFile("config.yaml"));

auto pool = std::make_shared<MySQLPool>(
    global_config,
    [](const MySQLConfig& cfg) {
        return std::make_unique<MySQLConnection>(cfg);
    }
);

// 连接池会自动从 global_config 中提取 mysql 子配置
// 并预创建 config.mysql.pool_size 个连接
```

### 2.4 使用 ConnectionGuard（推荐方式）

```cpp
// ConnectionGuard 是 RAII 守卫类
// 构造时从池中获取连接，析构时自动归还

void QueryUser(MySQLPool& pool, int64_t user_id) {
    // 获取连接（超时5秒）
    auto conn = pool.CreateConnection();  // 返回 ConnectionGuard
    
    // 通过 -> 操作符访问 MySQLConnection 的方法
    auto result = conn->Query(
        "SELECT * FROM users WHERE id = ?",
        {user_id}
    );
    
    while (result.Next()) {
        std::cout << result.GetString("username").value_or("") << std::endl;
    }
    
}  // conn 析构，自动归还连接到池中
```

### 2.5 ConnectionGuard 生命周期

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    ConnectionGuard 生命周期                              │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  auto conn = pool.CreateConnection();                                   │
│       │                                                                 │
│       ▼                                                                 │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  构造 ConnectionGuard                                            │   │
│  │                                                                  │   │
│  │  1. 调用 pool.Acquire()                                          │   │
│  │  2. 等待可用连接（最多5秒）                                       │   │
│  │  3. 从队列头部取出连接                                           │   │
│  │  4. 检查连接有效性（conn->Valid()）                              │   │
│  │     • 有效 → 直接使用                                            │   │
│  │     • 无效 → 重建连接                                            │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│       │                                                                 │
│       ▼                                                                 │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  使用连接                                                        │   │
│  │                                                                  │   │
│  │  conn->Query("SELECT ...");                                      │   │
│  │  conn->Execute("INSERT ...");                                    │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│       │                                                                 │
│       ▼  离开作用域                                                     │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  析构 ConnectionGuard                                            │   │
│  │                                                                  │   │
│  │  1. 调用 pool.Release(conn)                                      │   │
│  │  2. 检查连接有效性                                               │   │
│  │     • 有效 → 放回队列尾部                                        │   │
│  │     • 无效 → 重建后放回                                          │   │
│  │  3. notify_one() 唤醒等待的线程                                  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.6 注意事项

```cpp
// ❌ 错误：不要让 ConnectionGuard 的生命周期过长
class BadExample {
    MySQLPool::ConnectionGuard conn_;  // 长期持有连接
public:
    BadExample(MySQLPool& pool) : conn_(pool.CreateConnection()) {}
    // 连接被一个对象独占，其他请求可能获取不到连接
};

// ✓ 正确：用完即还
void GoodExample(MySQLPool& pool) {
    {
        auto conn = pool.CreateConnection();
        conn->Execute("INSERT ...");
    }  // 立即归还
    
    // 做其他事情...
    
    {
        auto conn = pool.CreateConnection();  // 需要时再获取
        conn->Query("SELECT ...");
    }
}

// ❌ 错误：不要跨线程传递 ConnectionGuard
std::thread t([guard = std::move(conn)]() {
    guard->Query("...");  // 危险！
});

// ✓ 正确：每个线程自己获取连接
std::thread t([&pool]() {
    auto conn = pool.CreateConnection();
    conn->Query("...");
});
```

---

## 3. MySQLConnection 使用

### 3.1 API 概览

```cpp
class MySQLConnection {
public:
    // 参数类型：支持多种类型
    using Param = std::variant<nullptr_t, int64_t, uint64_t, double, std::string, bool>;
    
    // 构造函数
    explicit MySQLConnection(const MySQLConfig& config);
    
    // 连接状态
    bool Valid() const;           // 检查连接是否有效
    MYSQL* Get();                 // 获取原生句柄（谨慎使用）
    
    // 查询操作（SELECT）
    MySQLResult Query(const std::string& sql, std::initializer_list<Param> params = {});
    MySQLResult Query(const std::string& sql, const std::vector<Param>& params);
    
    // 执行操作（INSERT/UPDATE/DELETE）
    uint64_t Execute(const std::string& sql, std::initializer_list<Param> params = {});
    uint64_t Execute(const std::string& sql, const std::vector<Param>& params);
    
    // 获取自增ID
    uint64_t LastInsertId();
};
```

### 3.2 参数化查询（防 SQL 注入）

**核心原则：永远不要拼接用户输入到 SQL 中！**

```cpp
// ❌ 危险：SQL 注入漏洞
std::string username = "'; DROP TABLE users; --";
std::string sql = "SELECT * FROM users WHERE username = '" + username + "'";
conn->Query(sql);  // 会执行 DROP TABLE！

// ✓ 安全：参数化查询
std::string username = "'; DROP TABLE users; --";
auto result = conn->Query(
    "SELECT * FROM users WHERE username = ?",
    {username}  // 参数会被正确转义
);
// 实际执行：SELECT * FROM users WHERE username = '\'; DROP TABLE users; --'
```

### 3.3 支持的参数类型

```cpp
using Param = std::variant<nullptr_t, int64_t, uint64_t, double, std::string, bool>;
```

| 类型 | SQL 转换 | 示例 |
|------|----------|------|
| `nullptr_t` | `NULL` | `conn->Execute("INSERT INTO t (a) VALUES (?)", {nullptr})` |
| `int64_t` / `uint64_t` | 数字 | `conn->Query("... WHERE id = ?", {123})` |
| `double` | 数字 | `conn->Query("... WHERE price > ?", {99.9})` |
| `std::string` | 转义字符串 | `conn->Query("... WHERE name = ?", {"Tom"})` |
| `bool` | `0` / `1` | `conn->Execute("... SET disabled = ?", {true})` |

### 3.4 Query 示例

```cpp
auto conn = pool.CreateConnection();

// 无参数查询
auto result = conn->Query("SELECT COUNT(*) FROM users");

// 单参数查询
auto result = conn->Query(
    "SELECT * FROM users WHERE id = ?",
    {user_id}
);

// 多参数查询
auto result = conn->Query(
    "SELECT * FROM users WHERE role = ? AND disabled = ? LIMIT ?, ?",
    {0, false, offset, limit}  // role=0, disabled=false, 分页
);

// 使用 vector 传参（参数数量动态）
std::vector<MySQLConnection::Param> params;
params.push_back(mobile_like);
if (disabled_filter.has_value()) {
    params.push_back(disabled_filter.value());
}
auto result = conn->Query(sql, params);
```

### 3.5 Execute 示例

```cpp
auto conn = pool.CreateConnection();

// INSERT
conn->Execute(
    "INSERT INTO users (uuid, mobile, password_hash) VALUES (?, ?, ?)",
    {uuid, mobile, password_hash}
);
uint64_t new_id = conn->LastInsertId();  // 获取自增ID

// UPDATE
uint64_t affected = conn->Execute(
    "UPDATE users SET display_name = ?, disabled = ? WHERE uuid = ?",
    {display_name, disabled, uuid}
);
if (affected == 0) {
    // 没有更新任何行（用户不存在）
}

// DELETE
uint64_t deleted = conn->Execute(
    "DELETE FROM user_sessions WHERE expires_at <= NOW()",
    {}
);
LOG_INFO("Cleaned {} expired sessions", deleted);
```

### 3.6 事务处理

```cpp
auto conn = pool.CreateConnection();

try {
    // 开始事务
    conn->Execute("START TRANSACTION", {});
    
    // 执行多个操作
    conn->Execute("UPDATE accounts SET balance = balance - ? WHERE id = ?", {100, from_id});
    conn->Execute("UPDATE accounts SET balance = balance + ? WHERE id = ?", {100, to_id});
    
    // 提交事务
    conn->Execute("COMMIT", {});
    
} catch (const MySQLException& e) {
    // 回滚事务
    conn->Execute("ROLLBACK", {});
    throw;
}
```

---

## 4. MySQLResult 使用

### 4.1 API 概览

```cpp
class MySQLResult {
public:
    // 遍历
    bool Next();              // 移动到下一行，返回是否还有数据
    
    // 元信息
    size_t RowCount() const;  // 总行数
    size_t FieldCount() const; // 列数
    bool Empty() const;       // 是否为空
    
    // 按索引获取（从0开始）
    bool IsNull(size_t col) const;
    std::optional<std::string> GetString(size_t col) const;
    std::optional<int64_t> GetInt(size_t col) const;
    std::optional<double> GetDouble(size_t col) const;
    
    // 按列名获取（推荐）
    bool IsNull(const std::string& col_name) const;
    std::optional<std::string> GetString(const std::string& col_name) const;
    std::optional<int64_t> GetInt(const std::string& col_name) const;
    std::optional<double> GetDouble(const std::string& col_name) const;
};
```

### 4.2 基本遍历模式

```cpp
auto result = conn->Query("SELECT id, username, email FROM users");

// 遍历所有行
while (result.Next()) {
    int64_t id = result.GetInt("id").value_or(0);
    std::string username = result.GetString("username").value_or("");
    
    // 处理可能为 NULL 的字段
    auto email = result.GetString("email");
    if (email.has_value()) {
        std::cout << "Email: " << email.value() << std::endl;
    } else {
        std::cout << "Email: (not set)" << std::endl;
    }
}
```

### 4.3 单行结果处理

```cpp
auto result = conn->Query("SELECT * FROM users WHERE id = ?", {user_id});

if (result.Next()) {
    // 找到用户
    UserEntity user;
    user.id = result.GetInt("id").value_or(0);
    user.uuid = result.GetString("uuid").value_or("");
    user.mobile = result.GetString("mobile").value_or("");
    user.disabled = result.GetInt("disabled").value_or(0) != 0;
    return user;
} else {
    // 用户不存在
    throw UserNotFoundException();
}
```

### 4.4 聚合查询

```cpp
// COUNT
auto result = conn->Query("SELECT COUNT(*) FROM users WHERE disabled = ?", {false});
int64_t count = 0;
if (result.Next()) {
    count = result.GetInt(0).value_or(0);  // 按索引获取第一列
}

// SUM / AVG
auto result = conn->Query("SELECT SUM(amount), AVG(amount) FROM orders");
if (result.Next()) {
    double total = result.GetDouble(0).value_or(0.0);
    double avg = result.GetDouble(1).value_or(0.0);
}
```

### 4.5 NULL 值处理

```cpp
// 方式1：使用 optional 的 value_or
std::string name = result.GetString("display_name").value_or("匿名用户");

// 方式2：显式检查
if (result.IsNull("email")) {
    // 处理 NULL 情况
} else {
    auto email = result.GetString("email").value();
}

// 方式3：使用 has_value()
auto email = result.GetString("email");
if (email.has_value()) {
    SendEmail(email.value());
}
```

### 4.6 按索引 vs 按列名获取

```cpp
// 按列名（推荐）：可读性好，不怕列顺序变化
auto id = result.GetInt("id");
auto name = result.GetString("username");

// 按索引：性能略好，但脆弱
auto id = result.GetInt(0);    // 第1列
auto name = result.GetString(1); // 第2列
// 如果 SELECT 列顺序变了，代码就错了

// 性能对比：
// - 按列名：内部有一次 map 查找，O(1)
// - 按索引：直接访问，O(1)
// 差异可忽略，优先选择可读性
```

### 4.7 完整使用示例

```cpp
UserEntity UserDB::ParseRow(MySQLResult& res) {
    UserEntity user;
    
    // 必填字段：使用 value_or 提供默认值
    user.id            = res.GetInt("id").value_or(0);
    user.uuid          = res.GetString("uuid").value_or("");
    user.mobile        = res.GetString("mobile").value_or("");
    user.password_hash = res.GetString("password_hash").value_or("");
    user.role          = IntToUserRole(res.GetInt("role").value_or(0));
    user.disabled      = res.GetInt("disabled").value_or(0) != 0;  // int → bool
    user.created_at    = res.GetString("created_at").value_or("");
    user.updated_at    = res.GetString("updated_at").value_or("");
    
    // 可选字段
    user.display_name  = res.GetString("display_name").value_or("");
    
    return user;
}

Result<UserEntity> UserDB::FindByUUID(const std::string& uuid) {
    try {
        auto conn = pool_->CreateConnection();
        
        auto res = conn->Query(
            "SELECT * FROM users WHERE uuid = ?",
            {uuid}
        );
        
        if (res.Next()) {
            return Result<UserEntity>::Ok(ParseRow(res));
        } else {
            return Result<UserEntity>::Fail(ErrorCode::UserNotFound);
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("FindByUUID failed: {}", e.what());
        return Result<UserEntity>::Fail(ErrorCode::ServiceUnavailable);
    }
}
```

---

## 5. 异常处理

### 5.1 异常类型

```cpp
// 基础异常：所有 MySQL 错误
class MySQLException : public std::runtime_error {
public:
    int mysql_errno() const;    // MySQL 错误码
    bool IsRetryable() const;   // 是否可重试
};

// 唯一键冲突（INSERT 时常见）
class MySQLDuplicateKeyException : public MySQLException {
public:
    const std::string& key_name() const;  // 冲突的索引名
};

// SQL 构建错误（参数数量不匹配）
class MySQLBuildException : public std::logic_error {};

// 结果集使用错误（如未调用 Next()）
class MySQLResultException : public std::runtime_error {};
```

### 5.2 异常处理示例

```cpp
Result<UserEntity> UserDB::Create(const UserEntity& user) {
    try {
        auto conn = pool_->CreateConnection();
        
        std::string uuid = UUIDHelper::Generate();
        
        conn->Execute(
            "INSERT INTO users (uuid, mobile, password_hash) VALUES (?, ?, ?)",
            {uuid, user.mobile, user.password_hash}
        );
        
        return FindByUUID(uuid);
        
    } catch (const MySQLDuplicateKeyException& e) {
        // 唯一键冲突：判断是哪个字段
        LOG_WARN("Duplicate key: {}", e.key_name());
        
        if (e.key_name() == "uk_mobile") {
            return Result<UserEntity>::Fail(ErrorCode::MobileTaken, "手机号已被注册");
        }
        return Result<UserEntity>::Fail(ErrorCode::UserAlreadyExists);
        
    } catch (const MySQLException& e) {
        // 其他 MySQL 错误
        LOG_ERROR("MySQL error {}: {}", e.mysql_errno(), e.what());
        
        if (e.IsRetryable()) {
            // 可重试的错误（死锁、连接断开等）
            // 可以选择重试或返回错误
        }
        
        return Result<UserEntity>::Fail(ErrorCode::ServiceUnavailable);
        
    } catch (const std::exception& e) {
        // 其他异常
        LOG_ERROR("Unexpected error: {}", e.what());
        return Result<UserEntity>::Fail(ErrorCode::Internal);
    }
}
```

### 5.3 可重试错误码

```cpp
bool MySQLException::IsRetryable() const {
    return errno_ == 1213 ||    // 死锁 (Deadlock found)
           errno_ == 2002 ||    // Socket 错误
           errno_ == 2003 ||    // 无法连接主机
           errno_ == 2006 ||    // MySQL server has gone away
           errno_ == 2013;      // Lost connection during query
}
```

---

## 6. 完整使用示例

### 6.1 DAO 层封装示例

```cpp
// user_db.h
class UserDB {
public:
    using MySQLPool = TemplateConnectionPool<MySQLConnection>;
    
    explicit UserDB(std::shared_ptr<MySQLPool> pool);
    
    Result<UserEntity> Create(const UserEntity& user);
    Result<UserEntity> FindById(int64_t id);
    Result<UserEntity> FindByUUID(const std::string& uuid);
    Result<void> Update(const UserEntity& user);
    Result<void> Delete(int64_t id);
    
private:
    std::shared_ptr<MySQLPool> pool_;
    UserEntity ParseRow(MySQLResult& res);
};

// user_db.cpp
UserDB::UserDB(std::shared_ptr<MySQLPool> pool)
    : pool_(std::move(pool)) {}

Result<UserEntity> UserDB::FindById(int64_t id) {
    try {
        // 1. 获取连接（RAII）
        auto conn = pool_->CreateConnection();
        
        // 2. 参数化查询
        auto res = conn->Query(
            "SELECT * FROM users WHERE id = ?",
            {id}
        );
        
        // 3. 处理结果
        if (res.Next()) {
            return Result<UserEntity>::Ok(ParseRow(res));
        }
        
        return Result<UserEntity>::Fail(ErrorCode::UserNotFound);
        
    } catch (const std::exception& e) {
        LOG_ERROR("FindById failed: {}", e.what());
        return Result<UserEntity>::Fail(ErrorCode::ServiceUnavailable);
    }
    // 4. 连接自动归还
}
```

### 6.2 Service 层调用示例

```cpp
// auth_service.cpp
Result<AuthResult> AuthService::LoginByPassword(
    const std::string& mobile,
    const std::string& password) {
    
    // 1. 查询用户
    auto user_res = user_db_->FindByMobile(mobile);
    if (!user_res.IsOk()) {
        // 用户不存在也返回"密码错误"，避免暴露用户是否存在
        return Result<AuthResult>::Fail(ErrorCode::WrongPassword, "用户名或密码错误");
    }
    
    auto& user = user_res.Value();
    
    // 2. 验证密码
    if (!PasswordHelper::Verify(password, user.password_hash)) {
        return Result<AuthResult>::Fail(ErrorCode::WrongPassword, "用户名或密码错误");
    }
    
    // 3. 生成 Token
    auto token_pair = jwt_srv_->GenerateTokenPair(user);
    
    // 4. 存储 Refresh Token
    token_repo_->SaveRefreshToken(user.id, token_pair.refresh_token);
    
    // 5. 返回结果
    AuthResult result;
    result.user = user;
    result.user.password_hash.clear();  // 不返回密码
    result.tokens = token_pair;
    
    return Result<AuthResult>::Ok(result);
}
```

---

## 7. 配置说明

```yaml
# config.yaml
mysql:
  host: "localhost"
  port: 3306
  database: "user_service"
  username: "root"
  password: "password"
  pool_size: 10                    # 连接池大小
  
  # 超时配置（毫秒）
  connection_timeout_ms: 5000      # 连接超时
  read_timeout_ms: 30000           # 读超时
  write_timeout_ms: 30000          # 写超时
  
  # 重试配置
  max_retries: 3                   # 最大重试次数
  retry_interval_ms: 1000          # 重试间隔
  
  # 其他
  auto_reconnect: true             # 自动重连
  charset: "utf8mb4"               # 字符集
```

---

## 8. 常见问题

### Q1: 获取连接超时？

```cpp
// 问题：pool.CreateConnection() 抛出超时异常
// 原因：所有连接都被占用

// 解决方案：
// 1. 增加 pool_size
// 2. 检查是否有连接泄漏（ConnectionGuard 生命周期过长）
// 3. 优化慢查询
```

### Q2: 连接断开后查询失败？

```cpp
// 问题：执行查询时报 "MySQL server has gone away"
// 原因：连接长时间空闲被服务端断开

// 解决方案：
// 连接池会自动检测并重建，通常不需要处理
// 如果频繁发生，检查：
// 1. MySQL 的 wait_timeout 配置
// 2. 应用的 auto_reconnect 配置
```

### Q3: 参数数量不匹配？

```cpp
// 问题：MySQLBuildException: Not enough parameters
// 原因：SQL 中的 ? 数量与参数数量不一致

conn->Query("SELECT * FROM users WHERE id = ? AND name = ?", {123});
//                                       ^            ^
//                                    2个占位符    只传了1个参数

// 解决：确保参数数量正确
conn->Query("SELECT * FROM users WHERE id = ? AND name = ?", {123, "Tom"});
```

---

## 9. 相关文件

| 文件 | 说明 |
|------|------|
| `include/pool/connection_pool.h` | 泛型连接池模板 |
| `include/db/mysql_connection.h` | MySQL 连接封装 |
| `include/db/mysql_result.h` | 查询结果集封装 |
| `include/db/user_db.h` | 用户 DAO 层 |
| `include/exception/mysql_exception.h` | MySQL 异常定义 |
| `src/db/*.cpp` | 实现文件 |

