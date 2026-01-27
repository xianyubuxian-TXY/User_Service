#pragma once
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <string>
#include <spdlog/spdlog.h>
namespace spdlog{
    class Logger;
}

namespace user_service{

class Logger{
public:
    static void Init(const std::string& log_path,
                     const std::string& filename,
                     const std::string& level="info",
                     size_t max_size = 5*1024*1024,
                     int max_files=3,
                     bool console_output=true);

    static void Shutdown();

    static bool IsInitialized(){return initialized;}
private:
    static spdlog::level::level_enum ParseLevel(const std::string& level);
    static bool initialized;
};


#define LOG_TRACE(...) do { if (user_service::Logger::IsInitialized()) { SPDLOG_TRACE(__VA_ARGS__); } } while(0)
#define LOG_DEBUG(...) do { if (user_service::Logger::IsInitialized()) { SPDLOG_DEBUG(__VA_ARGS__); } } while(0)
#define LOG_INFO(...) do { if (user_service::Logger::IsInitialized()) { SPDLOG_INFO(__VA_ARGS__); } } while(0)
#define LOG_WARN(...) do { if (user_service::Logger::IsInitialized()) { SPDLOG_WARN(__VA_ARGS__); } } while(0)
#define LOG_ERROR(...) do { if (user_service::Logger::IsInitialized()) { SPDLOG_ERROR(__VA_ARGS__); } } while(0)


}