# =============================================================================
# KAJEN_F / ZigbeeMatterLightSolution_SixG301M113W 固件编译 + SLC CLI 环境
#
# 工程生成文件硬编码了 /home/hans/.silabs/... 路径，因此镜像内固定使用
# 用户 hans 与 HOME=/home/hans，以匹配本机 Simplicity Studio 导出结果。
#
# 完整镜像含: slt / slc-cli / java21 / zap / cmake / ninja / gcc-arm / commander /
#             simplicity-sdk / matter_extension
#
# 构建镜像（在仓库根目录执行）:
#   ./docker/build-image.sh
#
# 仅编译（挂载源码，产物写回宿主机）:
#   docker run --rm \
#     -v "$PWD":/workspace \
#     -w /workspace \
#     kajen-sixg301:sdk-2025.12.1
#
# 若宿主机已有完整 ~/.silabs，可跳过镜像内 SDK 安装以加速构建:
#   INSTALL_SLT_PACKAGES=0 IMAGE_TAG=kajen-sixg301:slim ./docker/build-image.sh
#   docker run --rm \
#     -v "$PWD":/workspace \
#     -v "$HOME/.silabs":/home/hans/.silabs:ro \
#     -w /workspace \
#     kajen-sixg301:slim
# =============================================================================

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG BUILD_USER=hans
ARG BUILD_UID=1000
ARG BUILD_GID=1000

# 与本机 Studio 工程对齐的工具链 / SDK 版本
ARG SLT_CLI_URL=https://www.silabs.com/documents/public/software/slt-cli-1.1.0-linux-x64.zip
ARG INSTALL_SLT_PACKAGES=1
ARG CMAKE_VERSION=3.30.2
ARG NINJA_VERSION=1.12.1
ARG COMMANDER_VERSION=1.23.1
ARG GCC_ARM_VERSION=12.2.rel1
ARG SIMPLICITY_SDK_VERSION=2025.12.1
ARG MATTER_EXTENSION_VERSION=2.8.0
ARG SLC_CLI_VERSION=6.0.17
ARG JAVA21_VERSION=21.0.5
ARG ZAP_VERSION=2026.02.26
# CI firmware build only needs compile toolchain + SDK (no slc generate).
ARG INSTALL_SLC_TOOLS=0

ENV LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    HOME=/home/${BUILD_USER} \
    PATH="/home/${BUILD_USER}/.silabs/slt/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

RUN set -eux \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        aria2 \
        bzip2 \
        ca-certificates \
        curl \
        git \
        jq \
        libarchive-tools \
        libasound2t64 \
        libatk-bridge2.0-0t64 \
        libatk1.0-0t64 \
        libdrm2 \
        libgbm1 \
        libgl1 \
        libglib2.0-0t64 \
        libgtk-3-0t64 \
        libnss3 \
        libpcre2-16-0 \
        libpng16-16t64 \
        libstdc++6 \
        libx11-6 \
        libxkbcommon0 \
        libxcomposite1 \
        libxdamage1 \
        libxfixes3 \
        libxrandr2 \
        python3 \
        python3-pip \
        unzip \
        xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 24.04 默认用户可能占用 UID 1000，先清理再创建与工程硬编码 HOME 对齐的用户
RUN set -eux \
    && if id -u ubuntu >/dev/null 2>&1; then userdel -r ubuntu || true; fi \
    && groupadd --gid "${BUILD_GID}" "${BUILD_USER}" \
    && useradd --uid "${BUILD_UID}" --gid "${BUILD_GID}" \
        --create-home --home-dir "/home/${BUILD_USER}" \
        --shell /bin/bash "${BUILD_USER}"

# 安装 Silicon Labs Tooling (slt) CLI
RUN set -eux \
    && aria2c --allow-overwrite=true -o /tmp/slt.zip "${SLT_CLI_URL}" \
    && bsdtar -xf /tmp/slt.zip -C /usr/local/bin \
    && rm -f /tmp/slt.zip \
    && chmod +x /usr/local/bin/slt \
    && slt --version

USER ${BUILD_USER}
WORKDIR /home/${BUILD_USER}

COPY --chown=${BUILD_USER}:${BUILD_USER} docker/install_slt_packages.sh /tmp/install_slt_packages.sh
COPY --chown=${BUILD_USER}:${BUILD_USER} docker/pkg.ci.slt /tmp/pkg.ci.slt
COPY --chown=${BUILD_USER}:${BUILD_USER} docker/pkg.full.slt /tmp/pkg.full.slt
RUN chmod +x /tmp/install_slt_packages.sh

# Ensure SLT log location is writable.
# On CI/GHCR the previous build cache layer can leave ~/.silabs owned by root,
# causing `slt install` to panic when opening ~/.silabs/slt/slt.log.
USER root
RUN set -eux \
    && rm -rf "/home/${BUILD_USER}/.silabs" \
    && mkdir -p "/home/${BUILD_USER}/.silabs/slt" \
    && chown -R "${BUILD_UID}:${BUILD_GID}" "/home/${BUILD_USER}/.silabs"

USER ${BUILD_USER}

# Install toolchain + SDK via slt (see install_slt_packages.sh for per-package logs).
RUN set -eux \
    && mkdir -p "${HOME}/docker" \
    && cp /tmp/pkg.ci.slt /tmp/install_slt_packages.sh /tmp/pkg.full.slt "${HOME}/docker/" \
    && export CMAKE_VERSION="${CMAKE_VERSION}" \
       NINJA_VERSION="${NINJA_VERSION}" \
       COMMANDER_VERSION="${COMMANDER_VERSION}" \
       GCC_ARM_VERSION="${GCC_ARM_VERSION}" \
       SIMPLICITY_SDK_VERSION="${SIMPLICITY_SDK_VERSION}" \
       INSTALL_SLT_PACKAGES="${INSTALL_SLT_PACKAGES}" \
       INSTALL_SLC_TOOLS="${INSTALL_SLC_TOOLS}" \
    && "${HOME}/docker/install_slt_packages.sh" \
    && rm -rf /tmp/install_slt_packages.sh /tmp/pkg.ci.slt /tmp/pkg.full.slt

USER root
COPY --chown=${BUILD_USER}:${BUILD_USER} docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

USER ${BUILD_USER}
WORKDIR /workspace

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["build"]
