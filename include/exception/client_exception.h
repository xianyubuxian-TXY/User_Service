// common/client_exception.h
#pragma once

#include <stdexcept>
#include <string>
#include "error_codes.h"

namespace user_service {

class ClientException : public std::runtime_error {
public:
    // 只有错误码
    explicit ClientException(ErrorCode code)
        : std::runtime_error(std::string(GetErrorMessage(code)))
        , code_(code) {}

    // 错误码 + 自定义消息
    ClientException(ErrorCode code, const std::string& detail)
        : std::runtime_error(std::string(GetErrorMessage(code)) + ": " + detail)
        , code_(code)
        , detail_(detail) {}

    ErrorCode code() const { return code_; }
    const std::string& detail() const { return detail_; }

private:
    ErrorCode code_;
    std::string detail_;
};

// 便捷抛出宏（可选）
#define THROW_CLIENT_ERROR(code) \
    throw ClientException(code)

#define THROW_CLIENT_ERROR_MSG(code, msg) \
    throw ClientException(code, msg)

}  // namespace user_service
