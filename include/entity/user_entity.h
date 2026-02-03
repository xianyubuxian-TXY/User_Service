#pragma once
#include <string>

namespace user_service{

enum class UserRole{
    User = 0,           // 普通用户
    Admin = 1,          // 管理员
    SuperAdmin = 2      // 超级管理员
};


struct UserEntity {
    int64_t id = 0;
    std::string uuid;
    std::string mobile;
    std::string display_name;
    std::string password_hash;
    UserRole role = UserRole::User;
    bool disabled = false;
    std::string created_at;
    std::string updated_at;
};



inline std::string UserRoleToString(UserRole role){
    switch(role){
        case UserRole::User:        return "0";
        case UserRole::Admin:       return "1";
        case UserRole::SuperAdmin:  return "2";
        default:                    return "0";    
    }
}

inline UserRole StringToUserRole(std::string role){
    if(role=="0") return UserRole::User;
    if(role=="1") return UserRole::Admin;
    if(role=="2") return UserRole::SuperAdmin;
    return UserRole::User;
}



inline UserRole IntToUserRole(int role){
    return static_cast<UserRole>(role);
}

inline int UserRoleToInt(UserRole role){
    return static_cast<int>(role);
}

// 判断是否有管理员权限
inline bool IsAdmin(UserRole role) {
    return role == UserRole::Admin || role == UserRole::SuperAdmin;
}

inline bool IsSuperAdmin(UserRole role) {
    return role == UserRole::SuperAdmin;
}


    
}
