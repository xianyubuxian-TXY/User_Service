#include "cache/redis_client.h"
#include "common/logger.h"
#include <iostream>
namespace user_service{

ClientRedis::ClientRedis(const std::string& host,int port,
    const std::string& password,int db){
    sw::redis::ConnectionOptions conn_opt;
    conn_opt.host=host;
    conn_opt.port=port;

    if(!password.empty()){
        conn_opt.password=password;
    }

    conn_opt.db=db;
    
    sw::redis::ConnectionPoolOptions pool_opt;
    pool_opt.size=5;
    //获取连接时的最大等待时间
    //0 = 无限等待（不推荐，可能导致线程死锁） 
    pool_opt.wait_timeout=std::chrono::milliseconds(100);

    try{
        redis_=std::make_unique<sw::redis::Redis>(conn_opt,pool_opt);
        std::cout << "✓ Redis 连接成功" << std::endl;
    }catch(const sw::redis::Error& e){
        std::cerr << "✗ Redis 连接失败: " << e.what() << std::endl;
        LOG_ERROR("Redis connection failed: {}", e.what());
        throw;
    }
}


ClientRedis::ClientRedis(const RedisConfig& config){
    sw::redis::ConnectionOptions conn_opt;
    conn_opt.host=config.host;
    conn_opt.port=config.port;
    if(!config.password.empty()){
        conn_opt.password=config.password;
    }
    conn_opt.db=config.db;
    if(config.connect_timeout_ms){
        conn_opt.connect_timeout=std::chrono::milliseconds(*config.connect_timeout_ms);
    }
    if(config.socket_timeout_ms){
        conn_opt.socket_timeout=std::chrono::milliseconds(*config.socket_timeout_ms);
    }

    sw::redis::ConnectionPoolOptions pool_opt;
    pool_opt.size=config.pool_size;
    pool_opt.wait_timeout=std::chrono::milliseconds(config.wait_timeout_ms);

    try{
        redis_=std::make_unique<sw::redis::Redis>(conn_opt,pool_opt);
        std::cout << "✓ Redis 连接成功" << std::endl;
    }catch (const sw::redis::Error& e){
        std::cerr << "✗ Redis 连接失败: " << e.what() << std::endl;
        LOG_ERROR("Redis connection failed: {}", e.what());
        throw;
    }
}


bool ClientRedis::Set(const std::string& key,const std::string& value){       
    try{
        return redis_->set(key,value);
    }catch(const sw::redis::Error& e){
        LOG_ERROR("Redis set failed: {}", e.what());
        throw;
    }
}

bool ClientRedis::SetPx(const std::string& key,const std::string& value,
        std::chrono::milliseconds ttl){
    
    // Redis 的 PSETEX 命令不允许 TTL ≤ 0
    if (ttl.count() <= 0) {
        LOG_ERROR("Redis psetex failed: TTL must be positive (got {}ms)", ttl.count());
        return false;
    }

    try{
        redis_->psetex(key,ttl,value);
        return true;
    }catch(const sw::redis::Error& e){
        LOG_ERROR("Redis psetex failed: {}", e.what());
        throw;
    }
}


std::optional<std::string> ClientRedis::Get(const std::string& key){
    try{
        return redis_->get(key);
    }catch(const sw::redis::Error& e){
        LOG_ERROR("Redis get failed: {}", e.what());
        throw;
    }
}


//判断某个键是否存在
bool ClientRedis::Exists(const std::string& key){
    try{
        return redis_->exists(key);
    }catch(const sw::redis::Error& e){
        LOG_ERROR("Redis exists failed：{}",e.what());
        throw;
    }
}

//删除制定的键
bool ClientRedis::Del(const std::string& key){
    try{
        return redis_->del(key);
    }catch(const sw::redis::Error& e){
        LOG_ERROR("Redis del  failed：{}",e.what());
        throw;
    }
}

//为制定的key设置有效期
bool ClientRedis::PExpire(const std::string &key,std::chrono::milliseconds ttl){
    try{
        return redis_->pexpire(key,ttl);
    }catch(sw::redis::Error& e){
        LOG_ERROR("Redis pexpire failed：{}",e.what());
        throw;
    }
}


//Hash操作
bool ClientRedis::HSet(const std::string& key,
                        const std::pair<std::string,std::string>& field){
    try{
        return redis_->hset(key,field);
    }catch(const sw::redis::Error& e){
        LOG_ERROR("Redis hset failed：{}",e.what());
        throw;
    }
}

bool ClientRedis::HMSet(const std::string& key,
        const std::vector<std::pair<std::string,std::string>>& fields){
    
    if (fields.empty()) {
        LOG_WARN("Redis hmset skipped: empty fields for key={}", key);
        return true;  // 空操作视为成功
    }
    
    try{
        redis_->hset(key,fields.begin(),fields.end());
        return true;
    }catch(const sw::redis::Error& e){
        LOG_ERROR("Redis hmset failed：{}",e.what());
        throw;
    }
}

std::optional<std::string> ClientRedis::HGet(const std::string& key,
                            const std::string& field){
    try{
        return redis_->hget(key,field);
    }catch(const sw::redis::Error& e){
        LOG_ERROR("Redis hget failed：{}",e.what());
        throw;
    }
}

std::unordered_map<std::string,std::string> ClientRedis::HGetAll(const std::string& key){
    std::unordered_map<std::string,std::string> res;
    try{
        redis_->hgetall(key,std::inserter(res,res.begin()));
        return res;
    }catch(const sw::redis::Error& e){
        LOG_ERROR("Redis hgetall failed：{}",e.what());
        throw;
    }
}

bool ClientRedis::HDel(const std::string& key,const std::string& field){
    try{
        return redis_->hdel(key,field);
    }catch(const sw::redis::Error& e){
        LOG_ERROR("Redis hdel failed：{}",e.what());
        throw;
    }
}


//原子操作
int64_t ClientRedis::Incr(const std::string& key){
    try {
        return redis_->incr(key);
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis incr failed：{}",e.what());
        throw;
    }

}

int64_t ClientRedis::IncrBy(const std::string& key,int64_t increment){
    try {
        return redis_->incrby(key, increment);
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis incrby failed: key={}, error={}", key, e.what());
        throw;
    }
}


//Ping 测试
bool ClientRedis::Ping() noexcept{
    try {
        redis_->ping();
        return true;
    } catch (const sw::redis::Error& e) {
        LOG_ERROR("Redis ping failed: {}", e.what());
        return false;
    }
}

}