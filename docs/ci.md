# 持续集成

`.github/workflows/ci.yml` 在 push、pull request 和手动触发时执行四组独立的构建任务：

| 平台 | 环境 | 配置 |
|---|---|---|
| Linux x86_64 | Fedora 44 容器，Clang 22，`profiles/linux` | Debug |
| Linux x86_64 | Fedora 44 容器，GCC 16，`profiles/linux-gcc` | Debug |
| Windows x86_64 | Windows Server 2022，MSVC 19.4x，`profiles/windows` | Debug |
| Windows x86_64 | Fedora 44 容器，MinGW-w64 GCC 16，`profiles/mingw64` | Debug |

所有任务都通过 Conan 安装依赖，构建完整程序与 Slang 着色器。
配置显式设置 `BUILD_TESTING=OFF`，不构建测试目标，不运行测试或游戏。
Windows 同时覆盖原生 MSVC 构建和 Linux 上的 MinGW 交叉编译。
依赖和项目编译并行度均为 2，避免托管 runner 内存不足。

host 和 build profile 都显式指定：项目构建为 Debug，构建工具为 Release。
原生构建的两种上下文使用同一份 profile，MinGW 交叉编译的 build profile 使用
`profiles/linux-gcc`，保证构建工具运行于 Linux。并行度限制同时应用于两种上下文。

Fedora 临时容器以 root 运行，Conan 安装命令为两种上下文设置
`tools.system.package_manager:mode=install`，允许依赖配方通过 dnf 安装
`xkeyboard-config-devel` 等系统开发包。这项设置仅用于 CI，不修改本地 profile。

Conan 固定为 2.33.0，其默认设置表支持 GCC 16，解决此前 2.27.0 不识别该版本的问题。
Slang 使用完整的 2026.18.3 官方发行包。Conan 包缓存按平台、工具链和依赖配置隔离，
每组构建使用独立 runner 和构建目录，不共享 CMake 缓存。
相同分支的新运行取消旧运行；矩阵设置 `fail-fast: false`，一组失败不会取消其他组。

`assets/` 未纳入 Git，因此 CI 创建空目录以满足构建时的资源复制步骤。
本流程只验证编译和链接，不验证资源完整性、运行时行为或 Vulkan 验证层输出。
