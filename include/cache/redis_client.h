#pragma once
#include <sw/redis++/redis++.h>
#include <memory>
#include <string>
#include <optional>
#include <chrono>
#include <vector>
#include <unordered_map>
#include "config/config.h"

namespace user_service{

class ClientRedis{
public:
    ClientRedis(const std::string& host,int port,
                const std::string& password,int db);
    
    ClientRedis(const RedisConfig& config);

    bool Set(const std::string& key,const std::string& value);
    bool SetPx(const std::string& key,const std::string& value,
                std::chrono::milliseconds ttl);
    
    std::optional<std::string> Get(const std::string& key);

    //判断某个键是否存在
    bool Exists(const std::string& key);
    //删除制定的键
    bool Del(const std::string& key);
    //为制定的key设置有效期
    bool PExpire(const std::string &key,std::chrono::milliseconds ttl);

    //Hash操作
    bool HSet(const std::string& key,const std::pair<std::string,std::string>& field);
    bool HMSet(const std::string& key,
                const std::vector<std::pair<std::string,std::string>>& fields);
    std::optional<std::string> HGet(const std::string& key,
                                    const std::string& field);
    std::unordered_map<std::string,std::string> HGetAll(const std::string& key);
    bool HDel(const std::string& key,const std::string& field);

    //原子操作
    int64_t Incr(const std::string& key);
    int64_t IncrBy(const std::string& key,int64_t increment);

    //Ping 测试
    bool Ping() noexcept;

private:
    std::unique_ptr<sw::redis::Redis> redis_; 
};
}
