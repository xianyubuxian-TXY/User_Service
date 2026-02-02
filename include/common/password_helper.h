#pragma once

#include <string>
#include <random>
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace user_service {

class PasswordHelper {
public:
    /**
     * @brief 生成密码哈希（SHA256 + 盐）
     * @return 格式: $sha256$<salt>$<hash>
     * @note 生产环境建议使用 bcrypt/argon2
     */
    static std::string Hash(const std::string& password) {
        // 1. 生成 16 字节随机盐
        unsigned char salt_bytes[16];
        RAND_bytes(salt_bytes, sizeof(salt_bytes));
        std::string salt = BytesToHex(salt_bytes, sizeof(salt_bytes));
        
        // 2. 计算 hash = SHA256(salt + password)
        std::string salted = salt + password;
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(salted.c_str()), 
               salted.size(), hash);
        
        std::string hash_hex = BytesToHex(hash, SHA256_DIGEST_LENGTH);
        
        // 3. 返回格式: $sha256$salt$hash
        return "$sha256$" + salt + "$" + hash_hex;
    }
    
    /**
     * @brief 验证密码
     */
    static bool Verify(const std::string& password, const std::string& stored_hash) {
        // 解析存储的哈希: $sha256$<salt>$<hash>
        if (stored_hash.substr(0, 8) != "$sha256$") {
            return false;
        }
        
        size_t salt_start = 8;
        size_t salt_end = stored_hash.find('$', salt_start);
        if (salt_end == std::string::npos) {
            return false;
        }
        
        std::string salt = stored_hash.substr(salt_start, salt_end - salt_start);
        std::string expected_hash = stored_hash.substr(salt_end + 1);
        
        // 计算 hash = SHA256(salt + password)
        std::string salted = salt + password;
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(salted.c_str()), 
               salted.size(), hash);
        
        std::string actual_hash = BytesToHex(hash, SHA256_DIGEST_LENGTH);
        
        // 常量时间比较（防止时序攻击）
        return ConstantTimeCompare(expected_hash, actual_hash);
    }

private:
    static std::string BytesToHex(const unsigned char* data, size_t len) {
        std::ostringstream oss;
        for (size_t i = 0; i < len; ++i) {
            oss << std::hex << std::setw(2) << std::setfill('0') 
                << static_cast<int>(data[i]);
        }
        return oss.str();
    }
    
    static bool ConstantTimeCompare(const std::string& a, const std::string& b) {
        if (a.length() != b.length()) return false;
        volatile int result = 0;
        for (size_t i = 0; i < a.length(); ++i) {
            result |= a[i] ^ b[i];
        }
        return result == 0;
    }
};

}  // namespace user_service
