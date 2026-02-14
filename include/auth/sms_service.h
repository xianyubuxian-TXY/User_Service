#pragma once

#include<string>
#include<memory>
#include "common/auth_type.h"
#include "common/result.h"
#include "cache/redis_client.h"

namespace user_service{

/// @brief 短消息服务（SMS 全称 Short Message Service，Captcha 验证码缩写）
class SmsService{
public:
    SmsService(std::shared_ptr<RedisClient> redis, const SmsConfig& config);

    // 发送验证码
    virtual Result<int32_t> SendCaptcha(SmsScene scene,
                                        const std::string& mobile);

    // 验证验证码
    virtual Result<void> VerifyCaptcha(SmsScene scene,
                            const std::string& mobile,
                            const std::string& code);

    // 消费验证码（验证成功后删除，防止重复使用）
    virtual Result<void> ConsumeCaptcha(SmsScene scene, const std::string& mobile);
private:
    // 场景名称
    static std::string SceneName(SmsScene scene);

    // 生成随机验证码
    std::string GenerateCaptcha();

    // 实际发送短信（对接服务商）
    Result<void> DoSend(const std::string& mobile, const std::string& code, SmsScene scene);

    /*
    作用：生成短信验证码本身的缓存 Key
    存储值：实际的短信验证码（如682915）+ 过期时间（通常 5-10 分钟，验证码有效期）
    绑定 scene 原因：同一个手机号在不同场景的验证码独立（比如登录发了682915，注册可同时发954278，不会覆盖）
    典型 Key 示例：sms:code:login:13812345678、sms:code:find_pwd:13812345678
    */
    std::string CaptchaKey(SmsScene scene, const std::string& mobile);

    /*
    作用：生成短信发送间隔的缓存 Key（防频繁发送）
    存储值：最后一次发送短信的时间戳（如1735689600）
    设计原因：基础限流规则 —— 无论哪个场景，同一个手机号短时间内不能反复发验证码（比如默认 60 秒内只能发 1 次），无需绑定 scene（全局间隔限制）
    典型 Key 示例：sms:interval:13812345678
    */
    std::string IntervalKey(const std::string& mobile);
    
    /*
    作用：生成单场景验证码验证失败次数的缓存 Key（防暴力破解）
    存储值：该场景下，手机号累计验证验证码失败的次数（如2）
    绑定 scene 原因：不同场景的验证失败次数独立（比如登录验证失败 2 次，不影响注册场景）
    设计原因：防暴力破解验证码 —— 限制单场景下的失败次数（比如最多失败 3 次），超过则临时锁定该场景的短信发送 / 验证，避免攻击者枚举验证码
    典型 Key 示例：sms:verify_count:login:13812345678
    */
    std::string VerifyCountKey(SmsScene scene, const std::string& mobile);

    /*
    作用：生成单场景短信临时锁定的缓存 Key（风控兜底）
    存储值：锁定状态（如1）+ 锁定过期时间（比如 10 分钟，自动解锁）
    绑定 scene 原因：锁定仅针对当前场景（比如登录场景被锁，注册场景仍可正常使用）
    设计原因：兜底风控 —— 当满足「验证失败次数超限」「发送间隔频繁突破」等风险条件时，临时锁定该场景的短信服务，避免进一步的恶意操作，且非全局锁定，不影响用户其他业务操作，兼顾风控和用户体验
    典型 Key 示例：sms:lock:login:13812345678
    */
    std::string LockKey(SmsScene scene, const std::string& mobile);

private:
    std::shared_ptr<RedisClient> redis_;
    SmsConfig config_;
};

}
