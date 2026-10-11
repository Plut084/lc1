# 外部缺陷记录（Slang / 驱动 / 工具链）

这里记录**不属于本项目缺陷**、但会改变渲染结果或迫使代码绕行的外部问题：Slang 编译器、
GPU 驱动与工具链的缺陷，以及为它们保留的写法。每条包含：状态、现象、触发形状、外部链接、
本项目受影响的位置、现有绕行、以及"什么时候可以撤销绕行"的复测方法。

这些记录不是设计文档，也不描述我们的接口或所有权。升级 Slang、更新驱动或更换 GPU 后，
按各条的复测方法重新检查；只确认编译通过或 SPIR-V 形状不算验证。

## 1. NVIDIA 驱动：untyped Uniform 指针整块加载矩阵时忽略 MatrixStride

- 状态：**未修**。驱动 615.71.09（Arch Linux，RTX 4050 Laptop，Vulkan 1.4.351）复现；
  报告人在 2026-10-09 追加 615.78.08 仍然复现。
- 现象：`ConstantBuffer<T> data = ResourceDescriptorHeap[i];` 读出的 `float4x4`，
  逐元素（`data.m[row][col]`）读取正确，但**整块** `OpLoad %mat4v4float` 时驱动忽略
  `MatrixStride`：每列只比上一列偏移 4 字节（应为 16），并把每列当 4 个连续 float 读。
  矩阵内容与 `mul(matrix, vector)` 都因此错误。
- 触发形状：成员装饰 `RowMajor` + `MatrixStride 16`，指针来自
  `OpUntypedAccessChainKHR` → `OpBufferPointerEXT %_ptr_Uniform` → `OpLoad %mat4v4float`。
  这是本项目的常规形状（见下）。
- 外部链接：
  [NVIDIA 论坛：Incorrect RowMajor MatrixStride handling for whole matrix OpLoad through an untyped Uniform pointer](https://forums.developer.nvidia.com/t/vulkan-615-71-09-rtx-4050-descriptor-heap-incorrect-rowmajor-matrixstride-handling-for-whole-matrix-opload-through-an-untyped-uniform-pointer/383932)；
  最小复现 <https://github.com/OppositeNor/matrix-vector-load-stride-reproduce>。
- 该项目受影响的位置（全部为 heap `ConstantBuffer` 中的 `float4x4`）：
  `shaders/shader.slang` 的 `object_data.model`、`camera.view`、`camera.proj`、
  `object_data.normal_transform` 与切线变换；`shaders/visibility.slang` 的同一段；
  `shaders/temporal-shadows.slang` 的 `previous_view_projection`。
- 现有绕行：不整块读矩阵。按 GLM 存储的列逐列读取后线性组合，等价于四次 `OpLoad %v4float`；
  镜像手性用三列混合积，切线只用前三列。`shaders/text.slang` 更彻底：矩阵以四个 `float4`
  push constant 传入，根本不出现矩阵类型。
- 复测 / 撤销条件：把上述位置改回 `mul(matrix, float4(position, 1))`，在本机 GPU 上重跑
  `lc1_pbr_tests`（覆盖投影、物体平移、旋转×非均匀缩放、镜像与镜像 UV，并与 CPU `glm`
  结果对照），同时要求 validation 与 synchronization validation 无消息。全部通过才能删除
  逐列写法。仅检查编译或 `spirv-val` 不成立：该缺陷的 SPIR-V 自洽，validator 不会报错。

## 2. Slang：heap ConstantBuffer 的嵌套数组寻址（Uniform 类指针缺少 ArrayStride）

- 状态：上游 2026-07-07 以
  [#11647](https://github.com/shader-slang/slang/pull/11647)（把 descriptor-heap
  `ConstantBuffer` 改为 `StorageBuffer` 类）关闭 #11483。**本项目的 `ResourceDescriptorHeap[...]`
  写法在本机 2026.18 上仍是缺陷形状**，见下面的本机实测。
- 现象：从 `ResourceDescriptorHeap[i]` 读到的 `ConstantBuffer`，标量与普通成员正常，
  **嵌套数组**（数组元素、数组矩阵）寻址错误；同一结构改用 `[[vk::binding]] ConstantBuffer`
  绑定则正常。
- 根因（上游分析）：heap `ConstantBuffer` 的缓冲指针留在 `Uniform` 存储类，而
  `getPointerArrayStrideValue` 只为 `PhysicalStorageBuffer`/`StorageBuffer` 发出 pointer 型
  `ArrayStride`，因此派生出的数组元素指针没有 stride。SPIR-V 自身自洽，`spirv-val` 不报错，
  只有在真机上执行才表现为数据错误。
- 外部链接：[slang#11483](https://github.com/shader-slang/slang/issues/11483)，其中包含上游的
  最小判别方法与回归测试 `tests/spirv/descriptor-heap-constant-buffer-nested-array-stride.slang`。
- 本机实测（2026-10-11，`slangc` 2026.18，项目同款 flag）：

  ```slang
  struct UB { float a; float arr[4]; float4 v4[4]; float4x4 m[4]; };
  ConstantBuffer<UB> cb = ResourceDescriptorHeap[idx];
  ```

  反汇编结果：`OpBufferPointerEXT %_ptr_Uniform`，pointer 型 `ArrayStride` **0 个**（数组类型上
  仍有 `ArrayStride 16/16/64`）；同一结构换成 `StructuredBuffer` 则为 `StorageBuffer` 类并带
  pointer `ArrayStride`。生产 `shader.spv` 同形：7 个 heap 缓冲指针全为 `%_ptr_Uniform`，
  pointer 型 `ArrayStride` 为 0。
- 该项目受影响的位置：`shaders/shader.slang` 与 `shaders/visibility.slang` 的
  `lights.lights[i]`（`LightsData` 内的 `Light[10]`）。
- 现有绕行：把 `Light` 逐字段读进局部变量后再传给 `sample_light`。
- 复测 / 撤销条件：先用上面的最小结构编译并反汇编，确认 heap 缓冲指针是
  `OpTypePointer StorageBuffer` 且派生数组元素指针带 `ArrayStride`；再在本机 GPU 上跑
  `lc1_pbr_tests` 的多光源与阴影用例确认数值。两者都通过后才可以把 `Light` 整块传参。

## 3. 相关条目（检索用，本机未复现）

- [slang#12226](https://github.com/shader-slang/slang/issues/12226)：heap `ConstantBuffer`
  被当作 storage buffer 描述符读取（uniform 描述符 8 字节、storage 16 字节），表现为读到
  垃圾；2026.14 / NVIDIA RTX A4500，已由
  [commit 6dba5d2](https://github.com/shader-slang/slang/commit/6dba5d21203e4c8e8e3dca7fd0e57a421aaf08c3)
  修复。本机 2026.18 产物未复现（heap 缓冲指针为 `Uniform` 类，见第 2 条）。
- [NVIDIA 论坛：RTX 4060 fragment shader 在 heap index 非 0 时读到错误描述符](https://forums.developer.nvidia.com/t/rtx-4060-fragment-shader-reads-the-wrong-descriptor-for-any-heap-index-other-than-0/383842)
  与 [VK_EXT_descriptor_heap：uniform buffer 读取走全局内存而非常量加载](https://forums.developer.nvidia.com/t/vk-ext-descriptor-heap-uniform-buffer-loads-use-global-memory-loads-instead-of-constant-loads/363494)：
  native descriptor heap 路径上的其他驱动报告，遇到无法解释的 heap 读取结果时可先检索。
