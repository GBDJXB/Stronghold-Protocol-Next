# 原生 Windows 截图路线研究

研究日期：2026-11-13。范围是可选路线比较，不选定后端、不改实现、不操作游戏。可见性、遮挡和窗口透明判定由并行研究单独覆盖；本文不把任一截图 API 当作满足这些约束的证明。

历史研究快照：本文部分项目文件状态与 MaaFramework SDK 依赖描述反映研究时的旧实现。当前采集实现已改为原生 WGC，不依赖 MaaFramework；下文有关旧项目状态的内容仅作研究记录。

## 结论

- **WGC（Windows.Graphics.Capture）+ `IGraphicsCaptureItemInterop::CreateForWindow`** 是 HWND 绑定的原生窗口采集候选；配合 `Direct3D11CaptureFramePool::CreateFreeThreaded`，可不依赖消息泵，并从每个 `Direct3D11CaptureFrame::SystemRelativeTime` 取得基于 QPC 的帧时间。WinRT `TimeSpan` 值以 100 ns 为单位表达，不是原始 QPC tick；转换成墙钟或 QPC 域时间需要使用正确的时基映射，不能将该值直接当 QPC 计数或套用 QPC frequency。[S1-S5]
- **DXGI Desktop Duplication** 提供每个显示输出（通常对应一个显示器）的桌面图像更新及 QPC 时间戳。`LastPresentTime` 是最近桌面图像更新的 QPC；鼠标单独更新时可能为零。因此它适合区分“桌面图像更新”与“仅指针更新”，但目标客户区需要从显示器桌面图像裁切，不能直接视作窗口独立纹理。跨输出窗口需处理多输出拼接/失败及时间不同步。[S6-S10]
- **桌面 DC + `BitBlt`** 是概念和依赖最少的备选：取得桌面 DC，按客户区屏幕坐标拷贝矩形。API不提供所复制像素对应的源呈现时间戳；调用时刻只能记录为采集观察时间，不能满足严格的源帧新鲜度证明。[S11-S12]
- 以上路线都不自动证明采集期间不改变焦点、窗口位置、尺寸或透明度。`IsBorderRequired=false` 需要 Borderless 授权且可能被忽略；`IsCursorCaptureEnabled` 仅控制帧是否含鼠标指针。应用不得将请求成功、授权状态或边框开关当成这些副作用/约束已解决。[S13-S14]

## WGC：HWND 窗口与帧时间

桌面 Win32 程序可通过 `IGraphicsCaptureItemInterop::CreateForWindow(hwnd, ...)` 建立指向一个 HWND 的 `GraphicsCaptureItem`。微软 API 文档把该调用定义为以单个窗口为目标，参数直接接收 HWND。[S1] 微软的 Win32 HWND sample 展示了此用法，说明 Windows 10 1903 / SDK 18362 起的接口路径，并注明最小化窗口虽可枚举但不能捕获。[S2]

创建 D3D11 设备及 WinRT D3D 设备后，frame pool 可用 `CreateFreeThreaded` 建立：它去掉 `DispatcherQueue` 依赖，`FrameArrived` 在 pool 内部工作线程触发。[S3] 这只解决消息泵依赖，不自动保证回调线程上任意工作都合适；文档仍建议避免在事件处理器中做重活。取帧及关闭对象时需遵守 frame 生命周期，不能在归还 pool 后继续使用 frame surface。[S4]

`Direct3D11CaptureFrame.SystemRelativeTime` 的官方说明是 QPC (`QueryPerformanceCounter`) 时间，可与其他媒体元素同步；属性类型为 WinRT `TimeSpan`，数值单位为 100 ns，而非 QPC 原始 tick。[S4] 适配 Next 时应明确保存其 100 ns `TimeSpan` 值和必要的系统时间映射，并与请求起止时刻分开；不能把它误记为 QPC 计数，也不能简单乘/除 QPC frequency 将其解释为 QPC 原始值。不可把请求结束时刻写成帧产生时刻。该字段提供系统相对帧时间，但“它是否在目标游戏/系统上严格等于所需源呈现时刻、不同 frame 是否可重复/静止期间是否没有新 frame”仍需实测和明确语义，不在本研究中扩大解释。

Frame pool 的 buffer 数与尺寸由调用者提供。官方文档说明 surface 尺寸是 pool 尺寸，内容小于 pool 时多余部分未定义，内容大于 pool 时会裁剪，建议使用每帧 `ContentSize` 选取有效区域。[S4] 对 Next 而言应据此逐帧核实尺寸变化和复制区域，不默认窗口像素尺寸永不变化。

捕获期间系统提示边框与指针选项有独立语义：`IsCursorCaptureEnabled` 控制是否将 cursor 包含在捕获内容；`IsBorderRequired=false` 不是无提示的普通设置。关闭彩色边框前，应用必须请求 `GraphicsCaptureAccessKind.Borderless` 并取得用户同意；需要 manifest capability，拒绝授权后即使属性设置成功也会被忽略，同一窗口若其他应用要求边框仍可能显示。[S13-S14] 因而不能把 WGC 默认行为或 Borderless 属性视为对游戏画面完全无影响。

## DXGI Desktop Duplication：输出级桌面帧

`IDXGIOutput1::DuplicateOutput` 创建的是一个 adapter output 的 desktop duplication。微软文档指出若要获取整个桌面，需对每个活动输出创建接口，且没有显式保证各输出图像时间同步，应使用各自时间戳再决定如何组合。[S6] `AcquireNextFrame(timeout, ...)` 等待新桌面图像；超时返回 `DXGI_ERROR_WAIT_TIMEOUT`，等待不可取消，因此应使用有限超时以便检查停止/状态条件。[S7]

成功取帧时的 `DXGI_OUTDUPL_FRAME_INFO::LastPresentTime` 是操作系统以 QPC 取得的桌面图像上次更新时间。零值表示上次 `AcquireNextFrame` 之后桌面图像未更新。`LastMouseUpdateTime` 是指针位置/形状更新时间；若仅指针变动，`LastPresentTime`、累计帧数及更新区域元数据可为零。[S8] 需要图像新鲜度时必须看 `LastPresentTime`，不能把鼠标更新时间当作游戏画面更新时间。

窗口客户区映射到桌面输出需用实际输出桌面坐标系裁切。窗口完全位于一台显示器时，主要工作是将物理屏幕坐标减去该 output 的桌面原点，并检查裁切范围；跨屏窗口可能落入多个独立 output frame，分别采样会带来时间不一致，窗口落在显示器间空白也不存在可裁的输出像素。多显示器处理和旋转/模式变化后的重建是实现责任，不可只按虚拟桌面大矩形直接索引一个纹理。DXGI 文档还指出桌面切换、模式变化或 DWM/fullscreen 场景变化可导致 `DXGI_ERROR_ACCESS_LOST`，需释放并重建 duplication interface。[S7-S9]

## Win32 GDI：桌面 BitBlt

`GetDC(NULL)` 取得整个屏幕的 DC；`BitBlt` 从源 DC 按矩形复制颜色数据，`SRCCOPY` 表示直接复制源矩形。[S11-S12] 典型实现用桌面 DC 作 source、内存 DC/bitmap 作 destination，客户区左上角以 `ClientToScreen` 转屏幕坐标，再用客户区宽高拷贝。它的源是调用当下的桌面 DC，并没有 DXGI `FRAME_INFO` 或 WGC `SystemRelativeTime` 这样的帧时间字段。即便记录 QPC 调用时刻，也只能说明调用时间，不是源像素的呈现时间。

该路线接口路径短，基础设施比 D3D11/WGC 或 DXGI duplication 少；但像素仍需读回/转成可持久化布局，成本依分辨率和每秒采样次数线性增加。GDI BitBlt 会进行 DC 之间的像素复制/格式转换，并非“无需搬运”的免费读取。对小客户区矩形，搬运量低于复制整个显示器；具体 CPU、内存带宽和游戏帧率影响必须在目标机器上测量。本段是工作量/数据量的机制分析，不是性能基准。

## 资源成本与复杂度（不含未经测量的数值）

- 若输出尺寸为 `W×H`，一个 32-bit BGRA staging/readback frame 的原始缓冲下界约为 `4WH` 字节；RGB8 存档前图像约 `3WH` 字节，尚未计 GPU surface、pool 多缓冲、临时转换、PNG encoder、元数据和分配器开销。N 个同时保留的帧缓冲，像素存储量按 N 线性增长。WGC frame pool buffer count 是调用参数，不是零成本队列。[S3-S4]
- WGC 至少涉及帧池纹理和将纹理读回 CPU 可访问内存/转换为应用像素格式的工作；GPU 纹理来源不等于 CPU 侧零拷贝。是否可通过合适 staging/映射策略减少副本需实现确认。
- DXGI 的 duplication surface 面向每个 output；只裁小 ROI 可以减少后续转换/存档数据量，但 `AcquireNextFrame` 的输入仍是输出级桌面 image，不能由此宣称采集成本只与 ROI 成正比。
- BitBlt 每次复制目标矩形像素，后续 CPU 访问、通道整理、PNG 编码仍有成本。若只要窗口区域，其每次复制像素量与客户区面积相关。
- 三者的 CPU/GPU 占用、延迟、内存峰值和 FPS 影响都未在本机游戏场景测量。不得据算法路径推导具体毫秒、MB/s 或低于 5% FPS 影响；capture.md 中的 512 MiB、每秒 1 张及 FPS 目标均是观测和优化目标，不是初期功能验收硬门槛。

## 现有构建与参考代码

研究时的 `stronghold-capture.vcxproj` 固定 Windows SDK `10.0.26100.0`、MSVC v143、C++20，脚本由 VS Installer 的 `vswhere` 找 MSBuild；该快照列出的显式链接依赖为 `user32.lib`、`ole32.lib`、`windowscodecs.lib`、`shell32.lib`，并曾依赖 MaaFramework v5.15.0 SDK headers/runtime。[L15-L17] 本机 Windows SDK 中已有 `cppwinrt/winrt/Windows.Graphics.Capture.h`、`um/windows.graphics.capture.interop.h`、`um/d3d11.h` 和 `shared/dxgi1_2.h`；直接使用 SDK C++/WinRT 头不意味着需要额外 NuGet projection/codegen 项目。本文对当时项目链接库完整性的疑问不代表当前项目配置。

可复用参考（仅作为实现研究，不表示应整体移植）：

- [Microsoft Windows.UI.Composition Win32 Samples: ScreenCaptureforHWND](https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/tree/master/cpp/ScreenCaptureforHWND)：WGC Win32 / HWND 使用示例，仓库声明 MIT license；若复制代码，保留并遵守其 LICENSE 与版权/声明要求。[S2][S18]
- [MAA Framework v5.13.0 FramePool 实现](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaWin32ControlUnit/Screencap/FramePoolScreencap.cpp)：项目此前已静态研究 WGC、纹理映射、客户区裁切、尺寸处理和缓存；实现版本是 v5.13.0，且 MAA 代码/包装层存在自动调整窗口、伪最小化等行为，不能只摘后端类就假设被动。[S19]
- [MAA Framework v5.13.0 DXGI Window 实现](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaWin32ControlUnit/Screencap/DesktopDupWindowScreencap.cpp) 与 [DesktopDup 基类](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaWin32ControlUnit/Screencap/DesktopDupScreencap.cpp)：可参考 output 初始化、Acquire/Release 生命周期和窗口 ROI 路径；该集成包含 MAA 行为约束。此为研究来源，不是当前实现或分发依赖。
- [MAA Framework v5.13.0 ScreenDC 实现](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaWin32ControlUnit/Screencap/ScreenDCScreencap.cpp)：可参考桌面 DC + BitBlt 数据路径；其调用封装还包含确保前台和窗口位置的辅助行为，不适合整体复用为只读采集。[S20]

Microsoft 官方样例由微软仓库维护，许可为 MIT；任何源文件复制仍应随代码保留许可及版权说明。MAA 与 MaaFramework 的许可证不可混为一谈；本文对历史 MaaFramework 许可证内容仅作研究记录，当前实现不包含该框架。[S18-S19]

## 未验证事项

- 未在目标游戏上创建 WGC item/session；未测静止窗口的 `FrameArrived`/SystemRelativeTime 更新规律、时间年龄分布、帧尺寸变化、HDR、权限和 Borderless consent 实际流程。
- 未实现或验证 DXGI output 枚举、客户区跨输出裁切、输出时间差、access-lost 恢复、HDR 和显卡多适配器情形。
- 未实测 GDI 在目标渲染路径、遮挡/透明情境、DPI、多显示器和 HDR 下的像素正确性；不具备源帧时间戳这一点来自 API 契约。
- 已只读核对本机 SDK 中 WGC、WinRT interop、D3D11、DXGI 相关头文件存在；未编译原生后端，也未核验完整 API 使用所需链接库和最终链接配置。现有 v143/SDK 版本及项目显式依赖来自本地项目文件。
- 未比较 CPU、内存、落盘吞吐或对游戏 FPS 的影响，未选择任何路线，也未更改实现。

## 一手来源

- [S1] [IGraphicsCaptureItemInterop::CreateForWindow](https://learn.microsoft.com/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow)。
- [S2] [Microsoft Win32 HWND capture sample README](https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/blob/master/cpp/ScreenCaptureforHWND/README.md)；[sample source](https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/tree/master/cpp/ScreenCaptureforHWND)。
- [S3] [Direct3D11CaptureFramePool::CreateFreeThreaded](https://learn.microsoft.com/uwp/api/windows.graphics.capture.direct3d11captureframepool.createfreethreaded)。
- [S4] [Microsoft Screen capture: frame pool, `ContentSize`, `SystemRelativeTime`, frame lifetime](https://learn.microsoft.com/windows/uwp/audio-video-camera/screen-capture)。
- [S5] [D3D11CreateDevice](https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice)、[D3D11 BGRA device flag](https://learn.microsoft.com/windows/win32/api/d3d11/ne-d3d11-d3d11_create_device_flag)。
- [S6] [IDXGIOutput1::DuplicateOutput](https://learn.microsoft.com/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput)。
- [S7] [IDXGIOutputDuplication::AcquireNextFrame](https://learn.microsoft.com/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutputduplication-acquirenextframe)。
- [S8] [DXGI_OUTDUPL_FRAME_INFO](https://learn.microsoft.com/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_outdupl_frame_info)。
- [S9] [Desktop Duplication API overview and Microsoft sample flow](https://learn.microsoft.com/windows/win32/direct3ddxgi/desktop-dup-api).
- [S10] [IDXGIOutputDuplication interface](https://learn.microsoft.com/windows/win32/api/dxgi1_2/nn-dxgi1_2-idxgioutputduplication).
- [S11] [GetDC](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getdc).
- [S12] [BitBlt](https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-bitblt).
- [S13] [GraphicsCaptureSession::IsBorderRequired](https://learn.microsoft.com/uwp/api/windows.graphics.capture.graphicscapturesession.isborderrequired).
- [S14] [GraphicsCaptureSession::IsCursorCaptureEnabled](https://learn.microsoft.com/uwp/api/windows.graphics.capture.graphicscapturesession.iscursorcaptureenabled).
- [S15] 本地 [Visual Studio 项目](../stronghold-capture.vcxproj)、[构建脚本](../scripts/build.ps1)、[SDK / 第三方边界](third-party.md)。
- [S18] [Microsoft sample repository and MIT license](https://github.com/microsoft/Windows.UI.Composition-Win32-Samples)。
- [S19] [现有 MAA PC 截图研究](research-maa-pc-capture.md)。
- [S20] [现有 MaaEnd ScreenDC 研究](research-maaend-foreground-capture.md)。
