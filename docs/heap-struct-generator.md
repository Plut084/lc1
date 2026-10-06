# Descriptor heap 结构体生成器

`tools/generate_heap_structs.py` 使用 Python 3.10+ 标准库，将一份受限的 Slang 风格声明
生成可 `#include` 的 Slang 文件。不需要安装 Python 包，也不解析整个 shader。

```sh
python3 tools/generate_heap_structs.py tests/shaders/heap-material.heap \
    -o build/generated/heap-material.slang
```

## 声明与使用

输入直接写 shader 希望使用的资源类型：

```slang
struct Material {
    float4 base_color_factor;
    float metallic_factor;
    float roughness_factor;
    Texture2D<float4> base_color_texture;
    SamplerState base_color_sampler;
};

struct FrameBuffer {
    float4 clear_color;
    ConstantBuffer<MaterialData> material;
};
```

每个 `Name` 生成四项：

- `NameData`：保持字段顺序；资源字段变成 `uint <field>_index`，其他字段保持原类型。
- `Name`：保留输入声明中的资源类型。
- `resolve_name(NameData data)`：复制值字段，从对应 heap 解析资源字段。
- `load_name(uint index)`：通过 `ConstantBuffer<NameData>` 读取，再调用 resolver。

例如上面的存储结构包含 `base_color_texture_index`、`base_color_sampler_index` 和
`material_index`，可以这样使用：

```slang
#include "heap-material.slang"

// frame_buffer_index comes from push data.
FrameBuffer frame = load_frame_buffer(frame_buffer_index);
Material material = resolve_material(frame.material);
float4 color = material.base_color_texture.Sample(
    material.base_color_sampler, uv) * material.base_color_factor;
```

采样器使用 `SamplerDescriptorHeap`，其他资源使用 `ResourceDescriptorHeap`。
解析缓冲区字段得到的是缓冲区对象，不会递归读取整个资源树；`MaterialData` 必须显式
解析为 `Material`。这样调用处仍然控制何时访问下一级数据。

## 第一版语法范围

文件只包含 `struct Name { Type field; ... };` 和 `//`、`/* ... */` 注释。
类型名使用 PascalCase，字段使用 snake_case。声明先于引用。

支持：

- `float`、`int`、`uint` 标量、1–4 分量向量和 1–4 × 1–4 矩阵。
- 前面声明的无资源结构体，以及前面生成的 `NameData`。
- `Texture1D/2D/3D/Cube`、`Texture1DArray/2DArray/CubeArray`、
  `RWTexture1D/2D/3D`，元素类型可为数值标量或向量；省略时默认为 `float4`。
- `SamplerState`、`SamplerComparisonState`。
- `ConstantBuffer<T>`、`StructuredBuffer<T>`、`RWStructuredBuffer<T>`，
  元素必须为已知的纯数据类型；以及 `ByteAddressBuffer`、`RWByteAddressBuffer`。

不支持数组、初始化表达式、方法、属性、预处理、import、任意外部类型或嵌套泛型。
未知类型、未支持语法、重复字段和生成名称冲突会报错并返回非零退出码；解析失败不覆盖
已有输出。不要将正在编写的完整 `.slang` 文件作为 schema 输入。

普通类型也生成一份 `NameData`；无资源的 `Name` 可直接作为缓冲区元素。
有资源的类型不能直接嵌入 GPU 数据，须用对应 `NameData` 或指向它的缓冲区字段。

## 构建与验证

`shaders/HeapStructs.cmake` 提供 `generate_heap_structs(output source)`。调用方必须把
输出加入消费 shader 的 `DEPENDS`，并把输出目录加入 Slang 的 `-I`；首次构建尚无
Slang depfile，不能只依赖 depfile 建立生成顺序。修改 schema 或生成器会重新生成。
输出位于各平台自己的构建树。

通用生成 target 会发现 `shaders/` 下所有 `.heap`（包括子目录），只生成代码：

```sh
cmake --build --preset linux-x86_64-debug --target generate_heap_structs
```

例如 `shaders/modules/material.heap` 输出到
`build/linux-x86_64/Debug/shaders/generated/modules/material.slang`。
新增或删除 schema 会触发 CMake 重新配置；不会覆盖源码目录里的文件。

目录外的任意 schema 也可注册独立 target：

```cmake
add_heap_struct_target(generate_my_material
    "${CMAKE_CURRENT_BINARY_DIR}/generated/my-material.slang"
    "${CMAKE_CURRENT_SOURCE_DIR}/my-material.heap")
```

仓库的独立编译示例目标已完成生成与编译依赖连接：

```sh
cmake --preset linux-x86_64-debug
cmake --build --preset linux-x86_64-debug --target heap_struct_shader_test
ctest --test-dir build/linux-x86_64/Debug -R heap_struct_generator --output-on-failure
```

也可独立测试，不配置 C++ 项目：

```sh
python3 tests/test_heap_struct_generator.py --slangc /path/to/slangc -v
```

测试覆盖拒绝错误输入、字段存储顺序、heap 选择，以及用真实 Slang 编译包含
`FrameBuffer → MaterialData → Material → Texture/Sampler` 的 fragment shader。
省略 `--slangc` 时只执行生成器测试，编译测试明确跳过。

这不生成 C++ 结构体，不计算 padding、字段偏移或 Vulkan descriptor 编码。
CPU 上传端仍须匹配 Slang 的实际缓冲区布局；`Name` 中的资源对象不能按 `NameData`
直接拷贝。索引有效性、资源寿命和非一致索引访问语义仍由调用方负责。当前 resolver
不自动插入 `NonUniformResourceIndex`。示例使用来自 push data 的统一索引。

本工具不迁移主渲染 shader。编译测试不执行 GPU，也不证明 CPU/GPU 数据布局或
其他驱动、Windows 平台已经通过验证。
