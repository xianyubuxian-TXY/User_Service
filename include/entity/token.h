#pragma once
#include <string>


namespace user_service{
// ==================== Token 会话实体 ====================
// 一个会话（Session） ≈ 一次登录 ≈ 一个设备/客户端。
struct TokenSession {
    int64_t id = 0;
    int64_t user_id = 0;
    std::string token_hash;
    std::string expires_at;     // MySQL DATETIME 格式
    std::string created_at;
};

}