#pragma once

#include<memory>
#include<deque>
#include<mutex>
#include<condition_variable>
#include<chrono>
#include<functional>
#include<stdexcept>
#include<type_traits>
#include "../common/logger.h"
#include "config/config.h"

/*--------------------------------------------泛型实现----------------------------------------------*/

namespace user_service{

class MySQLConnection;
class RedisConnection;

template<typename>
struct ConnectionConfig;

template<>
struct ConnectionConfig<MySQLConnection>{using type=user_service::MySQLConfig;};

template<>
struct ConnectionConfig<RedisConnection>{using type=user_service::RedisConfig;};

template<typename T>
using Config_t=typename ConnectionConfig<T>::type; //typename：表示“类型”


template<typename>
inline constexpr bool always_false_v = false;





/// @brief 连接池模板类
/// @tparam T 连接类型：需要含有Valid()函数
template<typename T>
class TemplateConnectionPool{
public:
    using ConnectionPtr=std::unique_ptr<T>;

    // /*使用std::conditional_t实现「连接类型T→配置类型Config」的编译期推导，避免手动传递Config模板参数，
    //   消除「T与Config不匹配」的风险，同时简化模板类的使用*/
    // using Config =  std::conditional_t<std::is_same_v<T, MySQLConnection>,user::MySQLConfig,
    //                 std::conditional_t<std::is_same_v<T, RedisConnection>,user::RedisConfig,
    //                 void>>;

    using Config=Config_t<T>;
    using ConfigPtr=std::shared_ptr<const Config>;
    using CreateFunc=std::function<ConnectionPtr(const Config&)>;

    TemplateConnectionPool(std::shared_ptr<user_service::Config> global_config,CreateFunc func)
    :createConnFunc_(std::move(func))
    {
        // 校验全局配置非空
        if (!global_config) {
            throw std::invalid_argument("TemplateConnectionPool: global_config is nullptr");
        }
        // 校验回调函数有效
        if (!createConnFunc_) {
            throw std::invalid_argument("TemplateConnectionPool: createConnFunc is empty");
        }

        ExtractSubConfig(global_config);
        InitPool();
    }

    class ConnectionGuard{
    public:
        ConnectionGuard(TemplateConnectionPool& pool)
        :pool_(pool)
        {
            conn_=std::move(pool_.Acquire());
            if(!conn_){
                throw std::runtime_error("failed to acquire connection in ConnectionGuard");
            }
        }

        ConnectionGuard(const ConnectionGuard&)=delete;
        ConnectionGuard& operator=(const ConnectionGuard&)=delete;
        ConnectionGuard& operator=(ConnectionGuard&&)=delete;
        ConnectionGuard(ConnectionGuard&& other)noexcept
            :pool_(other.pool_){
            if(this==&other) return;
            // 转移资源所有权
            conn_ = std::move(other.conn_);
            other.conn_ = nullptr;  // 防止双重释放
        }

        T* get() const noexcept {
            return conn_.get();
        }

        T* operator->(){
            if(!conn_) throw std::runtime_error("connection is null in ConnectionGuard");
            return conn_.get();
        }
        T& operator*(){
            if(!conn_) throw std::runtime_error("connection is null in ConnectionGuard");
            return *conn_;
        }
 
        ~ConnectionGuard(){
            if(conn_){
                pool_.Release(std::move(conn_));
                conn_=nullptr;
            }
        }
    private:
        TemplateConnectionPool& pool_;
        ConnectionPtr conn_;
    };


    ConnectionGuard CreateConnection(){
        return ConnectionGuard(*this);
    }
private:
    // 提取子配置（封装分支逻辑，便于后续扩展）
    void ExtractSubConfig(const std::shared_ptr<user_service::Config>global_config){
        if constexpr(std::is_same_v<T,MySQLConnection>){
            config_=std::make_shared<const Config>(global_config->mysql);
        }else if constexpr(std::is_same_v<T,RedisConnection>){
            config_=std::make_shared<const Config>(global_config->redis);
        }else{
            // 不应该到这里：若没有对应的 connection_config 特化，上面的 using Config 已经报错了
            static_assert(always_false_v<T>, "Unsupported connection type, no matching sub-config");
        }
    }

    void InitPool(){
        // 校验配置非空（防御性编程，避免空指针解引用）
        if (!config_) {
            throw std::runtime_error("TemplateConnectionPool: config_ is nullptr");
        }
        int poolSize=config_->GetPoolSize();
        for(int i=0;i<poolSize;++i){
            auto conn=std::move(createConnFunc_(*config_));
            if(!conn){
                throw std::runtime_error("failed to create connection");
            }
            pool_.push_back(std::move(conn));
        }
    }

    ConnectionPtr Acquire(){
        std::unique_lock<std::mutex> lk(mutex_);
        bool ret=cond_.wait_for(lk,std::chrono::seconds(5),[this](){return !pool_.empty();});
        if(!ret){
            LOG_ERROR("Acquire connection failed");
            return nullptr;
        }

        // 新增：双重校验，避免队列空访问
        if (pool_.empty()) {
            LOG_ERROR("Connection pool is empty after wake up");
            return nullptr;
        }

        auto conn=std::move(pool_.front());
        pool_.pop_front();

        if (!conn->Valid()) {
            LOG_WARN("Acquired invalid connection, attempting rebuild");
            conn = createConnFunc_(*config_);
            if (!conn) {
                LOG_ERROR("Failed to rebuild invalid connection");
                return nullptr;
            }
        }
        
        return conn;
    }

    void Release(ConnectionPtr conn){
        std::unique_lock<std::mutex> lk(mutex_);
        if(!conn || !conn->Valid()){
            conn=std::move(createConnFunc_(*config_));
            //允许归还失败（直接退出，而不是抛出异常）
            if(!conn){
                LOG_ERROR("failed to release connection");
                return;
            }
        }
        pool_.push_back(std::move(conn));
        cond_.notify_one();
    }

    std::deque<ConnectionPtr> pool_;
    ConfigPtr config_;
    std::mutex mutex_;
    std::condition_variable cond_;

    //创建连接回调函数
    CreateFunc createConnFunc_;
};

}