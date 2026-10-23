# 文字渲染：FreeType 字形图集与世界／界面文本：历史验证

结果仅适用于记录中的代码状态与环境，不代表当前版本已通过验证。构建日志可能已清理。

## 验证记录（2026-10-13）

本机 Linux Clang Debug、RTX 4060 Laptop：

- 全部 11 个 CPU CTest 通过。`font_layout` 新增的检查：基线字符集仍只含 ASCII（中文名字宽度为 0，
  也就是修复前的症状）、请求 `啥句冲个` 的四个码位后每个都有字形／位图／advance 和 6 个顶点、
  重复码位不放大图集。
- `./lc1`（构建树 + `generators/conanrun.sh`，等价于 `make run`）：世界里画出 `啥句冲个 from Empire`，
  同时 `lc`、`yyx` 的标签照常显示；运行期间没有 validation／synchronization ERROR（ERROR 会 abort）。
  截图 `build/linux-x86_64/Debug/label-window.png`（构建产物，未入库）。
- 未验证：Windows 交叉构建、Release、其余 GPU 测试目标（`lc1_pbr_tests` 等）与远端 CI。
  字体来自 `~/.local/share/fonts/SourceHanSans/CN/SourceHanSansCN-Regular.otf`，`runtime/assets/` 未入库。

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
