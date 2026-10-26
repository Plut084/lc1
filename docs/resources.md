# 资源加载与借用生命周期

当前游戏表现对象直接构造资源，`ResourceManager` 尚不是统一加载入口；见[模块依赖图](module-dependencies.md)。
本页规定资源接口的所有权与卸载约束，应用成员顺序见[代码边界](code-layout.md#生命周期与资源)。

- `ResourceManager` 按场景独占，只在渲染线程使用。类型缓存以 `unique_ptr<Resource>` 拥有资源；
  重复加载返回稳定指针，失败不得留下空缓存项。
- `load<T>` 要求 T 派生自 Resource、可移动构造，并提供
  `load_from_file(Device const&, path const&) -> T`。网格仍使用独立缓存。
  `GpuTexture` 上传 CPU Image，不是文件加载 Resource；ResourceManager 不依赖 Renderer。
- 资源 ID 相对于指定 asset root 词法规范化，拒绝绝对路径和目录逃逸；不合并符号链接别名。
- 卸载会立即使该资源的借用失效，没有引用计数。卸载资源或销毁 manager 前，必须停止使用相关借用，
  并等待使用它的 GPU 工作完成。RAII 销毁本身不等待 GPU。
- `DrawItem`、`scene::Object`、`RenderTarget` 借用资源；资源在成员声明顺序中先于使用者。
  材质所有者保持纹理存活到 GPU 使用结束，Device 比所有 GPU 资源长寿。
- `GpuBuffer::upload` 必须执行所需 VMA flush；描述符使用前完整初始化，绘制容量溢出显式失败。

坐标采用右手系、Y 向上；glTF 无需轴转换，Z-up 数据需要导入校正（教程房间绕 X 轴 −90°）。
校正只在 Model 烘焙一次，不再逐实例应用；法线、镜像与绕序见[角色与模型校正](character.md#模型校正)，
切线与法线贴图见[光照](lighting.md)。
