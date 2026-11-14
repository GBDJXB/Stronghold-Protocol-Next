# MaaEnd 前台截图实现研究

研究日期：2026-11-13。对象为本地 `D:\Games\终末地MAA` 发行包及相应上游源码。仅静态阅读，未启动发行包、加载 DLL、截图、操作游戏或变更权限。本文件是独立研究记录，不改变当前组件设计或实现。

## 结论

本地 Windows 前台控制器明确指定 MaaFramework 的 **ScreenDC**，而非 FramePool / DXGI。核心是取得客户区屏幕坐标，从桌面 DC 用 BitBlt 复制对应矩形，转成 BGR 图像。截图前附带主动恢复前台和调整窗口到屏幕内的逻辑，不能把整个后端视为无副作用的只读采集。[S1][S4][S5][S6]

本地还指定管理员权限要求和 Seize 键鼠输入。上游 UI 具有权限检测、提示和用户确认后以管理员重启的流程；这些是应用层控制策略，不等于证明 BitBlt 本身必须提权。[S1][S7][S8]

## 1. 本地证据与版本边界

| 事实 | 证据 |
| --- | --- |
| 这是 MaaEnd 发行包，目录内没有发现源码工作树；agent 目录是 cpp-algo.exe、go-service.exe 等二进制 | 本地目录清单；README 的上游源码链接。[S2] |
| 本地 interface 版本字段为 v3.0.0；Win32-Front 匹配 UnityWndClass / Endfield，screencap=ScreenDC，mouse=Seize，keyboard=Seize，permission_required=true | 本地 interface.json 第15、28—39行。[S1] |
| 中文控制器说明要求游戏最前且不被遮挡，并提示抢占鼠标 | 本地 locales/interface/zh_cn.json 第17行。[S3] |
| MaaEnd.exe 的 PE FileVersion/ProductVersion 均为 2.10.0-beta.1 | 读取本地文件版本资源；该版本对应本次阅读的 MXU tag，不能将它误认为 MaaEnd 资源版本。 |
| MaaFramework.dll 静态 ASCII 字符串中提取到 v5.16.0-beta.2 | 静态读取该 DLL 字节，不执行 MaaVersion。它是选择参考 tag 的证据，不是已验证运行时版本或二进制与源码完全一致的证明。 |

因此，框架部分按 v5.16.0-beta.2，UI 部分按 v2.10.0-beta.1 阅读。MaaEnd v3.0.0 的打包工作流会下载 Framework 和 MXU 并将 mxu.exe 重命名为 MaaEnd.exe；本地可执行文件名不代表 UI 为 MaaEnd 自行实现。[S9] 不拿移动的 main 分支替代本地版本；本地二进制是否包含额外补丁未核验。

## 2. 调用链

### 窗口发现与控制器创建

MXU 的 getDesktopWindowFilters 从 interface 取类名/标题正则。Rust find_win32_windows_impl 调用 MaaToolkit 枚举，再按正则筛选；Toolkit 的 Windows 实现遍历可见窗口，只读类名、标题和 HWND，不在这条发现路径打开目标进程。随后 buildDesktopWindowControllerConfig 传递 HWND、截图与输入方法，Rust 用 Controller::new_win32 创建控制器。[S10][S11][S12]

Win32ControlUnitMgr::connect 设置线程 DPI 为 per-monitor-v2，检查 HWND，初始化截图后端与输入后端。build_screencap_units 按位掩码建立候选，ScreenDC 对应 ScreenDCScreencap；init_screencap 运行测速选择可用方法。本地只指定 ScreenDC，所以不能将框架支持多方法理解为本地配置存在 FramePool→DXGI 回退。测速也会实际调用截图，意味着连接阶段就可能触发截图附带行为。[S13]

### 像素获取

ScreenDCScreencap::screencap 的顺序：[S4]

1. 尝试确保窗口在前台、客户区位于屏幕范围内。
2. GetClientRect 取尺寸，ClientToScreen 取客户区左上角的桌面坐标。
3. GetDC(nullptr) 获取桌面 DC，创建兼容内存 DC 与位图。
4. BitBlt 使用 SRCCOPY，从桌面对应坐标复制 width×height 区域。
5. GetBitmapBits 读取像素，bgra_to_bgr 转为 BGR；作用域清理释放 DC/位图等资源。

这不是 PrintWindow，也不是 GPU 后台帧读取。**推论**：它复制屏幕矩形中的可见内容，不能凭 HWND 恢复被遮挡的游戏像素；遮挡物可能进入结果。ScreenDC 函数本身没有 PNG/metadata 存盘、进程创建时间校验或类似 Next 的完整来源校验。[S4]

### 缓存、预览与保存

Framework 接到截图后会进行控制器图像后处理，包括按选项缩放，然后更新缓存。因此底层客户区图像与 UI 缓存图不能直接等同；保留原尺寸需要明确 raw-size 配置。[S14]

MXU 的 ScreenshotService 按订阅周期触发 post_screencap，任务运行时跳过主动请求、依赖管线更新缓存。UI get_cached_image_impl 读取缓存，将编码图像转为 PNG data URL；ScreenshotPanel 的保存按钮下载当前显示的 PNG。其 UI 预览/下载路线不是 Next 的逐次原图加来源记录的原子目录存档设计。[S15][S16]

另有 MaaEnd Go tasker sink 在任务入口检查比例/分辨率，目标为16:9并考虑1280×720识别尺寸，某些不合格全屏路径还会尝试切换模式。这属于上层任务适配，不是 ScreenDC 的通用截图限制，不适合整体搬进任意比例截图组件。[S17]

## 3. 前台与窗口几何的附带行为

- ForegroundUtils 检查 GetForegroundWindow，不是单纯等待。需要恢复时调用 SetWindowPos、SetForegroundWindow，必要时临时 TOPMOST 再取消；恢复尝试有5000ms冷却。**这不是“等用户停止操作5秒后再继续”的防抖策略。** [S5]
- ScreenDC 对“恢复前台失败”和“窗口调整失败”仅记警告，仍继续尝试复制屏幕；因此返回图像不证明游戏确实已在最前。[S4]
- HwndUtils 对普通窗口以显示器工作区 rcWork 计算，可能移动或缩小越界客户区。v5.16.0-beta.2 对客户区恰好覆盖 rcMonitor 的 WS_POPUP 窗口增加二次确认豁免，避免将覆盖任务栏的全屏窗口缩入工作区；这不等于完全没有调整窗口行为。[S6]
- 即使调用方把 mouse/keyboard 设为 None，这些截图内部的窗口调整仍会执行，因为它们不在输入后端中。[S4][S5][S6] 此为源码路径推论，未实测。

## 4. 权限与对 Next 的参考意义

MXU Toolbar 在开始任务时检查 permission_required 和当前进程是否提升，未提升则显示权限对话框；用户选择后，后端用 ShellExecuteEx 的 runas 动词重启。这里核对的是自身进程 token，不是读取游戏 token。不能仅凭配置认定本机发行包当前已提升，也不能推导游戏访问拒绝的根因。[S7][S8]

Next 的 inspect_window 在选择窗口时先 OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)，再读进程路径/创建时间；MaaToolkit 的发现路径则没有这道前置条件。[S12][S18] **推论**：两者在目标进程查询被拒绝时可能有不同表现，但更换截图后端不会自动跳过 Next 的前置检查。它也不证明普通权限 ScreenDC 在本机目标游戏上可用。

可参考的是客户区坐标加桌面 BitBlt 的简单采集思路；不能未经取舍直接启用整套 ScreenDC、permission_required 或 Seize。若未来用于 Next，须单独决定是否允许置前/改几何、遮挡时采集桌面内容是否可接受，以及来源身份校验如何保持；本次没有实施这些改动。[S4][S5][S6][S18]

该像素链路在尺寸固定模型下需 O(W×H) 时间与图像内存；UI 编码和转换会增加常数成本。这是源码结构推导，没有实测性能数据。真实色彩、HDR、独占全屏、DPI、多屏、遮挡与不同权限组合均未验证，不能把 MaaEnd 控制器中文说明中的稳定性宣传作为跨游戏兼容性结论。[S3][S4][S14][S16]

## 一手来源索引

- [S1] [本地 interface.json](D:/Games/终末地MAA/interface.json:28)；对照 [MaaEnd v3.0.0 配置](https://github.com/MaaEnd/MaaEnd/blob/v3.0.0/assets/interface.json#L26-L39)。上游模板版本字段与打包后的本地字段不同，以本地为准。
- [S2] [本地 README](D:/Games/终末地MAA/README.md:26)。
- [S3] [本地中文控制器说明](D:/Games/终末地MAA/locales/interface/zh_cn.json:17)。
- [S4] [Framework v5.16.0-beta.2 ScreenDCScreencap.cpp](https://github.com/MaaXYZ/MaaFramework/blob/v5.16.0-beta.2/source/MaaWin32ControlUnit/Screencap/ScreenDCScreencap.cpp#L7-L103)。
- [S5] [同版本 ForegroundUtils.h](https://github.com/MaaXYZ/MaaFramework/blob/v5.16.0-beta.2/source/MaaWin32ControlUnit/Base/ForegroundUtils.h#L11-L56)。
- [S6] [同版本 HwndUtils.hpp](https://github.com/MaaXYZ/MaaFramework/blob/v5.16.0-beta.2/source/MaaWin32ControlUnit/Screencap/HwndUtils.hpp#L55-L192)。
- [S7] [MXU v2.10.0-beta.1 Toolbar 权限检查与提示](https://github.com/MaaXYZ/MXU/blob/v2.10.0-beta.1/src/components/Toolbar.tsx#L1280-L1372)。
- [S8] [同版本 system.rs 自身权限检测与管理员重启](https://github.com/MaaXYZ/MXU/blob/v2.10.0-beta.1/src-tauri/src/commands/system.rs#L148-L233)。
- [S9] [MaaEnd v3.0.0 打包流程](https://github.com/MaaEnd/MaaEnd/blob/v3.0.0/.github/workflows/install.yml#L319-L365)。
- [S10] [MXU 窗口筛选条件和控制器配置](https://github.com/MaaXYZ/MXU/blob/v2.10.0-beta.1/src/utils/controller.ts#L25-L94)。
- [S11] [MXU 窗口发现](https://github.com/MaaXYZ/MXU/blob/v2.10.0-beta.1/src-tauri/src/commands/maa_core.rs#L631-L680)及[控制器创建](https://github.com/MaaXYZ/MXU/blob/v2.10.0-beta.1/src-tauri/src/commands/maa_core.rs#L1000-L1018)。
- [S12] [Framework MaaToolkit Windows 窗口枚举](https://github.com/MaaXYZ/MaaFramework/blob/v5.16.0-beta.2/source/MaaToolkit/DesktopWindow/DesktopWindowWin32Finder.cpp)。
- [S13] [Framework Win32ControlUnitMgr 连接、后端构造及测速](https://github.com/MaaXYZ/MaaFramework/blob/v5.16.0-beta.2/source/MaaWin32ControlUnit/Manager/Win32ControlUnitMgr.cpp#L43-L229)。
- [S14] [Framework ControllerAgent 截图后处理与目标尺寸](https://github.com/MaaXYZ/MaaFramework/blob/v5.16.0-beta.2/source/MaaFramework/Controller/ControllerAgent.cpp#L1243-L1325)。
- [S15] [MXU ScreenshotService](https://github.com/MaaXYZ/MXU/blob/v2.10.0-beta.1/src-tauri/src/screenshot_service.rs#L152-L206)。
- [S16] [MXU 缓存 PNG data URL](https://github.com/MaaXYZ/MXU/blob/v2.10.0-beta.1/src-tauri/src/commands/maa_core.rs#L1810-L1830)及[UI 保存](https://github.com/MaaXYZ/MXU/blob/v2.10.0-beta.1/src/components/ScreenshotPanel.tsx#L329-L343)。
- [S17] [MaaEnd v3.0.0 Go 层比例检查](https://github.com/MaaEnd/MaaEnd/blob/v3.0.0/agent/go-service/taskersink/aspectratio/checker.go#L19-L24)及同文件 refreshResolution / tryAutoWindowMode 等流程。
- [S18] [Next 目标进程查询](D:/Projects/Stronghold-Protocol-Next/src/windows.cpp:48)。本次只读比较；主会话后续可能修改该文件。

## 未完成的核验

没有运行程序、读取游戏进程 token 或进行真实截图；版本是静态本地证据与对应 tag 的交叉研究。没有证明本地二进制与 tag 逐字节一致，也没有证明哪个权限机制导致 Next 目前的访问拒绝。本次仅新增此研究文档，不改变上述行为。
