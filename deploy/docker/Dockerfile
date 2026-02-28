# ==============================================================================
# deploy/docker/Dockerfile
# 
# 多阶段构建：
#   Stage 1 (builder): 编译 C++ 项目
#   Stage 2 (runtime): 精简运行时镜像
# ==============================================================================

# ==============================================================================
# Stage 1: Builder - 编译环境
# ==============================================================================
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# 安装编译工具链和所有依赖库
RUN apt-get update && apt-get install -y --no-install-recommends \
    # 编译工具
    build-essential \
    cmake \
    pkg-config \
    ca-certificates \
    git \
    wget \
    # gRPC 和 Protobuf（包含 cmake 配置文件）
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
# 编译 redis-plus-plus
# ==============================================================================
COPY thirdparty/redis-plus-plus-1.3.10.tar.gz /tmp/

RUN cd /tmp && \
    tar xzf redis-plus-plus-1.3.10.tar.gz && \
    cd redis-plus-plus-1.3.10 && \
    mkdir build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DREDIS_PLUS_PLUS_CXX_STANDARD=17 \
        -DREDIS_PLUS_PLUS_BUILD_TEST=OFF && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    rm -rf /tmp/redis-plus-plus*

# ==============================================================================
# 编译项目
# ==============================================================================
WORKDIR /build

# 复制所有源代码
COPY CMakeLists.txt ./
COPY api/ api/
COPY include/ include/
COPY src/ src/
COPY thirdparty/json/ thirdparty/json/
COPY configs/ configs/

# 执行构建
RUN mkdir build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS=OFF \
        -DBUILD_SHARED_LIBS=OFF && \
    make -j$(nproc)

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

# 安装运行时依赖
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
    # 工具
    curl \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

# 复制 redis-plus-plus 动态库
COPY --from=builder /usr/local/lib/libredis++.so* /usr/local/lib/
RUN ldconfig

# 安装 gRPC 健康检查工具
RUN curl -fsSL \
    https://github.com/grpc-ecosystem/grpc-health-probe/releases/download/v0.4.24/grpc_health_probe-linux-amd64 \
    -o /usr/local/bin/grpc_health_probe && \
    chmod +x /usr/local/bin/grpc_health_probe || true

# 创建非 root 用户
RUN useradd -m -s /bin/bash -u 1000 appuser

WORKDIR /app

# 复制可执行文件
COPY --from=builder /build/build/bin/user_server ./
COPY --from=builder /build/build/bin/auth_server ./

# 复制配置和启动脚本
COPY configs/ /app/configs/
COPY deploy/docker/entrypoint.sh ./
RUN chmod +x entrypoint.sh

# 设置权限
RUN mkdir -p /app/logs && chown -R appuser:appuser /app

USER appuser

EXPOSE 50051 9090

ENV CONFIG_PATH=/app/configs/config.docker.yaml
ENV SERVICE_NAME=user-service

HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD grpc_health_probe -addr=:50051 || exit 1

ENTRYPOINT ["./entrypoint.sh"]
