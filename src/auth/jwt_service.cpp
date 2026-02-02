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

// ==================== 构造函数 ====================
// 初始化JWT服务，传入全局配置
JwtService::JwtService(const SecurityConfig& config) : config_(config) {}

// ==================== Base64URL 编码 ====================
// JWT专用Base64URL编码，无padding，替换+/为-_
std::string JwtService::Base64UrlEncode(const std::string& input) {
    static const char table[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    
    std::string encoded;
    encoded.reserve(((input.size() + 2) / 3) * 4);
    
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            encoded.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        encoded.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    // 不添加 padding（JWT 标准）
    return encoded;
}

// ==================== Base64URL 解码 ====================
// 解析JWT的Base64URL编码内容，还原原始字符串
std::string JwtService::Base64UrlDecode(const std::string& input) {
    static const int table[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,63,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
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
        if (table[c] == -1) break;
        val = (val << 6) + table[c];
        valb += 6;
        if (valb >= 0) {
            decoded.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return decoded;
}

// ==================== HMAC-SHA256 签名 ====================
// HMAC-SHA256哈希计算，生成二进制签名结果，用于JWT签名
std::string JwtService::HmacSha256(const std::string& key, const std::string& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    
    HMAC(EVP_sha256(), 
         key.c_str(), static_cast<int>(key.length()),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         hash, &len);
    
    return std::string(reinterpret_cast<char*>(hash), len);
}

// ==================== 简易 JSON 构建（不依赖外部库） ====================
// JSON字符串转义，处理特殊字符，避免JSON格式错误
static std::string EscapeJsonString(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b";  break;
            case '\f': result += "\\f";  break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:   result += c;      break;
        }
    }
    return result;
}

// 从键值对构建JSON字符串，适配JWT载荷构建，自动区分数字/字符串值
static std::string BuildJson(const std::map<std::string, std::string>& claims) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& [key, value] : claims) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << EscapeJsonString(key) << "\":";
        // 判断是否为数字，数字值不添加引号
        bool is_number = !value.empty() && 
            (isdigit(value[0]) || (value[0] == '-' && value.size() > 1));
        if (is_number) {
            oss << value;
        } else {
            oss << "\"" << EscapeJsonString(value) << "\"";
        }
    }
    oss << "}";
    return oss.str();
}

// ==================== 简易 JSON 解析 ====================
// 解析简单JSON对象为键值对，仅支持JWT载荷的基础JSON格式
static std::map<std::string, std::string> ParseJson(const std::string& json) {
    std::map<std::string, std::string> result;
    
    size_t i = 0;
    auto skip_whitespace = [&]() {
        while (i < json.size() && isspace(json[i])) i++;
    };
    
    auto parse_string = [&]() -> std::string {
        if (json[i] != '"') return "";
        i++; // 跳过左引号
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
        if (i < json.size()) i++; // 跳过右引号
        return s;
    };
    
    auto parse_value = [&]() -> std::string {
        skip_whitespace();
        if (i >= json.size()) return "";
        // 解析字符串值
        if (json[i] == '"') {
            return parse_string();
        }
        // 解析数字/其他非字符串值
        std::string val;
        while (i < json.size() && json[i] != ',' && json[i] != '}') {
            if (!isspace(json[i])) val += json[i];
            i++;
        }
        return val;
    };
    
    skip_whitespace();
    if (i >= json.size() || json[i] != '{') return result;
    i++; // 跳过左大括号
    
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

// ==================== 创建 JWT ====================
// 生成JWT令牌，传入载荷和签名秘钥，按JWT标准拼接header/payload/signature
std::string JwtService::CreateJwt(const std::map<std::string, std::string>& claims,
                                   const std::string& secret) {
    // JWT头部，指定算法为HS256，类型为JWT
    std::string header = R"({"alg":"HS256","typ":"JWT"})";
    std::string header_b64 = Base64UrlEncode(header);
    
    // 构建JWT载荷并编码
    std::string payload = BuildJson(claims);
    std::string payload_b64 = Base64UrlEncode(payload);
    
    // 生成签名并编码，拼接最终JWT
    std::string signature_input = header_b64 + "." + payload_b64;
    std::string signature = Base64UrlEncode(HmacSha256(secret, signature_input));
    
    return signature_input + "." + signature;
}

// ==================== 验证 JWT ====================
// 验证JWT有效性：校验格式、签名、过期时间，有效则返回解析后的载荷
JwtService::JwtVerifyResult JwtService::VerifyJwt(
    const std::string& token, const std::string& secret) {
    
    JwtVerifyResult result;
    result.success = false;
    
    // 校验JWT格式，必须包含两个分隔符
    size_t dot1 = token.find('.');
    size_t dot2 = token.find('.', dot1 + 1);
    if (dot1 == std::string::npos || dot2 == std::string::npos) {
        result.error_code = ErrorCode::TokenInvalid;
        return result;
    }
    // 分割header/payload/signature
    std::string header_b64 = token.substr(0, dot1);
    std::string payload_b64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
    std::string signature = token.substr(dot2 + 1);
    
    // 重新生成签名，校验签名一致性
    std::string signature_input = header_b64 + "." + payload_b64;
    std::string expected_signature = Base64UrlEncode(HmacSha256(secret, signature_input));
    if (signature != expected_signature) {
        result.error_code = ErrorCode::TokenInvalid;
        return result;
    }
    
    // 解析载荷并校验过期时间
    std::string payload_json = Base64UrlDecode(payload_b64);
    result.claims = ParseJson(payload_json);
    
    if (result.claims.count("exp")) {
        try {
            int64_t exp = std::stoll(result.claims["exp"]);
            auto now = std::chrono::system_clock::now();
            auto exp_time = std::chrono::system_clock::from_time_t(exp);
            if (now > exp_time) {
                result.error_code = ErrorCode::TokenExpired;
                return result; // 令牌已过期
            }
        } catch (...) {
            result.error_code = ErrorCode::TokenInvalid;
            return result; // 过期时间格式错误
        }
    }
    
    result.success = true;
    result.error_code = ErrorCode::Ok;
    return result;
}

// ==================== 生成 Token 对 ====================
// 生成Access/Refresh双令牌，对外核心接口，返回令牌对和Access Token有效期
TokenPair JwtService::GenerateTokenPair(const std::string& user_id) {
    TokenPair result;
    result.access_token = GenerateAccessToken(user_id);
    result.refresh_token = GenerateRefreshToken(user_id);
    result.expires_in = config_.access_token_ttl_seconds;
    return result;
}

// ==================== 生成 Access Token ====================
// 生成访问令牌，载荷包含用户ID、签发时间、过期时间等核心信息
std::string JwtService::GenerateAccessToken(const std::string& user_id) {
    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::seconds(config_.access_token_ttl_seconds);
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);
    
    std::map<std::string, std::string> claims;
    claims["iss"] = config_.jwt_issuer;
    claims["sub"] = "access";
    claims["uid"] = user_id;
    claims["iat"] = std::to_string(std::chrono::system_clock::to_time_t(now));
    claims["exp"] = std::to_string(std::chrono::system_clock::to_time_t(exp));
    claims["jti"] = std::to_string(dis(gen));  // ✅ JWT ID
    
    return CreateJwt(claims, config_.jwt_secret);
}

// ==================== 生成 Refresh Token ====================
// 生成刷新令牌，载荷仅包含核心用户标识，有效期更长，用于刷新Access Token
std::string JwtService::GenerateRefreshToken(const std::string& user_id) {
    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::seconds(config_.refresh_token_ttl_seconds);
    
    // ✅ 添加随机因子，确保同一秒内生成的 token 不同
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);
    
    std::map<std::string, std::string> claims;
    claims["iss"] = config_.jwt_issuer;
    claims["sub"] = "refresh";
    claims["uid"] = user_id;
    claims["iat"] = std::to_string(std::chrono::system_clock::to_time_t(now));
    claims["exp"] = std::to_string(std::chrono::system_clock::to_time_t(exp));
    claims["jti"] = std::to_string(dis(gen));  // ✅ JWT ID，随机唯一值
    
    return CreateJwt(claims, config_.jwt_secret);
}

// ==================== 验证 Access Token ====================
// 验证访问令牌并解析载荷，校验令牌类型、签发者，有效则返回结构化用户信息
Result<AccessTokenPayload> JwtService::VerifyAccessToken(const std::string& token) {
    // 参数校验
    if (token.empty()) {
        return Result<AccessTokenPayload>::Fail(ErrorCode::TokenMissing, "Token 不能为空");
    }
    
    // 验证 JWT
    auto verify_result = VerifyJwt(token, config_.jwt_secret);
    if (!verify_result.success) {
        return Result<AccessTokenPayload>::Fail(verify_result.error_code);
    }
    
    const auto& claims = verify_result.claims;
    
    // 校验签发者和令牌类型是否匹配
    auto iss_it = claims.find("iss");
    if (iss_it != claims.end() && iss_it->second != config_.jwt_issuer) {
        return Result<AccessTokenPayload>::Fail(ErrorCode::TokenInvalid, "令牌签发者不匹配");
    }
    
    auto sub_it = claims.find("sub");
    if (sub_it != claims.end() && sub_it->second != "access") {
        return Result<AccessTokenPayload>::Fail(ErrorCode::TokenInvalid, "令牌类型不匹配");
    }
    
    // 检查 uid 是否存在
    auto uid_it = claims.find("uid");
    if (uid_it == claims.end() || uid_it->second.empty()) {
        return Result<AccessTokenPayload>::Fail(ErrorCode::TokenInvalid, "令牌缺少用户标识");
    }
    
    // 解析为结构化载荷返回
    AccessTokenPayload payload;
    payload.user_id = uid_it->second;
    
    auto exp_it = claims.find("exp");
    if (exp_it != claims.end()) {
        try {
            int64_t exp = std::stoll(exp_it->second);
            payload.expires_at = std::chrono::system_clock::from_time_t(exp);
        } catch (...) {
            // 解析失败使用默认值
        }
    }
    
    return Result<AccessTokenPayload>::Ok(payload);
}

// ==================== 解析 Refresh Token ====================
// 验证刷新令牌并提取用户ID，校验令牌类型、签发者，有效则返回关联用户标识
Result<std::string> JwtService::ParseRefreshToken(const std::string& token) {
    // 参数校验
    if (token.empty()) {
        return Result<std::string>::Fail(ErrorCode::TokenMissing, "Token 不能为空");
    }
    
    // 验证 JWT
    auto verify_result = VerifyJwt(token, config_.jwt_secret);
    if (!verify_result.success) {
        return Result<std::string>::Fail(verify_result.error_code);
    }
    
    const auto& claims = verify_result.claims;
    
    // 校验签发者和令牌类型是否匹配
    auto iss_it = claims.find("iss");
    if (iss_it != claims.end() && iss_it->second != config_.jwt_issuer) {
        return Result<std::string>::Fail(ErrorCode::TokenInvalid, "令牌签发者不匹配");
    }
    
    auto sub_it = claims.find("sub");
    if (sub_it != claims.end() && sub_it->second != "refresh") {
        return Result<std::string>::Fail(ErrorCode::TokenInvalid, "令牌类型不匹配");
    }
    
    // 提取并返回用户ID
    auto uid_it = claims.find("uid");
    if (uid_it != claims.end() && !uid_it->second.empty()) {
        return Result<std::string>::Ok(uid_it->second);
    }
    
    return Result<std::string>::Fail(ErrorCode::TokenInvalid, "令牌缺少用户标识");
}

// ==================== Token 哈希 ====================
// 对令牌进行SHA256哈希，生成十六进制字符串，用于服务端令牌存储（避免明文）
std::string JwtService::HashToken(const std::string& token) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(token.c_str()), 
           token.length(), hash);
    // 转换为十六进制字符串
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

} // namespace user_service
