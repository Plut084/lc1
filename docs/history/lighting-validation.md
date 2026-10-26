# 光照与 heap 迁移：历史验证

保留原记录中的阶段与环境；未标日期的段落不补推日期。“本轮”指当时迁移，不是当前工作树。
构建日志可能已清理，旧测试数量和失败状态不代表当前测试状态。

## PBR 与法线贴图

2026-10-02 在 RTX 4060 Laptop 上通过 Debug/Release 构建和各 3 项 CPU CTest，
两种构建的 PBR GPU 数值测试，以及 Debug 阴影路径、时间阴影 GPU 回归；最终运行无 Vulkan 验证消息。
最初球阵曾通过 45 帧连续运行检查；扩展后的展区随主程序启动、渲染及正常退出。
实施顺序和后续范围见 [PBR 计划](../history/pbr.md)。

2026-10-03 完成法线贴图与展示外观修正：Linux Debug/Release 各 4 项 CPU CTest 通过，
PBR 数值、镜像/缩放法线、阴影投影开关及既有阴影 GPU 回归通过，验证与同步验证无报错。
Windows Release 交叉构建通过；主程序使用细节法线和无文字投影的展区运行并正常退出。

## 材质 heap 迁移

本轮 Linux Debug 验证：完整应用构建、`lc1_pbr_tests`、`lc1_shadow_path_tests`、
`lc1_temporal_shadow_tests` 和 `lc1_descriptor_heap_tests` 通过；GPU validation 与同步验证
无警告或错误。日志位于构建目录 `artifacts/material-*.log`。CPU CTest 为 5/6：
未改动的地图缩放上限为 6144，而 `continent` 测试仍预期 96；此项未在材质迁移中修改。
Windows 和其他 GPU 尚未验证。

## 全通道 heap 迁移的验证边界

本轮新增 `descriptor_heap_shader_interfaces` CPU 检查，读取七个生产 shader 的 SPIR-V，
要求 native heap 扩展且没有 `Binding` / `DescriptorSet` 装饰。完整项目和 GPU 测试目标可编译。
窗口 GPU 测试因沙箱无法连接 Wayland 未执行；用户已授权提升权限，但自动审批服务持续返回
404（审批模型不受支持），没有完成审批。沙箱内独立 heap 测试也在创建实例时返回
`ErrorIncompatibleDriver`。前一节的 GPU 通过结果属于上一次材质迁移，不代表本轮新增的
TLAS 描述符、时间累积、深度预览 heap 路径已在 GPU 上验证。

本轮 CPU CTest 为 6/7（包含新增的 shader 接口检查），剩余失败仍是既有地图缩放范围断言。

2026-10-06 修复 `shaderInt64` 未启用及直接 AS heap 读取导致的设备丢失后，Linux Debug
的 PBR、阴影路径、时间/空间阴影 GPU 测试均通过，主程序启动、resize 和正常退出通过，
validation 与 synchronization validation 无消息。生产 TLAS 改为通过 heap UBO 读取地址；
具体证据、回归与尚未验证的平台见 [阴影验证记录](../history/ray-query-validation.md#地址读取修复后的验证2026-10-06)。
