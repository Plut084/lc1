# 持续集成

`.github/workflows/ci.yml` 在 push、pull request 和手动触发时执行两个独立的构建任务：

| 平台 | 环境 | 配置 |
|---|---|---|
| Linux x86_64 | Fedora 44 容器，Clang 22，`profiles/linux` | Debug |
| Windows x86_64 | Windows Server 2022，MSVC 19.4x，`profiles/windows` | Debug |

两个任务都通过 Conan 安装依赖，构建完整程序与 Slang 着色器。
配置显式设置 `BUILD_TESTING=OFF`，不构建测试目标，不运行测试或游戏。
Windows 使用原生 MSVC 构建；现有 MinGW 交叉编译流程保持独立。
依赖和项目编译并行度均为 2，避免托管 runner 内存不足。

Conan 固定为 2.27.0，Slang 使用完整的 2026.18.3 官方发行包。Conan 包缓存按平台、
工具链和依赖配置隔离。相同分支的新运行取消旧运行；一个平台失败不会取消另一个平台。

`assets/` 未纳入 Git，因此 CI 创建空目录以满足构建时的资源复制步骤。
本流程只验证编译和链接，不验证资源完整性、运行时行为或 Vulkan 验证层输出。
