# MaaFramework v5.15.0 被动截图补丁成本核验

研究日期：2026-11-13。范围：固定 tag v5.15.0 的 Windows FramePool / DXGI Window 采集路径、目标基线中的副作用与 ≤1 秒新鲜度、构建依赖、公开 C API、当前正式版本候选、许可证及本机构建工具。仅静态核验；未构建、运行、操作游戏或修改实现。只新增本报告。

历史研究快照：本文讨论 MaaFramework 补丁方案及其许可证，仅记录当时的评估；当前原生 WGC 实现不依赖 MaaFramework，该项目也不是当前分发依赖。

## 结论

v5.15.0 的“去窗口副作用”与“保证交付帧 ≤1 秒新鲜”是两件事。前者需去掉 FramePool 的伪最小化包装，并改写 DXGI Window 路径中显式置前和移窗调用；后者还需读出 WGC 源帧时间、让图像与时间戳经 API 成对原子返回，并按交付年龄拒收。缓存帧只有保留原始源时间并满足帧龄约束时才可继续作为新鲜画面，不能在复用时重置时间。现有正式 C API 的缓存图像接口不带源帧时间；请求完成时间不能替代。FramePool 最长约 2 秒的取帧等待是请求延迟，延迟和取得帧的年龄是不同量：若等待后取得的新帧本身很新，仍可能满足年龄要求；但较长等待会影响常见的 1 秒交付性能目标，也不能证明返回帧年龄。因需改 Win32 控制单元与时间戳传播链，完整新鲜度补丁不是只删几行的小改动。[S1–S7]

MAA 的默认 FramePool wrapper 启动伪最小化监控线程；最小化时会改扩展样式、alpha 并调用 `ShowWindow(SW_SHOWNOACTIVATE)`。Manager 的 v5.15 源码确认 DXGI Window 候选会创建 `DesktopDupWindowScreencap`。该后端每次截图显式调用 `ensure_foreground_with_cooldown` 及 `ensure_window_on_screen`；失败时只记警告并继续截图。后一个 helper 按需 `SetWindowPos` 移动或缩放窗口。被动补丁须绕开 FramePool wrapper，并从 DXGI Window 路径移除/替换上述调用，确保不再置前或改窗口几何；调用方应独立判定条件不满足时拒绝采集。关闭鼠标/键盘输入无法关闭截图路径中的这些行为。[S1][S2][S3][S3a][S3b]

FramePool 清空排队旧帧后最多等待约 2 秒；若超时但有 `cached_image_`，仍返回其 clone。它没有在返回契约中携带帧生成时间。删掉 fallback 可避免已知的缓存旧帧路径，但仍不能证明成功帧年龄 ≤1 秒；等待时长本身既不能证明满足、也不能证明违反帧年龄要求。较长等待影响交付延迟/吞吐目标。需要按 `Direct3D11CaptureFrame` 的 `SystemRelativeTime` 等源时间戳过滤，并把它和对应像素绑定、跨控制单元/API交付；还要处理时间基准转换、缺失时间戳、旧帧/超时与请求并发关联。不要用 API 请求结束时间冒充源帧时间。[S1][S4][S5]

## 接口与当前版本候选

v5.15.0 正式 API 提供异步 `MaaControllerPostScreencap`、状态/等待查询、`MaaControllerCachedImage` 及原始分辨率查询。CachedImage 语义是取控制器缓存的最新截图；签名输出图像缓冲，不含源帧时间字段，公开头文件也没有配对时间戳查询。它可作为候选被动采集接入点，但不是满足时效证明的接口。[S4][S5]

官方 release 页面显示 v5.16.0 于 2026-11-10 发布，为当前已核验的较新正式版本。其发布说明未列出可配置的被动/无窗口副作用模式或截图源时间戳能力；当前公开集成文档仍将 CachedImage 描述为缓存截图，不提供时间戳。因此新版本可列为后续候选调查对象，但本次没有证据表明升级即可满足本项目约束，也不建议据此自动升级。[S6][S7]

## 真实构建成本与 ABI

- 顶层 CMake 默认构建多种控制器/工具；Windows 下可关闭非目标功能并指定 `MaaWin32ControlUnit` target，但该 target 显式依赖 `MaaUtils`，并链接 OpenCV、ZLIB、Boost.System 以及 Windows D3D11/DXGI/DWM/runtimeobject/winmm。`MaaUtils` 又公开依赖 Boost 与 OpenCV。故不能按“仅编译截图单元、无需 OpenCV”估算；至少须准备对应版本/架构的 MaaDeps 及其构建配置。[S8][S9]
- 官方 Windows CI 对 x64/ARM64 使用 MSVC 2022、Windows SDK 26100，递归拉取 submodule、下载 MaaDeps，再配置和构建全项目；构建产物安装也包含 bin 下依赖 DLL。官方构建指南要求 Git、Python 3、CMake 3.24+、Windows MSVC 2022，并建议带 submodules 克隆源码。v5.15 顶层 CMake 实际要求 CMake 3.28，因此该 tag 的顶层要求高于指南文档泛列的 3.24。[S8][S10][S11]
- 理论上可请求 CMake 的 `MaaWin32ControlUnit` 目标及其依赖目标；但它并非单文件独立目标，也不能由已查证构建脚本证明“补丁 DLL + 官方其余 DLL”组合兼容。Win32 Control Unit 导出创建/销毁函数，但其句柄接口、框架间交互属于跨 DLL ABI 边界；同 tag、同架构、同配置是必要控制条件，不等于已证明可替换官方库。需同一源码树一致构建并验证版本/ABI；本次不主张混用。[S9][S10]
- 本机只读环境检查使用项目 `scripts/build.ps1` 的固定 vswhere 路径 `C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe` 和相同的 C++ 工具要求，找到 Visual Studio 2022 Community；其 MSBuild 与 MSVC v143 `cl.exe` 存在。`vswhere -find` 未在该 VS 安装中找到 CMake 或 Ninja；PowerShell PATH 也没有 `cmake`、`ninja`、`msbuild`、`cl` 命令。此结果与 VS 内置 MSBuild/编译器存在不矛盾；未安装工具、未构建。[L1]

## 许可证原则

固定 tag 随附许可证为 LGPL-3.0。其原文允许复制和分发许可证文本（第 0 条），并规定分发组合程序时提供显著版权/许可告知及 GPLv3、LGPLv3 文本；允许使用合适的共享库机制，要求其运行时可接受与链接版本接口兼容的修改库；修改库再发布须按 LGPL 传递并提供对应源代码（第 2、4 条）。若把补丁并入 MaaFramework DLL，应随分发提供对应修改源码、保留声明和许可证材料，并允许用户替换/调试修改版库；若只在内部使用、不分发，分发义务情形不同。此为许可证文本的原则性摘要，不是针对具体发布模式的法律意见。[S12]

## 未核验

未构建 v5.15.0。未核实 WGC 时间戳在不同 Windows/驱动/游戏下的可用性与时钟映射，不证明任何 API 扩展能达到 ≤1 秒。未核验 v5.16.0 全部源码差异；发布页/公开 API 文档的未提及不能证明源码里绝无相关私有实现。未做法律审查或 DLL 混配实验。

## 一手来源

- [S1] [v5.15 FramePoolScreencap.cpp](https://raw.githubusercontent.com/MaaXYZ/MaaFramework/v5.15.0/source/MaaWin32ControlUnit/Screencap/FramePoolScreencap.cpp)（清队列、2 秒轮询、缓存回退、图像复制）。
- [S2] [v5.15 Win32ControlUnitMgr.cpp](https://raw.githubusercontent.com/MaaXYZ/MaaFramework/v5.15.0/source/MaaWin32ControlUnit/Manager/Win32ControlUnitMgr.cpp)（候选构造与 FramePoolWithPseudoMinimize wrapper）。
- [S3] [v5.15 FramePoolWithPseudoMinimizeScreencap.h](https://raw.githubusercontent.com/MaaXYZ/MaaFramework/v5.15.0/source/MaaWin32ControlUnit/Screencap/FramePoolWithPseudoMinimizeScreencap.h)、[PseudoMinimizeHelper.cpp](https://raw.githubusercontent.com/MaaXYZ/MaaFramework/v5.15.0/source/MaaWin32ControlUnit/Screencap/PseudoMinimizeHelper.cpp)。
- [S3a] [v5.15 HwndUtils.hpp](https://raw.githubusercontent.com/MaaXYZ/MaaFramework/v5.15.0/source/MaaWin32ControlUnit/Screencap/HwndUtils.hpp)（`ensure_window_on_screen` 的窗口移动/缩放实现）。
- [S3b] [v5.15 DesktopDupWindowScreencap.cpp](https://github.com/MaaXYZ/MaaFramework/blob/v5.15.0/source/MaaWin32ControlUnit/Screencap/DesktopDupWindowScreencap.cpp)（GitHub Connector 读取固定 tag 正文；确认截图函数调用置前与移窗 helper，并在失败后继续）。此前 web fetch 对该文件失败，不作为证据。
- [S4] [v5.15 MaaController C API 文档](https://github.com/MaaXYZ/MaaFramework/blob/v5.15.0/docs/en_us/2.2-IntegratedInterfaceOverview.md#maacontrollercachedimage)、[公开控制器头文件](https://github.com/MaaXYZ/MaaFramework/blob/v5.15.0/include/MaaFramework/Instance/MaaController.h)。
- [S5] [v5.15 MaaDef.h](https://github.com/MaaXYZ/MaaFramework/blob/v5.15.0/include/MaaFramework/MaaDef.h)（FramePool/PrintWindow 的 pseudo-minimize 说明）。
- [S6] [Framework releases](https://github.com/MaaXYZ/MaaFramework/releases)（v5.16.0 发布日期与 v5.15.0 tag）。
- [S7] [当前集成接口文档](https://maafw.com/docs/2.2-IntegratedInterfaceOverview/)（CachedImage 语义）。
- [S8] [v5.15 顶层 CMakeLists](https://raw.githubusercontent.com/MaaXYZ/MaaFramework/v5.15.0/CMakeLists.txt)。
- [S9] [v5.15 MaaWin32ControlUnit/CMakeLists](https://raw.githubusercontent.com/MaaXYZ/MaaFramework/v5.15.0/source/MaaWin32ControlUnit/CMakeLists.txt)。
- [S10] [v5.15 Windows build CI](https://github.com/MaaXYZ/MaaFramework/blob/v5.15.0/.github/workflows/build.yml)。
- [S11] [v5.15 构建指南](https://github.com/MaaXYZ/MaaFramework/blob/v5.15.0/docs/en_us/4.1-BuildGuide.md)。
- [S12] [v5.15 LICENSE.md](https://github.com/MaaXYZ/MaaFramework/blob/v5.15.0/LICENSE.md)。
- [L1] 本机 PowerShell 通过 `scripts/build.ps1` 使用的固定 vswhere 路径检查已安装 VS，并查询 `vswhere -find` 与 PowerShell PATH；只读，未安装或构建。
