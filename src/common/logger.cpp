#include "common/logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>    //带颜色的终端输出，多线程安全
#include <spdlog/sinks/rotating_file_sink.h>    //按文件大小轮转，保留N个备份
#include <spdlog/async.h>
#include <filesystem>
#include <memory>
#include <vector>
#include <iostream>

namespace user_service {

bool Logger::initialized = false;

void Logger::Init(const std::string& log_path,
                  const std::string& filename,
                  const std::string& level,
                  size_t max_size,
                  int max_files,
                  bool console_output)
{
    if (initialized) return;

    try {
        std::vector<spdlog::sink_ptr> sinks;

        // ==================== 控制台输出 ====================
        if (console_output) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(spdlog::level::debug);
            console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
            sinks.push_back(console_sink);
        }

        // ==================== 创建日志目录 ====================
        std::filesystem::path dir_path(log_path);
        if (!std::filesystem::exists(dir_path)) {
            if (!std::filesystem::create_directories(dir_path)) {
                std::cerr << "Logger Error: 无法创建日志目录: " << log_path << std::endl;
                // 降级：仅使用控制台输出
                if (!sinks.empty()) {
                    auto logger = std::make_shared<spdlog::logger>("user_service", sinks.begin(), sinks.end());
                    logger->set_level(ParseLevel(level));
                    spdlog::set_default_logger(logger);
                    initialized = true;
                    SPDLOG_WARN("日志目录创建失败，仅输出到控制台");
                }
                return;
            }
            std::cout << ">>> 日志目录已创建: " << std::filesystem::absolute(dir_path) << std::endl;
        }

        // ==================== 文件输出（轮转） ====================
        std::string full_path = (dir_path / filename).string();
        auto rfile_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            full_path, max_size, max_files
        );
        rfile_sink->set_level(spdlog::level::trace);
        rfile_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
        sinks.push_back(rfile_sink);

        // ==================== 创建异步日志系统 ====================
        spdlog::init_thread_pool(8192, 1);  // 日志队列大小 8K, 后台线程 1
        auto logger = std::make_shared<spdlog::async_logger>(
            "user_service",
            sinks.begin(),
            sinks.end(),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block
        );

        // 设置日志级别
        logger->set_level(ParseLevel(level));

        // 设置为默认日志器
        spdlog::set_default_logger(logger);

        // 每3秒自动刷新一次
        spdlog::flush_every(std::chrono::seconds(3));

        initialized = true;

        // ==================== 立即刷新初始化日志 ====================
        SPDLOG_INFO("Logger initialized, level={}, path={}", level, full_path);
        logger->flush();  // 立即写入，确保初始化日志不丢失

        std::cout << ">>> 日志文件: " << std::filesystem::absolute(full_path) << std::endl;

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger Error: spdlog 初始化失败: " << ex.what() << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Logger Error: " << ex.what() << std::endl;
    }
}

void Logger::Shutdown() {
    if (initialized) {
        SPDLOG_INFO("Logger shutting down...");

        // 确保所有日志写入
        if (auto logger = spdlog::default_logger()) {
            logger->flush();
        }

        spdlog::shutdown();
        initialized = false;
    }
}

spdlog::level::level_enum Logger::ParseLevel(const std::string& level) {
    if (level == "trace") return spdlog::level::trace;
    if (level == "debug") return spdlog::level::debug;
    if (level == "info") return spdlog::level::info;
    if (level == "warn") return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    if (level == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

}  // namespace user_service
