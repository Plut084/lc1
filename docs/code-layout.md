# 应用、游戏与引擎的代码边界

目录按职责划分，CMake 目标按依赖方向连接：`lc1` → `lc1_game` → `lc1_engine`。
游戏类型沿用现有 `lc1` 命名空间；渲染场景类型使用 `lc1::scene`，应用入口使用 `lc1::app`。
完整模块索引及 Mermaid 依赖图见 [模块依赖图](module-dependencies.md)。

| 位置 | CMake 目标 | 职责 |
|---|---|---|
| `app/main.cpp`、`app/application.*` | `lc1` | 进程入口、窗口和 Vulkan 生命周期、输入接入、光标捕获、窗口标题、交换链和每帧调度 |
| `include/lc1/game/`、`src/game/` | `lc1_game` | 大陆、角色、控制器、游戏会话、相机玩法、世界的可绘制资源 |
| `game/debug/pbr-showcase.*`，分别位于上述头文件和源码目录 | `lc1_game` | 当前嵌入游戏世界的 PBR 展示，供游戏和 GPU 测试共同使用 |
| 其余引擎模块，包括 `scene/`、`vk/`、窗口及资源模块 | `lc1_engine` | 场景表达、渲染和平台能力 |

`vk/` 内部分为 `core`、`resources`、`presentation`、`render`；呈现与渲染各自依赖底层
资源，由应用组合。目录职责与依赖约定见 [Vulkan 代码分层](vulkan-layers.md)。

`Character` 的声明位于 `lc1/game/character.hpp`，使用 `lc1::Character`。它依赖大陆的
出生点和碰撞规则，并组合 `scene::Object` 保存唯一的世界变换与外观引用。场景层不负责角色规则。
`Party` 位于 `lc1/game/party.hpp`，管理队伍成员、招募转移与队长命令；`Character` 保存跟随／等待命令并沿用地面碰撞移动。当前原型流程见
[对话与招募](dialog.md)。`DialogCatalog` 拥有多份 YAML 内容和状态入口规则，
`Dialog` 表示一场双向交流，`DialogSystem` 管理并发会话、NPC 发起与冷却；
三者均不依赖窗口或渲染。`GameSession` 负责输入接入、控制器暂停与文字呈现。

## 游戏会话与窗口的接口

`GameSession` 是客户端本地游戏会话，持有大陆、角色、相机和场景资源，读取 `InputRouter`
的动作状态，提供相机、绘制列表、灯光、曝光及 `GameLocation`。`Application` 轮询窗口事件、
更新 `InputRouter`，并负责退出策略和光标捕获。会话根据焦点、输入上下文和 ImGui 捕获状态限制
游戏操作，在内部生成 `PlayerInput`／`MapCameraInput` 交给控制器；不再需要 `GameInput` 中转。

当前本地原型中的 `GameSession` 还借用 `Window`，读取指针状态和尺寸，并布局屏幕文字和调试面板，
并直接构建游戏内 ImGui 菜单与音乐浏览器。ImGui Vulkan 后端、窗口生命周期和输入路由仍由
`Application` 管理。这是当前原型的集成边界，不代表未来服务器拥有 UI/窗口依赖；客户端/服务端
目标边界见 `AGENTS.md` 的 Project and gameplay 约定。

`GameLocation` 中的字符串视图借用会话中的大陆数据或静态名称，不能超过会话的生命周期。

`GameSession::draws()` 每次从地形、展示物和角色集合生成本帧的绘制列表；玩家也在角色
集合中，不额外追加。会话不保存需要手动同步的汇总列表或绘制下标。应用将返回的
`vector<DrawItem>` 保存在帧录制回调内，直到 `RenderGraph::execute()` 返回，保证
各 pass 借用的 draw span 有效。

`GameSession` 将动作转换为角色移动、相机操作和调试命令。
每帧先调用 `GameSession::update_view_mode(router)`，由会话判断是否切换视角；再由应用设置
光标捕获，最后调用 `GameSession::update(router, delta_seconds)` 读取鼠标增量并更新游戏。
捕获状态变化会重置鼠标基线，提前读取增量会使第一人称视角跳动。
窗口标题由应用根据游戏位置和灯光状态拼接并设置。

游戏层可以使用引擎的 GPU 资源；这不表示所有游戏类都需要 GPU。
角色、大陆、控制器等规则仍可独立运行 CPU 测试。设备和 Renderer 必须比 GameSession 长寿，
帧资源在会话之后创建、之前销毁；退出或异常时由 Application 先等待 GPU 完成。

## 构建与验证

游戏 CPU 测试链接 `lc1_game`，纯渲染测试链接 `lc1_engine`；PBR 集成测试使用游戏库里的展示代码，
不再直接编译或包含 `app/` 下的文件。两个库共用公开头文件根目录，构建目标分离并不隔离头文件可见性；
仍需避免在引擎实现中引用游戏层。
现有 `InputRouter` 的动作枚举和默认绑定尚未通用化，此次没有扩大到输入系统重设计。
