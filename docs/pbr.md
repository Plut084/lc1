# PBR 渲染实施计划

> 本文是早期设计记录；当前 CPU 材质与 native heap 实现见 [光照](lighting.md)。
> `GpuMaterial`、材质 set 2、`make_material()` 和固定材质池已被替换。

状态：2026-10-03 已实现阶段 1、2，以及阶段 3 的 MR/emissive 采样、MikkTSpace 切线和法线贴图。
AO、完整 sampler 配置、glTF 材质导入与 IBL 尚未实现。当前行为、运行方法和测试见 [光照](lighting.md)。

目标采用 glTF 2.0 metallic-roughness 材质模型，在现有前向渲染、逐采样材质计算和
ray-query 阴影通路上实现直接光、五类材质贴图、HDR 输出，最终补齐基于环境贴图的间接光照（IBL）。
首个可验收版本是参数驱动的 PBR 球阵和 HDR 输出；完整目标还包含纹理材质及 IBL。

## 实施前基线

- `shaders/shader.slang` 仍使用 Blinn–Phong，环境项为固定的 `albedo * 0.05`。
- `shaders/modules/pbr.slang` 有材质草稿及部分 GGX/几何函数，尚未被主 shader 导入；
  缺少完整 BRDF、采样和输出通路，不能视作已完成的 PBR 实现。
- `scene::PbrMaterial` 是 CPU 数据草稿。GPU 侧 `Material` 目前只持有一个底色纹理的 set 2，
  由 `Renderer::make_material()` 分配和填写。
- `GpuTexture` 固定使用 `R8G8B8A8_SRGB`；`Vertex` 有法线和 UV，没有切线。
- 主颜色附件使用输出目标的格式，MSAA 直接 resolve 到输出目标，尚无 HDR 中间附件及色调映射。
- 已有方向光、点光、聚光灯及球形光的中心采样光照，以及独立的阴影可见度、时间累积和空间滤波。
- glTF 导入仍在编写中，尚无完整的 primitive、材质、纹理到绘制项的通路。

## 材质数据与资源职责

| 部分 | 职责 |
|---|---|
| `scene::PbrMaterial` | CPU 材质参数和贴图引用，不依赖 descriptor pool 或 pipeline |
| 场景/模型资源所有者 | 保存 CPU 图像及共享的 GPU 纹理、采样器和材质，解析资源引用 |
| `GpuTexture` / `Sampler` | 上传指定色彩空间的纹理，保存图像及采样状态 |
| `Renderer::make_material()` | 接收参数及已解析的 GPU 纹理绑定，按 shader 布局创建 set 2 |
| `Material` | 持有材质 UBO 和 descriptor set；首轮保留现有名称，避免同时改所有调用点 |
| `FrameResources` | 每帧槽的 HDR、MSAA、深度附件及色调映射资源 |

CPU 参数补齐 `emissive_factor`、`normal_scale`、`occlusion_strength`，修正拼写并提供默认值。
glTF 默认值为底色白、metallic = 1、roughness = 1、emissive = 0、normal scale = 1、
occlusion strength = 1。现有程序生成地形显式采用非金属材质，例如 metallic = 0、roughness = 0.8，
避免把 glTF 的导入默认值直接当成旧场景的外观设定。

贴图引用需表达图像、sampler 和 UV 集；先实现 UV0，导入阶段再支持 UV1。
只读图像指针须注明非拥有关系、默认空，并由地址稳定的场景资源存储保证有效。
GPU 创建参数使用 `GpuTexture const*` 等已解析引用，不让 renderer 根据 CPU 指针自行猜测缓存或资源路径。
无贴图是合法材质，由创建阶段绑定替代纹理并设置必要的标志位。

已实现的 `PbrParameters` 与 GPU 上传结构分离，后者采用四个 16 字节行：底色、发光色、
metallic/roughness/normal scale/occlusion strength、标志位及预留。
已用 `sizeof`、`offsetof` 和 Slang 生成的 SPIR-V 布局核对 0/16/32/48 字节偏移，CPU 指针不进入 UBO。
GPU 材质可复用现有 `DescriptorSet` 对 UBO 的所有权，不再增加第二份 buffer 所有者。

首版材质创建后不变，一个材质跨帧共享一份 UBO/set。修改 CPU 描述不会自动更新 GPU；
需要换材质时创建新对象，等待旧对象的 GPU 使用完成再释放。
device、pool、图像 view 和 sampler 必须覆盖 descriptor 的实际使用期；RAII 析构不会代替 GPU 等待。

## Descriptor 布局

沿用 set 0 全局、set 1 对象、set 2 材质。set 2 的建议布局如下，保留已有的底色 binding 0：

| binding | 类型 | 内容 |
|---|---|---|
| 0 | Combined image sampler | Base color |
| 1 | Uniform buffer | 材质参数 |
| 2 | Combined image sampler | Metallic-roughness |
| 3 | Combined image sampler | Normal |
| 4 | Combined image sampler | Occlusion |
| 5 | Combined image sampler | Emissive |

所有纹理 binding 都写入有效资源。底色、MR、AO、emissive 的无贴图替代值为白色；
发光贴图缺失时，白色仍允许 `emissive_factor` 表示常量发光。
normal 缺失时用标志位绕过扰动，并绑定有效的平坦法线替代纹理，避免空 descriptor。

材质池按每材质 1 个 UBO descriptor、5 个图像 descriptor 计算容量。
首版继续使用显式容量，并在耗尽时清楚报错；不在这一步引入 bindless 或动态 UBO。
保留旧的 `make_material(texture)` 入口作为非金属材质的便捷包装，迁移现有场景与测试。

各 pipeline 从同一处取得材质布局定义。低编号 set 的共享仍遵守 Vulkan 的完整 pipeline-layout
兼容规则；独立深度预览等布局不同的通路继续显式重新绑定。IBL 阶段统一扩展正常通路的 set 0。

## 光照、贴图与输出约定

### 直接光 BRDF

在 `pbr.slang` 实现无资源绑定的材质求值函数，绑定声明留在入口 shader 或专用材质模块。
采用 Cook–Torrance：GGX 法线分布、Smith-GGX 遮蔽项、Schlick Fresnel，漫反射使用 Lambert。
约定材质粗糙度为感知粗糙度，`alpha = roughness²`，统一函数命名与参数含义，避免重复平方。

```text
F0 = lerp(0.04, base_color, metallic)
diffuse = (1 - F) * (1 - metallic) * base_color / pi
specular = D * G * F / (4 * NdotV * NdotL)
direct = Σ visibility * radiance * (diffuse + specular) * NdotL
```

限定为常规单次散射微表面近似。roughness 求值下限先取 0.045，处理掠射角、退化半程向量和零分母，
确保边界输入不会出现 NaN/Inf。底色、metallic、roughness 的输入检查与着色器防护应有一致语义。
`sample_light()` 的距离和角度衰减仅应用一次，局部光保留无截断的平方反比衰减。

球形光继续采用中心方向/强度和圆盘软阴影近似；这不能给出随光源面积变化的正确 PBR 高光。
完整面积光 BRDF 积分或 LTC 属于后续独立工作。

### 纹理语义

| 贴图 | GPU 采样色彩空间 | 求值 |
|---|---|---|
| Base color | sRGB，alpha 保持线性 | 乘线性 base color factor 和顶点颜色 |
| Metallic-roughness | 线性 UNORM | B × metallic factor；G × roughness factor |
| Normal | 线性 UNORM | RGB 解码到 [-1, 1]，XY 乘 normal scale，再归一化 |
| Occlusion | 线性 UNORM | `lerp(1, R, occlusion_strength)` |
| Emissive | sRGB | 乘线性 emissive factor，加到 HDR 光照结果 |

`GpuTexture` 增加明确的色彩空间/用途参数；纹理缓存键包含图像身份及色彩空间。
同一图像兼作颜色和数据贴图时，首版允许分别上传，不能错误复用同一种解码格式。
采样器按过滤和环绕配置共享，不能用当前全局 repeat sampler 覆盖所有 glTF sampler。
检查所选格式的采样、blit 和线性过滤支持；sRGB mip 在正确色彩空间过滤，normal mip 后在 shader 归一化。
后续按画面需求再考虑法线方差导致的高光抗锯齿。

normal mapping 增加 `float4 tangent`：xyz 为切线，w 为手性。法线用逆转置变换，切线用模型线性部分变换，
再对法线正交化；副切线结合 tangent.w 和镜像变换符号构造。导入时烘焙的变换也需同步处理切线手性。
读取已有 glTF tangent；缺失时生成与 MikkTSpace 一致的切线，所需依赖遵循 Conan 规则。
生成时保留 UV 接缝和手性分裂，退化 UV 不得造成非法向量，也不能静默应用错误的法线贴图。

BRDF 使用扰动后的着色法线，阴影射线偏移、历史判定及滤波继续使用未受贴图扰动的表面法线。
AO 首版只衰减间接漫反射，不乘直接光或 emissive；间接高光遮蔽需要独立近似，不直接套同一个 AO。

### HDR 与间接光

正常绘制顺序为：

```text
阴影可见度 → 时间累积/空间滤波
                         ↓
PBR 材质和光源 → 线性 RGBA16F（MSAA）→ 线性 HDR resolve
                         → 曝光/色调映射 → 输出目标
```

HDR 单采样附件必须保留供后处理采样；1× MSAA 时直接写它。
按实际 HDR/depth 格式和用途查询采样数支持，在不超过现有 4× 上限的范围内选择共同支持值。
每帧槽持有自己的附件，复用和 resize 遵守该槽 fence；颜色写入到后处理采样使用明确的 sync2 屏障。
共享阴影历史 resize 仍需要现有的跨帧同步，不能误当成单槽资源处理。

首版用固定曝光和 Reinhard 色调映射，先保证亮度大于 1 的值在色调映射前不被裁剪。
MSAA resolve 在色调映射之前完成。sRGB attachment 自动完成输出编码，shader 不再重复 gamma 校正。
UNORM 输出必须由调用方明确显示编码约定；线性离屏读回与 sRGB 显示输出不可混为一谈。
曝光等显示参数归输出通路，不属于某个材质。独立深度预览保持自己的输出路径。

首个直接光版本默认关闭固定 `0.05` 环境补光，以便验收 BRDF；无直接光也无环境光的金属应当暗。
最终用 IBL 提供间接光：HDR 环境图、漫反射 irradiance、按粗糙度预过滤的镜面环境 mip、BRDF 积分 LUT。
IBL 资源归环境所有，绑定到全局 set 0，不在每个材质中复制。
此阶段需要补齐浮点图像加载、cubemap/分层图像及相应 view 支持；环境预计算在加载阶段进行一次，
运行时只采样。首版限定单张全局环境图，包含必要的坐标朝向、曝光和资源生命周期约定。

## 分阶段实施与验收

| 阶段 | 主要改动 | 可验收结果 |
|---|---|---|
| 1. 材质数据通路 | 独立 CPU 材质定义、GPU UBO、set 2、替代纹理、SRGB/UNORM 上传、旧接口适配 | 同一 mesh 可使用不同参数的材质；材质共享与释放通过 validation |
| 2. 直接光与 HDR | 完整 GGX BRDF，接入原光源/阴影，HDR 附件、resolve、色调映射 | 金属度/粗糙度球阵可辨；高亮不提前裁剪；所有现有光源正常；独立深度预览通过 GPU 测试 |
| 3. 完整贴图 | MR、emissive、AO 采样，tangent/TBN 和 normal scale，纹理采样器配置 | 各通道和 factor 生效；平坦法线、镜像和非均匀缩放正确；AO 随间接光接入验证 |
| 4. glTF 静态材质 | primitive→材质、纹理/sampler 引用、UV0/UV1、切线及节点变换 | 导入一个含多材质 primitive 的静态模型，外观与属性符合支持范围 |
| 5. IBL | HDR 环境加载、cubemap、irradiance、镜面预过滤、BRDF LUT、AO 接入间接漫反射 | 无直接光时金属反射环境；粗糙度增加使反射变宽；环境旋转与朝向正确 |

阶段 1 → 2 → 3 是主干；阶段 4、5 均建立在此基础上，可以分别验收。
阶段 2 起使用独立的小型材质展示场景，不依赖大陆地形来判断 BRDF 是否正确。

glTF 首轮支持静态、不透明、单面的 metallic-roughness 材质，支持普通解码图像及材质所需 UV 集。
每个 primitive 先对应独立 `GpuMesh` 和 `Material` 引用，复用现有 `DrawItem`，共享实例复用同一份几何。
这样不会为了一个 primitive 的材质重复绘制整个 mesh，也能沿用已有 BLAS/TLAS 粒度。
所选范围之外的 alpha 模式、双面材质、必要扩展及纹理格式须明确报出限制。

MASK/BLEND 需要后续同时设计主通道、深度预览、ray-query 遮挡及时间历史的材质语义。
尤其 MASK 不能只在主 fragment 中 discard，否则阴影仍将整张面视为不透明。
透明排序、clearcoat、transmission、动态反射探针、完整面积光和全局光追不属于本轮。

## 验证与主要改动位置

材质展示场景包括 metallic × roughness 球阵、纹理通道色块、法线平面、
镜像/非均匀缩放实例，以及无灯的发光物体。提供 base color、metallic、roughness、normal、AO、
直接光和间接光的调试输出，参数和纹理错误能分别定位。

有针对性的验证包括：

- CPU：材质默认值、参数合法性、图像色彩空间缓存键、glTF primitive/材质/UV 引用和切线手性。
- GPU 离屏：实际 shader 的 MR 通道、sRGB 解码、factor 乘法、默认贴图、HDR 值和色调映射，
  验证掠射角与粗糙度端点没有 NaN/Inf。使用可计算的输入与容差，不依赖跨显卡逐像素截图相等。
- 环境照明：常量环境的参考结果、prefilter 粗糙度变化和 AO 只影响约定的间接项。
- 集成：多帧、resize、独立深度预览与正常光照往返、材质共享及销毁；运行现有阴影路径和时间阴影 GPU 测试，
  同时开启 validation 与 synchronization validation，要求无验证错误。
- 构建：Linux Debug/Release 的 C++ 与 Slang；改动导入或依赖时补 Windows 交叉构建。
  CPU CTest 与需要真实 GPU 的测试继续分开运行。

主要改动落在 `scene/` 材质定义、`vk/render/material.hpp`、`vk/render/render-data.hpp`、
`vk/resources/gpu-texture.*`、`vk/resources/sampler.hpp`、`vertex.hpp`、`vk/render/pipeline.*`、
`vk/render/renderer.*`、`vk/render/frame-resources.hpp` 和 Slang 材质/后处理模块。
阶段 4 再接入现有 glTF/scene graph 草稿；阶段 5 扩展 GPU 图像能力及环境资源。
每阶段实施后更新 `docs/lighting.md`，区分已实现行为与后续目标。
