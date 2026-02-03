#include "jwt_service.h"
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <stdexcept>
#include <random>

namespace user_service {

// ============================================================================
// 构造函数
// ============================================================================

/// @brief 初始化 JWT 服务
/// @param config 安全配置，包含：
///   - jwt_secret: HMAC-SHA256 签名密钥（建议 32 字节以上）
///   - jwt_issuer: 签发者标识（如 "user-service"）
///   - access_token_ttl_seconds: Access Token 有效期（秒）
///   - refresh_token_ttl_seconds: Refresh Token 有效期（秒）
JwtService::JwtService(const SecurityConfig& config) : config_(config) {}

// ============================================================================
// Base64URL 编解码
// ============================================================================

/// @brief JWT 专用 Base64URL 编码
/// @details 与标准 Base64 的区别：
///   1. 字符集：使用 '-' 替代 '+'，'_' 替代 '/'（URL 安全）
///   2. 无 Padding：不添加末尾的 '=' 字符（JWT 标准要求）
/// @param input 原始二进制数据
/// @return Base64URL 编码字符串
/// @example "Hello" -> "SGVsbG8"
std::string JwtService::Base64UrlEncode(const std::string& input) {
    // Base64URL 字符表（64 个字符）
    static const char table[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    
    std::string encoded;
    encoded.reserve(((input.size() + 2) / 3) * 4);  // 预分配空间，3 字节 → 4 字符
    
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;       // 累积输入字节
        valb += 8;                   // 记录累积的位数
        while (valb >= 0) {
            encoded.push_back(table[(val >> valb) & 0x3F]);  // 取 6 位编码
            valb -= 6;
        }
    }
    // 处理剩余位（不足 6 位时补 0）
    if (valb > -6) {
        encoded.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    // JWT 标准：不添加 '=' padding
    return encoded;
}

/// @brief JWT 专用 Base64URL 解码
/// @details 将 Base64URL 编码字符串还原为原始二进制数据
/// @param input Base64URL 编码字符串
/// @return 解码后的原始数据
/// @example "SGVsbG8" -> "Hello"
std::string JwtService::Base64UrlDecode(const std::string& input) {
    // 反向查找表：字符 → 6 位值，-1 表示非法字符
    static const int table[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  // 0x00-0x0F
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  // 0x10-0x1F
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,  // 0x20-0x2F ('-' = 62)
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,  // 0x30-0x3F ('0'-'9' = 52-61)
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,  // 0x40-0x4F ('A'-'O' = 0-14)
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,63,  // 0x50-0x5F ('P'-'Z' = 15-25, '_' = 63)
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,  // 0x60-0x6F ('a'-'o' = 26-40)
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,  // 0x70-0x7F ('p'-'z' = 41-51)
        // 0x80-0xFF 全部为 -1（非 ASCII 字符）
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    
    std::string decoded;
    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (table[c] == -1) break;   // 遇到非法字符停止
        val = (val << 6) + table[c]; // 累积 6 位
        valb += 6;
        if (valb >= 0) {
            decoded.push_back(char((val >> valb) & 0xFF));  // 取 8 位输出
            valb -= 8;
        }
    }
    return decoded;
}

// ============================================================================
// HMAC-SHA256 签名
// ============================================================================

/// @brief 使用 HMAC-SHA256 算法生成消息认证码
/// @details JWT 签名算法 HS256 的核心实现：
///   签名 = HMAC-SHA256(密钥, Header.Payload)
/// @param key 签名密钥（jwt_secret）
/// @param data 待签名数据（Header.Payload 的 Base64URL 拼接）
/// @return 32 字节的二进制签名（需再经 Base64URL 编码）
std::string JwtService::HmacSha256(const std::string& key, const std::string& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];  // 存储哈希结果
    unsigned int len = 0;
    
    // OpenSSL HMAC 函数
    HMAC(EVP_sha256(),                                              // 哈希算法：SHA-256
         key.c_str(), static_cast<int>(key.length()),               // 密钥
         reinterpret_cast<const unsigned char*>(data.c_str()),      // 待签名数据
         data.length(),
         hash, &len);                                                // 输出
    
    return std::string(reinterpret_cast<char*>(hash), len);  // 返回二进制签名
}

// ============================================================================
// 简易 JSON 工具（不依赖外部库）
// ============================================================================

/// @brief JSON 字符串转义
/// @details 处理 JSON 中的特殊字符，避免格式错误
/// @param s 原始字符串
/// @return 转义后的字符串
static std::string EscapeJsonString(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;  // 双引号
            case '\\': result += "\\\\"; break;  // 反斜杠
            case '\b': result += "\\b";  break;  // 退格
            case '\f': result += "\\f";  break;  // 换页
            case '\n': result += "\\n";  break;  // 换行
            case '\r': result += "\\r";  break;  // 回车
            case '\t': result += "\\t";  break;  // 制表符
            default:   result += c;      break;
        }
    }
    return result;
}

/// @brief 从键值对构建 JSON 字符串
/// @details 自动区分数字和字符串值：
///   - 数字值不加引号：{"exp": 1234567890}
///   - 字符串值加引号：{"uid": "abc-123"}
/// @param claims JWT 载荷的键值对
/// @return JSON 字符串
static std::string BuildJson(const std::map<std::string, std::string>& claims) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& [key, value] : claims) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << EscapeJsonString(key) << "\":";
        
        // 判断是否为数字（以数字开头，或负号+数字）
        bool is_number = !value.empty() && 
            (isdigit(value[0]) || (value[0] == '-' && value.size() > 1));
        if (is_number) {
            oss << value;  // 数字不加引号
        } else {
            oss << "\"" << EscapeJsonString(value) << "\"";  // 字符串加引号
        }
    }
    oss << "}";
    return oss.str();
}

/// @brief 解析简单 JSON 对象为键值对
/// @details 仅支持 JWT 载荷的基础 JSON 格式：
///   - 单层对象
///   - 值为字符串或数字
///   - 不支持嵌套对象、数组
/// @param json JSON 字符串
/// @return 解析后的键值对
static std::map<std::string, std::string> ParseJson(const std::string& json) {
    std::map<std::string, std::string> result;
    
    size_t i = 0;
    
    // 跳过空白字符
    auto skip_whitespace = [&]() {
        while (i < json.size() && isspace(json[i])) i++;
    };
    
    // 解析 JSON 字符串（处理转义）
    auto parse_string = [&]() -> std::string {
        if (json[i] != '"') return "";
        i++;  // 跳过左引号
        std::string s;
        while (i < json.size() && json[i] != '"') {
            if (json[i] == '\\' && i + 1 < json.size()) {
                i++;
                switch (json[i]) {
                    case '"':  s += '"';  break;
                    case '\\': s += '\\'; break;
                    case 'n':  s += '\n'; break;
                    case 'r':  s += '\r'; break;
                    case 't':  s += '\t'; break;
                    default:   s += json[i]; break;
                }
            } else {
                s += json[i];
            }
            i++;
        }
        if (i < json.size()) i++;  // 跳过右引号
        return s;
    };
    
    // 解析值（字符串或数字）
    auto parse_value = [&]() -> std::string {
        skip_whitespace();
        if (i >= json.size()) return "";
        if (json[i] == '"') {
            return parse_string();  // 字符串值
        }
        // 数字/布尔值（直接读取到分隔符）
        std::string val;
        while (i < json.size() && json[i] != ',' && json[i] != '}') {
            if (!isspace(json[i])) val += json[i];
            i++;
        }
        return val;
    };
    
    skip_whitespace();
    if (i >= json.size() || json[i] != '{') return result;
    i++;  // 跳过 '{'
    
    while (i < json.size()) {
        skip_whitespace();
        if (json[i] == '}') break;
        if (json[i] == ',') { i++; continue; }
        
        std::string key = parse_string();
        skip_whitespace();
        if (i < json.size() && json[i] == ':') i++;
        std::string value = parse_value();
        
        if (!key.empty()) {
            result[key] = value;
        }
    }
    
    return result;
}

// ============================================================================
// JWT 核心操作
// ============================================================================

/// @brief 创建 JWT 令牌
/// @details JWT 结构（三段式，用 '.' 分隔）：
///   ┌──────────────────────────────────────────────────────────────┐
///   │  Header.Payload.Signature                                    │
///   ├──────────────────────────────────────────────────────────────┤
///   │  Header (固定):                                               │
///   │    {"alg":"HS256","typ":"JWT"}                               │
///   │    → Base64URL 编码后约 36 字符                                │
///   ├──────────────────────────────────────────────────────────────┤
///   │  Payload (可变):                                              │
///   │    由 claims 参数决定，见 GenerateAccessToken/RefreshToken    │
///   │    → Base64URL 编码后长度随内容变化                            │
///   ├──────────────────────────────────────────────────────────────┤
///   │  Signature:                                                  │
///   │    HMAC-SHA256(secret, Header.Payload)                       │
///   │    → Base64URL 编码后约 43 字符                                │
///   └──────────────────────────────────────────────────────────────┘
/// 
/// @param claims JWT 载荷（键值对）
/// @param secret 签名密钥
/// @return 完整的 JWT 字符串
std::string JwtService::CreateJwt(const std::map<std::string, std::string>& claims,
                                   const std::string& secret) {
    // 1. JWT Header（固定值）
    //    alg: 签名算法，使用 HMAC-SHA256
    //    typ: 令牌类型，固定为 JWT
    std::string header = R"({"alg":"HS256","typ":"JWT"})";
    std::string header_b64 = Base64UrlEncode(header);
    
    // 2. JWT Payload（由 claims 构建）
    std::string payload = BuildJson(claims);
    std::string payload_b64 = Base64UrlEncode(payload);
    
    // 3. JWT Signature
    //    签名输入 = Header_Base64 + "." + Payload_Base64
    //    签名 = Base64URL(HMAC-SHA256(secret, 签名输入))
    std::string signature_input = header_b64 + "." + payload_b64;
    std::string signature = Base64UrlEncode(HmacSha256(secret, signature_input));
    
    // 4. 拼接完整 JWT
    return signature_input + "." + signature;
}

/// @brief 验证 JWT 令牌
/// @details 验证流程：
///   1. 格式检查：必须包含两个 '.' 分隔符
///   2. 签名验证：重新计算签名并比对
///   3. 过期检查：比对 exp 字段与当前时间
/// @param token 待验证的 JWT 字符串
/// @param secret 签名密钥
/// @return 验证结果（包含 success 标志和解析后的 claims）
JwtService::JwtVerifyResult JwtService::VerifyJwt(
    const std::string& token, const std::string& secret) {
    
    JwtVerifyResult result;
    result.success = false;
    
    // ========== 1. 格式校验 ==========
    // JWT 格式：Header.Payload.Signature（必须有两个 '.'）
    size_t dot1 = token.find('.');
    size_t dot2 = token.find('.', dot1 + 1);
    if (dot1 == std::string::npos || dot2 == std::string::npos) {
        result.error_code = ErrorCode::TokenInvalid;
        return result;
    }
    
    // 分割三段
    std::string header_b64 = token.substr(0, dot1);
    std::string payload_b64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
    std::string signature = token.substr(dot2 + 1);
    
    // ========== 2. 签名验证 ==========
    // 重新计算签名，与令牌中的签名比对
    std::string signature_input = header_b64 + "." + payload_b64;
    std::string expected_signature = Base64UrlEncode(HmacSha256(secret, signature_input));
    if (signature != expected_signature) {
        result.error_code = ErrorCode::TokenInvalid;  // 签名不匹配，令牌被篡改
        return result;
    }
    
    // ========== 3. 解析 Payload ==========
    std::string payload_json = Base64UrlDecode(payload_b64);
    result.claims = ParseJson(payload_json);
    
    // ========== 4. 过期时间校验 ==========
    if (result.claims.count("exp")) {
        try {
            int64_t exp = std::stoll(result.claims["exp"]);
            auto now = std::chrono::system_clock::now();
            auto exp_time = std::chrono::system_clock::from_time_t(exp);
            if (now > exp_time) {
                result.error_code = ErrorCode::TokenExpired;  // 令牌已过期
                return result;
            }
        } catch (...) {
            result.error_code = ErrorCode::TokenInvalid;  // exp 格式错误
            return result;
        }
    }
    
    result.success = true;
    result.error_code = ErrorCode::Ok;
    return result;
}

// ============================================================================
// Token 生成接口
// ============================================================================

/// @brief 生成 Access Token 和 Refresh Token 令牌对
/// @details 双令牌机制说明：
///   ┌────────────────────────────────────────────────────────────────┐
///   │  Access Token                                                  │
///   │  ├─ 用途：业务接口鉴权（放在 Authorization: Bearer xxx）        │
///   │  ├─ 有效期：短（如 2 小时），减少泄露风险                        │
///   │  └─ 载荷：包含完整用户信息（id, uuid, mobile）                  │
///   ├────────────────────────────────────────────────────────────────┤
///   │  Refresh Token                                                 │
///   │  ├─ 用途：仅用于刷新 Access Token                              │
///   │  ├─ 有效期：长（如 7 天），支持用户长期在线                      │
///   │  └─ 载荷：仅包含用户 ID，最小化信息暴露                         │
///   └────────────────────────────────────────────────────────────────┘
/// 
/// @param user 用户实体（包含 id, uuid, mobile 等字段）
/// @return TokenPair 包含两个令牌和 Access Token 过期时间（秒）
TokenPair JwtService::GenerateTokenPair(const UserEntity& user) {
    TokenPair result;
    result.access_token = GenerateAccessToken(user);
    result.refresh_token = GenerateRefreshToken(user);
    result.expires_in = config_.access_token_ttl_seconds;  // Access Token 有效期（秒）
    return result;
}

/// @brief 生成 Access Token（访问令牌）
/// @details
///   ┌────────────────────────────────────────────────────────────────┐
///   │  Access Token JWT Payload 结构                                 │
///   ├──────────┬─────────────────────────────────────────────────────┤
///   │  字段    │  说明                                               │
///   ├──────────┼─────────────────────────────────────────────────────┤
///   │  iss     │  签发者（Issuer），如 "user-service"                 │
///   │  sub     │  主题（Subject），固定为 "access" 标识令牌类型        │
///   │  id      │  用户数据库 ID（int64_t），用于内部快速查库           │
///   │  uid     │  用户 UUID，对外暴露的用户标识（API 返回用）          │
///   │  mobile  │  用户手机号，业务需要时可直接从令牌获取               │
///   │  iat     │  签发时间（Issued At），Unix 时间戳（秒）             │
///   │  exp     │  过期时间（Expiration），Unix 时间戳（秒）            │
///   │  jti     │  JWT ID，随机数，确保每次生成的令牌唯一               │
///   └──────────┴─────────────────────────────────────────────────────┘
/// 
///   示例 Payload（解码后）：
///   {
///     "iss": "user-service",
///     "sub": "access",
///     "id": "12345",
///     "uid": "550e8400-e29b-41d4-a716-446655440000",
///     "mobile": "13800138000",
///     "iat": 1704067200,
///     "exp": 1704074400,
///     "jti": "582943"
///   }
/// 
/// @param user_id 用户数据库 ID（字符串格式）
/// @param user_uuid 用户 UUID
/// @param mobile 用户手机号
/// @return JWT 格式的 Access Token 字符串
std::string JwtService::GenerateAccessToken(const UserEntity& user) {
    
    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::seconds(config_.access_token_ttl_seconds);
    
    // 随机数生成器（用于 jti 字段，确保令牌唯一性）
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);
    
    // 构建 JWT Claims
    std::map<std::string, std::string> claims;
    claims["iss"] = config_.jwt_issuer;   // 签发者
    claims["sub"] = "access";              // 令牌类型标识
    claims["id"]  = std::to_string(user.id);               // 数据库用户 ID（内部使用）
    claims["uid"] = user.uuid;             // 用户 UUID（对外暴露）
    claims["mobile"] = user.mobile;        // 手机号
    claims["role"] = UserRoleToString(user.role);
    claims["iat"] = std::to_string(std::chrono::system_clock::to_time_t(now));  // 签发时间
    claims["exp"] = std::to_string(std::chrono::system_clock::to_time_t(exp));  // 过期时间
    claims["jti"] = std::to_string(dis(gen));  // JWT ID（6 位随机数）
    
    return CreateJwt(claims, config_.jwt_secret);
}

/// @brief 生成 Refresh Token（刷新令牌）
/// @details
///   ┌────────────────────────────────────────────────────────────────┐
///   │  Refresh Token JWT Payload 结构                                │
///   ├──────────┬─────────────────────────────────────────────────────┤
///   │  字段    │  说明                                               │
///   ├──────────┼─────────────────────────────────────────────────────┤
///   │  iss     │  签发者（Issuer），如 "user-service"                 │
///   │  sub     │  主题（Subject），固定为 "refresh" 标识令牌类型       │
///   │  uid     │  用户数据库 ID，用于查库获取用户信息生成新令牌        │
///   │  iat     │  签发时间（Issued At），Unix 时间戳（秒）             │
///   │  exp     │  过期时间（Expiration），Unix 时间戳（秒）            │
///   │  jti     │  JWT ID，随机数，确保每次生成的令牌唯一               │
///   └──────────┴─────────────────────────────────────────────────────┘
/// 
///   示例 Payload（解码后）：
///   {
///     "iss": "user-service",
///     "sub": "refresh",
///     "uid": "12345",
///     "iat": 1704067200,
///     "exp": 1704672000,
///     "jti": "847291"
///   }
/// 
///   与 Access Token 的区别：
///   1. sub = "refresh"（用于区分令牌类型）
///   2. 仅包含 uid（数据库 ID），不包含 uuid、mobile
///   3. 有效期更长（如 7 天 vs 2 小时）
///   4. 仅用于调用 RefreshToken 接口，不能用于业务鉴权
/// 
/// @param user_id 用户数据库 ID（字符串格式）
/// @return JWT 格式的 Refresh Token 字符串
std::string JwtService::GenerateRefreshToken(const UserEntity& user) {
    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::seconds(config_.refresh_token_ttl_seconds);
    
    // 随机数生成器（确保同一秒内生成的 token 不同）
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);
    
    // 构建 JWT Claims（最小化载荷）
    std::map<std::string, std::string> claims;
    claims["iss"] = config_.jwt_issuer;   // 签发者
    claims["sub"] = "refresh";             // 令牌类型标识
    claims["uid"] = std::to_string(user.id);      // 用户数据库 ID（注意：这里是数据库 ID，不是 UUID）
    claims["iat"] = std::to_string(std::chrono::system_clock::to_time_t(now));  // 签发时间
    claims["exp"] = std::to_string(std::chrono::system_clock::to_time_t(exp));  // 过期时间
    claims["jti"] = std::to_string(dis(gen));  // JWT ID（6 位随机数）
    
    return CreateJwt(claims, config_.jwt_secret);
}

// ============================================================================
// Token 验证/解析接口
// ============================================================================

/// @brief 验证 Access Token 并解析载荷
/// @details 验证流程：
///   1. 空值检查
///   2. JWT 签名和过期时间验证（调用 VerifyJwt）
///   3. 签发者校验（iss 必须匹配配置）
///   4. 令牌类型校验（sub 必须为 "access"）
///   5. 必要字段检查（id, uid 必须存在）
///   6. 解析并返回结构化载荷
/// 
/// @param token Access Token 字符串
/// @return Result<AccessTokenPayload> 成功时返回解析后的载荷
///         载荷包含：user_id, user_uuid, mobile, expires_at
Result<AccessTokenPayload> JwtService::VerifyAccessToken(const std::string& token) {
    // ========== 1. 参数校验 ==========
    if (token.empty()) {
        return Result<AccessTokenPayload>::Fail(ErrorCode::TokenMissing, "Token 不能为空");
    }
    
    // ========== 2. JWT 验证（签名 + 过期时间）==========
    auto verify_result = VerifyJwt(token, config_.jwt_secret);
    if (!verify_result.success) {
        return Result<AccessTokenPayload>::Fail(verify_result.error_code);
    }
    
    const auto& claims = verify_result.claims;
    
    // ========== 3. 签发者校验 ==========
    auto iss_it = claims.find("iss");
    if (iss_it != claims.end() && iss_it->second != config_.jwt_issuer) {
        return Result<AccessTokenPayload>::Fail(ErrorCode::TokenInvalid, "令牌签发者不匹配");
    }
    
    // ========== 4. 令牌类型校验 ==========
    // 确保是 Access Token，而不是 Refresh Token
    auto sub_it = claims.find("sub");
    if (sub_it != claims.end() && sub_it->second != "access") {
        return Result<AccessTokenPayload>::Fail(ErrorCode::TokenInvalid, "令牌类型不匹配");
    }
    
    // ========== 5. 必要字段检查 ==========
    auto id_it = claims.find("id");       // 数据库 ID
    auto uid_it = claims.find("uid");     // UUID
    auto mobile_it = claims.find("mobile");
    auto role_it = claims.find("role");
    
    if (id_it == claims.end() || id_it->second.empty()) {
        return Result<AccessTokenPayload>::Fail(ErrorCode::TokenInvalid, "令牌缺少用户ID");
    }
    if (uid_it == claims.end() || uid_it->second.empty()) {
        return Result<AccessTokenPayload>::Fail(ErrorCode::TokenInvalid, "令牌缺少用户UUID");
    }
    if(role_it==claims.end() || role_it->second.empty()){
        return Result<AccessTokenPayload>::Fail(ErrorCode::TokenInvalid, "令牌缺少用户ROLE");
    }
    
    // ========== 6. 解析为结构化载荷 ==========
    AccessTokenPayload payload;
    
    // 解析 user_id (int64_t)
    try {
        payload.user_id = std::stoll(id_it->second);
    } catch (...) {
        return Result<AccessTokenPayload>::Fail(ErrorCode::TokenInvalid, "用户ID格式错误");
    }
    
    // 解析 user_uuid (string)
    payload.user_uuid = uid_it->second;
    
    // 解析 mobile (可选字段)
    if (mobile_it != claims.end()) {
        payload.mobile = mobile_it->second;
    }
    
    // 解析 role
    payload.role=StringToUserRole(role_it->second);

    // 解析过期时间
    auto exp_it = claims.find("exp");
    if (exp_it != claims.end()) {
        try {
            int64_t exp = std::stoll(exp_it->second);
            payload.expires_at = std::chrono::system_clock::from_time_t(exp);
        } catch (...) {
            payload.expires_at = std::chrono::system_clock::now();  // 解析失败使用当前时间
        }
    }
    
    return Result<AccessTokenPayload>::Ok(payload);
}

/// @brief 验证 Refresh Token 并提取用户 ID
/// @details 验证流程：
///   1. 空值检查
///   2. JWT 签名和过期时间验证
///   3. 签发者校验
///   4. 令牌类型校验（sub 必须为 "refresh"）
///   5. 提取用户数据库 ID
/// 
///   注意：返回的是数据库 ID（用于查库生成新令牌），不是 UUID
/// 
/// @param token Refresh Token 字符串
/// @return Result<std::string> 成功时返回用户数据库 ID
Result<std::string> JwtService::ParseRefreshToken(const std::string& token) {
    // ========== 1. 参数校验 ==========
    if (token.empty()) {
        return Result<std::string>::Fail(ErrorCode::TokenMissing, "Token 不能为空");
    }
    
    // ========== 2. JWT 验证 ==========
    auto verify_result = VerifyJwt(token, config_.jwt_secret);
    if (!verify_result.success) {
        return Result<std::string>::Fail(verify_result.error_code);
    }
    
    const auto& claims = verify_result.claims;
    
    // ========== 3. 签发者校验 ==========
    auto iss_it = claims.find("iss");
    if (iss_it != claims.end() && iss_it->second != config_.jwt_issuer) {
        return Result<std::string>::Fail(ErrorCode::TokenInvalid, "令牌签发者不匹配");
    }
    
    // ========== 4. 令牌类型校验 ==========
    // 确保是 Refresh Token，而不是 Access Token
    auto sub_it = claims.find("sub");
    if (sub_it != claims.end() && sub_it->second != "refresh") {
        return Result<std::string>::Fail(ErrorCode::TokenInvalid, "令牌类型不匹配");
    }
    
    // ========== 5. 提取用户 ID ==========
    auto uid_it = claims.find("uid");
    if (uid_it != claims.end() && !uid_it->second.empty()) {
        return Result<std::string>::Ok(uid_it->second);  // 返回数据库 ID
    }
    
    return Result<std::string>::Fail(ErrorCode::TokenInvalid, "令牌缺少用户标识");
}

// ============================================================================
// Token 哈希（用于安全存储）
// ============================================================================

/// @brief 对 Token 进行 SHA256 哈希
/// @details 用于服务端存储 Token 时避免明文：
///   1. 数据库存储 Refresh Token 时，存哈希值而非原文
///   2. 验证时：Hash(用户传的Token) == 数据库存储的哈希值
///   3. 即使数据库泄露，攻击者也无法还原原始 Token
/// 
/// @param token 原始 Token 字符串
/// @return 64 字符的十六进制哈希字符串（SHA256 = 32 字节 = 64 hex）
/// @example "eyJhbG..." → "a1b2c3d4e5f6..."
std::string JwtService::HashToken(const std::string& token) {
    unsigned char hash[SHA256_DIGEST_LENGTH];  // 32 字节
    SHA256(reinterpret_cast<const unsigned char*>(token.c_str()), 
           token.length(), hash);
    
    // 转换为十六进制字符串
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();  // 64 字符
}

} // namespace user_service
