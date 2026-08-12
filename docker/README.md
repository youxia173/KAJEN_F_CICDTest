# Docker 固件编译环境（KAJEN_F / SixG301）

基于同事 Li-Bat 方案改编，适配本仓库硬编码路径 `/home/hans/.silabs/...`。

## 0. 先安装 Docker（你当前机器还没有）

在 Ubuntu / Debian：

```bash
# 官方便捷脚本（也可按 Docker 文档用 apt 装 docker-ce）
curl -fsSL https://get.docker.com | sudo sh
sudo usermod -aG docker "$USER"
# 注销再登录后，或执行：
newgrp docker
docker version
```

装好后确认：

```bash
docker run --rm hello-world
```

> 目标架构：**linux/amd64**。若是 ARM 本机，需要额外的 amd64 模拟，不推荐起步阶段使用。

## 1. 本仓库 docker/ 文件

```
docker/
├── Dockerfile         # 镜像定义（用户 hans + SiSDK 2025.12.1）
├── entrypoint.sh      # 路径对齐 + build / generate / shell
├── build-image.sh     # 一键构建镜像
└── README.md          # 本说明
```

| 项目 | 本工程取值 |
|------|------------|
| 默认镜像标签 | `kajen-sixg301:sdk-2025.12.1` |
| 容器用户 / HOME | `hans` / `/home/hans` |
| CMake 目录 | `ZigbeeMatterLightSolution_SixG301M113W_cmake` |
| SLC part | `simg301m113wih` |
| SiSDK | `2025.12.1` |
| Matter extension | `2.8.0` |

## 2. 构建镜像

**所有命令在仓库根目录执行。**

### 方式 A — 完整镜像（SDK 打进镜像，适合以后 CI）

首次很慢、体积大，需要能访问 Docker Hub 与 Silicon Labs 下载站。

```bash
./docker/build-image.sh
```

### 方式 B — 精简镜像（推荐你先试；挂载本机已有 Studio SDK）

你本机已有 `~/.silabs`，可先走这条，几分钟就能验证 Docker 流程：

```bash
INSTALL_SLT_PACKAGES=0 IMAGE_TAG=kajen-sixg301:slim ./docker/build-image.sh
```

## 3. 编译固件

### 完整镜像

```bash
docker run --rm \
  -v "$PWD":/workspace \
  -w /workspace \
  kajen-sixg301:sdk-2025.12.1
```

### 精简镜像

```bash
docker run --rm \
  -v "$PWD":/workspace \
  -v "$HOME/.silabs":/home/hans/.silabs:ro \
  -w /workspace \
  kajen-sixg301:slim
```

默认命令是 `build`。成功后产物在宿主机 `artifact/ZigbeeMatterLightSolution_SixG301M113W-full.s37`。

发版改名：

```bash
bash scripts/release/package_firmware.sh 0.3.6
```

### 其它命令

| 命令 | 说明 |
|------|------|
| `build` | 配置并编译（默认） |
| `generate [all\|app\|bootloader]` | SLC 重新 Generate |
| `check-slc` | 检查 java / slc / zap |
| `shell` | 交互 bash |

## 4. GHCR（GitHub 云端自动构建镜像）

镜像放在 **GitHub Container Registry**（私有，跟仓库权限走），不用本机上传完整包。

### 第一次（仓库管理员）

1. Push 含 `.github/workflows/docker-image.yml` 的代码  
2. GitHub → **Actions** → **Build Docker image (GHCR)** → **Run workflow**  
   （首次可能要 30～90 分钟，slt 在云端下载 SDK）  
3. 完成后在 **Packages** 里能看到 `kajen-sixg301`  

镜像地址示例：

```text
ghcr.io/barryjim/kajen-sixg301:sdk-2025.12.1
```

### 开启 CI 云端编固件

仓库 **Variables**（可选）：

| 变量 | 值 |
|------|-----|
| `ENABLE_FIRMWARE_BUILD` | `true` |
| `GHCR_FIRMWARE_IMAGE` | 留空则用默认 `ghcr.io/<owner>/kajen-sixg301:sdk-2025.12.1` |

之后 push/PR 会 pull GHCR 镜像并在容器里 `build`。

### 何时重新 build 镜像

- 改了 `docker/Dockerfile` 或 `entrypoint.sh`（push 到 main 会自动触发）  
- 或手动再跑 **Build Docker image (GHCR)**

### 本机仍可用 slim（不依赖 GHCR）

```bash
docker run --rm -v "$PWD":/workspace -v "$HOME/.silabs":/home/hans/.silabs:ro \
  -w /workspace --entrypoint /workspace/docker/entrypoint.sh kajen-sixg301:slim build
```

## 5. 常见问题

| 现象 | 处理 |
|------|------|
| `docker: command not found` | 先完成本文第 0 节安装 |
| permission denied | `sudo usermod -aG docker $USER` 后重新登录 |
| `~/.silabs/slt/installs not found` | 精简镜像必须挂载宿主 SDK；或改打完整镜像 |
| CMake 找不到 SDK | entrypoint 会软链 hash；确认本机/镜像 SiSDK 版本为 2025.12.1 |
| 产物权限不对 | `BUILD_UID=$(id -u) BUILD_GID=$(id -g) ./docker/build-image.sh` |
