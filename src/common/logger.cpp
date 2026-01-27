#include "common/logger.h"


#include <spdlog/sinks/stdout_color_sinks.h>    //带颜色的终端输出，多线程安全
#include <spdlog/sinks/rotating_file_sink.h>    //按文件带下轮转，保留N个备份
#include <spdlog/async.h>
#include <filesystem>
#include <memory>
#include <vector>
#include <iostream>

namespace user_service{

bool Logger::initialized=false;    
void Logger::Init(const std::string& log_path,
            const std::string& filename,
            const std::string& level,
            size_t max_size,
            int max_files,
            bool console_output)
{
    if(initialized) return;

    std::vector<spdlog::sink_ptr> sinks;    

    //控制台输出
    if(console_output){
        auto console_sink=std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::debug);
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
        sinks.push_back(console_sink);
    }

    //文件输出（轮转）
    std::string full_path=log_path+"/"+filename;
    auto rfile_sink=std::make_shared<spdlog::sinks::rotating_file_sink_mt>(full_path,max_size,max_files);
    rfile_sink->set_level(spdlog::level::trace);
    rfile_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
    sinks.push_back(rfile_sink);

    //创建异步日志系统
    spdlog::init_thread_pool(8192,1);   //日志队列大小 8K,后台线程 1
    auto logger=std::make_shared<spdlog::async_logger>(
        "user_service",sinks.begin(),sinks.end(),spdlog::thread_pool(),spdlog::async_overflow_policy::block
    );

    //设置日志级别
    logger->set_level(ParseLevel(level));

    //设置为默认日志器
    spdlog::set_default_logger(logger);

    //每3秒刷新一次
    spdlog::flush_every(std::chrono::seconds(3));

    initialized=true;
    SPDLOG_INFO("Logger initialized, level={}, path={}", level, full_path);

}

void Logger::Shutdown() {
    if (initialized) {
        spdlog::shutdown();
        initialized = false;
    }
}

spdlog::level::level_enum Logger::ParseLevel(const std::string& level){
    if (level == "trace") return spdlog::level::trace;
    if (level == "debug") return spdlog::level::debug;
    if (level == "info") return spdlog::level::info;
    if (level == "warn") return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    if (level == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}
}