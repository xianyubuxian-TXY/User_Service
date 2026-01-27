# deploy/docker/Dockerfile

# ============================================
# 构建阶段
# ============================================
FROM ubuntu:22.04 AS builder

# 避免交互式安装提示
ENV DEBIAN_FRONTEND=noninteractive

# 安装构建依赖
RUN apt-get update && apt-get install -y \
    # 基础构建工具
    build-essential \
    cmake \
    git \
    pkg-config \
    # gRPC 和 Protobuf
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler \
    protobuf-compiler-grpc \
    # MySQL 客户端库
    libmysqlclient-dev \
    # Redis 客户端库 (hiredis)
    libhiredis-dev \
    # UUID 生成
    uuid-dev \
    # Kafka 客户端 (librdkafka)
    librdkafka-dev \
    # ─────────────────────────────────────────────────────────
    # 新增依赖
    # ─────────────────────────────────────────────────────────
    # Google Test (单元测试框架)
    libgtest-dev \
    libgmock-dev \
    # Zookeeper 客户端库 (多线程版本)
    libzookeeper-mt-dev \
    && rm -rf /var/lib/apt/lists/*


# 复制源码
WORKDIR /build
COPY . .

# CMake 构建
RUN mkdir -p build && cd build \
    && cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
    && make -j$(nproc)

# ============================================
# 运行时镜像
# ============================================
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# 运行阶段依赖（只装运行库 .so，不装 -dev）
RUN apt-get update && apt-get install -y \
    # gRPC 运行时
    libgrpc++1 \
    # Protobuf 运行时（Ubuntu 22.04 是 libprotobuf23）
    libprotobuf23 \
    # MySQL 客户端运行时
    libmysqlclient21 \
    # Redis 客户端运行时（Ubuntu 22.04 是 0.14）
    libhiredis0.14 \
    # UUID 运行时
    libuuid1 \
    # Kafka 运行时
    librdkafka1 \
    # ─────────────────────────────────────────────────────────
    # 新增：Zookeeper 运行时
    # ─────────────────────────────────────────────────────────
    libzookeeper-mt2 \
    # 调试工具（可选，生产环境可删除）
    curl \
    && rm -rf /var/lib/apt/lists/*

# 创建非 root 用户（安全最佳实践）
RUN useradd -m -s /bin/bash appuser

WORKDIR /app

# 从构建阶段复制二进制文件
COPY --from=builder /build/build/user_server .

# 复制配置文件
COPY configs/ /app/configs/

# 创建日志目录
RUN mkdir -p /app/logs && chown -R appuser:appuser /app

# 切换到非 root 用户
USER appuser

# 暴露端口
EXPOSE 50051

# 健康检查
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

# 启动命令
CMD ["./user_server", "--config", "/app/configs/config.docker.yaml"]
