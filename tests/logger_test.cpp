#include <gtest/gtest.h>
#include "common/logger.h" 
#include <filesystem>
#include <fstream>
#include <thread>
#include <regex>

namespace fs = std::filesystem;

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建临时目录用于测试
        test_log_dir = fs::temp_directory_path() / "logger_test";
        if (fs::exists(test_log_dir)) {
            fs::remove_all(test_log_dir);
        }
        fs::create_directory(test_log_dir);
    }

    void TearDown() override {
        // 清理
        user::Logger::Shutdown();
        if (fs::exists(test_log_dir)) {
            fs::remove_all(test_log_dir);
        }
    }

    // 检查日志文件是否包含指定内容
    bool LogFileContains(const std::string& filename, const std::string& content) {
        std::string full_path = (test_log_dir / filename).string();
        // 等待日志刷新
        std::this_thread::sleep_for(std::chrono::seconds(4));
        
        if (!fs::exists(full_path)) {
            return false;
        }
        
        std::ifstream file(full_path);
        std::string line;
        while (std::getline(file, line)) {
            if (line.find(content) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    // 检查日志是否符合格式
    bool LogFileHasValidFormat(const std::string& filename) {
        std::string full_path = (test_log_dir / filename).string();
        std::this_thread::sleep_for(std::chrono::seconds(4));
        
        if (!fs::exists(full_path)) {
            return false;
        }
        
        std::regex log_pattern(R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+\] \[(\w+)\] \[\d+\] \[.*:\d+\] .*)");
        
        std::ifstream file(full_path);
        std::string line;
        if (std::getline(file, line)) {
            return std::regex_match(line, log_pattern);
        }
        return false;
    }

    fs::path test_log_dir;
};

// 测试日志初始化
TEST_F(LoggerTest, InitializeLogger) {
    std::string log_file = "test_log.txt";
    user::Logger::Init(test_log_dir.string(), log_file);
    
    LOG_INFO("Test log message");
    
    EXPECT_TRUE(fs::exists(test_log_dir / log_file));
    EXPECT_TRUE(LogFileContains(log_file, "Logger initialized"));
}

// 测试不同日志级别
TEST_F(LoggerTest, LogLevels) {
    std::string log_file = "levels_test.txt";
    user::Logger::Init(test_log_dir.string(), log_file, "trace");
    
    LOG_DEBUG("This is a debug message");
    LOG_INFO("This is an info message");
    LOG_WARN("This is a warning message");
    LOG_ERROR("This is an error message");
    
    EXPECT_TRUE(LogFileContains(log_file, "debug message"));
    EXPECT_TRUE(LogFileContains(log_file, "info message"));
    EXPECT_TRUE(LogFileContains(log_file, "warning message"));
    EXPECT_TRUE(LogFileContains(log_file, "error message"));
}

// 测试日志级别过滤
TEST_F(LoggerTest, LogLevelFiltering) {
    std::string log_file = "filter_test.txt";
    user::Logger::Init(test_log_dir.string(), log_file, "error");
    
    LOG_TRACE("This is a trace message");
    LOG_DEBUG("This is a debug message");
    LOG_INFO("This is an info message");
    LOG_WARN("This is a warning message");
    LOG_ERROR("This is an error message");
    
    EXPECT_FALSE(LogFileContains(log_file, "trace message"));
    EXPECT_FALSE(LogFileContains(log_file, "debug message"));
    EXPECT_FALSE(LogFileContains(log_file, "info message"));
    EXPECT_FALSE(LogFileContains(log_file, "warning message"));
    EXPECT_TRUE(LogFileContains(log_file, "error message"));
}

// 测试日志格式
TEST_F(LoggerTest, LogFormat) {
    std::string log_file = "format_test.txt";
    user::Logger::Init(test_log_dir.string(), log_file, "info");
    
    LOG_INFO("Testing log format");
    
    EXPECT_TRUE(LogFileHasValidFormat(log_file));
}

// 测试日志文件轮转
TEST_F(LoggerTest, LogRotation) {
    std::string log_file = "rotation_test.txt";
    // 使用更小的文件大小来触发轮转（256字节）
    user::Logger::Init(test_log_dir.string(), log_file, "info", 256, 3);
    
    // 写入足够的日志来触发轮转
    for (int i = 0; i < 50; i++) {
        LOG_INFO("Log rotation test line {}: This is a long message to fill space", i);
        
        // 每5次刷新一次
        if (i % 5 == 0) {
            if (auto logger = spdlog::get("user_logger")) {
                logger->flush();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    // 确保所有日志都写入磁盘
    if (auto logger = spdlog::get("user_logger")) {
        logger->flush();
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 检查原始日志文件
    EXPECT_TRUE(fs::exists(test_log_dir / log_file));
    
    // 修正：spdlog 的轮转文件格式是 filename.1.ext，而不是 filename.ext.1
    // 例如：rotation_test.1.txt 而不是 rotation_test.txt.1
    std::string base_name = log_file.substr(0, log_file.find_last_of('.'));
    std::string extension = log_file.substr(log_file.find_last_of('.'));
    
    bool has_rotated = fs::exists(test_log_dir / (base_name + ".1" + extension)) ||
                       fs::exists(test_log_dir / (base_name + ".2" + extension)) ||
                       fs::exists(test_log_dir / (base_name + ".3" + extension));
    
    // 如果没有轮转，输出调试信息
    if (!has_rotated) {
        std::cout << "Checking for rotated files in: " << test_log_dir << std::endl;
        for (const auto& entry : fs::directory_iterator(test_log_dir)) {
            std::cout << "Found file: " << entry.path().filename() << 
                         " (size: " << fs::file_size(entry.path()) << " bytes)" << std::endl;
        }
    }
    
    EXPECT_TRUE(has_rotated);
    
    // 额外验证：至少应该有一个轮转文件
    int rotated_count = 0;
    for (int i = 1; i <= 3; i++) {
        if (fs::exists(test_log_dir / (base_name + "." + std::to_string(i) + extension))) {
            rotated_count++;
        }
    }
    EXPECT_GT(rotated_count, 0) << "Expected at least one rotated log file";
}

// 测试 Shutdown 功能
TEST_F(LoggerTest, ShutdownTest) {
    std::string log_file = "shutdown_test.txt";
    user::Logger::Init(test_log_dir.string(), log_file);
    
    LOG_INFO("Before shutdown");
    
    // 刷新日志
    if (auto logger = spdlog::get("user_logger")) {
        logger->flush();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 验证第一条日志
    EXPECT_TRUE(LogFileContains(log_file, "Before shutdown"));
    
    // 关闭 logger
    user::Logger::Shutdown();
    
    // 这条日志不应该被记录（因为 logger 已关闭）
    LOG_INFO("After shutdown");
    
    // 再次验证，确保没有新日志
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_FALSE(LogFileContains(log_file, "After shutdown"));
}

// 测试解析日志级别
TEST_F(LoggerTest, ParseLevelTest) {
    std::string log_file = "parse_level_test.txt";
    
    // 测试不同级别
    std::vector<std::string> levels = {"debug", "info", "warn", "error"};
    
    for (const auto& level : levels) {
        // 确保之前的 logger 已关闭
        user::Logger::Shutdown();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // 如果文件存在，先删除
        fs::path log_path = test_log_dir / log_file;
        if (fs::exists(log_path)) {
            fs::remove(log_path);
        }
        
        // 初始化新的 logger
        user::Logger::Init(test_log_dir.string(), log_file, level, 1024 * 1024, 1, false);
        
        // 写入不同级别的日志
        LOG_DEBUG("DEBUG level");
        LOG_INFO("INFO level");
        LOG_WARN("WARN level");
        LOG_ERROR("ERROR level");
        
        // 刷新日志
        if (auto logger = spdlog::get("user_logger")) {
            logger->flush();
        }
        
        // 等待日志写入
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 验证级别过滤是否正确
        if (level == "debug") {
            EXPECT_TRUE(LogFileContains(log_file, "DEBUG level")) 
                << "Failed for level: " << level;
            EXPECT_TRUE(LogFileContains(log_file, "INFO level"));
        } else if (level == "info") {
            EXPECT_FALSE(LogFileContains(log_file, "DEBUG level")) 
                << "Failed for level: " << level;
            EXPECT_TRUE(LogFileContains(log_file, "INFO level"));
        } else if (level == "warn") {
            EXPECT_FALSE(LogFileContains(log_file, "INFO level")) 
                << "Failed for level: " << level;
            EXPECT_TRUE(LogFileContains(log_file, "WARN level"));
        } else if (level == "error") {
            EXPECT_FALSE(LogFileContains(log_file, "WARN level")) 
                << "Failed for level: " << level;
            EXPECT_TRUE(LogFileContains(log_file, "ERROR level"));
        }
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
