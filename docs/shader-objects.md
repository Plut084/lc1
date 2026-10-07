# Shader object 渲染

项目的五个生产绘制通道及 GPU 测试使用 `VK_EXT_shader_object`，不创建或绑定
graphics pipeline。`Pipeline`、`PipelineKind`、`ShaderModule`、`ShaderStages` 已删除。
ImGui 官方 Vulkan 后端仍管理自己的 pipeline、pipeline layout 和 descriptor set。

## 资源与状态

`resources/ShaderObject` 从文件读取对齐的 SPIR-V words，检查长度、magic 和入口名称，
创建并拥有 `vk::raii::ShaderEXT`。创建使用 `eDescriptorHeap`，descriptor set layout 和
push constant range 均为空；资源索引仍通过原生 descriptor heap 与 push data 提交。
设备需要检查并启用 `shaderObject`，应用、带 surface 的默认设备路径和离屏 heap 测试
均已声明这一要求。动态状态命令使用 shader object 提供的能力，不额外要求整组
extended dynamic state 扩展或无关 feature。

`render/GraphicsShaders` 只拥有一个顶点阶段和可选片元阶段。它以 `bindShadersEXT`
绑定 shader，显式解除未使用的 tessellation/geometry 阶段；深度通道也解除 fragment。
当前设备没有启用 mesh/task shader。阶段接口由现有 Slang 入口约定匹配，shader 创建时
设置实际的 `nextStage`。它不持有固定功能状态、pipeline 或 layout。

与旧 shader module 不同，shader object 是绘制时直接使用的可执行资源：各 pass 保存它们，
直到全部 GPU 使用结束后才能销毁。Device 仍须比 shader 长寿；应用退出的 GPU 等待不变。
绑定助手属于 render，`resources/CommandBuffer` 不包含或绑定 render 层的 shader 类型。

各 pass 在所属类中持有自己的 `GraphicsState` 配置，与对应的 `GraphicsShaders` 一起保存。
`Renderer` 持有主通道和色调映射状态；`RayQueryShadows` 持有可见度、时间累积和空间滤波状态。
主通道附件创建和 `sample_count()` 都读取
`Renderer` 的主通道状态中的采样数，避免与录制状态维护两份配置。
这些值描述 pass 所需的状态，不缓存命令缓冲当前状态。

`set_graphics_state` 在每个 pass 开始时应用其配置，显式设置顶点输入、拓扑、剔除、深度、depth bias、
采样数、sample mask、颜色写入及关闭的混合状态。顶点描述是固定数组，状态设置不做堆分配。
viewport/scissor 由调用方用 `setViewportWithCount` / `setScissorWithCount` 设置；
网格 draw 保留按模型行列式切换 front face。各 pass 不依赖前一 pass 或 ImGui 留下的状态。

| 通道 | 顶点输入 | 采样 | 颜色附件 | 深度 |
|---|---|---|---|---|
| PBR | 完整 Vertex | 1×/2×/4×，逐采样着色 | RGBA16F | 测试并写入 |
| Ray-query 可见度 | 完整 Vertex | 1× | RGBA32Uint + RGBA32Sfloat | 测试并写入 |
| 时间累积 | 无，全屏三角形 | 1× | RGBA32Uint + RGBA32Sfloat | 关闭 |
| 空间滤波 | 无，全屏三角形 | 1× | RGBA32Uint | 关闭 |
| 色调映射 | 无，全屏三角形 | 1× | 调用方输出格式 | 关闭 |

格式支持检查、负高度 viewport、HDR resolve、时间历史、TLAS、帧槽 fence 和 resize 同步
沿用现有契约。色调映射的全屏 pass 继续使用原来的正高度 viewport。

## 完整逐采样着色

shader object 没有对应 `sampleShadingEnable/minSampleShading` 的动态状态命令。
Vulkan 规定片元入口静态使用 `SampleId` 或 `SamplePosition` 时启用完整 sample shading，
等价于 `minSampleShading = 1.0`；仅设置多采样附件不能代替这个要求。

主片元入口接受 `SV_SampleIndex`，使用 `EvaluateAttributeAtSample` 在该采样点插值 UV。
这提供实际的 SampleId 读取，避免未使用的参数在优化时被删除。阴影可见度仍单采样计算，
没有按 MSAA 采样点重复 ray query。

`descriptor_heap_shader_interfaces` CTest 除原生 heap 检查外，还检查主 shader 保留
`SampleRateShading` capability、`BuiltIn SampleId` 与对它的 `OpLoad`。
`lc1_descriptor_heap_tests` 增加逐采样 GPU fixture：输出采样编号的一阶、二阶矩，
读取 resolve 后的全部像素并比较解析均值，同时检查公共动态状态可覆盖故意留下的错误状态。

参考：[Vulkan shader object 状态要求](https://docs.vulkan.org/spec/latest/chapters/shaders.html#shaders-objects-state)、
[sample shading 规则](https://docs.vulkan.org/spec/latest/chapters/primsrast.html#primsrast-sampleshading)。

## 验证记录（2026-10-06）

本机 Linux / RTX 4060 Laptop / NVIDIA 610.57.04：

- 迁移前 Debug 构建和五个 GPU 测试目标通过；CPU CTest 为 6/7，大陆相机缩放范围断言已失败。
- 迁移后 Linux Clang Debug/Release 完整构建通过；两种配置的 CPU CTest 均为 6/7，
  剩余失败仍是上述断言。没有修改该玩法测试或放宽其检查。
- 两种配置的 `lc1_pbr_tests`、`lc1_temporal_shadow_tests`、`lc1_shadow_path_tests`、
  `lc1_descriptor_heap_tests`、`lc1_descriptor_heap_bulk_tests` 均通过，
  validation 与 synchronization validation 无警告或错误。Release GPU 测试显式加载
  Debug Conan 提供的验证层，不能把 Release 主程序默认关闭验证当作验证证据。
- Debug/Release 各九个生产及测试 SPIR-V 模块通过 `spirv-val --target-env vulkan1.4`；
  优化后的主 shader 保留实际 SampleId 读取。
- 负向 fixture 将采样编号替换成常量后，4× 回读得到约 0.12549，期望 0.3125，
  GPU 断言按预期失败，证明检查能发现逐采样行为丢失。
- `make run` 的 Debug 主程序运行了正常渲染和 ImGui，swapchain 从 1280×720 变为
  1882×2032，随后正常退出，validation 与同步验证无消息。
- Windows MinGW Release 交叉构建通过；未验证 Windows GPU 执行、MSVC、其他驱动或远端 CI。
  构建仍有迁移前已存在的 `model.hpp` 未完成 glTF 导入相关警告。

各 Linux 构建目录 `artifacts/shader-object-lc1_*.log` 保存 GPU 结果；Debug 下另有
`shader-object-baseline-*.log`、`shader-object-negative-sampling.log` 和
`shader-object-game-smoke.log`。本次未做性能基准，不能据此宣称 shader object 更快。
