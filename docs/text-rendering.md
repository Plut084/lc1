# 文字渲染：FreeType 字形图集与世界／界面文本

世界里的展牌文字和屏幕上的 HUD 都由同一条文字通路绘制，不再用 `stb_easy_font` 生成四边形
再走不透明 PBR 材质。文字只有一处进出 GPU 的方式：一个混合（alpha blending）的文字 pass。

## 三层结构

| 层 | 位置 | 职责 |
|---|---|---|
| CPU 文字层 | `include/lc1/text/`、`src/text/` | `decode_utf8`、`GlyphAtlas`（FreeType 光栅化 + 图集）、`measure_text`／`append_text`（字距、换行、摆放），完全不依赖 Vulkan |
| GPU 资源 | `include/lc1/vk/resources/gpu-font.hpp` | `GpuFont`：CPU 图集 + `GpuTexture` + 钳位采样器设置 |
| 绘制 | `include/lc1/vk/render/text-pass.hpp`、`shaders/text.slang` | `TextPass`：布局、逐帧顶点缓冲、世界文字与界面文字两次绘制 |

`GpuFont` 只负责上传：构造参数是一个已经光栅化好的 `GlyphAtlas`（CPU 资源），和
`GpuTexture` 接收 `Image` 的分工一致——路径、字符集、像素尺寸都属于 `GlyphAtlas` 的构造参数。
`GpuFont` 直接构造，不经 `ResourceManager`：文字是渲染线程独占的资源，
而 `ResourceManager` 目前在本仓库中未被使用。

## 图集

`GlyphAtlas` 在构造时打开 `.ttf`，用固定像素尺寸（默认 48px em）光栅化固定字符集
（默认可打印 ASCII），按行货架式打包进一张 RGBA8 位图：RGB 恒为 255，覆盖率放在 alpha。
每个字形记录 `uv_min/uv_max`、位图尺寸、`bearing`（从笔位到位图左上角，y 向上）和 `advance`，
另存 `line_height`、`ascent`、`descent` 以及 `FT_Get_Kerning` 得到的成对字距。

- 采样固定在 mip 0：`GpuTexture` 总会生成 mipmap，图集一旦降到 mip 0 以上，相邻字形就会互相
  渗透。`GpuFont::sampler_info()` 因此返回钳位到边缘、`maxLod = 0` 的采样器，shader 里用
  `SampleLevel(..., 0)` 采样。
- 字形之间留一个像素空白，四周覆盖率都是 0，线性过滤不会把邻居的覆盖率带过来。
- 未映射的码位（字体没有该字形，或不在字符集内）不产生条目，也不占位：布局直接跳过，笔位不前进。
- 空格有度量没有位图，会推进笔位但不产生几何。

## 布局与摆放

`append_text` 把 UTF-8 文本变成 `lc1::Vertex` 四边形（每个字形 6 个顶点，三角形列表，无索引缓冲），
和网格顶点用同一套顶点布局与卷绕方向：正面朝平面的 +z，逆时针，与旧的展牌四边形一致，
所以背面剔除的行为不变。`TextPlacement` 给出平面原点（x、y 在平面内，z 是平面深度）、每图集像素
对应多少调用方单位、颜色和是否水平居中；`origin` 落在第一行的 ascent 线上，也就是首行顶部，
后续行沿 -y 向下排列——展牌坐标一直是以"文字顶部"为锚点调的，保持这个约定不用重调数值。

默认平面为 xy 平面（右为 +x、上为 +y），`WorldTextItem::normal` 可以指定任意正面方向：

- 默认 `{0, 0, 1}`，兼容现有展牌；非单位法向在布局时归一化，零向量及非有限值报错。
- 文字上方采用世界 +Y 在平面上的投影；当法向与 +Y 近乎平行
  （归一化后 `abs(normal.y) > 0.999`）时改用 +Z，保证朝上／朝下的文字也有确定朝向。
- `center` 仍是首行顶部的水平中心；高度、宽度限制及换行均沿文字自身平面计算。
- 法向只确定朝向，不支持任意绕法向的滚转；跨过备用方向阈值时方向会切换，
  不适合用作需要连续旋转的朝向插值。


- 世界文字：传世界坐标，缩放是世界单位／图集像素，绘制时乘相机的 view-projection。
- 屏幕文字：传像素坐标，原点在**左下角**，缩放是像素／图集像素。界面 pass 的正高度 viewport
  配上 `y_ndc = 1 - 2y/H` 的变换，正好和场景 pass 的负高度 viewport 对应，两边卷绕方向一致。

`measure_text` 用同一套笔位算术（含字距）返回宽度、行数与高度，调用方据此决定缩放
（例如展牌 `height` 与 `max_width` 之间取小）。

## 绘制通路

| pass | 输入 | 输出 | 说明 |
|---|---|---|---|
| HDR lighting（末尾） | — | `hdr.color` | 场景不透明绘制结束后，用同一渲染实例的深度附件混合绘制世界文字 |
| UI text | `output.color` | `output.ui_text` | 色调映射之后，只做混叠，无深度 |

世界文字**不单独开一个渲染实例**：HDR 的 MSAA 颜色附件带
`VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`、`storeOp = DONT_CARE`，规范对这种惰性分配内存的
说明是"渲染通道实例结束后就不再需要"，第二个实例用 `loadOp = Load` 重新打开它并不成立。
留在同一个实例里则不需要第二次 resolve、不需要改 storeOp、不需要跨实例屏障，而且直接复用
场景深度——文字被几何体遮挡，和普通场景内容一样参与色调映射。

界面文字在色调映射之后绘制（和 ImGui overlay 同位置）：先发一条
`COLOR_ATTACHMENT_OUTPUT` 写→读写的内存屏障，再以 `loadOp = Load` 打开输出目标，
变换是像素到裁剪空间的正交矩阵。应用把 ImGui pass 的输入从 `output.color` 改成
`output.ui_text`，两者顺序因此由资源依赖固定，而不是靠注册顺序。

## 描述符与缓冲预算

- 一帧只支持**一个字体**：帧槽的 descriptor heap 里只有一组 atlas 图槽和一个钳位采样器槽。
  `TextFrameInput` 只有一个 `font` 指针，从类型上就写不出同帧两种字体，而不是靠注释提醒。
- 世界文字和界面文字各有一个帧槽内的 host-visible 顶点缓冲（`world_text_vertices`、
  `screen_text_vertices`）。两者不能共用：同一帧录制期间两次上传会覆盖同一块内存，
  而 GPU 要等整个帧录制完才执行。缓冲按当帧顶点数增长，重建发生在帧槽 fence 之后。
- 顶点缓冲不是描述符，不占 heap 容量；heap 的采样器容量从 `5N+1` 提到 `5N+2`（字体采样器），
  图集用原有的图槽余量。

## 字体文件

运行时的字体放在 `runtime/assets/fonts/`（例如 `LiberationSans-Regular.ttf`），和其余素材一样不入库，
`copy_assets` 会把它复制进构建树。`GameSession` 用
`asset_root / "fonts/LiberationSans-Regular.ttf"` 构造 `GlyphAtlas`，换字体只需换这个文件
（或改这一行常量）。
CPU 测试 `lc1_font_tests` 读同一个路径，文件不存在时打印一行说明并跳过——CI 里 `runtime/assets/` 为空。

## 验证记录（2026-10-11）

本机 Linux Clang Debug、RTX 4060 Laptop：

- 应用、CPU 测试与 GPU 测试目标全部构建成功，无新增警告；`clang-format` 通过。
- CPU CTest：`font_layout`（图集度量、字距、换行、居中、缩放、UTF-8 容错）通过，
  其余既有测试通过（`save_data` 的失败来自 saves/01 与 save.cpp 的未提交改动，与本次无关）。
- GPU：`lc1_pbr_tests`（新增"展牌文字进入场景"对照）、`lc1_shadow_path_tests`、
  `lc1_temporal_shadow_tests`、`lc1_descriptor_heap_tests` 全部通过，验证与同步验证无消息。
- `make run`：展牌文字为真实字形、按世界坐标透视缩放，走到黑色框架后方时被正确遮挡；
  左下角 HUD 显示 `<角色名> | <区域> / <地点>`；ImGui 仍绘制在 HUD 之上；
  validation 与 synchronization validation 全程无消息。
- 未验证：Windows 交叉构建、Release、其他 GPU 与远端 CI。
