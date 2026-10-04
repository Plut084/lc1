# 引擎语义审查（2026-10-01）

这次审查关注名称、职责、所有权与接口实际行为是否一致。基于当前工作区，包括正在搭建的
Scene、资源管理和角色代码；区分已经可以复现的问题与尚未接入的设计风险。
除 Character 相关实现外，本报告中的建议没有批量应用到代码。

最值得先处理的是生命周期承诺与输入状态的一致性。场景是否组织成树可以等到挂点、模型节点
真正需要它时再决定；目前没有证据要求重写 Vulkan 层、引入 ECS 或建立统一对象继承树。

## 1. 优先处理：资源引用的有效期与 unload 冲突

位置：[resource-manager.hpp](../include/lc1/resource-manager.hpp)，`load<T>`、`unload<T>`。

类注释承诺引用一直有效到管理器销毁，并明确说没有单独卸载。但 `unload<T>` 对拥有
`unique_ptr<Resource>` 的 map 调用 `extract`，又不保留返回的 node handle。临时 node handle
在语句结束时析构，因此资源立即销毁。这和承诺冲突；调用者持有的对象引用、材质引用或
尚未执行完的 GPU 命令，都不会因为 RAII 自动停止引用这个资源。

这是现有接口的确定矛盾，但当前演示主要直接构造资源，不能据此声称演示已经发生悬空访问。

建议当前选择简单的场景级生命周期：去掉单独卸载入口，等场景的使用者和 GPU 工作结束后统一
销毁。若确实需要流式卸载，再明确增加失效检测、使用者释放协议和 GPU 延迟销毁，而不是只把
`extract` 换成 `erase`。

## 2. 优先处理：InputRouter 的 context 与 action 可能属于不同快照

位置：[input.cpp](../src/input.cpp)，`push/pop/update`；[input.hpp](../include/lc1/input.hpp)。

`push/pop` 立即修改 `active_context()`，但 action 数组只在 `update()` 中重算。已用现有
代码复现：

```text
W 按下 → update() → push(Ui)
active_context() == Ui：true
held(MoveForward)：true

再次 update() 后：
held(MoveForward)：false
released(MoveForward)：false
```

调用方如果在处理 UI 事件时切换 context，然后本帧继续读取 action，会读到上一份 context
计算出来的数据。目前主循环没有启用 UI 切换，这属于可以复现的 API 一致性问题，尚不是当前
演示必现的移动故障。

另外，`released` 现在表达的是绑定键的释放事件，并不覆盖 context 失活导致的动作取消。
所以注释中“动作结束”的说法需要限定范围。拖拽等持续操作需要单独处理取消，不能只等
`released()`。

建议先确定一种契约：context 变更在下一次解析输入时统一提交，或者变更时立即用当前设备
快照重新解析。若应用需要区分物理松键和 UI 抢占，应增加取消语义，不必把它伪装成松键。
本次 `PlayerInput` 只是玩家控制器的输入边界，尚未替整个路由器解决快照一致性。

## 3. Scene 接入前处理：活动相机的身份与 vector 元素地址不同

位置：[scene.hpp](../include/lc1/scene/scene.hpp)，`cameras_` 和 `active_camera_`。

`cameras_` 是按值保存的 `vector<FpsCamera>`，活动相机使用元素指针。等添加接口接入后，
vector 扩容会让这个指针失效。预留容量只能延后问题，不能定义稳定身份。

当前没有公开的添加/选择相机接口，活动相机仍为空；renderer 的 Scene 重载也会明确拒绝
空相机。因此这是接口补完之前应解决的风险，不是当前主循环已经解引用悬空指针。

建议由 Scene 拥有相机，并用稳定 ID/句柄选择活动相机。如果初期只增不删，索引就够；如果
要删除和复用槽位，再加 generation。另一种简单方案是由 `unique_ptr` 保存稳定地址，
但仍须明确删除活动相机时的行为。

此外，[scene.cpp](../src/scene/scene.cpp) 还定义着头文件中已注释掉的 `Scene::render`，
并使用了不同于 `lc1::Renderer` 的 `scene::Renderer` 前置声明。它没有加入构建，目前不会
导致主程序失败。接入 Scene 时应删除这份过期实现，确定由渲染器消费 Scene 或场景快照，
避免同时保留两个不一致的入口。

## 4. 名称与职责：FpsCamera 实际承担了通用相机和控制约束

位置：[camera.hpp](../include/lc1/scene/camera.hpp)、[map-camera-controller.cpp](../src/game/map-camera-controller.cpp)。

当前斜视角也使用 `FpsCamera`，说明“第一人称”不是这台相机的固有身份。它同时包含位置与
方向、透视参数、鼠标式 yaw/pitch 操作、水平移动方式和 ±89° 的俯仰限制。

这正是讨论中容易把角色头部与相机混为一谈的原因。世界空间姿态、投影模型、控制模式是
三个概念。等需要完整头部动画、翻滚或尸体视角时，固定 world-up 和禁止 roll 的表示会成为
实际限制。

建议分步调整：先把名字改成与当前用途一致的 `PerspectiveCamera`；未来需要任意姿态时，
让相机接受位置和旋转，把第一人称的角度限制留在控制器。无需现在就做复杂的相机继承体系。
其 `move_local` 还把正 z 解释为 forward，而项目局部前方约定为 −Z：应明确这是控制输入轴，
或改为标准局部坐标转换，不能同时暗示两种约定。

## 5. 数据边界：scene::Light 实际是 GPU 上传结构

位置：[scene/lights/light.hpp](../include/lc1/scene/lights/light.hpp)、
[vk/render-data.hpp](../include/lc1/vk/render-data.hpp)。

`scene::Light` 包含对齐、padding、整数类型编号和 shader 布局断言，实际是 GPU 数据格式；
真正的场景灯光是 `PointLight/SpotLight/SphereLight/DirectionalLight`。现在 `Scene::lights()`
名字像是查询场景灯光，实际却遍历、转换并分配一份 GPU 记录数组。

建议将上传格式命名为 `LightData`，与 `CameraData/ObjectData` 放在同一处；场景保留其灯光
对象。转换入口命名为 `collect_light_data` 或放进渲染快照构建步骤，让它的成本与返回内容
明确。灯的 `intensity` 还需要按光源类型说明单位或约定；当前 shader 是经验光照，不能仅因
参数叫 intensity 就承诺完整物理照明语义。

## 6. Model 当前是静态网格数据，Object 当前是一个可绘制实例

位置：[model.hpp](../include/lc1/model.hpp)、[object.hpp](../include/lc1/scene/object.hpp)。

`Model` 目前把 OBJ 的 shape 合并进一个顶点/索引集合；虽然读取 material 数组，却没有保留
每个面的材质对应关系。因此，它还不是能完整表示多材质、多节点、骨骼和动画的模型资源。
`Object` 则是一份变换加一组 mesh/material 引用，也不等于任意游戏实体。

这两个名字目前可用，但扩展时应避免把所有新职责直接压进一个 mesh 和一个 material。
正式模型可以拥有多个可绘制部分及其资源引用；对象实例负责世界变换和实例状态。
若想先把当前范围写清楚，`StaticMeshData` 和 `MeshInstance` 会更准确；不需要为了改名
立即引入完整模型系统。

本次已经把导入校正归到 `Model`，烘焙进静态顶点，并校正法线和镜像绕序。之后接入骨骼时
需要同时处理骨骼和逆绑定矩阵，不能直接把“只改顶点”推广成所有模型的导入协议。
资源缓存目前只以路径为键，如果同一路径可以有不同校正配置，应明确资源描述的身份。

## 7. 层次边界：Character、资源加载协议和输入默认绑定

`lc1::Character` 借用 `Continent` 做游戏碰撞，并包含行走/奔跑参数。该边界问题已修正：
声明和实现已迁到 `game/character.*`，不再属于 `scene` 命名空间；外观仍组合 `scene::Object`。
游戏代码已拆入 `lc1_game`，由应用链接；`lc1_game` 依赖 `lc1_engine`，引擎不反向链接游戏库。
`GameSession`、`PlayerView` 和调试展示也已迁入游戏层，窗口操作保留在应用层。

`ResourceManager::load<T>` 要求所有资源派生自 `Resource`，并提供接收 `Device` 的静态加载
函数；实际的 `Model/Image/GpuMesh` 等没有形成这套统一协议，仍存在专用网格缓存。
不要为了让模板成立而强迫 CPU 资源也依赖 GPU 设备。可以先采用明确的资源类型缓存；
等确有统一需求时再区分 CPU 导入、GPU 上传和缓存生命周期。

`default_bindings()` 位于通用 `input.hpp`，却包含游戏和调试快捷键。这是应用配置，放到
游戏/应用层更直观。`Action` 当前也绑定这一个游戏，但项目只有一个游戏时无需急着把整个
InputRouter 模板化。

## 8. 一个小而确定的行为问题：Stopwatch::restart 没有重置全部统计状态

位置：[game-clock.cpp](../src/game-clock.cpp)，`Stopwatch::restart`。

它清空时间、累计窗口和总帧数，却保留 `fps_frames_`、`fps_`。重启后第一次刷新 FPS 可能
把重启前的帧数算到新的时间窗口里，立即读取 `fps()` 也会得到上一次的数据。
如果 restart 表示重新开始统计，应一并清空这两个成员；若只想重置时间，则应另起名字。

## 建议顺序

1. 统一资源卸载的生命周期契约，以及 context/action 快照的提交时机。
2. 接入 Scene 时完成稳定相机引用和过期入口清理。
3. 接入模型动画前明确 Model、模型实例、骨骼姿态的边界。
4. 顺着实际需求拆分相机姿态/投影、Light/LightData，再整理命名和命名空间。

Vulkan 对象的 RAII、FrameLoop 与 Renderer 的分工、共享 GPU 网格和每实例绘制变换，目前
已经有清楚的职责。上述调整应保留这些边界，避免让 Scene 同时接管输入、资源上传、GPU
同步和绘制。
