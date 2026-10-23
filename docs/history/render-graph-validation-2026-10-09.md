# RenderGraph 验证记录（2026-10-09）

本文保存当时的验证结果，不代表后续提交或其他平台通过验证。
当前设计见 [RenderGraph](../render-graph.md)。日志路径相对于仓库根目录，构建产物不保证长期保留。


Linux Clang Debug，RTX 4060 Laptop GPU，NVIDIA 610.57.04：

- 应用、CPU 测试和三个 GPU 测试目标构建成功，无编译警告；clang-format 检查通过。
- CPU／shader CTest 共 8 项全部通过，包括新增 `render_graph`。
- `lc1_shadow_path_tests`、`lc1_pbr_tests`、`lc1_temporal_shadow_tests` 全部通过，
  Vulkan validation 和 synchronization validation 无警告或错误。
- 主程序成功启动，交换链从 1280×720 经 1249×1497 变为 1536×1024，期间无 Vulkan 诊断。
  进程随后以退出码 0 提前结束，因此窗口脚本计划中的后两次 resize 未执行，整段定时
  smoke 脚本不计为通过。GPU 测试中的 resize／恢复尺寸检查已通过。

日志位于 `build/linux-x86_64/Debug/artifacts/render-graph-{build,ctest,shadow-paths,pbr,temporal-shadows,game-smoke}.log`。
本次未验证 Windows、Release 或其他 GPU／驱动。
