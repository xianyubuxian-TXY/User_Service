#pragma once

#include <memory>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include "../common/logger.h"
#include "config/config.h"

namespace user_service {

// ==================== 前置声明 ====================

class MySQLConnection;
class RedisConnection;

// ==================== 编译期类型推导 ====================

/**
 * @brief 连接类型到配置类型的映射（主模板）
 * 
 * 通过模板特化实现「连接类型 T → 配置类型 Config」的编译期推导，
 * 避免手动传递 Config 模板参数，消除「T 与 Config 不匹配」的风险。
 * 
 * @tparam T 连接类型
 */
template<typename>
struct ConnectionConfig;

/// @brief MySQLConnection 对应 MySQLConfig
template<>
struct ConnectionConfig<MySQLConnection> { 
    using type = user_service::MySQLConfig; 
};

/// @brief RedisConnection 对应 RedisConfig
template<>
struct ConnectionConfig<RedisConnection> { 
    using type = user_service::RedisConfig; 
};

/**
 * @brief 配置类型别名（简化使用）
 * @tparam T 连接类型
 * 
 * @example
 * @code
 *   Config_t<MySQLConnection>  // 等价于 MySQLConfig
 *   Config_t<RedisConnection>  // 等价于 RedisConfig
 * @endcode
 */
template<typename T>
using Config_t = typename ConnectionConfig<T>::type;

/**
 * @brief 用于 static_assert 的编译期 false 值
 * 
 * 直接写 static_assert(false, ...) 会在模板定义时就触发错误，
 * 而 always_false_v<T> 依赖模板参数 T，只有实例化时才会触发。
 */
template<typename>
inline constexpr bool always_false_v = false;


// ==================== 连接池模板类 ====================

/**
 * @brief 泛型数据库连接池
 * 
 * 提供线程安全的连接获取与归还机制，支持连接有效性检测和自动重建。
 * 使用 RAII 守卫类 ConnectionGuard 自动管理连接生命周期。
 * 
 * @tparam T 连接类型，需满足以下要求：
 *           - 提供 Valid() 方法判断连接有效性
 *           - 在 ConnectionConfig 中有对应的配置类型特化
 * 
 * @note 线程安全：所有公共方法均可在多线程环境下安全调用
 * 
 * @example
 * @code
 *   // 创建 MySQL 连接池
 *   auto pool = std::make_shared<TemplateConnectionPool<MySQLConnection>>(
 *       global_config,
 *       [](const MySQLConfig& cfg) {
 *           return std::make_unique<MySQLConnection>(cfg);
 *       }
 *   );
 *   
 *   // 使用连接（RAII 自动归还）
 *   {
 *       auto guard = pool->CreateConnection();
 *       auto result = guard->Query("SELECT * FROM users");
 *   } // guard 析构时自动归还连接
 * @endcode
 */
template<typename T>
class TemplateConnectionPool {
public:
    /// 连接智能指针类型
    using ConnectionPtr = std::unique_ptr<T>;
    
    /// 配置类型（编译期自动推导）
    using Config = Config_t<T>;
    
    /// 配置智能指针类型（const 保证配置不可变）
    using ConfigPtr = std::shared_ptr<const Config>;
    
    /// 连接创建回调函数类型
    using CreateFunc = std::function<ConnectionPtr(const Config&)>;

    /**
     * @brief 构造函数，初始化连接池
     * 
     * @param global_config 全局配置对象，包含各数据库的子配置
     * @param func 连接创建回调函数
     * 
     * @throws std::invalid_argument global_config 为空或 func 无效
     * @throws std::runtime_error 初始化连接失败
     * 
     * @note 构造时会立即创建 pool_size 个连接
     */
    TemplateConnectionPool(std::shared_ptr<user_service::Config> global_config, CreateFunc func)
        : createConnFunc_(std::move(func))
    {
        // 参数校验（快速失败原则）
        if (!global_config) {
            throw std::invalid_argument("TemplateConnectionPool: global_config is nullptr");
        }
        if (!createConnFunc_) {
            throw std::invalid_argument("TemplateConnectionPool: createConnFunc is empty");
        }

        // 从全局配置中提取对应类型的子配置
        ExtractSubConfig(global_config);
        
        // 预创建连接填充池
        InitPool();
    }

    /**
     * @brief 连接守卫类（RAII 模式）
     * 
     * 自动管理连接的获取与归还，确保连接不会泄漏。
     * 析构时自动将连接归还到池中。
     * 
     * @note 禁止拷贝，仅支持移动
     * @note 支持智能指针风格的 -> 和 * 操作符
     * 
     * @example
     * @code
     *   auto guard = pool.CreateConnection();
     *   guard->Query("SELECT 1");  // 通过 -> 访问连接
     *   (*guard).Execute("...");   // 通过 * 访问连接
     * @endcode
     */
    class ConnectionGuard {
    public:
        /**
         * @brief 构造函数，从池中获取连接
         * @param pool 连接池引用
         * @throws std::runtime_error 获取连接失败
         */
        ConnectionGuard(TemplateConnectionPool& pool)
            : pool_(pool)
        {
            conn_ = std::move(pool_.Acquire());
            if (!conn_) {
                throw std::runtime_error("Failed to acquire connection in ConnectionGuard");
            }
        }

        /// @name 禁用拷贝和拷贝赋值
        /// @{
        ConnectionGuard(const ConnectionGuard&) = delete;
        ConnectionGuard& operator=(const ConnectionGuard&) = delete;
        ConnectionGuard& operator=(ConnectionGuard&&) = delete;
        /// @}

        /**
         * @brief 移动构造函数
         * @param other 被移动的对象
         * @note 移动后 other.conn_ 置为 nullptr，防止析构时重复归还
         */
        ConnectionGuard(ConnectionGuard&& other) noexcept
            : pool_(other.pool_)
        {
            if (this == &other) return;
            conn_ = std::move(other.conn_);
            other.conn_ = nullptr;  // 防止双重释放
        }

        /**
         * @brief 获取原始连接指针
         * @return T* 连接指针，可能为 nullptr
         */
        T* get() const noexcept {
            return conn_.get();
        }

        /**
         * @brief 箭头运算符重载，支持 guard->method() 语法
         * @return T* 连接指针
         * @throws std::runtime_error 连接为空
         */
        T* operator->() {
            if (!conn_) throw std::runtime_error("Connection is null in ConnectionGuard");
            return conn_.get();
        }

        /**
         * @brief 解引用运算符重载，支持 (*guard).method() 语法
         * @return T& 连接引用
         * @throws std::runtime_error 连接为空
         */
        T& operator*() {
            if (!conn_) throw std::runtime_error("Connection is null in ConnectionGuard");
            return *conn_;
        }

        /**
         * @brief 析构函数，自动归还连接到池中
         */
        ~ConnectionGuard() {
            if (conn_) {
                pool_.Release(std::move(conn_));
                conn_ = nullptr;
            }
        }

    private:
        TemplateConnectionPool& pool_;  ///< 所属连接池的引用
        ConnectionPtr conn_;            ///< 持有的连接
    };

    /**
     * @brief 创建连接守卫（推荐使用方式）
     * 
     * @return ConnectionGuard RAII 守卫对象
     * @throws std::runtime_error 获取连接失败
     * 
     * @example
     * @code
     *   auto guard = pool.CreateConnection();
     *   // 使用 guard->... 访问连接
     * @endcode
     */
    ConnectionGuard CreateConnection() {
        return ConnectionGuard(*this);
    }

private:
    /**
     * @brief 从全局配置中提取对应类型的子配置
     * 
     * 使用 if constexpr 在编译期选择正确的配置字段，
     * 避免运行时类型判断的开销。
     * 
     * @param global_config 全局配置对象
     */
    void ExtractSubConfig(const std::shared_ptr<user_service::Config> global_config) {
        /*
         * if constexpr：编译期条件分支
         * - 只有匹配的分支会被编译
         * - 不匹配的分支即使语法错误也不会报错（因为不会实例化）
         */
        if constexpr (std::is_same_v<T, MySQLConnection>) {
            config_ = std::make_shared<const Config>(global_config->mysql);
        } else if constexpr (std::is_same_v<T, RedisConnection>) {
            config_ = std::make_shared<const Config>(global_config->redis);
        } else {
            // 编译期断言：不支持的连接类型
            // 注意：不能直接 static_assert(false, ...)，否则模板定义时就报错
            static_assert(always_false_v<T>, "Unsupported connection type, no matching sub-config");
        }
    }

    /**
     * @brief 初始化连接池
     * 
     * 根据配置的 pool_size 预创建指定数量的连接。
     * 
     * @throws std::runtime_error 配置为空或创建连接失败
     */
    void InitPool() {
        // 防御性检查（理论上不会为空，但多一层保护）
        if (!config_) {
            throw std::runtime_error("TemplateConnectionPool: config_ is nullptr");
        }
        
        int poolSize = config_->GetPoolSize();
        for (int i = 0; i < poolSize; ++i) {
            auto conn = std::move(createConnFunc_(*config_));
            if (!conn) {
                throw std::runtime_error("Failed to create connection during pool initialization");
            }
            pool_.push_back(std::move(conn));
        }
    }

    /**
     * @brief 从池中获取一个连接
     * 
     * 如果池为空，最多等待 5 秒。获取后会检测连接有效性，
     * 无效连接会尝试重建。
     * 
     * @return ConnectionPtr 连接指针，失败返回 nullptr
     * 
     * @note 线程安全
     * @note 超时时间硬编码为 5 秒，可根据需要改为配置项
     */
    ConnectionPtr Acquire() {
        std::unique_lock<std::mutex> lk(mutex_);
        
        // 带超时的等待：避免无限阻塞
        bool ret = cond_.wait_for(lk, std::chrono::seconds(5), [this]() {
            return !pool_.empty();
        });
        
        if (!ret) {
            LOG_ERROR("Acquire connection timeout after 5 seconds");
            return nullptr;
        }

        // 双重校验：防止虚假唤醒导致的空队列访问
        if (pool_.empty()) {
            LOG_ERROR("Connection pool is empty after wake up (spurious wakeup?)");
            return nullptr;
        }

        auto conn = std::move(pool_.front());
        pool_.pop_front();

        // 连接有效性检测：长时间空闲的连接可能已断开
        if (!conn->Valid()) {
            LOG_WARN("Acquired invalid connection, attempting rebuild");
            conn = createConnFunc_(*config_);
            if (!conn) {
                LOG_ERROR("Failed to rebuild invalid connection");
                return nullptr;
            }
        }

        return conn;
    }

    /**
     * @brief 将连接归还到池中
     * 
     * 归还前会检测连接有效性，无效连接会尝试重建。
     * 
     * @param conn 要归还的连接
     * 
     * @note 线程安全
     * @note 重建失败时不会抛异常，仅记录日志（析构函数中调用，不能抛异常）
     */
    void Release(ConnectionPtr conn) {
        std::unique_lock<std::mutex> lk(mutex_);
        
        // 检测并重建无效连接
        if (!conn || !conn->Valid()) {
            conn = std::move(createConnFunc_(*config_));
            // 重建失败：静默处理（析构函数中调用，不能抛异常）
            if (!conn) {
                LOG_ERROR("Failed to rebuild connection during release, connection lost");
                return;
            }
        }
        
        pool_.push_back(std::move(conn));
        cond_.notify_one();  // 唤醒一个等待的线程
    }

private:
    std::deque<ConnectionPtr> pool_;  ///< 连接队列（双端队列，支持高效头部弹出）
    ConfigPtr config_;                ///< 数据库配置（const 保证不可变）
    std::mutex mutex_;                ///< 互斥锁（保护 pool_ 的并发访问）
    std::condition_variable cond_;    ///< 条件变量（用于等待可用连接）
    CreateFunc createConnFunc_;       ///< 连接创建回调函数
};

} // namespace user_service
