#pragma once
#include <string>

namespace user_service{
    struct UserEntity {
        int64_t id = 0;
        std::string uuid;
        std::string mobile;
        std::string display_name;
        std::string password_hash;
        bool disabled = false;
        std::string created_at;
        std::string updated_at;
    };
    
}
