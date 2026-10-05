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
