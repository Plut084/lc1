# Descriptor heap 最小 GPU 测试

`lc1_descriptor_heap_tests` 使用原生 `VK_EXT_descriptor_heap`、
`VK_KHR_shader_untyped_pointers` 和 `VK_EXT_shader_object`，离屏绘制两个三角形，不需要窗口或显示服务器。
不创建 descriptor set、descriptor set layout 或 pipeline layout，也不使用
`VkShaderDescriptorSetAndBindingMappingInfoEXT`。

基础测试和 bulk 测试均使用 `GraphicsShaders` 与 `set_graphics_state`，不创建 graphics
pipeline。基础测试还要求 `sampleRateShading` 与 4× RGBA8 颜色附件，使用独立 shader 入口
验证 1×/4×/1× 的逐采样结果：输出采样编号的一阶、二阶矩，resolve 后逐像素比较解析均值。
每次绘制前故意设置错误的采样数、零 sample mask、关闭颜色写入及 rasterizer discard，
验证公共动态状态设置能恢复它们。生产主光照的 SampleId 读取另由
`descriptor_heap_shader_interfaces` CTest 检查。迁移结果见 [shader object](shader-objects.md)。

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

## DescriptorHeapCache

`DescriptorHeapCache` 组合并借用一个 `DescriptorHeap`，用于渲染线程上的重复查询：

```cpp
DescriptorHeap heap{device, sampler_capacity, image_capacity, buffer_capacity};
DescriptorHeapCache cache{heap};
auto const image_index = cache.image(texture.image());
auto const buffer_index = cache.buffer(uniform_buffer);
auto const sampler_index = cache.sampler();
```

同一资源重复查询返回相同索引，不重复分配；不同资源不会因内容相同而合并。
当前 heap 只支持整张 sampled image view、整块 uniform buffer 和一种固定 sampler，
cache 保持这个范围。若未来增加 view、offset/range、layout 或 sampler 参数，必须同时
扩展 key，不能将不同描述符当作同一个缓存项。

image 以 `vk::ImageView`、buffer 以 `vk::Buffer` 为键，不使用 C++ 对象地址；移动资源
不会改变缓存命中。缓存不持有 GPU 资源。销毁或替换资源前必须调用对应的
`cache.invalidate(resource)`，因为 Vulkan 句柄也可能复用。批量销毁资源前可 `clear()`，
但这会忘记其他存活资源的索引，之后查询将消耗新槽位。

失效或 clear 只清除 CPU 查找记录，不释放、覆盖或回收 heap 槽位，不等待 GPU。
先前返回的索引在 heap 和原资源仍存活时继续有效；真正销毁资源仍须等待所有 GPU 使用
完成。当前 append-only 分配器不适合通过反复 clear 解决长期 resize 的槽位回收问题。
heap 必须比 cache 长寿且不能在借用期间移动。cache 不可复制。

Renderer 若持有 heap 和 cache，按此顺序声明，保证 cache 先析构；外部持有的材质和
帧资源必须在销毁前通知缓存失效。帧资源专用索引也可直接保存在 `FrameResources`，
避免为了取得同一索引而反复查找。构建缓存本身不会自动迁移 Renderer 的现有分配调用。

现有 `lc1_descriptor_heap_tests` 已通过 cache 获得绘制索引，并增加以下验证：

- 两种图像和 buffer 的索引互异，重复查询不消耗槽位，固定 sampler 只分配一次。
- 移动 buffer/texture 后仍命中原索引。
- 满 heap 仍能命中已有条目；失效只影响指定资源，clear 忘记所有类型。
- 分配失败不留下占位条目，后续重试仍明确失败。
- 两轮实际绘制和像素回读验证缓存索引能用于 GPU 访问。

本机 Linux Debug / NVIDIA GeForce RTX 4060 Laptop GPU 验证通过，validation 和
synchronization validation 无警告或错误。材质迁移完成后，完整 CMake target 已构建并重新运行通过，日志为构建目录中的
`artifacts/material-descriptor-heap.log`。尚未验证 Windows，也未实现自动资源失效或槽位回收。

材质主通道使用每个帧槽自己的 heap 和固定槽位索引；`write_image` / `write_sampler`
只允许覆盖已经分配的槽位，调用者必须先等待所有使用该槽位的 GPU 工作完成。
这些原位写入不经过 cache。不要覆盖仍由 cache 映射为另一资源的槽位，否则会破坏缓存语义。

## 全通道迁移

项目 `DescriptorSet` 类及其源文件已删除，生产通道全部使用 heap；ImGui 官方后端是明确保留的
例外。每个帧槽的缓存复用固定 buffer 索引；会原位更新的图片槽位仍由帧槽直接管理。
新增的 acceleration-structure 分区按 `getDescriptorSizeEXT(eAccelerationStructureKHR)`
独立确定 stride，不能假定与 uniform buffer 描述符同尺寸。仅当容量非零时查询该类型，
不影响不启用 AS 扩展的基础 heap 测试。TLAS 描述符使用 AS device address 和允许的零 size，
底层 AS 必须在所有 GPU 使用结束前存活。深度图片可显式指定 `eDepthStencilReadOnlyOptimal`。

2026-10-06 的 GPU 排查发现本机直接 AS heap 地址读取异常；生产 Renderer 改为通过
heap UBO 读取 TLAS 地址，仍使用原生 heap 和 push-data 索引。上面的 AS 分区接口保留，
但不属于当前生产路径的通过范围。复现证据、设备版本与替代路径见
[TLAS 地址读取](ray-query-shadows.md#tlas-地址的-heap-读取2026-10-06)。

`ctest -R descriptor_heap_shader_interfaces` 检查生产 SPIR-V 不再需要 set/binding 映射。
本轮 GPU 运行受到沙箱/审批服务故障阻塞，详见 [光照验证边界](lighting.md)。上面的 GPU
通过记录属于更早的实现，不能作为新增 AS 和辅助通道迁移的 GPU 验证结果。
