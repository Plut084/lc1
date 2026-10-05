# Descriptor heap 最小 GPU 测试

`lc1_descriptor_heap_tests` 使用原生 `VK_EXT_descriptor_heap` 与
`VK_KHR_shader_untyped_pointers`，离屏绘制两个三角形，不需要窗口或显示服务器。
不创建 descriptor set、descriptor set layout 或 pipeline layout，也不使用
`VkShaderDescriptorSetAndBindingMappingInfoEXT`。

## 构建和运行

先按项目常规流程生成 Linux Debug 依赖和 preset，然后从仓库根目录运行：

```bash
cmake --preset linux-x86_64-debug
cmake --build --preset linux-x86_64-debug --target lc1_descriptor_heap_tests -j 8
cd build/linux-x86_64/Debug
. ./generators/conanrun.sh
./lc1_descriptor_heap_tests
```

这是显式 GPU 测试目标，不加入默认 CPU CTest，也不随普通游戏目标构建。
需要支持上述两个扩展和对应 feature 的 Vulkan 1.4 GPU，以及 Khronos validation
layer。测试始终启用 validation 和 synchronization validation；不支持时报告错误。

## 验证内容

测试直接使用项目的 `lc1::DescriptorHeap`：

- `allocate_buffer` 分配两份 uniform buffer 描述符，保存三角形顶点和颜色系数。
- `allocate_image`、`allocate_sampler` 分配两组纹理和采样器描述符；纹理分别为橙色和青色。
- 返回值直接作为 shader 的 heap 索引使用，测试侧不编码描述符，也不计算 reserved
  range、分区偏移或 stride。
- `CommandBuffer::bind_descriptor_heap` 绑定 heap，`DescriptorHeap::push_data` 为每次
  draw 传入三个索引。shader 直接使用 `ResourceDescriptorHeap` 和 `SamplerDescriptorHeap`。
- 两个槽位全部用尽后检查第三次分配失败，并检查零容量 heap 拒绝分配。
- 对同一个 command buffer 进行两次录制、提交和像素回读，每次都检查两个三角形及背景。
  第二次通过 `CommandBuffer::reset` 清除 heap 绑定缓存，检查重新录制时的绑定行为。
- 成功后输出 `artifacts/descriptor-heap.ppm`，并以退出码 0 返回。

pipeline 使用 `eDescriptorHeapEXT`，pipeline layout 为空。Slang 的
`[[vk::push_constant]]` 声明接收 `vkCmdPushDataEXT` 传入的数据，没有旧 set/binding 映射。

所有资源都活到提交 fence 完成；测试不涉及在途 GPU 访问期间修改 heap。
`deallocate*` 仍未实现，槽位回收、TLAS、resize 和完整游戏渲染迁移不属于本测试范围。

## 大缓冲与大量槽位验证

独立目标 `lc1_descriptor_heap_bulk_tests` 只增加测试，不修改生产分配器。
同样要求 validation 和 synchronization validation，不加入默认 CPU CTest。

```bash
cmake --preset linux-x86_64-debug
cmake --build --preset linux-x86_64-debug --target lc1_descriptor_heap_bulk_tests -j 8
cd build/linux-x86_64/Debug
. ./generators/conanrun.sh
./lc1_descriptor_heap_bulk_tests uniform
./lc1_descriptor_heap_bulk_tests storage
```

无参数或传入 `all` 时依次运行两个路径；单独运行可保留其中一个路径失败时另一条路径的结果。

- `uniform` 使用生产 `DescriptorHeap::allocate_buffer` 分配 1,024 个槽位，
  对应 1,024 块 16 KiB UBO，合计 16 MiB。预留非零 image 分区，验证 buffer
  分区偏移；打乱槽位访问顺序及块内记录顺序，并检查第 1,025 次分配被拒绝。
- `storage` 使用一块包含 16 MiB 有效数据及对齐前缀的 storage buffer，分别创建
  全数据范围和后半段 8 MiB 子范围的描述符。shader 使用 `StructuredBuffer<uint4>`，
  以非连续顺序访问所有记录。前缀填充不同数据，用于发现偏移错误。
- 每条路径更新两轮数据；更新前上一轮提交已经完成。输入 buffer 和描述符保留，
  验证修改数据后不会继续读到旧内容。push data 始终只有 16 字节的索引及寻址参数。
- 输出到 `RGBA32Uint` 图像并回读，逐项比较所有 `uint4` 的四个分量，包含首尾记录。
  不只抽查像素，也不以编译或无 validation 报错代替数值验证。

**结果边界：** `storage` 的描述符写入、heap 绑定直接使用测试内的 Vulkan 调用，
因为当前生产接口只创建整块 UBO 描述符，不能表达 storage descriptor 或子范围。
因此该路径通过只证明当前 Slang/驱动的原生 heap 大缓冲访问可用，不能证明
`DescriptorHeap` 已支持这些操作。测试不覆盖槽位回收、多帧同时在途、heap 扩容、
TLAS 或 GPU 写入 storage buffer；不为让测试通过而修改生产代码。

### 本机验证结果（2026-10-05）

Linux Debug，NVIDIA GeForce RTX 4060 Laptop GPU：两个模式均以退出码 0 完成，
validation 和 synchronization validation 无警告或错误输出。

- `uniform`：每轮完整比较 1,048,576 条 `uint4`，两轮通过；超容量分配被拒绝。
- `storage`：每轮分别比较全范围 1,048,576 条和子范围 524,288 条 `uint4`，
  两轮通过；本机对齐前缀为 16 字节，子范围起点位于该前缀之后 8 MiB。
- 实测设备限制：`maxUniformBufferRange = 65536`，
  `maxStorageBufferRange = 4294967295`。没有把 16 MiB 数据当作单个 UBO 测试。

本次日志保存在构建目录的 `artifacts/descriptor-heap-bulk-uniform.log` 和
`artifacts/descriptor-heap-bulk-storage.log`。这些结果不代表其他驱动或 Windows 已验证。
