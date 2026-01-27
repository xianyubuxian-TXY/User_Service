#include "user_db.h"
#include "common/utils.h"
#include "common/uuid.h"
#include "exception/mysql_exception.h"
#include "exception/converter/mysql_error_converter.h"
#include "db/mysql_result.h"

namespace user_service{

UserDB::UserDB(std::shared_ptr<MySQLPool> pool)
    : pool_(pool)
{}

UserEntity UserDB::Create(const UserEntity& user){
    try{
        auto conn=pool_->CreateConnection();

        // 检查连接是否有效
        EnsureValidConnection(conn.get());

        // 生成UUID（通用唯一识别码）
        std::string uuid=UUIDHelper::Generate();

        std::string sql="Insert into users (uuid,username,email,mobile,display_name,password_hash) "
        "Values (?,?,?,?,?,?)";
        conn->Execute(sql,{uuid,user.username,user.email,user.mobile,user.display_name,user.password_hash});
        
        // 通过UUID查询用户（id、created_at等是插入后自动填写的，所以需要再次获取）
        return FindByUUID(uuid);
    }catch (const MySQLDuplicateException& e){
        // 重复键：判断具体字段，抛更精确的错误码
        LOG_INFO("[UserDB] Create duplicate: %s", e.what());
        
        std::string msg = e.what();
        if (msg.find("username") != std::string::npos) {
            throw ClientException(ErrorCode::UsernameTaken);
        } else if (msg.find("email") != std::string::npos) {
            throw ClientException(ErrorCode::EmailTaken);
        } else if (msg.find("mobile") != std::string::npos) {
            throw ClientException(ErrorCode::MobileTaken);
        }
        throw ClientException(ErrorCode::UserAlreadyExists);
    }catch (const ClientException&){
        // 已经是 ClientException，直接抛出
        throw;
    }catch (const MySQLException& e){
        // MySQL异常，先转为ClientException，在抛出
        RethrowAsClientException(e, "UserDB::Create");
    }catch (const std::exception& e){
        // 其它异常，直接抛出"内部异常"
        RethrowAsClientException(e, "UserDB::Create");
    }
}

UserEntity UserDB::FindById(int64_t id){
    try{
        UserEntity user;

        auto conn=pool_->CreateConnection();
        // 检查连接是否有效
        EnsureValidConnection(conn.get());

        std::string sql="Select * from users where id=?";

        auto res=conn->Query(sql,{id});
        // 解析结果
        if(res.Next()){
            user=ParseRow(res);
        }else{
            throw ClientException(ErrorCode::UserNotFound);
        }
        return user;
    }catch (const ClientException&){
        // 已经是 ClientException，直接抛出
        throw;
    }catch (const MySQLException& e){
        // MySQL异常，先转为ClientException，在抛出
        RethrowAsClientException(e, "UserDB::FindById");
    }catch (const std::exception& e){
        // 其它异常，直接抛出"内部异常"
        RethrowAsClientException(e, "UserDB::FindById");
    }
}

UserEntity UserDB::FindByUUID(const std::string& uuid){
    try{
        UserEntity user;

        auto conn=pool_->CreateConnection();
        // 检查连接是否有效
        EnsureValidConnection(conn.get());

        std::string sql="Select * from users where uuid=?";

        auto res=conn->Query(sql,{uuid});
        if(res.Next()){
            user=ParseRow(res);
        }else{
            throw ClientException(ErrorCode::UserNotFound);
        }
        return user;
    }catch (const ClientException&){
        // 已经是 ClientException，直接抛出
        throw;
    }catch (const MySQLException& e){
        // MySQL异常，先转为ClientException，在抛出
        RethrowAsClientException(e, "UserDB::FindByUUID");
    }catch (const std::exception& e){
        // 其它异常，直接抛出"内部异常"
        RethrowAsClientException(e, "UserDB::FindByUUID");
    }
}

UserEntity UserDB::FindByUsername(const std::string& username){
    try{
        UserEntity user;

        auto conn=pool_->CreateConnection();
        // 检查连接是否有效
        EnsureValidConnection(conn.get());

        std::string sql="Select * from users where username=?";

        auto res=conn->Query(sql,{username});
        if(res.Next()){
            user=ParseRow(res);
        }else{
            throw ClientException(ErrorCode::UserNotFound);
        }
        return user;
    }catch (const ClientException&){
        // 已经是 ClientException，直接抛出
        throw;
    }catch (const MySQLException& e){
        // MySQL异常，先转为ClientException，在抛出
        RethrowAsClientException(e, "UserDB::FindByUsername");
    }catch (const std::exception& e){
        // 其它异常，直接抛出"内部异常"
        RethrowAsClientException(e, "UserDB::FindByUsername");
    }
}

bool UserDB::Update(const UserEntity& user){
    try{
        auto conn=pool_->CreateConnection();
        // 检查连接是否有效
        EnsureValidConnection(conn.get());

        std::string sql="Update users Set email=?,mobile=?,display_name=?,"
                        "password_hash=?,disabled=? where uuid=?";
        int64_t affect_row=conn->Execute(sql,{user.email,user.mobile,user.display_name,user.password_hash,
                            user.disabled,user.uuid});
        return affect_row>0;
    }catch (const ClientException&){
        // 已经是 ClientException，直接抛出
        throw;
    }catch (const MySQLException& e){
        // MySQL异常，先转为ClientException，在抛出
        RethrowAsClientException(e, "UserDB::Update");
    }catch (const std::exception& e){
        // 其它异常，直接抛出"内部异常"
        RethrowAsClientException(e, "UserDB::Update");
    }
}

bool UserDB::Delete(int64_t id){
    try{
        auto conn=pool_->CreateConnection();
        // 检查连接是否有效
        EnsureValidConnection(conn.get());

        std::string sql="Delete from users where id=?";
        auto affect_row=conn->Execute(sql,{id});
        return affect_row>0;
    }catch (const ClientException&){
        // 已经是 ClientException，直接抛出
        throw;
    }catch (const MySQLException& e){
        // MySQL异常，先转为ClientException，在抛出
        RethrowAsClientException(e, "UserDB::Delete");
    }catch (const std::exception& e){
        // 其它异常，直接抛出"内部异常"
        RethrowAsClientException(e, "UserDB::Delete");
    }
}

bool UserDB::DeleteByUUID(const std::string& uuid){
    try{
        auto conn=pool_->CreateConnection();
        // 检查连接是否有效
        EnsureValidConnection(conn.get());

        std::string sql="Delete from users where uuid=?";
        auto affect_row=conn->Execute(sql,{uuid});
        return affect_row>0;
    }catch (const ClientException&){
        // 已经是 ClientException，直接抛出
        throw;
    }catch (const MySQLException& e){
        // MySQL异常，先转为ClientException，在抛出
        RethrowAsClientException(e, "UserDB::DeleteByUUID");
    }catch (const std::exception& e){
        // 其它异常，直接抛出"内部异常"
        RethrowAsClientException(e, "UserDB::DeleteByUUID");
    }
}

std::pair<std::vector<UserEntity>, PageResult> 
UserDB::FindAll(const PageParams& page, const std::string& username_filter){
    std::pair<std::vector<UserEntity>, PageResult> users;
    try{
        auto conn=pool_->CreateConnection();
        // 检查连接是否有效
        EnsureValidConnection(conn.get());

        int offset=page.Offset(); // 查询的起始位置
        std::string like_pattern = "%" + username_filter + "%";
        std::string sql="Select * from users where username like ? "
                        "Order by created_at desc,id desc limit ?,?";
        auto res=conn->Query(sql,{like_pattern,offset,page.page_size});

        // 填充PageResult
        int total_records=res.RowCount();
        users.second=PageResult::Create(page.page,page.page_size,total_records);
        
        // 为vector预分配内存，避免频繁扩容
        users.first.reserve(total_records);
        // 解析结果
        while(res.Next()){
            users.first.push_back(ParseRow(res));
        }

        return users;
    }catch (const ClientException&){
        // 已经是 ClientException，直接抛出
        throw;
    }catch (const MySQLException& e){
        // MySQL异常，先转为ClientException，在抛出
        RethrowAsClientException(e, "UserDB::FindAll");
    }catch (const std::exception& e){
        // 其它异常，直接抛出"内部异常"
        RethrowAsClientException(e, "UserDB::FindAll");
    }
}

bool UserDB::ExistsByUsername(const std::string& username){
    return ExistsByField("username",username);
}   

bool UserDB::ExistsByMobile(const std::string& mobile){
    return ExistsByField("mobile",mobile);
}

bool UserDB::ExistsByEmail(const std::string& email){
    return ExistsByField("email",email);
}

bool UserDB::ExistsByField(const std::string& field_name,const std::string& field_val){
    try{
        auto conn=pool_->CreateConnection();
        // 检查连接是否有效
        EnsureValidConnection(conn.get());

        // 注意：字段名不能用参数化，需要拼接（已验证 field_name 是安全的内部值）
        std::string sql="Select 1 from users where " + field_name + "=? limit 1";
        auto res=conn->Query(sql,{field_val});
        return res.RowCount()>0;
    }catch (const ClientException&){
        // 已经是 ClientException，直接抛出
        throw;
    }catch (const MySQLException& e){
        // MySQL异常，先转为ClientException，在抛出
        RethrowAsClientException(e, "UserDB::ExistsByField");
    }catch (const std::exception& e){
        // 其它异常，直接抛出"内部异常"
        RethrowAsClientException(e, "UserDB::ExistsByField");
    }
}

UserEntity UserDB::ParseRow(MySQLResult& res){
    UserEntity user;
    user.id=*res.GetInt(0);
    user.uuid=*res.GetString(1);
    user.username=*res.GetString(2);
    user.email=res.GetString(3).value_or("");
    user.mobile=res.GetString(4).value_or("");
    user.display_name=res.GetString(5).value_or("");
    user.password_hash=*res.GetString(6);
    user.disabled=*res.GetInt(7);
    user.created_at=*res.GetString(8);
    user.updated_at=*res.GetString(9);
    return user;
}

}
