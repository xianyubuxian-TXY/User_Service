#pragma once

#include "config/config.h"
#include "mysql_result.h"
#include <mysql/mysql.h>
#include <string>
#include <variant>
namespace user_service{



class MySQLConnection{
public:
    using Param=std::variant<nullptr_t,int64_t,uint64_t,double,std::string,bool>;

    explicit MySQLConnection(const MySQLConfig& config);
    ~MySQLConnection();

    //禁止拷贝，允许移动
    MySQLConnection(const MySQLConnection&)=delete;
    MySQLConnection& operator=(const MySQLConnection&)=delete;
    MySQLConnection(MySQLConnection&& other)=delete;
    MySQLConnection& operator=(MySQLConnection&& other)=delete;

    //有效性检查
    bool Valid() const {return mysql_ && mysql_ping(mysql_)==0;}

    MYSQL* Get() {return mysql_;}

    //Select操作
    //存在限制：“sql中不可以包含非占位符'?'”
    MySQLResult Query(const std::string& sql,std::initializer_list<Param> param={});

    //Insert/Update/Delete操作
    //存在限制：“sql中不可以包含非占位符'?'”
    uint64_t Execute(const std::string& sql,std::initializer_list<Param> params={});

    // 流式查询（逐行获取，适合大结果集）
    // 注意：遍历期间不能执行其他查询！
    // MySQLResult QueryUnbuffered(const std::string& sql);

    //获取Insert后的id
    uint64_t LastInsertId();

private:
    // 初始化并设置选项
    void InitAndSetOptions(const MySQLConfig& config);

     // 判断错误是否值得重试
    bool IsRetryableError(unsigned int err_code);

    //重试连接
    void ConnectWithRetry(const MySQLConfig& config);

    //转义方法（只能用于转义“参数值”）（防SQL注入的简单处理）
    std::string Escape(const std::string& str);

    /// @brief SQL语句包装器： “？为占位符 ——>sql语句中不可以包含非占位符 ？”
    /// @param sql sql语句，用"?"占位
    /// @param params 参数
    /// @return 完整的sql语句
    /// @note 对于不在Param中的类型，可以先转为string类型，在作为参数传入
    std::string BuildSQL(const std::string& sql, std::initializer_list<Param> params);

    void ThrowByErrorCode(unsigned int code,const std::string& msg);
private:
    MYSQL* mysql_=nullptr;
};


}