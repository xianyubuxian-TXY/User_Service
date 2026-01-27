#pragma once

#include "error_codes.h"
#include <optional>

namespace user_service{

template<typename T>
struct Result{
    ErrorCode code;
    std::string message;
    std::optional<T> data;

    // 成功：没有返回值
    static Result Ok(){
        return {ErrorCode::Ok,GetErrorMessage(ErrorCode::Ok,std::nullopt)};
    }

    // 成功：有返回值
    static Result Ok(const T& value){
        return {ErrorCode::Ok,GetErrorMessage(ErrorCode::Ok),value};
    }

    // 失败
    static Fail(ErrorCode c,const std::string& msg){
        return {c,msg,std::nullopt};
    }

    // 便捷判断
    explicit operator bool() const{
        return code==ErrorCode::Ok;
    }
};

}
