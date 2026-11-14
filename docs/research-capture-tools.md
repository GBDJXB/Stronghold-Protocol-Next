# Windows 窗口截图候选调研

研究日期：2026-11-13。范围是成熟 Windows C++ 截图路线与工具，不改实现、不启动游戏、不做实机截图，也不选定方案。项目需求以 [capture.md](capture.md) 为准：后台但完整可见的窗口、不得置前或改几何/透明度、保留客户区原尺寸、确认源帧新鲜度，并支持本机 DPI 与多显示器。

## 结论摘要

值得保留比较的路线包括 **原生 WGC**、**ScreenCaptureLite**、**OBS Window Capture 的 WGC 实现**，以及可用 Rust FFI 包装的 **windows-capture**。WGC 的 `SystemRelativeTime` 是 compositor 帧 QPC；DXGI Desktop Duplication 也提供每个显示输出的 `LastPresentTime` QPC，所以时间戳并非 WGC 独有。两者都不是游戏逻辑更新时间。ScreenCaptureLite 当前 Windows 窗口实现用 `PrintWindow`，失败后再 BitBlt，并修改系统动画设置，需特别审查。

FFmpeg `gdigrab` 有按 HWND/标题捕获窗口的实现，但其 PTS 是媒体采样时钟，不能当作窗口源帧时间。`ddagrab` 是显示器输出捕获，不是窗口采集；如果独立可见性检查先证明客户区完全可见，它仍可作为候选，但须自行裁切、处理跨显示器和坐标尺度，且 FFmpeg 过滤器不暴露 DXGI 的 `LastPresentTime`。本文不据此替项目决定后端。

## 需求对照

| 候选 | 后台窗口与遮挡 | 源帧时间 / 静止帧 | 客户区尺寸、DPI、多屏 | C++ 接入与工程代价 |
|---|---|---|---|---|
| **原生 WGC：`IGraphicsCaptureItemInterop::CreateForWindow` + `Direct3D11CaptureFramePool`** | API 直接以 HWND 建立单窗口 GraphicsCaptureItem，不需把窗口映射到桌面矩形。系统 API 未提供“窗口完整可见/无遮挡”判据，仍须项目侧独立检查；该检查是否能可靠识别透明、逐像素遮挡需另作设计与验证。没有找到 API 要求置前、移动、缩放或改透明度的证据。 | `SystemRelativeTime` 是 compositor 渲染该帧时的 QPC 时间；`ContentSize` 是该帧渲染时的内容尺寸。可与当前 QPC 比较帧龄。它不是游戏逻辑更新时刻；对画面内容不变时是否得到可作新鲜度证明的持续新时间戳，须按 Windows 版本及游戏实测，不能将重复像素等价为陈旧，也不能只凭调用结束时间判新。 | `ContentSize` 可用于逐帧核对/适配帧尺寸。直接窗口捕获绕过桌面矩形坐标换算，理论上减少混合 DPI 坐标误差；仍要处理窗口跨屏 DPI/尺寸改变和 frame-pool 重建。多显示器和目标游戏的尺寸行为未实测。 | Win32 原生 API，Windows 10 1903 起支持 `CreateForWindow`；C++/WinRT、D3D11、Windows SDK，需处理 WinRT 初始化、帧池/设备丢失和同步。官方文档与 Microsoft 员工维护的 Win32 示例可作起点；它是示例而非稳定、独立发布的通用库。维护责任主要在本项目。 |
| **ScreenCaptureLite (`smasherprog/screen_capture_lite`)** | Windows 窗口路径通过 `GetWindowDC` 后调用 `PrintWindow(PW_RENDERFULLCONTENT)`；失败时回退到 BitBlt。项目没有“完全无遮挡”判断接口，不能保证回退像素符合可见性条件。源码初始化窗口采集时调用 `SystemParametersInfo(SPI_SETANIMATION, ... SPIF_UPDATEINIFILE | SPIF_SENDCHANGE)` 修改全局动画设置；原样使用前必须确认恢复路径。 | 回调给图像和 Window/Monitor 描述，未见源帧时间戳字段；`onNewFrame` 按采样间隔回调，`onFrameChanged` 按图像差异触发，均不能证明源帧年龄。静止画面重复采样不能作为源时间证据。 | 窗口路径按窗口客户区大小构造输出，尺寸变化会触发重建。仓库有 DPI/多显示器问题报告，但不是当前缺陷证明；目标机行为未实测。 | 原生 C++17、CMake 3.16+，仓库称无 Windows 外部依赖，MIT。README 明确 master 不稳定、应使用 tags；项目自称跨平台窗口/屏幕捕获库。源时间戳缺失和全局系统设置副作用是原样使用的主要排除理由，具体版本仍须固定 commit 核验。 |
| **OBS Studio Window Capture (WGC 模式)** | 当前上游源码实现 WGC 与旧 BitBlt 两模式；目标 HWND 按窗口匹配器选取。源码明确在窗口最小化或不可见时跳过 WGC 初始化，但没有证明任意部分遮挡、部分离屏均被拒绝。未见采集前置前/改窗口几何路径；这项需固定版本源码全路径复查后再作为约束结论。 | OBS 暴露渲染纹理/宽高和 libobs 视频管线时间，不是 WGC `SystemRelativeTime` 的公开稳定截图记录接口；不能把 OBS 输出/编码时间戳直接解释为源 compositor 帧时刻。对静止内容的新鲜度同样没有项目可直接消费的保证。 | 可选客户区捕获、源尺寸按窗口客户区取值；代码带 DPI awareness 上下文适配（旧 BitBlt 分支）。WGC 窗口模式尺寸改变会重建采集。需要将 OBS 的渲染/色彩处理链与“原始像素”定义区分。多屏混合 DPI行为仍需目标机验证。 | C/C++ 工程成熟、活跃；完整 OBS Studio 为 GPL-2.0-or-later。先前把 Qt 当作 libobs 构建必需依赖的说法证据不足：GUI 应用与单独 libobs/插件构建的实际依赖边界尚未核实，列为未知。独立嵌入成本须基于实际目标和固定构建选项核算，不能直接套整套 OBS 工具链。 |
| **windows-capture (`NiiightmareXD/windows-capture`)** | Rust 库提供 WGC，并从 2.0.1 起另支持 DXGI Desktop Duplication；README 示例使用 picker 选屏幕/窗口。未见完整可见、遮挡或透明度检查契约，仍需独立检查。 | WGC `Frame::timestamp()` 直接读 `Direct3D11CaptureFrame::SystemRelativeTime`；DXGI 示例可访问 `frame_info().LastPresentTime`。分别是窗口 compositor 和显示输出更新 QPC，不是游戏逻辑更新时间。静止画面和实际更新时间语义仍需目标系统核验。 | WGC 帧暴露宽高；DXGI 路径以显示器为对象，若要窗口需自行裁切。DPI/多屏按所选 API 处理，未实测。 | Rust crate 2.0.1 有 docs.rs 文档、MIT，仓库同时提供 Python 绑定。未发现官方 C ABI；可写 Rust `cdylib`/C ABI 适配供 C++ 调用，但会引入 Rust 工具链、跨语言生命周期/错误处理、打包和适配层维护成本。GitHub Releases 页面无发布条目，crate/docs.rs 版本可锁定。 |
| **FFmpeg `gdigrab`** | 官方文档支持 `title=` 与 `hwnd=` 单窗口输入，称窗口位置无关。源码对 HWND 调用 `GetDC(hwnd)`、以客户区矩形 BitBlt；它没有检查客户区是否被别的窗口遮挡，文档/代码未建立遮挡时的像素语义保证，因此不能满足“只有完整可见才采集”的验收条件；没有证据显示它主动置前。 | 源码设置包 PTS 为 `av_gettime()`，即采集时刻，而非被捕获窗口帧生成时刻；故无法证明内容新鲜度。静止画面仍按 framerate 重复采样。 | 默认全客户区尺寸；源码通过 `GetClientRect` 得尺寸，并含设备像素尺度换算；混合 DPI 下窗口跨屏尺寸和映射仍需核验。 | FFmpeg C API/C++ 可链接，但要选定构建配置并管理 DLL/编解码器与 LGPL/GPL 合规；若只调用 gdigrab，需确认自建发行版是否启用了设备。对只需一张 PNG 的组件而言，媒体输入、时间基和格式转换接口偏重。 |
| **FFmpeg `ddagrab`** | Desktop Duplication API 捕获指定显示器输出，不接受窗口 HWND；窗口需独立定位并裁切。若外部可见性检查已证明目标客户区完全可见，桌面裁切仍可能满足采集前提，所以不是因存在遮挡就直接淘汰。 | Microsoft DXGI `LastPresentTime` 是该显示输出桌面图像最近更新的 QPC，零值表示自上次取帧后未更新。FFmpeg 内部读取并检查该字段以过滤无更新帧/保留旧纹理，但 AVFrame 时间戳按 framerate 与 `av_gettime_relative()` 形成媒体时间轴，没有将 per-output `LastPresentTime` 作为可消费来源 timestamp 输出；时间粒度亦是显示输出，不是窗口。 | 输出为单个显示器画面；跨屏客户区需合并多个输出，部分离屏时无法完整提供目标矩形。需自行维护窗口/显示器坐标变换、裁切和可见性检查；分辨率切换需处理输出会话变化。 | 使用 FFmpeg D3D11 filter 与 DLL/构建配置；比窗口绑定采集多一层裁切/合屏逻辑，许可证按确切构建审查。对于目标窗始终完整落在单一显示器的情形可作为候选，但通用性较低。 |

## 候选筛选补充

- **ScreenCaptureLite (`smasherprog/screen_capture_lite`)** 是真实的 C++17 CMake 小库，MIT 许可，已纳入比较。上游 README 建议按 tag 而不是不稳定的 master 锁定；本文读取了当前 master 的 Windows 实现，不能据此声称所有历史版本都有相同副作用。
- **windows-capture (`NiiightmareXD/windows-capture`)** 是 Rust 与 Python WGC/DXGI 库，不是同名 .NET 包。其 Rust API 有 WGC `SystemRelativeTime` 和 DXGI `LastPresentTime` 封装；没有发现原生 C ABI。Rust `cdylib` 适配是可行的工程推论，需额外维护，不是该库当前提供的稳定 API。
- **timestamp 不是 WGC 独有**：Microsoft DXGI `DXGI_OUTDUPL_FRAME_INFO.LastPresentTime` 给出每个输出桌面图像上次更新时的 QPC；粒度是整个输出，不是某个窗口。不能把 FFmpeg 的 `AVFrame` PTS 自动等同于该字段。
- **WGC 样例定位**：`robmikh/Win32CaptureSample` 是 Win32 C++ sample，不是有稳定 ABI 和承诺兼容性的通用库。可用作 API 接入和版本适配参考，不应把 sample 的成熟度等同于项目可直接依赖的生产级封装。

## 需要单独验证的边界

1. 记录 WGC `SystemRelativeTime`、DXGI `LastPresentTime` 与当前 QPC 的单位换算、来源时钟和图像粒度；区分 compositor/输出图像时间与游戏内容状态更新时间。
2. 不要把“WGC 返回目标窗口画面”当成“窗口完全可见”。隐藏/最小化、任意像素遮挡、离屏、透明度应由独立可见性层判定；WGC 自身的单窗口像素源不等于可见性证据。
3. 在项目目标机对窗口/无边框、实际 DPI、混合 DPI 多屏、窗口跨屏、静止画面、遮挡/部分离屏与 resize 做验证。本文未操作游戏，未提供性能、资源占用、稳定性或兼容性实测结论。

## 一手来源

- Microsoft：[`IGraphicsCaptureItemInterop::CreateForWindow`](https://learn.microsoft.com/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow)（HWND 单窗目标；Windows 10 1903 起）。
- Microsoft：[`Direct3D11CaptureFrame.SystemRelativeTime`](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframe.systemrelativetime)（compositor 渲染帧时 QPC）；[Screen capture 指南](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)（FramePool、ContentSize、SystemRelativeTime、帧生命周期）。
- Microsoft 工程师维护的 [Win32CaptureSample](https://github.com/robmikh/Win32CaptureSample)；[版本发行记录](https://github.com/robmikh/Win32CaptureSample/releases)（v1.6.2 发布记录显示 2026-05 的 HDR/JXR 修复，v1.5.0 加入 Windows 11 26100 功能）。
- Microsoft：[DXGI_OUTDUPL_FRAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_outdupl_frame_info) 的 `LastPresentTime`（桌面图像更新 QPC；零值含义）。
- ScreenCaptureLite：[README](https://github.com/smasherprog/screen_capture_lite/blob/master/README.md)（C++ API、帧回调、master/tag 稳定性说明）；[CMakeLists.txt](https://github.com/smasherprog/screen_capture_lite/blob/master/CMakeLists.txt)（C++17、CMake 3.16、版本 17.1）；[MIT LICENSE](https://github.com/smasherprog/screen_capture_lite/blob/master/LICENSE)；[`GDIFrameProcessor.cpp`](https://github.com/smasherprog/screen_capture_lite/blob/master/src_cpp/windows/GDIFrameProcessor.cpp)（系统动画设置、PrintWindow 与 BitBlt 回退）；[public Image 定义](https://github.com/smasherprog/screen_capture_lite/blob/master/include/ScreenCapture.h)（回调图像结构无 timestamp 字段）。链接指向滚动 master，实施前需锁 commit 并复查恢复路径。
- windows-capture：[仓库 README](https://github.com/NiiightmareXD/windows-capture)（Rust/Python、v2.0.1、WGC/DXGI、MIT）；[2.0.1 docs.rs Frame API](https://docs.rs/windows-capture/2.0.1/windows_capture/frame/struct.Frame.html)；[当前 Frame 源码](https://github.com/NiiightmareXD/windows-capture/blob/main/src/frame.rs)（`timestamp()` 调用 `SystemRelativeTime`）；[GitHub Releases](https://github.com/NiiightmareXD/windows-capture/releases)（当前无 release 条目）。
- FFmpeg：[gdigrab 官方设备文档](https://ffmpeg.org/ffmpeg-devices.html#gdigrab)；[gdigrab 源码](https://github.com/FFmpeg/FFmpeg/blob/master/libavdevice/gdigrab.c)（hwnd/title、GDI BitBlt、PTS 来源）；[ddagrab 源码文档](https://ffmpeg.org/doxygen/trunk/vsrc__ddagrab_8c.html)（读取 `LastPresentTime`、PTS 使用相对采样时钟、更新缺失时复用旧帧）；[FFmpeg 许可说明](https://ffmpeg.org/legal.html)。源码链接指向滚动分支，未固定到本次某一 commit，需在真实候选版本评审时锁定。
- OBS：[Window Capture 源码 v33.0.0](https://github.com/obsproject/obs-studio/blob/33.0.0/plugins/win-capture/window-capture.c)；[WGC 实现](https://github.com/obsproject/obs-studio/tree/33.0.0/libobs-winrt)；[许可证文件 v33.0.0](https://github.com/obsproject/obs-studio/blob/33.0.0/COPYING)；[v33.0.0 发布记录](https://github.com/obsproject/obs-studio/releases/tag/33.0.0)（2026-10-23）。

## 证据边界

源码/文档事实、根据采集 API 形态作出的遮挡与集成成本推论、尚未验证的目标机行为在正文分别说明。没有证据支持本地游戏对任一候选的采集兼容性，也没有证明任一候选能单独完成项目定义的可见性判定。本文只供父级设计整合，不构成选型。
