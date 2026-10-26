# Shader object 渲染：历史验证

结果仅适用于记录中的代码状态与环境，不代表当前版本已通过验证。构建日志可能已清理。

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
