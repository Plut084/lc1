# Vulkan 最小知识 —— 只讲这个项目用得到的那部分

这份文档不讲 Vulkan 全貌，只讲读懂并能改动 `src/vk/` 所必需的六个概念。每条都对着仓库里
现成的代码讲，行号会随重构漂移，按函数名找。

第 1–4 条代码里已经有了；第 5、6 条（描述符集、管线）**还没有** —— 这一版骨架没有着色器、没有
管线，它们是下一步要建的东西，所以按"会插在哪里"来讲。

---

## 1. Queue / 队列族 —— 命令提交的通道，以及为什么只有一条

**核心模型：GPU 不等你。** CPU 把命令录进 command buffer，然后**提交**到队列，GPU 异步执行。
提交这个动作在 `frame_loop.cpp` 的 `submit2`（`draw_frame` 里，紧跟在 `renderer.record` 之后）。

队列族（queue family）是"能力相同的一组队列"。物理设备报告它有哪几个族，每族有几个队列。
族之间能力不同：图形、计算、传输、稀疏绑定。

**关键点：呈现（present）不是队列的标志位。** 看 `device.cpp` 的
`find_graphics_present_family`：

```cpp
if (!(families[i].queueFlags & vk::QueueFlagBits::eGraphics))
    continue;
if (device.getSurfaceSupportKHR(i, surface) == vk::True) {   // ← 这里
```

能不能呈现，得拿**具体的 surface** 去问 `vkGetPhysicalDeviceSurfaceSupportKHR`，因为同一个
GPU 在不同的窗口系统下答案可能不同。这也是为什么 `Device` 的构造函数必须先拿到 surface 才能
选设备 —— `AGENTS.md` 里"Init order is load-bearing"说的就是这件事。

**为什么只有一条队列。** `Device` 的构造函数里：

```cpp
queue_info.setQueueFamilyIndex(queue_family_)
    .setQueueCount(1)          // 只要一条
    .setQueuePriorities(priority);
```

因为图形和呈现用的是同一个族，一条队列两件事都能干。多队列不会更快（同一个硬件），只会引入
**跨队列同步**：图像所有权转移、额外信号量、额外的同步 bug 面。对这个项目是纯负担。

队列不是你创建的，是**取出来**的：

```cpp
queue_ = handle_.getQueue(queue_family_, 0);   // 族索引 + 族内序号
```

所以 `vk::raii::Queue` 全程序只有一个，归 `Device` 所有 —— `frame_loop.hpp` 的注释特别强调
了这点，别的模块都是**借用** `Device const&`。

---

## 2. Semaphore vs Fence —— 谁在等

两者都在表达"等某件事做完"，区别只有一个：**谁等**。

| | 谁等 | 位置 | 状态管理 |
|---|---|---|---|
| **Fence** | CPU 等 | `waitForFences` | 有 signaled/unsignaled，**必须手动 reset** |
| **Semaphore** | GPU 等 GPU | submit 的等待/信号列表 | 二元，被等到就自动翻转 |

### Fence：CPU 侧的闸门

`draw_frame` 第一件事就是：

```cpp
vk::Result const waited = raii_device.waitForFences(
    *fences_[frame], vk::True, fence_timeout_ns);
```

它阻塞 CPU，直到这一格（slot）上一帧的 GPU 工作做完。做完之后命令缓冲才能安全重录。

注意超时用的是 `1'000'000'000ull`（1 秒）而不是 `UINT64_MAX`。注释写得很清楚：无界等待会让
**死锁和"GPU 很慢"变得无法区分**，而且没有出路。有界超时至少能报错。

### Semaphore：GPU 之间的接力棒

这一帧里有两个信号量，它们的交接完全不经过 CPU：

- **`image_available_[frame]`** —— 由呈现引擎在 `acquireNextImage` 时 signal，被这次 submit
  **等待**。含义："这张交换链图像已经归你了，可以往上画。"
- **`render_finished`** —— 由这次 submit **signal**，被 `presentKHR` 等待。含义："画完了，
  可以拿去显示了。"

`render_finished` 为什么住在 `SwapchainImage` 里而不是帧循环里，`swapchain.hpp` 开头有一大段
注释解释 —— 简单说，交换链图像的实际数量可能比你要的多，且每次重建都可能变。把信号量绑在
图像上，"数组开小了"和"重建时漏了资源"就变成了同一个（不可能写出来的）bug。

### 为什么两个都要 —— `frames_in_flight = 2` 的意义

如果每帧都 `waitForFences` 等 GPU 完全画完才录下一帧，CPU 和 GPU 就是**轮流空转**：CPU 录的
时候 GPU 闲着，GPU 画的时候 CPU 闲着。

所以要两份（`FrameLoop::frames_in_flight`）：CPU 在录第 N+1 帧时，GPU 还在画第 N 帧。fence
保证的不是"整条流水线空了"，而是"**这一格**空了，可以复用"。

### 最容易写死的那个 bug

```cpp
raii_device.resetFences(*fences_[frame]);   // 位置是刻意的
```

fence 一旦 reset 就变成 unsignaled。如果这条路径后面**没走到 submit**，就永远没人 signal 它 →
下一次 wait 死等，**没有任何验证层消息**。所以 reset 只能放在"确定会到达 submit"的那条路径上。
这就是 `AGENTS.md` 四条不变量里的第一条。

另一个相关细节：fence 创建时带 `eSignaled` 标志。第一帧的 wait 不能阻塞在一个从没提交过的
fence 上。

---

## 3. Render pass 与 beginRendering —— 清屏为什么不是一条命令

### 传统 Vulkan 1.0 的三步走

先 `vkCreateRenderPass` 声明"我要几个附件、什么格式、怎么加载/存储"，再
`vkCreateFramebuffer` 把实际图像绑上去，最后 `vkCmdBeginRenderPass`。麻烦在于**附件一变就得
全部重建** —— 窗口一 resize，这三个对象都得重来。这是 Vulkan 最出名的样板代码。

### 这个项目用的是 dynamic rendering

Vulkan 1.3 把它并进核心了。附件信息直接写在**录制时**，不需要 render pass 和 framebuffer
对象：

```cpp
command_buffer.beginRendering(rendering);   // renderer.cpp
```

`Device` 在选设备时校验了这个能力（`device.cpp` 的 `supports_required_features`）：

```cpp
return features13.dynamicRendering == vk::True && ...;
```

这就是为什么改窗口尺寸不需要重建任何管线对象。

### 清屏是附件的属性，不是命令

```cpp
color_attachment
    .setImageView(*target.view)
    .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
    .setLoadOp(vk::AttachmentLoadOp::eClear)      // ← 关键
    .setStoreOp(vk::AttachmentStoreOp::eStore)
    .setClearValue(vk::ClearValue{}.setColor(clear_color));
```

`eClear` 的意思是："这个渲染通道**开始时**，用 `clearValue` 填满这个附件。"
换成 `eLoad` 就是"保留上一帧的内容"。

清屏发生在 `beginRendering` 那一刻，由渲染通道硬件完成，比单独发一条清屏命令快。

**所以 `Renderer::record` 里 0 次绘制调用不是半成品，是完整的**：`loadOp = CLEAR` 就是这一帧
的全部内容。橙色背景就指定在这里（`clear_r/g/b` 三个常量在 `renderer.cpp` 文件开头）。

---

## 4. Image layout —— 为什么每帧要转两次

### layout 是什么

GPU 图像在显存里**不是按行线性排列**的，而是切成小块（tile）并按适合读写的顺序 swizzle 过。
"layout" 就是告诉 GPU："这块内存现在按哪种排布来解释。"

不同用途要求不同排布：

| layout | 什么时候 |
|---|---|
| `eUndefined` | 内容无所谓（刚 acquire 到，要整个覆盖写） |
| `eColorAttachmentOptimal` | 当渲染目标往上画 |
| `ePresentSrcKHR` | 交给呈现引擎显示 —— 系统合成器**只认这个** |

所以一帧两次转换，正好够用：

```
acquire → [UNDEFINED → COLOR_ATTACHMENT_OPTIMAL] → 画 → [→ PRESENT_SRC_KHR] → present
```

### 为什么不能省

转换可能要真的重排一遍显存里的 tile，不是免费的。但**更不能省**，因为：

1. 呈现引擎要求 `ePresentSrcKHR`，不转就是错的。
2. `beginRendering` 不接受 `eUndefined`。`renderer.cpp` 里有一条注释专门标了这个坑：
   `VUID-VkRenderingAttachmentInfo-imageView-06135` 禁止在这里用 UNDEFINED，
   **尽管紧挨着上面的 barrier 合法地用了 UNDEFINED 作为 oldLayout**。这是这条路径上最常见的
   错误。

### barrier 不只是换排布，它同时是同步点

`vk::ImageMemoryBarrier2` 里的 `srcStageMask` / `dstStageMask` 在说："等**之前**的某些阶段
做完，**之后**的某些阶段才能开始。"

`renderer.cpp` 里那段长注释讲的是这个项目踩过的坑：

> acquire → 渲染 的那个 barrier，`srcStageMask` 必须是 `COLOR_ATTACHMENT_OUTPUT`，跟
> submit 等待 acquire 信号量的阶段一致。`VK_PIPELINE_STAGE_2_NONE` 是 sync2 里很诱人的写法
> （"之前的内容无所谓"），而且对着这条 barrier 自己的 VUID 是合法的 —— 但它让这次排布转换
> 相对 acquire **没有顺序关系**，同步验证层会报 `SYNC-HAZARD-WRITE-AFTER-READ`。

这也是 `AGENTS.md` 里那句"'通过验证'不等于'正确'"的由来：普通验证层抓不到，只有同步验证层能。

**一句话记住：layout = GPU 怎么读这块内存；barrier = 换排布 + 顺带把前后阶段的顺序排好。**

---

## 5. Descriptor set —— 着色器怎么拿到贴图和矩阵

> **代码里还没有。** 这是加着色器时要建的第一个东西。

CPU 上的代码直接传参就行，但着色器**在 GPU 上跑，读不到你的内存**。它只能通过**描述符**读：
一个描述符 = "某个绑定点上是一张 2D 贴图" 或 "某个绑定点上是一个 uniform buffer"。

### 三个层次（从模板到实例）

1. **descriptor set layout（模板）** —— 声明"这个管线要几个描述符、分别在哪个 binding、什么
   类型"（`eCombinedImageSampler`、`eUniformBuffer`……）。创建管线时交给它。
2. **descriptor pool（池）** —— 描述符的内存池，创建时给上限（几张图、几个 buffer）。描述符集
   从池里分配。
3. **descriptor set（实例）** —— 真正的那一组。你把具体的 image view + sampler、具体的 buffer
   写进去。

### 设计上的关键决定：一次绑一批

不要"每个单位换一次贴图"。合理的分法是按**变化频率**分组：

- binding 0 = 相机矩阵（每帧变一次）
- binding 1 = 贴图数组（几千个 sprite 共用，用索引取）
- binding 2 = 每实例数据（位置/大小/颜色/贴图编号，走 instance buffer）

这样画几千个单位**不用重新绑定描述符**。这就是后面要做 instancing 的原因 —— 一个 draw call
画完所有单位，而不是几千个 draw call。

### 会插在哪里

`Renderer::record` 里，`beginRendering` 之后、`draw` 之前 `bindDescriptorSets`。矩阵这类数据
由游戏侧每帧填进 DrawList 传进来。

### 为什么它"麻烦但只写一次"

描述符集布局跟**着色器代码是绑死的**（着色器里写 `layout(binding = 0)`）。所以它一写完就固定
下来，之后加功能是往数组里加东西，不是改布局。

---

## 6. Pipeline —— 那一大堆状态是干嘛的

> **代码里还没有。** 跟描述符集一起，是加着色器时要建的第二个东西。

管线 = "GPU 怎么把顶点变成像素"的一整套状态，打包成一个**不可变对象**。对着 RTS 的 2D 需求：

| 状态 | 这个项目怎么用 |
|---|---|
| 顶点/片元着色器 | "把贴图贴到四边形上，乘个颜色" |
| 顶点输入 | 顶点就是四个角的固定四边形；位置/大小走每实例数据 |
| 光栅化 | 填充模式、正面朝向；2D 用不上剔除 |
| 视口/裁剪 | 世界坐标 → 屏幕坐标 |
| 混合 | 2D 的 UI 和透明贴图要 alpha 混合 |
| 深度/模板 | 先用不上，层叠顺序靠绘制顺序解决 |

`VkGraphicsPipelineCreateInfo` 字段多到吓人，但绝大多数都有唯一正确答案，而且这个项目最终也
就只有一两条管线（sprite 一条，UI 可能一条）。**写完就不动了。**

**关键性质：管线是启动时创建、之后 immutable 的。** 运行时想改任何一项，要么换一条管线（管线
切换有开销，别每帧换），要么把这项声明成**动态状态**（`setDynamicStates`）—— 视口和裁剪这种
每帧都可能变的东西适合动态。

---

## 怎么记这六条

- **队列**：命令提交的通道；GPU 异步干活，提交完就走。
- **信号量/栅栏**：等谁 —— 栅栏是 CPU 等（要手动 reset），信号量是 GPU 等 GPU（自动翻转）。
- **渲染通道**：清屏是附件的属性（`loadOp`），不是一条命令。
- **布局**：GPU 怎么读这块显存；换布局同时是一次同步。
- **描述符集**：着色器唯一的读数据方式；按变化频率分组，一次绑一批。
- **管线**：一整套固定的渲染状态，启动时建好就不动。

## 真正该记住的一句话

这份文档看着内容不少，但这六条里**没有一条是每帧都要你操心的**。日常改的东西是：

- 换个清屏颜色 → `renderer.cpp` 开头三个常量
- 加一个要画的东西 → 往 DrawList 里加数据
- 调窗口尺寸/标题 → `src/window.cpp` 的 `glfwCreateWindow`（main.cpp 里已经看不到 GLFW 了）

它们全在 `Renderer::record` 这一层之上或之内的固定位置。Vulkan 的复杂度是**一次性**的，
不是持续性的 —— 这也是为什么这个项目的渲染层值得保持小。
