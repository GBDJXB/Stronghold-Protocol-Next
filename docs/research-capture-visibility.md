# 截图路线共有的可见性与新鲜度边界

研究日期：2026-11-13。依据当前 [需求基线](capture.md)、Windows 官方文档与 Chromium 的实现说明。仅检查文档、源码及本机 SDK 头文件是否存在；未枚举实际游戏窗口、启动采集、改变权限或修改截图实现。本文是选型研究，不是已经采用的设计。

## 结论

截图后端、窗口条件检查、采集准入调度和存档是不同职责。更换像素采集工具不能自动满足“任意客户区遮挡即不可见”“目标自身完全不透明”“源帧年龄可确认”等要求。现有 `IsWindowVisible`、虚拟桌面外接矩形和请求起止时间不足以证明这些条件。下列处理方向均待用户选型后设计，不代表本组件已经实现。

## 可见性检查能证明什么

| 检查 | 一手证据 | 对当前需求的意义与限制 |
| --- | --- | --- |
| 显示与最小化 | `IsWindowVisible` 检查自身及祖先的 `WS_VISIBLE`；完全被遮挡仍可返回非零。[V1] | 需要独立处理最小化、遮挡，不能将该布尔值解释为客户区完整可见。 |
| DWM 隐藏与边界 | `DWMWA_CLOAKED` 可查询被隐藏的合成窗口；`DWMWA_EXTENDED_FRAME_BOUNDS` 提供屏幕空间窗口边界。[V2] | 可辅助过滤实际不显示的窗口与窗口外框；目标仍按客户区判断，不能把外框、阴影简单等同于客户区覆盖。 |
| 多显示器覆盖 | `EnumDisplayMonitors` 可获取显示器矩形。[V3] | 应按实际显示器区域覆盖判断；虚拟桌面包围矩形不能识别显示器之间的空隙。本文不实测当前拓扑。 |
| 遮挡物区域 | `GetWindowRgn` 返回由 `SetWindowRgn` 定义的可绘制区域；坐标相对窗口原点，且 `ERROR` 也可能表示没有设置区域。[V4] | 区域可以缩小候选覆盖范围，但无法从查询失败直接推导窗口没有遮挡。矩形相交只是保守判断，不能证明像素级遮挡。 |
| 全局透明度与色键 | `GetLayeredWindowAttributes` 返回 alpha 与 color key；对由 `UpdateLayeredWindow` 设置的 layered window 会失败。[V5] | alpha=255 仅在对应模式下有意义；色键、逐像素 alpha 等不能用一个全局 alpha 全面判断。不能将失败解释为完全不透明。 |

Chromium 会按窗口层次计算遮挡，但其目标是发现**完全遮挡**并减少渲染。其文档明确忽略透明、浮动、非矩形等窗口，允许部分漏判。这不符合 Next 的“客户区任意部分被可见窗口覆盖即不可见”，不能整体搬用该忽略列表；它只证明成熟项目也需要单独实现和限定遮挡检测。[V6]

可行的组件方向是：对已知不可见返回具体原因；对无法判断的透明/覆盖形式明确返回未知，避免把“没有发现异常”写成“已证明无遮挡”。如何处理未知状态须在实现方案中明确，不能无声降低需求，也不能许诺 Win32 矩形枚举能准确理解所有 DirectComposition、逐像素透明和系统覆盖层。需求已经允许离散检查之间的短暂异常漏检；这个时间窗口限制与未知覆盖形式是两个不同限制。[V1][V4][V5][V6]

## 新鲜度不是请求完成时间

WGC `Direct3D11CaptureFrame.SystemRelativeTime` 是合成器渲染该帧时的 QPC 时间，可与同一时间基准下的当前时间比较。它不是 UTC 请求结束时间，也不是游戏逻辑更新或动画事件时间。获取时间戳的类型与单位必须正确换算，不能直接把不同单位数值相减。[F1]

因此，“没有新帧”“获得年龄超过 1 秒的帧”“缺少源时间戳”应与“当前像素恰好相同”分开。具体后端能否在静止窗口持续提供可确认的新帧，需要按所选实现和实际环境验证；不能通过更新缓存读取时间把旧帧重新标成新帧。即使源帧足够新，在 PNG/元数据交付前也可能经过较长编码或存盘，交付时年龄仍需要考虑。[F1][本地需求]

## 不应被塞入基础截图组件的职责

- 基础截图候选职责：窗口条件检查、采集开始准入（至少 500 ms、忙时拒绝）、图像及源时间信息、每次成功采集的 PNG/元数据存档、耗时与资源观测点。
- 周期策略与待识别队列是调用侧/pipeline 的工作；容量 3 清空旧队列不等于截图请求排队。
- 连续失败 3 次暂停代打、记录断点、显式恢复及恢复后的游戏状态重确认属于完整 pipeline，不能在选型时默认加入截图工作进程。
- 不重新增加截图 SHA-256、容量删除策略或加载动画识别；吞吐、内存、游戏帧率影响均是待测目标，不能凭选用某 API 宣称达标。

以上职责划分来自当前需求文本，是供选型讨论的边界整理，不是新增需求。[本地需求]

## 本机可核验项与未验证项

本机 Windows SDK `10.0.26100.0` 中存在 C++/WinRT `Windows.Graphics.Capture.h`、`windows.graphics.capture.interop.h`、D3D11 及 DXGI 1.2 头文件。只能证明已有编译接口材料，不能证明完整样例已编译、运行时 API 支持、真实游戏捕获成功或性能达标。

当前游戏 `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` 拒绝访问仍是独立兼容性问题。允许管理员运行是已确认的可选执行条件，不是根因证据，也不保证提权后访问成功；换后端也不会自动绕过现有前置检查。此次未验证目标 token、ACL 或其他访问限制。

## 来源

- [本地需求] [capture.md：已确认的后续需求基线](capture.md)。
- [V1] [Microsoft：IsWindowVisible](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-iswindowvisible)。
- [V2] [Microsoft：DWMWINDOWATTRIBUTE](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute)。
- [V3] [Microsoft：EnumDisplayMonitors](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaymonitors)。
- [V4] [Microsoft：GetWindowRgn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrgn)。
- [V5] [Microsoft：GetLayeredWindowAttributes](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getlayeredwindowattributes)。
- [V6] [Chromium：Windows Native Window Occlusion Detection](https://chromium.googlesource.com/chromium/src/+/master/docs/windows_native_window_occlusion_tracking.md)。阅读的是移动分支，具体代码复用前应固定提交。
- [F1] [Microsoft：Direct3D11CaptureFrame.SystemRelativeTime](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframe.systemrelativetime)。
