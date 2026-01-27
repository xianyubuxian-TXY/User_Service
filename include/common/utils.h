#pragma once
#include <string>
#include "build/api/proto/user_service.grpc.pb.h"
namespace user_service{

inline std::string TimestampToString(const google::protobuf::Timestamp& ts) {
    if (ts.seconds() == 0) {
        return "1970-01-01 00:00:00";  // 或返回当前时间
    }
    std::time_t seconds = ts.seconds();
    std::tm tm;
    gmtime_r(&seconds, &tm);  // 线程安全版本
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}



}