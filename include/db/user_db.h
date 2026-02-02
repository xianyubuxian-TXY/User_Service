#pragma once

#include <optional>
#include <string>
#include <vector>
#include "pool/connection_pool.h"
#include "mysql_connection.h"
#include "config/config.h"
#include "common/result.h"
#include "entity/user_entity.h"
#include "entity/page.h"

namespace user_service {

struct UserQueryParams {
    PageParams page_params;
    std::optional<std::string> mobile_like;
    std::optional<bool> disabled;
};


class MySQLResult;

// DAO封装
class UserDB {
public:
    using MySQLPool = TemplateConnectionPool<MySQLConnection>;
    using UserListResult = Result<std::pair<std::vector<UserEntity>, PageResult>>;

    explicit UserDB(std::shared_ptr<MySQLPool> pool);

    // ==================== CRUD 操作 ====================
    
    // 创建用户
    Result<UserEntity> Create(const UserEntity& user);
    
    // 查询用户
    Result<UserEntity> FindById(int64_t id);
    Result<UserEntity> FindByUUID(const std::string& uuid);
    Result<UserEntity> FindByMobile(const std::string& mobile);  // 手机号查询（登录用）
    
    // 更新用户
    Result<void> Update(const UserEntity& user);
    
    // 删除用户
    Result<void> Delete(int64_t id);
    Result<void> DeleteByUUID(const std::string& uuid);
    
    // ==================== 分页查询 ====================
    
    // 查询用户列表（分页查询：手机号模糊查询）
    UserListResult FindAll(const PageParams& page, const std::string& mobile_filter = "");
    
    // 查询用户列表（分页查询：多参数查询）
    Result<std::vector<UserEntity>> FindAll(const UserQueryParams& params);
    // ==================== 辅助查询 ====================
    // 判断是否存在
    Result<bool> ExistsByMobile(const std::string& mobile);

    // 统计用户数量
    Result<int64_t> Count(const UserQueryParams& params);

private:
    // 根据字段寻找
    Result<UserEntity> FindByField(const std::string& field_name, const std::string& field_val);

    // 判断字段值是否存在
    Result<bool> ExistsByField(const std::string& field_name, const std::string& field_val);

    // 解析查询的一行
    UserEntity ParseRow(MySQLResult& res);

    std::shared_ptr<MySQLPool> pool_;
};

}
