#pragma once

#include <optional>
#include <string>
#include "pool/connection_pool.h"
#include "mysql_connection.h"
#include "config/config.h"
#include "../../build/api/proto/user_service.grpc.pb.h"


namespace user_service{

struct UserEntity {
    int64_t id = 0;
    std::string uuid;
    std::string username;
    std::string email;
    std::string mobile;
    std::string display_name;
    std::string password_hash;
    bool disabled = false;
    std::string created_at;
    std::string updated_at;
};

// 分页查询参数
struct PageParams {
    int page = 1;           // 当前页
    int page_size = 20;     // 每页记录数

    // 计算 offset
    int Offset() const{
        return (page-1)*page_size;
    }

    // 参数校验 
    void Validate(){
        if(page<1) page=1;
        if(page_size<1) page_size=20;
        if(page_size>100) page_size=100; // 防止一次查太多,导致性能太差
    }
};

// 分页查询结果
struct PageResult {
    int total_records;  // 总记录数        
    int total_pages;    // 总页数
    int page;           // 当前页
    int page_size;      // 每页记录数量

    static PageResult Create(int page,int page_size,int total_records){
        PageResult res;
        res.page=page;
        res.page_size=page_size;
        res.total_records=total_records;
        res.total_pages=(total_records+page_size-1)/page_size; //下取整
        return res;
    }
};

class MySQLResult;

// DAO封装
class UserDB{
    using MySQLPool=TemplateConnectionPool<MySQLConnection>;

    explicit UserDB(std::shared_ptr<MySQLPool> pool);

    // CRUD 操作：Create、Read、Update、Delete
    UserEntity Create(const UserEntity& user);
    UserEntity FindById(int64_t id);
    UserEntity FindByUUID(const std::string& uuid);
    UserEntity FindByUsername(const std::string& username);
    bool Update(const UserEntity& user);
    bool Delete(int64_t id);
    bool DeleteByUUID(const std::string& uuid);
    
    // 分页查询（模糊查询）
    std::pair<std::vector<UserEntity>, PageResult> 
    FindAll(const PageParams& page, const std::string& username_filter = "");
    
    // 辅助查询
    bool ExistsByUsername(const std::string& username);
    bool ExistsByMobile(const std::string& mobile);
    bool ExistsByEmail(const std::string& email);
    


private:
    bool ExistsByField(const std::string& field_name,const std::string& field_val);

    UserEntity ParseRow(MySQLResult& res);

    std::shared_ptr<MySQLPool> pool_;


};




}