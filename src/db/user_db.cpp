#include "user_db.h"
#include "entity/user_entity.h"
#include "entity/page.h"
#include "common/uuid.h"
#include "exception/mysql_exception.h"
#include "db/mysql_result.h"
#include "common/result.h"
#include "common/error_codes.h"

namespace user_service {

// 定义一个宏，用来检查连接是否有效（减少重复代码）
#define CHECK_CONN(Conn, ResultType)                                            \
    if (!Conn->Valid()) {                                                       \
        LOG_ERROR("mysql_conn is invalid");                                     \
        return ResultType::Fail(ErrorCode::ServiceUnavailable,                  \
                                GetErrorMessage(ErrorCode::ServiceUnavailable));\
    }

UserDB::UserDB(std::shared_ptr<MySQLPool> pool)
    : pool_(pool)
{}

// ==================== Create ====================

Result<UserEntity> UserDB::Create(const UserEntity& user) {
    try {
        auto conn = pool_->CreateConnection();

        // 检查连接是否有效
        CHECK_CONN(conn, Result<UserEntity>);

        // 生成UUID（通用唯一识别码）
        std::string uuid = UUIDHelper::Generate();

        // 拼接并执行sql
        // 字段：uuid, mobile, display_name, password_hash
        std::string sql = "INSERT INTO users (uuid, mobile, display_name, password_hash, role) "
                          "VALUES (?, ?, ?, ?, ?)";
        conn->Execute(sql, {
            uuid, 
            user.mobile, 
            user.display_name, 
            user.password_hash,
            UserRoleToInt(user.role)
        });
        
        // 通过UUID查询用户（id、created_at等是插入后自动填写的，所以需要再次获取）
        LOG_INFO("Create user success, mobile={}", user.mobile);
        return FindByUUID(uuid);

    } catch (const MySQLDuplicateKeyException& e) {
        // 唯一键冲突：获取冲突的键，返回对应错误码
        LOG_ERROR("Duplicate key error: {}", e.what());
        
        std::string key_name = e.key_name();
        ErrorCode code;
        
        if (key_name == "uk_mobile") {
            code = ErrorCode::MobileTaken;          // 手机号已被注册
        } else if (key_name == "uk_uuid") {
            code = ErrorCode::UserAlreadyExists;    // UUID冲突（极小概率）
        } else {
            code = ErrorCode::UserAlreadyExists;
        }
        
        return Result<UserEntity>::Fail(code, GetErrorMessage(code));

    } catch (const std::exception& e) {
        // 后端执行发生错误，统一返回"模糊信息"给前端
        LOG_ERROR("Create user failed: {}", e.what());
        ErrorCode code = ErrorCode::ServiceUnavailable;
        return Result<UserEntity>::Fail(code, GetErrorMessage(code));
    }
}

// ==================== Read ====================

Result<UserEntity> UserDB::FindById(int64_t id) {
    return FindByField("id", std::to_string(id));
}

Result<UserEntity> UserDB::FindByUUID(const std::string& uuid) {
    return FindByField("uuid", uuid);
}

Result<UserEntity> UserDB::FindByMobile(const std::string& mobile) {
    return FindByField("mobile", mobile);
}

// ==================== Update ====================

Result<void> UserDB::Update(const UserEntity& user) {
    try {
        auto conn = pool_->CreateConnection();

        // 检查连接是否有效
        CHECK_CONN(conn, Result<void>);

        // 拼接sql并执行
        // 注意：mobile 一般不允许直接修改，需要通过换绑接口（后续实现）
        std::string sql = "UPDATE users SET display_name = ?, password_hash = ?, "
                          "disabled = ?, role = ? WHERE uuid = ?";
        
        int64_t affect_row = conn->Execute(sql, {
            user.display_name, 
            user.password_hash,
            user.disabled,
            UserRoleToInt(user.role),  // 新增
            user.uuid
        });
        
        // 通过影响的行数判断是否更新成功
        if (affect_row == 0) {
            LOG_INFO("User with uuid={} not found", user.uuid);
            ErrorCode code = ErrorCode::UserNotFound;
            return Result<void>::Fail(code, GetErrorMessage(code));
        }
        
        LOG_INFO("User with uuid={} update success", user.uuid);
        return Result<void>::Ok();

    } catch (const std::exception& e) {
        // 统一返回"模糊信息"
        LOG_ERROR("Update user failed: {}", e.what());
        ErrorCode code = ErrorCode::ServiceUnavailable;
        return Result<void>::Fail(code, GetErrorMessage(code));
    }
}

// ==================== Delete ====================

Result<void> UserDB::Delete(int64_t id) {
    try {
        auto conn = pool_->CreateConnection();

        // 检查连接是否有效
        CHECK_CONN(conn, Result<void>);

        std::string sql = "DELETE FROM users WHERE id = ?";
        auto affect_row = conn->Execute(sql, {std::to_string(id)});

        // 通过影响的行数判断是否删除成功
        if (affect_row == 0) {
            LOG_INFO("User with id={} not found or already deleted", id);
            ErrorCode code = ErrorCode::UserNotFound;
            return Result<void>::Fail(code, GetErrorMessage(code));
        }
        
        LOG_INFO("User with id={} delete success", id);
        return Result<void>::Ok();

    } catch (const std::exception& e) {
        // 统一返回"模糊信息"
        LOG_ERROR("Delete user failed: {}", e.what());
        ErrorCode code = ErrorCode::ServiceUnavailable;
        return Result<void>::Fail(code, GetErrorMessage(code));
    }
}

Result<void> UserDB::DeleteByUUID(const std::string& uuid) {
    try {
        auto conn = pool_->CreateConnection();

        // 检查连接是否有效
        CHECK_CONN(conn, Result<void>);

        std::string sql = "DELETE FROM users WHERE uuid = ?";
        auto affect_row = conn->Execute(sql, {uuid});

        // 通过影响的行数判断是否删除成功
        if (affect_row == 0) {
            LOG_INFO("User with uuid={} not found or already deleted", uuid);
            ErrorCode code = ErrorCode::UserNotFound;
            return Result<void>::Fail(code, GetErrorMessage(code));
        }
        
        LOG_INFO("User with uuid={} delete success", uuid);
        return Result<void>::Ok();

    } catch (const std::exception& e) {
        // 统一返回"模糊信息"
        LOG_ERROR("Delete user by uuid failed: {}", e.what());
        ErrorCode code = ErrorCode::ServiceUnavailable;
        return Result<void>::Fail(code, GetErrorMessage(code));
    }
}

// ==================== 分页查询 ====================

UserDB::UserListResult UserDB::FindAll(const PageParams& page, const std::string& mobile_filter) {
    std::pair<std::vector<UserEntity>, PageResult> result;
    
    try {
        auto conn = pool_->CreateConnection();

        // 检查连接是否有效
        CHECK_CONN(conn, UserListResult);

        // 模糊查询模式
        std::string like_pattern = "%" + mobile_filter + "%";

        // 1. 先查询总记录数
        std::string count_sql = "SELECT COUNT(*) FROM users WHERE mobile LIKE ?";
        auto count_res = conn->Query(count_sql, {like_pattern});
        
        int64_t total_records = 0;
        if (count_res.Next()) {
            total_records = count_res.GetInt(0).value_or(0);
        }

        // 2. 填充分页信息
        result.second = PageResult::Create(page.page, page.page_size, total_records);

        // 3. 分页查询数据
        int64_t offset = static_cast<int64_t>(page.Offset());
        int64_t limit = static_cast<int64_t>(page.page_size);
        
        std::string data_sql = "SELECT * FROM users WHERE mobile LIKE ? "
                               "ORDER BY created_at DESC, id DESC "
                               "LIMIT ?, ?";
        auto data_res = conn->Query(data_sql, {
            like_pattern, 
            offset,
            limit
        });
        
        // 为 vector 预分配内存，避免频繁扩容
        result.first.reserve(page.page_size);
        
        // 解析结果
        while (data_res.Next()) {
            result.first.push_back(ParseRow(data_res));
        }

        return UserListResult::Ok(result);

    } catch (const std::exception& e) {
        LOG_ERROR("FindAll users failed: {}", e.what());
        ErrorCode code = ErrorCode::ServiceUnavailable;
        return UserListResult::Fail(code, GetErrorMessage(code));
    }
}

Result<std::vector<UserEntity>> UserDB::FindAll(const UserQueryParams& params) {
    try {
        auto conn = pool_->CreateConnection();

        // 检查连接是否有效
        CHECK_CONN(conn, Result<std::vector<UserEntity>>);

        // 动态构建 SQL 和参数
        std::string sql = "SELECT * FROM users WHERE 1=1";
        std::vector<MySQLConnection::Param> bindings;

        // 手机号模糊查询
        if (params.mobile_like.has_value() && !params.mobile_like.value().empty()) {
            sql += " AND mobile LIKE ?";
            bindings.push_back("%" + params.mobile_like.value() + "%");
        }

        // 禁用状态过滤
        if (params.disabled.has_value()) {
            sql += " AND disabled = ?";
            bindings.push_back(params.disabled.value());
        }

        // 排序 + 分页
        sql += " ORDER BY created_at DESC, id DESC LIMIT ?, ?";
        bindings.push_back(static_cast<int64_t>(params.page_params.Offset()));
        bindings.push_back(static_cast<int64_t>(params.page_params.page_size));

        // 执行查询
        auto res = conn->Query(sql, bindings);

        // 解析结果
        std::vector<UserEntity> users;
        users.reserve(params.page_params.page_size);

        while (res.Next()) {
            users.push_back(ParseRow(res));
        }

        LOG_DEBUG("FindAll users with params, count={}", users.size());
        return Result<std::vector<UserEntity>>::Ok(users);

    } catch (const std::exception& e) {
        LOG_ERROR("FindAll users with params failed: {}", e.what());
        ErrorCode code = ErrorCode::ServiceUnavailable;
        return Result<std::vector<UserEntity>>::Fail(code, GetErrorMessage(code));
    }
}

// ==================== 辅助查询 ====================

Result<bool> UserDB::ExistsByMobile(const std::string& mobile) {
    return ExistsByField("mobile", mobile);
}

// 统计用户数量
Result<int64_t> UserDB::Count(const UserQueryParams& params) {
    try {
        auto conn = pool_->CreateConnection();

        // 检查连接是否有效
        CHECK_CONN(conn, Result<int64_t>);

        // 动态构建 SQL 和参数（与 FindAll 保持一致的过滤条件）
        std::string sql = "SELECT COUNT(*) FROM users WHERE 1=1";
        std::vector<MySQLConnection::Param> bindings;

        // 手机号模糊查询
        if (params.mobile_like.has_value() && !params.mobile_like.value().empty()) {
            sql += " AND mobile LIKE ?";
            bindings.push_back("%" + params.mobile_like.value() + "%");
        }

        // 禁用状态过滤
        if (params.disabled.has_value()) {
            sql += " AND disabled = ?";
            bindings.push_back(params.disabled.value());
        }

        // 执行查询
        auto res = conn->Query(sql, bindings);

        int64_t count = 0;
        if (res.Next()) {
            count = res.GetInt(0).value_or(0);
        }

        LOG_DEBUG("Count users with params, count={}", count);
        return Result<int64_t>::Ok(count);

    } catch (const std::exception& e) {
        LOG_ERROR("Count users failed: {}", e.what());
        ErrorCode code = ErrorCode::ServiceUnavailable;
        return Result<int64_t>::Fail(code, GetErrorMessage(code));
    }
}


// ==================== 私有方法 ====================

Result<UserEntity> UserDB::FindByField(const std::string& field_name, const std::string& field_val) {
    try {
        auto conn = pool_->CreateConnection();

        // 检查连接是否有效
        CHECK_CONN(conn, Result<UserEntity>);

        // 注意：字段名不能参数化，需要拼接（field_name 是内部传入的安全值）
        // 参数值通过 "?" 传入，避免 SQL 注入
        std::string sql = "SELECT * FROM users WHERE " + field_name + " = ?";

        auto res = conn->Query(sql, {field_val});
        
        // 解析结果
        if (res.Next()) {
            UserEntity user = ParseRow(res);
            LOG_DEBUG("Find user by {}={} success", field_name, field_val);
            return Result<UserEntity>::Ok(user);
        } else {
            LOG_DEBUG("User not found by {}={}", field_name, field_val);
            ErrorCode code = ErrorCode::UserNotFound;
            return Result<UserEntity>::Fail(code, GetErrorMessage(code));
        }

    } catch (const std::exception& e) {
        LOG_ERROR("FindByField failed: {}", e.what());
        ErrorCode code = ErrorCode::ServiceUnavailable;
        return Result<UserEntity>::Fail(code, GetErrorMessage(code));
    }
}

Result<bool> UserDB::ExistsByField(const std::string& field_name, const std::string& field_val) {
    try {
        auto conn = pool_->CreateConnection();

        // 检查连接是否有效
        CHECK_CONN(conn, Result<bool>);

        // 注意：字段名不能参数化，需要拼接（field_name 是内部传入的安全值）
        std::string sql = "SELECT 1 FROM users WHERE " + field_name + " = ? LIMIT 1";
        auto res = conn->Query(sql, {field_val});

        bool exists = res.Next();
        return Result<bool>::Ok(exists);

    } catch (const std::exception& e) {
        LOG_ERROR("ExistsByField failed: {}", e.what());
        ErrorCode code = ErrorCode::ServiceUnavailable;
        return Result<bool>::Fail(code, GetErrorMessage(code));
    }
}

UserEntity UserDB::ParseRow(MySQLResult& res) {
    UserEntity user;
    
    // 必填字段
    user.id            = res.GetInt("id").value_or(0);
    user.uuid          = res.GetString("uuid").value_or("");
    user.mobile        = res.GetString("mobile").value_or("");
    user.password_hash = res.GetString("password_hash").value_or("");
    user.role          = IntToUserRole(res.GetInt("role").value_or(0));
    user.disabled      = res.GetInt("disabled").value_or(0) != 0;
    user.created_at    = res.GetString("created_at").value_or("");
    user.updated_at    = res.GetString("updated_at").value_or("");
    
    // 可选字段
    user.display_name  = res.GetString("display_name").value_or("");
    
    return user;
}

#undef CHECK_CONN   // 用完取消宏定义：避免污染全局

}
