#include "sms_service.h"
#include "common/logger.h"

#include <random>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace user_service{

SmsService::SmsService(std::shared_ptr<RedisClient> redis, const SmsConfig& config)
    : redis_(std::move(redis)), config_(config) {
}

// 发送验证码
Result<int32_t> SmsService::SendCaptcha(SmsScene scene,
                                    const std::string& mobile){

    // 辅助 lambda：安全获取 TTL（秒）
    auto safeTTLSeconds = [this](const std::string& key, int64_t default_val = 60) -> int64_t {
        auto r = redis_->PTTL(key);
        if (r.IsOk() && r.Value() > 0) {
            return r.Value() / 1000;
        }
        return default_val;
    };

    // 1.检查是否被锁定（如：验证失败超出限度，被锁的）
    auto lock_exists=redis_->Exists(LockKey(scene,mobile));
    if(!lock_exists.IsOk()){
        // Redis 故障，保守起见拒绝发送
        LOG_ERROR("Redis error when checking lock: {}", lock_exists.message);
        return Result<int32_t>::Fail(ErrorCode::ServiceUnavailable, 
                                            "服务暂时不可用，请稍后重试");
    }
    
    if(lock_exists.Value()){
        // 被锁的
        int64_t ttl = safeTTLSeconds(LockKey(scene, mobile), config_.lock_seconds);
        return Result<int32_t>::Fail(
                        ErrorCode::RateLimited,
                        fmt::format("操作过于频繁，请{}秒后再试", ttl));
    }

    // 2.检查发送间隔
    auto interval_exists=redis_->Exists(IntervalKey(mobile));
    if(!interval_exists.IsOk()){
        LOG_ERROR("Redis error when checking interval: {}", interval_exists.message);
        return Result<int32_t>::Fail(ErrorCode::ServiceUnavailable,
                                            "服务暂时不可用，请稍后重试");
    }
    if(interval_exists.Value()){
        int64_t ttl = safeTTLSeconds(LockKey(scene, mobile), config_.lock_seconds);
        return Result<int32_t>::Fail(
                        ErrorCode::RateLimited,
                        fmt::format("操作过于频繁，请{}秒后再试", ttl));               
    }

    // 3.生成验证码
    std::string code=GenerateCaptcha();

    // 4.存储到Redis（原子操作）
    // 存储验证码（关键操作，必须成功）
    auto set_code=redis_->SetPx(CaptchaKey(scene,mobile),code,
                    std::chrono::milliseconds(config_.code_ttl_seconds*1000));
    
    if (!set_code.IsOk()) {
        LOG_ERROR("Failed to store sms code: {}", set_code.message);
        return Result<int32_t>::Fail(ErrorCode::ServiceUnavailable,
                                            "服务暂时不可用，请稍后重试");
    }
    

    // 存储发送间隔（非关键，失败可忽略）
    auto set_interval=redis_->SetPx(IntervalKey(mobile),"1",
                    std::chrono::milliseconds(config_.send_interval_seconds*1000));

    if (!set_interval.IsOk()) {
        LOG_WARN("Failed to set interval key: {}", set_interval.message);
        // 不影响主流程，继续
    }

    // 5.发送短信
    auto send_result = DoSend(mobile, code, scene);

    if(!send_result.IsOk()){
        // 内部错误，进行日志输出
        LOG_ERROR("SMS send failed: mobile={}", mobile);
        // 发送失败，清理已存储的验证码（回滚）
        redis_->Del(CaptchaKey(scene, mobile));
        return Result<int32_t>::Fail(ErrorCode::ServiceUnavailable,
                                        "短信发送失败，请稍后重试");
    }

    LOG_INFO("SMS sent: mobile={}, scene={}", mobile, SceneName(scene));

    int32_t result=config_.send_interval_seconds;
    return Result<int32_t>::Ok(result);
}

// 验证验证码
Result<void> SmsService::VerifyCaptcha(SmsScene scene,
                        const std::string& mobile,
                        const std::string& input_code){
    
    // 辅助 lambda：安全获取 TTL（秒）
    auto safeTTLSeconds = [this](const std::string& key, int64_t default_val = 60) -> int64_t {
        auto r = redis_->PTTL(key);
        if (r.IsOk() && r.Value() > 0) {
            return r.Value() / 1000;
        }
        return default_val;
    };


    // 1.检查是否被锁定（如：验证失败超出限度，被锁的）
    auto lock_exists=redis_->Exists(LockKey(scene,mobile));
    if(!lock_exists.IsOk()){
         // Redis 故障，保守起见拒绝验证（防止暴力破解绕过）
        LOG_ERROR("Redis error when checking lock: {}", lock_exists.message);
        return Result<void>::Fail(ErrorCode::ServiceUnavailable, 
                                            "服务暂时不可用，请稍后重试");
    }
    
    if(lock_exists.Value()){
        // 被锁定
        int64_t ttl= safeTTLSeconds(LockKey(scene, mobile), config_.retry_ttl_seconds);
        return Result<void>::Fail(
                        ErrorCode::RateLimited,
                        fmt::format("操作过于频繁，请{}秒后再试", ttl));
    }

    // 2.获取正确的验证码
    auto stored_code_result = redis_->Get(CaptchaKey(scene, mobile));
    if (!stored_code_result.IsOk()) {
        LOG_ERROR("Redis error when getting code: {}", stored_code_result.message);
        return Result<void>::Fail(ErrorCode::ServiceUnavailable,
                                  "服务暂时不可用，请稍后重试");
    }
    if (!stored_code_result.Value().has_value()) {
        // 未找到验证码
        return Result<void>::Fail(
            ErrorCode::CaptchaExpired,
            "验证码已过期，请重新获取");
    }
    const std::string& stored_code = stored_code_result.Value().value();

    // 3.对比验证码
    if(input_code!=stored_code){
        // 增加错误计数
        auto count_result = redis_->Incr(VerifyCountKey(scene, mobile));
        if (!count_result.IsOk()) {
            // 计数失败，保守拒绝（防止无限尝试）
            LOG_ERROR("Redis INCR failed: {}", count_result.message);
            return Result<void>::Fail(ErrorCode::ServiceUnavailable,
                                      "服务暂时不可用，请稍后重试");
        }

        int64_t count = count_result.Value();

        // 设置计数器过期时间（失败可忽略）
        redis_->PExpire(VerifyCountKey(scene, mobile),
                        std::chrono::milliseconds(config_.retry_ttl_seconds * 1000));

        // 失败次数超出最大限度
        if (count >= config_.max_retry_count) {
            // 触发锁定
            redis_->SetPx(LockKey(scene, mobile), "1",
                          std::chrono::milliseconds(config_.lock_seconds * 1000));
            redis_->Del(CaptchaKey(scene, mobile));       // 删除验证码
            redis_->Del(VerifyCountKey(scene, mobile)); // 删除计数器
            // “间隔”不能删，因为是全局的，会影响其它场景

            LOG_WARN("SMS verify locked: mobile={}, scene={}", mobile, SceneName(scene));

            return Result<void>::Fail(
                ErrorCode::AccountLocked,
                fmt::format("错误次数过多，请{}分钟后再试", config_.lock_seconds / 60));
        }
        // 验证码错误的返回
        return Result<void>::Fail(ErrorCode::CaptchaWrong, 
            fmt::format("验证码错误，还剩{}次机会", 
                        config_.max_retry_count - count));
    }

    // 4. 验证成功，清理错误计数
    /* 不删除验证码，如：
        验证码正确 → 创建用户（可能失败：用户名冲突、数据库异常）
                 ↓ 失败
           用户想用同一个验证码重试
    */
    redis_->Del(VerifyCountKey(scene, mobile));

    LOG_INFO("SMS verify success: mobile={}, scene={}", mobile, SceneName(scene));
    return Result<void>::Ok();
}

// 消费验证码（业务成功后调用，防止重复使用）
Result<void> SmsService::ConsumeCaptcha(SmsScene scene, const std::string& mobile){
    redis_->Del(CaptchaKey(scene, mobile));
    LOG_INFO("SMS code consumed: mobile={}, scene={}", mobile, SceneName(scene));
    return Result<void>::Ok();
}


// ==================== 实际发送短信 ====================
// 实际发送短信（对接服务商）
Result<void> SmsService::DoSend(const std::string& mobile, const std::string& code, SmsScene scene){
    // TODO: 对接实际短信服务商
    
    // 开发环境：打印到日志
    LOG_INFO("[DEV SMS] mobile={}, code={}, scene={}", 
        mobile, code, SceneName(scene));

    // 模拟随机失败（测试用）
    // if (rand() % 10 == 0) {
    //     return Result<void>::Fail(ErrorCode::ServiceUnavailable, "模拟发送失败");
    // }

    return Result<void>::Ok();
}

// 生成随机验证码
std::string SmsService::GenerateCaptcha() {
    // 静态局部变量：仅初始化一次，提升性能，避免频繁创建随机数对象
    static std::random_device rd;                // 随机数种子源，获取硬件/系统随机数
    static std::mt19937 gen(rd());               // 梅森旋转算法生成器，高性能、高随机性
    static std::uniform_int_distribution<> dis(0, 9); // 均匀分布，确保0-9每个数字概率均等
    
    std::string code;
    code.reserve(config_.code_len); // 预分配内存，避免字符串拼接时多次扩容，提升效率
    for (int i = 0; i < config_.code_len; ++i) {
        code += std::to_string(dis(gen)); // 循环生成单个数字，拼接为验证码
    }
    return code;
}



// ==================== Key 生成 ====================

// 场景名称
std::string SmsService::SceneName(SmsScene scene){
    switch(scene){
        case SmsScene::Register:        return "register";
        case SmsScene::Login:           return "login";
        case SmsScene::ResetPassword:   return "reset_password";
        case SmsScene::DeleteUser:      return "delete_user";
        default:                        return "unknown";
    }
}

/*
作用：生成短信验证码本身的缓存 Key
存储值：实际的短信验证码（如682915）+ 过期时间（通常 5-10 分钟，验证码有效期）
绑定 scene 原因：同一个手机号在不同场景的验证码独立（比如登录发了682915，注册可同时发954278，不会覆盖）
典型 Key 示例：sms:code:login:13812345678、sms:code:find_pwd:13812345678
*/
std::string SmsService::CaptchaKey(SmsScene scene, const std::string& mobile){
    return "sms:code:"+SceneName(scene)+":"+mobile;
}

/*
作用：生成短信发送间隔的缓存 Key（防频繁发送）
存储值：最后一次发送短信的时间戳（如1735689600）
设计原因：基础限流规则 —— 无论哪个场景，同一个手机号短时间内不能反复发验证码（比如默认 60 秒内只能发 1 次），无需绑定 scene（全局间隔限制）
典型 Key 示例：sms:interval:13812345678
*/
std::string SmsService::IntervalKey(const std::string& mobile){
    return "sms:interval:"+mobile;
}

/*
作用：生成单场景验证码验证失败次数的缓存 Key（防暴力破解）
存储值：该场景下，手机号累计验证验证码失败的次数（如2）
绑定 scene 原因：不同场景的验证失败次数独立（比如登录验证失败 2 次，不影响注册场景）
设计原因：防暴力破解验证码 —— 限制单场景下的失败次数（比如最多失败 3 次），超过则临时锁定该场景的短信发送 / 验证，避免攻击者枚举验证码
典型 Key 示例：sms:verify_count:login:13812345678
*/
std::string SmsService::VerifyCountKey(SmsScene scene, const std::string& mobile){
    return "sms:verify_count:"+SceneName(scene)+":"+mobile;
}

/*
作用：生成单场景短信临时锁定的缓存 Key（风控兜底）
存储值：锁定状态（如1）+ 锁定过期时间（比如 10 分钟，自动解锁）
绑定 scene 原因：锁定仅针对当前场景（比如登录场景被锁，注册场景仍可正常使用）
设计原因：兜底风控 —— 当满足「验证失败次数超限」「发送间隔频繁突破」等风险条件时，临时锁定该场景的短信服务，避免进一步的恶意操作，且非全局锁定，不影响用户其他业务操作，兼顾风控和用户体验
典型 Key 示例：sms:lock:login:13812345678
*/
std::string SmsService::LockKey(SmsScene scene, const std::string& mobile){
    return "sms:lock:"+SceneName(scene)+":"+mobile;
}


}