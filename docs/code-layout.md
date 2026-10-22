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

## 本地会话的职责

`GameSession` 是本地原型的协调者，不是服务器 `GameMode`，也不是网络复制状态。
它组合以下对象；当前仍编入 `lc1_game`，没有增加网络占位层。

| 对象 | 职责与边界 |
|---|---|
| `GameWorld` | 拥有大陆、势力、装备、武器、队伍、角色和 `DialogSystem`；装配世界、推进规则、生成存档快照；不依赖 Window、ImGui、音频或 Vulkan |
| `WorldView` | 借用只读世界，拥有地形/角色 GPU 资源、字体、展示场景、灯光与音频 context/listener；生成绘制项和世界文字 |
| `PlayerView` | 拥有视角模式、记忆观察角和相机；生成世界空间控制意图，世界更新后再更新相机 |
| `GameUi` | 菜单、调试面板、保存反馈及屏幕文字；返回保存请求和音量变化，由会话执行 |
| `MusicBrowser` | 音乐目录、文件浏览 UI、播放状态与音源；借用 `WorldView` 的音频 context |
| `GameSession` | 读取输入、按焦点/context/ImGui capture 门控、协调各阶段，并向 Application 转交保存请求和表现数据 |

`Character` 位于 `game/character.*`，内部唯一的 `scene::Transform` 保存脚底位置和身体朝向。
它不持有网格或材质，也不生成 `DrawItem`；`Party` 同样只处理成员、招募和命令，不负责绘制。
`WorldView::draws()` 从角色变换生成本帧绘制项，玩家是角色集合的一员，不重复追加。
Application 通过 `GameSession::world()`、`world_view()`、`player_view()` 和 `ui()` 的只读引用
查询各自的数据；引用有效期不超过会话。会话保留更新协调、保存请求、保存反馈和宽高比设置入口，
不为每个子对象查询重复提供转发方法。

角色当前共用占位网格/材质。`scene::Object` 仍可用于其他渲染场景，不再是角色世界姿态的所有者。

`DialogCatalog` 拥有 YAML 内容和状态入口规则，`Dialog` 表示一场交流，`DialogSystem` 管理
并发会话、NPC 发起和冷却。`GameWorld` 负责对话参与者注册、控制器暂停及交互动作执行；
`WorldView` 和 `GameUi` 只读这些状态来生成世界/屏幕文字。详见[对话与招募](dialog.md)。

## 每帧时序

Application 轮询事件、更新 `InputRouter`、开始 ImGui 帧，然后：

1. 调用 `GameSession::update_view_mode()`；会话判断是否切换视角。
2. Application 应用光标捕获。捕获变化会重置鼠标基线，必须先于鼠标增量读取。
3. `GameSession::update()` 解析输入门控、交互和表现操作。
4. `PlayerView::resolve_input()` 直接读取游戏动作，并从只读角色和视角信息生成 `PlayerCommand`，不修改角色。
5. `GameWorld::update()` 应用玩家命令、推进角色物理/AI，再处理交互和对话。
6. `PlayerView::update_pointer()` 在鼠标门控允许时读取设备滚轮与逻辑光标坐标，
   解析边缘平移；`update_camera()` 读取最终姿态；`WorldView::update()` 更新灯光、展示和监听位置。
7. `GameUi::update()` 构建 UI 并返回开始、退出、保存及音量请求；会话接通开始操作，
   Application 执行保存和退出。主菜单/设置屏蔽玩法输入，世界继续模拟；返回主菜单会释放光标。
   菜单切换后的玩法门控在下一帧生效，光标捕获在本帧 UI 更新后同步。

模拟时间要求有限且非负，每帧最多推进 0.1 秒；角色、对话、相机和展示使用同一份时间。
当前没有固定步长或追赶长帧。角色名单按 keyid 排序，非跟随者先更新、跟随者后更新，
避免依赖 `unordered_map` 遍历顺序，也让跟随者读取本帧队长的位置。

## 生命周期与资源

Application 仍负责窗口/Vulkan 生命周期、输入路由、光标捕获、交换链和 ImGui 后端。
它拥有 `Save` 与 `GameSession`；`GameWorld` 构造时消费 Save，不保留借用。
世界可以只用 Save、大陆和对话目录构造、更新、生成快照，无需创建窗口或设备。

会话成员按 `GameWorld → WorldView → PlayerView → MusicBrowser → GameUi` 构造并逆序销毁。
世界先于借用它的表现与控制器存在，音频 context 先于音源存在。`GameWorld` 在销毁角色前
清空活动对话；当前角色名单固定，不支持运行期添加/删除，新增该能力时需同步维护名单和借用。
`GameSession`、`GameWorld`、`WorldView` 和 `MusicBrowser` 禁止复制/移动。

Application 在正常/异常退出时先等 GPU 完成。`DrawItem` 借用 WorldView 的资源；录制回调持有
本帧 `vector<DrawItem>` 直到 `RenderGraph::execute()` 返回。设备和 Renderer 比会话长寿，
FrameLoop 的帧资源在会话之后构造、之前销毁。`GameLocation` 的字符串视图借用世界或静态名称。

## 构建与验证

游戏规则和表现目前仍在同一个 `lc1_game` 静态库中；这次隔离的是源码依赖与运行期构造，
未声称已提供无图形依赖的独立构建目标。引擎不得包含游戏头文件，app/game 不包含引擎私有实现。

`lc1_game_world_tests` 覆盖无窗口世界装配、交互招募、跟随、存档恢复、统一时间以及
玩家控制 → 物理 → 相机的时序。原有大陆、角色、对话和视角 CPU 测试继续运行。
GPU 渲染和窗口交互需要单独验证。动作枚举、context、默认绑定属于 `game/input.*`；
引擎只保留设备状态和模板路由。`lc1_input_tests` 使用自定义动作/context 验证通用路由。
