#include "auth_service.h"

#include "common/logger.h"
#include "common/validator.h"
#include "common/password_helper.h"
#include "entity/user_entity.h"
#include "common/time_utils.h"

namespace user_service{

AuthService::AuthService(std::shared_ptr<Config> config,
                            std::shared_ptr<UserDB> user_db,
                            std::shared_ptr<RedisClient> redis_cli,
                            std::shared_ptr<TokenRepository> token_repo,
                            std::shared_ptr<JwtService> jwt_srv,
                            std::shared_ptr<SmsService> sms_srv)
    : config_(config),
      user_db_(user_db),
      redis_cli_(redis_cli),
      token_repo_(token_repo),
      jwt_srv_(jwt_srv),
      sms_srv_(sms_srv)
    {}

// 发送验证码
Result<int32_t> AuthService::SendVerifyCode(const std::string& mobile, SmsScene scene) {
    std::string error;
    
    // 1. 手机格式校验
    if (!IsValidMobile(mobile, error)) {
        return Result<int32_t>::Fail(ErrorCode::InvalidArgument, error);
    }

    // 2. 业务校验
    if (scene == SmsScene::Register) {
        // 注册：手机号不能已存在
        auto res = user_db_->ExistsByMobile(mobile);
        if (!res.IsOk()) {
            return Result<int32_t>::Fail(res.code, res.message);
        }
        if (res.Value()) {
            return Result<int32_t>::Fail(ErrorCode::MobileTaken, "该手机号已被用于注册");
        }
    } else if (scene == SmsScene::Login || 
               scene == SmsScene::ResetPassword || 
               scene == SmsScene::DeleteUser) {
        // 登录/重置密码/注销：手机号必须已存在
        auto res = user_db_->ExistsByMobile(mobile);
        if (!res.IsOk()) {
            return Result<int32_t>::Fail(res.code, res.message);
        }
        if (!res.Value()) {
            return Result<int32_t>::Fail(ErrorCode::UserNotFound, "该手机号未注册");
        }
    }
    // 其他场景不做额外校验

    // 3. 发送短信
    auto send_captcha_result = sms_srv_->SendCaptcha(scene, mobile);
    return send_captcha_result;
}

// 注册 
Result<AuthResult> AuthService::Register(const std::string& mobile,
    const std::string& verify_code,
    const std::string& password,
    const std::string& display_name){
    
    std::string error;
    // 1.参数校验
    if (!IsValidMobile(mobile,error)) {
        return Result<AuthResult>::Fail(ErrorCode::InvalidArgument,error);
    }
    if (!IsValidPassword(password,error,config_->password)) {
        return Result<AuthResult>::Fail(ErrorCode::InvalidArgument,error);
    }
    if (!IsValidVerifyCode(verify_code,error,config_->sms)) {
        return Result<AuthResult>::Fail(ErrorCode::InvalidArgument,error);
    }
    if(!IsValidDisplayName(display_name,error)){
        return Result<AuthResult>::Fail(ErrorCode::InvalidArgument,error);
    }

    // 2.验证“验证码”
    auto verify_result=sms_srv_->VerifyCaptcha(SmsScene::Register,mobile,verify_code);
    if(!verify_result.IsOk()){
        return Result<AuthResult>::Fail(verify_result.code,verify_result.message);
    }

    // 3.构建用户
    UserEntity user;
    user.mobile=mobile;
    user.password_hash=PasswordHelper::Hash(password);
    user.display_name=display_name;

    // 4.创建用户
    auto create_res=user_db_->Create(user);
    if(!create_res.IsOk()){
        // 创建失败
        return Result<AuthResult>::Fail(create_res.code,create_res.message);
    }

    auto& created_user=create_res.data.value();    // 创建的user

    // 5.生成Token
    auto token_pair=jwt_srv_->GenerateTokenPair(created_user);

    // 6.存储Refresh Token
    auto store_res=StoreRefreshToken(created_user.id,token_pair.refresh_token);

    // 7.返回结果
    AuthResult  result;
    result.user=created_user;
    result.user.password_hash.clear(); // 清除密码（不返回密码）
    result.tokens=token_pair;

    LOG_INFO("User registered: mobile={}, uuid={}", mobile, created_user.uuid);
    return Result<AuthResult>::Ok(result);
}

// 
Result<AuthResult> AuthService::LoginByPassword(const std::string& mobile,
                                    const std::string& password){

    // 1.参数校验                                    
    std::string err;
    if(!IsValidMobile(mobile,err)){
        return Result<AuthResult>::Fail(ErrorCode::InvalidArgument,err);
    }

    if(!IsValidPassword(password,err,config_->password)){
        return Result<AuthResult>::Fail(ErrorCode::InvalidArgument,err);
    }

    // 2.检查登录失败次数（防暴力破解）
    auto attempt_res=CheckLoginFailedAttempts(mobile);
    if(!attempt_res.IsOk()){
        return Result<AuthResult>::Fail(attempt_res.code, attempt_res.message);
    }

    // 3.查询用户
    auto user_res=user_db_->FindByMobile(mobile);
    if(!user_res.IsOk()){
              // 用户不存在也返回"账号或密码错误"，避免泄露用户是否存在
              if (user_res.code == ErrorCode::UserNotFound) {
                RecordLoginFailure(mobile);
                return Result<AuthResult>::Fail(ErrorCode::WrongPassword, "账号或密码错误");
            }
            return Result<AuthResult>::Fail(user_res.code, user_res.message);
    }

    auto& user=user_res.data.value();

    // 4.检查账号状态
    if(user.disabled){
        return Result<AuthResult>::Fail(ErrorCode::UserDisabled, "账号已被禁用");
    }

    // 5.验证密码
    if(!PasswordHelper::Verify(password,user.password_hash)){
        // 登录失败，记录失败次数
        RecordLoginFailure(mobile);
        return Result<AuthResult>::Fail(ErrorCode::WrongPassword, "账号或密码错误");
    }

    // 6.登录成功，清除记录
    ClearLoginFailure(mobile);

    // 7.生成Token
    auto token_pair=jwt_srv_->GenerateTokenPair(user);

    // 8.存储Refresh Token
    auto store_res=StoreRefreshToken(user.id,token_pair.refresh_token);

    // 9.返回结果
    AuthResult result;
    result.user=user;
    result.user.password_hash.clear();  // 清空密码，不返回密码
    result.tokens=token_pair;

    LOG_INFO("User login by password: mobile={}, uuid={}", mobile, user.uuid);
    return Result<AuthResult>::Ok(result);
}

Result<AuthResult> AuthService::LoginByCode(const std::string& mobile,
                                const std::string& verify_code){
    std::string error;

    // 1. 参数校验
    if (!IsValidMobile(mobile, error)) {
        return Result<AuthResult>::Fail(ErrorCode::InvalidArgument, error);
    }
    if (!IsValidVerifyCode(verify_code, error,config_->sms)) {
        return Result<AuthResult>::Fail(ErrorCode::InvalidArgument, error);
    }

    // 2.验证码验证
    auto verify_res=sms_srv_->VerifyCaptcha(SmsScene::Login,mobile,verify_code);
    if(!verify_res.IsOk()){
        return Result<AuthResult>::Fail(verify_res.code,verify_res.message);
    }

    // 3.查询用户
    auto user_res=user_db_->FindByMobile(mobile);
    if(!user_res.IsOk()){
        // 用户不存在
        if (user_res.code == ErrorCode::UserNotFound) {
            return Result<AuthResult>::Fail(ErrorCode::UserNotFound, "用户不存在，请先注册");
        }

        return Result<AuthResult>::Fail(user_res.code, user_res.message);
    }

    auto& user=user_res.data.value();

    // 4.检查账户状态
    if(user.disabled){
        return Result<AuthResult>::Fail(ErrorCode::UserDisabled, "账号已被禁用");
    }

    // 5.清除登录失败记录（验证码登录成功也清除）
    ClearLoginFailure(mobile);

    // 6. 生成 Token
    auto token_pair = jwt_srv_->GenerateTokenPair(user);

    // 7. 存储 Refresh Token
    auto store_res = StoreRefreshToken(user.id, token_pair.refresh_token);
    if (!store_res) {
        LOG_WARN("Store refresh token failed for user {}: {}", user.uuid, store_res.message);
    }

    // 8. 返回结果
    AuthResult result;
    result.user = user;
    result.user.password_hash.clear();
    result.tokens = token_pair;

    LOG_INFO("User login by code: mobile={}, uuid={}", mobile, user.uuid);
    return Result<AuthResult>::Ok(result);
}

// ============================================================================
// 重置密码
// ============================================================================

Result<void> AuthService::ResetPassword(const std::string& mobile,
    const std::string& verify_code,
    const std::string& new_password) {
    
    std::string error;

    // 1. 参数校验
    if (!IsValidMobile(mobile, error)) {
        return Result<void>::Fail(ErrorCode::InvalidArgument, error);
    }
    if (!IsValidVerifyCode(verify_code, error,config_->sms)) {
        return Result<void>::Fail(ErrorCode::InvalidArgument, error);
    }
    if (!IsValidPassword(new_password, error,config_->password)) {
        return Result<void>::Fail(ErrorCode::InvalidArgument, error);
    }

    // 2.验证码验证
    auto verify_res=sms_srv_->VerifyCaptcha(SmsScene::ResetPassword,mobile,verify_code);
    if(!verify_res.IsOk()){
        return Result<void>::Fail(verify_res.code, verify_res.message);
    }

    // 3.查询用户
    auto user_res=user_db_->FindByMobile(mobile);
    if(!user_res.IsOk()){
        if (user_res.code == ErrorCode::UserNotFound) {
            return Result<void>::Fail(ErrorCode::UserNotFound, "用户不存在");
        }
        return Result<void>::Fail(user_res.code, user_res.message);
    }

    auto& user = user_res.data.value();

    // 4. 更新密码
    user.password_hash = PasswordHelper::Hash(new_password);
    auto update_res = user_db_->Update(user);
    if (!update_res) {
        return Result<void>::Fail(update_res.code, update_res.message);
    }

    // 5. 使该用户所有 Refresh Token 失效（强制重新登录）
    auto revoke_res = token_repo_->DeleteByUserId(user.id);
    if (!revoke_res) {
        LOG_WARN("Revoke tokens failed for user {}: {}", user.uuid, revoke_res.message);
    }

    // 6. 清除登录失败记录
    ClearLoginFailure(mobile);

    LOG_INFO("User reset password: mobile={}, uuid={}", mobile, user.uuid);
    return Result<void>::Ok();
}


// ============================================================================
// 刷新 Token
// ============================================================================

Result<TokenPair> AuthService::RefreshToken(const std::string& refresh_token){
    // 1. 参数校验
    if (refresh_token.empty()) {
        return Result<TokenPair>::Fail(ErrorCode::InvalidArgument, "refresh_token 不能为空");
    }

    // 2.解析Token，获取user_id
    auto verify_res = jwt_srv_->ParseRefreshToken(refresh_token);
    if (!verify_res.IsOk()) {
        return Result<TokenPair>::Fail(verify_res.code, verify_res.message);
    }

    std::string user_id_str=verify_res.Value();
    int64_t user_id = std::stoll(user_id_str);

    // 3.查询用户，检查状态
    auto user_res=user_db_->FindById(user_id);
    if(!user_res.IsOk()){
        return Result<TokenPair>::Fail(user_res.code, user_res.message);
    }
    if(user_res.data.value().disabled){
        return Result<TokenPair>::Fail(ErrorCode::UserDisabled, "账号已被禁用");
    }

    auto& user=user_res.Value();

    // 4.校验令牌哈希是否有效（数据库中存在且未过期）
    std::string token_hash=jwt_srv_->HashToken(refresh_token);

    auto exists_res=token_repo_->IsTokenValid(token_hash);
    if(!exists_res.IsOk()){
        // 执行失败
        return Result<TokenPair>::Fail(exists_res.code,exists_res.message);
    }else if(!exists_res.data.value_or(false)){
        // Token不存在
        return Result<TokenPair>::Fail(ErrorCode::TokenRevoked, "Token 已失效");
    }
    
    // 5.删除旧的Refresh Token 的哈希值
    token_repo_->DeleteByTokenHash(token_hash);

    // 6.生成新的Token对
    auto new_tokens=jwt_srv_->GenerateTokenPair(user);

    // 7.存储新的Refresh Token的哈希值
    auto store_res=StoreRefreshToken(user_id,new_tokens.refresh_token);
    if (!store_res) {
        LOG_WARN("Store new refresh token failed for user_id={}: {}", user_id, store_res.message);
    }

    LOG_DEBUG("Token refreshed for user_id={}", user_id);
    return Result<TokenPair>::Ok(new_tokens);

}


Result<void> AuthService::Logout(const std::string& refresh_token){
    // 1.空 Token 直接返回成功（幂等性）
    if(refresh_token.empty()){
        return Result<void>::Ok();
    }

    // 2.获取 refresh_token 的哈希值
    std::string token_hash = jwt_srv_->HashToken(refresh_token);

    // 3.删除 token （无论存在与否都返回成功，保证幂等性）
    auto del_res=token_repo_->DeleteByTokenHash(token_hash);
    if(!del_res.IsOk()){
        // 删除失败只记录日志，仍然返回成功
        LOG_WARN("Delete refresh token failed: {}", del_res.message);
    }

    LOG_DEBUG("User logged out, token_hash={}", token_hash.substr(0, 8) + "...");
    return Result<void>::Ok();
}


Result<void> AuthService::LogoutAll(const std::string& user_uuid){
    // 1. 获取 user_id
    auto user_res = user_db_->FindByUUID(user_uuid);
    if (!user_res.IsOk()) {
        return Result<void>::Fail(user_res.code, user_res.message);
    }

    int64_t user_id = user_res.data.value().id;

    // 2. 删除该用户所有 token
    auto del_res = token_repo_->DeleteByUserId(user_id);
    if (!del_res.IsOk()) {
        LOG_WARN("Delete all tokens failed for user_id={}: {}", user_id, del_res.message);
    }

    LOG_INFO("User logged out from all devices, user_id={}", user_id);
    return Result<void>::Ok();
}

Result<TokenValidationResult> AuthService::ValidateAccessToken(const std::string& access_token) {
    TokenValidationResult validation;
    
    // 空 token 快速返回
    if (access_token.empty()) {
        return Result<TokenValidationResult>::Fail(ErrorCode::TokenMissing, "Access token is required");
    }
    
    // 1. 解析 JWT
    auto payload_result = jwt_srv_->VerifyAccessToken(access_token);
    if (!payload_result.IsOk()) {
        return Result<TokenValidationResult>::Fail(payload_result.code, payload_result.message);
    }
    
    const auto& payload = payload_result.Value();
    
    // 2. 检查是否在黑名单中（可选）
    // std::string blacklist_key = "token:blacklist:" + payload.user_uuid;
    // if (redis_cli_->Exists(blacklist_key)) {
    //     return Result<TokenValidationResult>::Fail(ErrorCode::TokenRevoked, "Token has been revoked");
    // }
    
    // 3. 填充验证结果
    validation.user_id = payload.user_id;
    validation.user_uuid = payload.user_uuid;
    validation.mobile = payload.mobile;
    validation.expires_at = payload.expires_at;
    
    return Result<TokenValidationResult>::Ok(validation);
}

// ============================================================================
// 私有方法：检查登录失败次数
// ============================================================================

Result<void> AuthService::CheckLoginFailedAttempts(const std::string& mobile) {
    std::string key = "login:fail:" + mobile;
    
    auto count_res = redis_cli_->Get(key);
    if (!count_res.IsOk()) {
        // Redis 错误，记录日志但不阻止登录
        LOG_WARN("Check login attempts failed: {}", count_res.message);
        return Result<void>::Ok();
    }

    if (count_res.data.has_value() && count_res.data.value().has_value()) {  //有值：说明在失败窗口期或被锁定中（注意：嵌套optional要双层检查）
        int count = std::stoi(count_res.data.value().value());

        // 登录失败次数最大限度
        if (count >= config_->login.max_failed_attempts) {
            // 获取剩余锁定时间（毫秒）
            auto ttl_res = redis_cli_->PTTL(key);
            
            int64_t ttl_ms;
            if (ttl_res.IsOk() && ttl_res.data.has_value() && ttl_res.data.value() > 0) {
                ttl_ms = ttl_res.data.value();
            } else {
                // PTTL 失败或 key 无过期时间，使用配置值（转毫秒）
                ttl_ms = config_->login.lock_duration_seconds * 1000;
            }

            // 毫秒转分钟（向上取整）
            int minutes = static_cast<int>((ttl_ms + 59999) / 60000);
            if (minutes < 1) minutes = 1;  // 至少显示1分钟

            return Result<void>::Fail(
                ErrorCode::AccountLocked,
                "登录失败次数过多，请" + std::to_string(minutes) + "分钟后再试"
            );
        }
    }

    return Result<void>::Ok();
}

// ============================================================================
// 私有方法：记录登录失败
// ============================================================================

void AuthService::RecordLoginFailure(const std::string& mobile) {
    std::string key = "login:fail:" + mobile;
    
    // 1.失败次数 +1
    auto incr_res = redis_cli_->Incr(key);
    if (!incr_res.IsOk()) {
        LOG_WARN("Record login failure failed: {}", incr_res.message);
        return;
    }

    // 2.判断是否触发锁定
    int64_t count=incr_res.data.value_or(1);
    if(count == config_->login.max_failed_attempts){
        // 刚好达到上限（第一次触发），设置锁定时间
        std::chrono::milliseconds ttl{config_->login.lock_duration_seconds*1000};
        redis_cli_->PExpire(key,ttl);
        LOG_WARN("Account locked: mobile={}, duration={}s", 
            mobile, config_->login.lock_duration_seconds);
    }else if(count < config_->login.max_failed_attempts){
        // 未达上限 -> 设置窗口期（只在第一次失败时设置）
        auto ttl_res=redis_cli_->PTTL(key);
        if(!ttl_res.IsOk() || ttl_res.data.value_or(-1)<0){
            // key 不存在/没有过期时间，设置窗口期
            std::chrono::milliseconds ttl{config_->login.failed_attempts_window*1000};
            redis_cli_->PExpire(key,ttl);
        }
        // 如果已有过期时间，不刷新（保持原窗口期）
    }
}

// ============================================================================
// 私有方法：清除登录失败记录
// ============================================================================

void AuthService::ClearLoginFailure(const std::string& mobile) {
    std::string key = "login:fail:" + mobile;
    
    auto del_res = redis_cli_->Del(key);
    if (!del_res) {
        LOG_WARN("Clear login failure failed: {}", del_res.message);
    }
}

// ============================================================================
// 私有方法：存储 Refresh Token
// ============================================================================

Result<void> AuthService::StoreRefreshToken(int64_t user_id, const std::string& refresh_token) {
    auto token_hash=jwt_srv_->HashToken(refresh_token);
    return token_repo_->SaveRefreshToken(user_id,token_hash,config_->security.refresh_token_ttl_seconds);
}



}