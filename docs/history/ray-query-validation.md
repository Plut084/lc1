# Ray-query：历史验证

以下保留迁移时的受阻记录及后续修复结果。旧状态已由后文取代，不表示当前仍未验证。
构建日志不保证长期保留。

本轮全 heap 迁移的 shader 接口检查与构建已通过；GPU 运行因沙箱访问和自动审批服务故障
尚未完成。下列/既有 GPU 通过记录属于迁移前的实现；本轮边界见 [光照](../lighting.md)。

### 地址读取修复后的验证（2026-10-06）

Linux Debug / RTX 4060 Laptop / NVIDIA 610.57.04：`lc1_pbr_tests`、
`lc1_shadow_path_tests`（含新增遮挡回归）、`lc1_temporal_shadow_tests` 均通过，
validation 和 synchronization validation 无警告或错误。主程序运行 12 秒，经历窗口
resize，收到 SIGTERM 后正常退出（退出码 0）。shader heap 接口 CTest 和可见度 shader 的
`spirv-val --target-env vulkan1.4` 通过。使用跳过射线的诊断 shader 时，新增遮挡回归按预期失败。

日志位于 Linux Debug 构建目录的 `artifacts/int64-{pbr,shadow-paths,temporal-shadows,game-smoke}.log`。
这些结果补充并取代前一段对本轮 GPU 尚未执行的状态描述；不代表 Windows、Release 或
其他 GPU/驱动已经验证。本次没有重跑完整 CPU CTest。
