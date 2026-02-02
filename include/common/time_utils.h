#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <google/protobuf/timestamp.pb.h>

namespace user_service {

// ==================== chrono <-> string ====================

/**
 * @brief chrono::time_point 转字符串
 * @param tp 时间点
 * @return 格式 "YYYY-MM-DD HH:MM:SS"（UTC）
 */
inline std::string TimePointToString(const std::chrono::system_clock::time_point& tp) {
    std::time_t time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
    gmtime_r(&time, &tm);  // 线程安全（POSIX）
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

/**
 * @brief 字符串转 chrono::time_point
 * @param time_str 格式 "YYYY-MM-DD HH:MM:SS"（UTC）
 * @return 时间点，解析失败返回 epoch
 */
inline std::chrono::system_clock::time_point StringToTimePoint(const std::string& time_str) {
    if (time_str.empty()) {
        return std::chrono::system_clock::time_point{};
    }
    
    std::tm tm = {};
    std::istringstream ss(time_str);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    if (ss.fail()) {
        return std::chrono::system_clock::time_point{};
    }
    
    std::time_t time = timegm(&tm);  // UTC
    return std::chrono::system_clock::from_time_t(time);
}

// ==================== Unix 时间戳 <-> string ====================

/**
 * @brief Unix 时间戳（秒）转字符串
 */
inline std::string UnixToString(int64_t unix_seconds) {
    if (unix_seconds <= 0) {
        return "1970-01-01 00:00:00";
    }
    std::time_t time = static_cast<std::time_t>(unix_seconds);
    std::tm tm;
    gmtime_r(&time, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

/**
 * @brief 字符串转 Unix 时间戳（秒）
 */
inline int64_t StringToUnix(const std::string& time_str) {
    if (time_str.empty()) return 0;
    
    std::tm tm = {};
    std::istringstream ss(time_str);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    if (ss.fail()) return 0;
    return static_cast<int64_t>(timegm(&tm));
}

// ==================== Protobuf Timestamp ====================

/**
 * @brief Protobuf Timestamp 转字符串
 */
inline std::string TimestampToString(const google::protobuf::Timestamp& ts) {
    return UnixToString(ts.seconds());
}

/**
 * @brief 字符串转 Protobuf Timestamp
 */
inline void StringToTimestamp(const std::string& time_str, google::protobuf::Timestamp* ts) {
    if (time_str.empty() || !ts) return;
    ts->set_seconds(StringToUnix(time_str));
    ts->set_nanos(0);
}

/**
 * @brief chrono::time_point 转 Protobuf Timestamp
 */
inline void TimePointToTimestamp(const std::chrono::system_clock::time_point& tp,
                                  google::protobuf::Timestamp* ts) {
    if (!ts) return;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        tp.time_since_epoch()
    ).count();
    ts->set_seconds(seconds);
    ts->set_nanos(0);
}

// ==================== 便捷函数 ====================

/**
 * @brief 获取当前 Unix 时间戳（秒）
 */
inline int64_t NowSeconds() {
    return std::time(nullptr);
}

/**
 * @brief 获取当前时间字符串
 */
inline std::string NowString() {
    return UnixToString(NowSeconds());
}

/**
 * @brief 获取 N 秒后的时间字符串
 */
inline std::string FutureString(int64_t seconds_from_now) {
    return UnixToString(NowSeconds() + seconds_from_now);
}

/**
 * @brief 获取 N 天后的时间字符串
 */
inline std::string FutureDaysString(int days) {
    return FutureString(days * 24 * 3600);
}

/**
 * @brief 判断时间字符串是否已过期
 */
inline bool IsExpired(const std::string& expires_at) {
    return StringToUnix(expires_at) < NowSeconds();
}

}  // namespace user_service
