# Character：角色状态、模型与控制

当前实现是站立、沿地面移动的静态角色实例。主角、士兵和 NPC 都可以使用
`scene::Character`；由玩家还是 AI 控制，不改变角色自身的类型。

## 数据归属

| 数据 | 所属对象 |
|---|---|
| 世界位置与身体朝向 | `Character` 内部的 `Object::transform`，只保存一份 |
| 相对身体的观察角度 | `Character` |
| 身体宽高、眼睛高度、行走和奔跑速度 | 每个角色的 `CharacterConfig` |
| 模型原点、单位、坐标轴的校正 | `Model`，在上传 GPU 前统一处理 |
| CPU 顶点和索引 | `Model` |
| GPU 顶点、索引和 BLAS | `GpuMesh`，多个角色可以借用同一份 |
| 材质和网格的引用 | `Object`，引用不拥有资源 |
| 玩家输入和控制决策 | 玩家控制器；角色不包含 `InputRouter`、`Window` 或相机 |
| 当前观察画面的相机 | 应用/场景，按需要读取角色眼睛位置和观察方向 |

角色世界坐标采用右手系、Y 向上，位置约定为脚底。身体 yaw 为绕 +Y 的右手旋转角，
单位为度，零角度朝向 −Z。观察 yaw 相对身体，正数向左；观察 pitch 正数向上。
当前分别限制在 ±85°、±89°，防止视线基向量退化。

`heading()` 是不包含俯仰的观察方向，`body_heading()` 是身体朝向，
`look_direction()` 是包含俯仰的完整观察方向。它们在抬头或向侧面观察时有意不同。

## 构造和移动

下面的网格和材质都必须比角色及其 GPU 绘制活得更久：

```cpp
auto const model = lc1::Model::load_from_file(
    asset_path, {.rotation = {-90.0F, 0.0F, 0.0F}});
auto const mesh = model.gen_mesh(device);
auto const material = renderer.make_material(texture);

lc1::scene::Character character{
    continent,
    {.transform = {.position = continent.spawn()},
     .mesh = &mesh,
     .material = &material}};

character.set_body_yaw(90.0F);
character.set_look_angles(0.0F, 20.0F);
character.move(continent, {0.0F, 0.0F, -0.5F});
auto const draw = character.draw_item();
```

−90° 的校正仅是 Z-up 模型转为 Y-up 的示例；已经符合坐标约定的资源不需要它。

构造时校验出生点、身体参数、世界变换以及网格/材质配对。世界变换保持直立、单位缩放；
模型尺寸通过资源校正处理，实际碰撞尺寸通过 `CharacterConfig` 设置，避免视觉缩放
悄悄改变身体大小。`Character(continent)` 还可以创建没有可绘制资源的模拟角色，方便
CPU 测试；对这种角色调用 `draw_item()` 会明确报错。

`move(continent, displacement)` 接受以米为单位的 `vec3` 世界位移，当前要求 y 为零。
它借用大陆的碰撞查询，让身体沿障碍滑动，并把结果写回同一份世界位置。

`walk(continent, direction, delta_seconds, running)` 根据角色速度计算位移。方向投影到
XZ 平面，长度超过 1 时归一化，保留较小输入的模拟量强度；单次时间最多取 0.1 秒，
避免断点或调整窗口造成瞬移。第一人称控制器使用 `heading()`，因此抬头不会减慢行走。

这两个方法都不自动转身。第一人称可以侧移、后退；斜视角控制器则主动让身体朝向移动方向。

## 模型校正

`Model` 的文件加载和程序化构造都接受 `scene::Transform correction`。这是资源数据，
不是角色世界状态。当前静态模型路径在构造完成前将它应用到顶点：

- 位置使用完整的平移、旋转、缩放。
- 法线使用线性变换的逆转置，并归一化。
- 镜像校正会翻转三角形索引顺序，因为烘焙后的反射不再体现在每次绘制的矩阵行列式中。

`vertices()` 和 `indices()` 返回已经校正的数据；`correction()` 仅供查看导入配置，
调用方不能再次把它乘进角色绘制矩阵。多个角色共享同一个 `GpuMesh` 即共享这份结果。

当前 `ResourceManager::load_mesh(path)` 仍使用默认校正。如果以后要通过缓存加载不同的
导入配置，应为模型定义稳定的资源描述或把配置纳入缓存键，不能只按文件路径区分。

## 控制与视角

两个玩家控制器都借用 `Continent`，在 `update` 调用期间借用 `Character`，不保存角色位置。
它们消费应用层提供的 `PlayerInput`，不直接读取窗口或键盘：

- `FirstPersonCharacterController`：鼠标水平移动转动身体，垂直移动改变观察俯仰；
  WASD 相对角色的水平观察方向移动。
- `ThirdPersonCharacterController`：以外部传入的相机水平方向解释 WASD，并主动转身。

应用层根据第一人称/斜视角模式、Gameplay/UI context 和窗口焦点决定是否提供控制输入。
鼠标捕获是模式切换时对窗口的操作，不用于反推是否启用鼠标观察。
M 切换时使用同一个角色，保存第一人称观察角度，并保留斜视角的缩放与锁定状态。

## 后续动画边界

当前没有骨骼、蒙皮、动画播放、死亡或布娃娃系统。眼睛位置暂时是脚底加眼睛高度，
观察角度只表达观察目标，不会单独变形头部网格。

接入动画后，由角色实例拥有播放进度和当前骨骼姿态，借用共享的骨骼定义、网格和动画片段。
眼睛位置可以从最终头部挂点计算；相机如何跟随挂点由视角控制决定。
骨骼、逆绑定矩阵和动画轨道必须采用一致的坐标校正，不能只套用当前静态顶点的处理步骤。

## 验证

`lc1_character_tests` 覆盖角色初始状态、身体与观察的区别、碰撞滑动、实例世界矩阵、
配置和无效输入、第一人称倒退/侧移、切换控制器，以及模型校正后的顶点、法线和镜像绕序。
原有 `lc1_continent_tests` 已使用 `Character`，继续检查速度、世界边界和城市穿行。
