# ==============================================================================
# deploy/docker/Dockerfile
# 
# 多阶段构建：
#   Stage 1 (builder): 编译 C++ 项目
#   Stage 2 (runtime): 精简运行时镜像
#
# 构建命令：
#   docker build -f deploy/docker/Dockerfile -t user-service:latest .
#
# 运行命令：
#   docker run -d -p 50051:50051 -e SERVICE_NAME=user-service user-service:latest
# ==============================================================================

# ==============================================================================
# Stage 1: Builder - 编译环境
# ==============================================================================
FROM ubuntu:24.04 AS builder

# 设置非交互式安装，避免 apt 安装过程中的交互提示
ENV DEBIAN_FRONTEND=noninteractive

# 设置时区（可选，避免某些包安装时的时区提示）
ENV TZ=Asia/Shanghai

# 安装编译工具链和所有依赖库
# 分组说明：
#   - build-essential cmake pkg-config: 基础编译工具
#   - libgrpc++-dev libprotobuf-dev protobuf-compiler*: gRPC/Protobuf
#   - libmysqlclient-dev: MySQL 客户端库
#   - libhiredis-dev: Redis C 客户端
#   - uuid-dev: UUID 生成
#   - libzookeeper-mt-dev: ZooKeeper 多线程客户端
#   - libyaml-cpp-dev: YAML 配置解析
#   - libspdlog-dev libfmt-dev: 日志库
#   - libssl-dev: OpenSSL（JWT 签名）
#   - ca-certificates: HTTPS 证书
#   - git wget curl: 下载工具
RUN apt-get update && apt-get install -y --no-install-recommends \
    # 编译工具
    build-essential \
    cmake \
    pkg-config \
    ca-certificates \
    git \
    wget \
    curl \
    # gRPC 和 Protobuf
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler \
    protobuf-compiler-grpc \
    # 数据库
    libmysqlclient-dev \
    libhiredis-dev \
    # 其他依赖
    uuid-dev \
    libzookeeper-mt-dev \
    libyaml-cpp-dev \
    libspdlog-dev \
    libfmt-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# ==============================================================================
# 编译 redis-plus-plus（Ubuntu 24.04 仓库中没有预编译包）
# ==============================================================================
# 方式一：从本地 thirdparty 目录复制（推荐，避免网络问题）
COPY thirdparty/redis-plus-plus-1.3.10.tar.gz /tmp/

RUN cd /tmp && \
    tar xzf redis-plus-plus-1.3.10.tar.gz && \
    cd redis-plus-plus-1.3.10 && \
    mkdir build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DREDIS_PLUS_PLUS_CXX_STANDARD=17 \
        -DREDIS_PLUS_PLUS_BUILD_TEST=OFF \
        -DREDIS_PLUS_PLUS_BUILD_STATIC=OFF && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    rm -rf /tmp/redis-plus-plus*

# # 方式二：从 GitHub 下载（备选）
# RUN cd /tmp && \
#     wget https://github.com/sewenew/redis-plus-plus/archive/refs/tags/1.3.10.tar.gz && \
#     tar xzf 1.3.10.tar.gz && \
#     cd redis-plus-plus-1.3.10 && \
#     mkdir build && cd build && \
#     cmake .. -DCMAKE_BUILD_TYPE=Release -DREDIS_PLUS_PLUS_CXX_STANDARD=17 && \
#     make -j$(nproc) && make install && ldconfig && \
#     rm -rf /tmp/redis-plus-plus* /tmp/1.3.10.tar.gz

# ==============================================================================
# 编译项目
# ==============================================================================
WORKDIR /build

# 先复制 CMake 配置文件（利用 Docker 缓存层）
COPY CMakeLists.txt ./
COPY api/CMakeLists.txt api/
COPY api/proto/ api/proto/
COPY src/CMakeLists.txt src/

# 复制源代码
COPY include/ include/
COPY src/ src/
COPY thirdparty/json/ thirdparty/json/
COPY configs/ configs/

# 执行构建
RUN rm -rf build && mkdir build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS=OFF \
        -DBUILD_SHARED_LIBS=OFF && \
    make -j$(nproc) VERBOSE=1

# 验证构建产物
RUN ls -la /build/build/bin/ && \
    file /build/build/bin/user_server && \
    file /build/build/bin/auth_server

# ==============================================================================
# Stage 2: Runtime - 运行时镜像
# ==============================================================================
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# 安装运行时依赖（只安装动态库，不安装开发文件）
# 注意：Ubuntu 24.04 的库版本号可能与其他版本不同
RUN apt-get update && apt-get install -y --no-install-recommends \
    # gRPC 运行时
    libgrpc++1.51t64 \
    libprotobuf32t64 \
    # 数据库客户端
    libmysqlclient21 \
    libhiredis1.1.0 \
    # 其他运行时库
    libuuid1 \
    libzookeeper-mt2 \
    libyaml-cpp0.8 \
    libspdlog1.12 \
    libfmt9 \
    libssl3t64 \
    # 工具（调试用，可在生产环境移除）
    curl \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

# 复制 redis-plus-plus 动态库
COPY --from=builder /usr/local/lib/libredis++.so* /usr/local/lib/
RUN ldconfig

# 安装 gRPC 健康检查工具（用于容器健康检查）
RUN curl -fsSL \
    https://github.com/grpc-ecosystem/grpc-health-probe/releases/download/v0.4.24/grpc_health_probe-linux-amd64 \
    -o /usr/local/bin/grpc_health_probe && \
    chmod +x /usr/local/bin/grpc_health_probe || \
    echo "Warning: Failed to download grpc_health_probe"

# 创建非 root 用户（安全最佳实践）
RUN useradd -m -s /bin/bash -u 1000 appuser

# 设置工作目录
WORKDIR /app

# 复制可执行文件
COPY --from=builder /build/build/bin/user_server ./
COPY --from=builder /build/build/bin/auth_server ./

# 复制配置文件
COPY configs/ /app/configs/

# 复制启动脚本
COPY deploy/docker/entrypoint.sh ./
RUN chmod +x entrypoint.sh

# 创建日志目录并设置权限
RUN mkdir -p /app/logs && \
    chown -R appuser:appuser /app

# 切换到非 root 用户
USER appuser

# 暴露端口
#   50051: gRPC 服务端口
#   9090:  Prometheus 指标端口（可选）
EXPOSE 50051 9090

# 环境变量（可被 docker-compose 或 K8s 覆盖）
ENV CONFIG_PATH=/app/configs/config.docker.yaml
ENV SERVICE_NAME=user-service
ENV LOG_LEVEL=info

# 健康检查
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD grpc_health_probe -addr=:50051 || exit 1

# 入口点
ENTRYPOINT ["./entrypoint.sh"]
