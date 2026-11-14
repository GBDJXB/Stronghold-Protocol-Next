# MAA v6.22.0 明日方舟 PC 截图实现研究

研究日期：2026-11-13。对象为项目区的 MAA-v6.22 发行包以及官方 MAA v6.22.0、MaaFramework v5.13.0 源码。仅静态阅读；没有启动 MAA、加载截图 DLL、查询游戏进程、实际截图、发送输入或调整权限。本文件是研究记录，不改变 Next 的设计或实现。

## 结论

MAA 的 PC 截图路线是 **WPF 窗口绑定入口 → MaaCore Win32Controller → 动态加载 MaaWin32ControlUnit.dll → Framework 的具体截图后端 → cv::Mat → MAA 缓存及识别缩放**，不是 ADB 截图，也不是 MAA 自己直接在 Win32Controller 内实现像素采集。[S1][S2][S3][S4][S5]

与 Next 当前窗口访问问题最相关的差异有两个：MAA GUI 的窗口绑定入口先要求自身具有管理员身份；其窗口发现只枚举可见窗口并精确匹配游戏标题，没有 Next 发现阶段的 OpenProcess/进程创建时间检查。管理员要求是 MAA 应用入口的策略，不能从中推导每一种截图 API 都必须提权，更不能据此认定本机访问拒绝的根因已经证明。[S1][S6][L3]

MAA 支持多个截图方法，方法集合由 GUI 配置传入；Framework 可在传入的位掩码候选内测速选择。**不能把它描述成固定的 FramePool → DXGI 顺序回退**。本地一个配置文件存有 FramePool 选项，也不能据此认定当前正在用 PC 模式或已实测成功。[S1][S7][L2]

## 1. 本地证据与版本边界

| 检查结果 | 来源与限制 |
| --- | --- |
| 本地目录含 MAA.exe、MaaCore.dll、MaaWin32ControlUnit.dll、MaaUtils.dll 等发行文件，根目录未见源码工作树 | 本次本地目录检查。[L1] |
| MAA.exe 的 FileVersion/ProductVersion 均为 6.22.0 | 静态读取 Windows 文件版本资源，不执行该程序。[L1] |
| gui.new.json 的 Win32Extra 保存 ScreencapMethod=FramePool、MouseMethod=PostMessageWithCursorPos、KeyboardMethod=SendMessage；同文件还保存 TouchMode=MiniTouch | 这些是已保存选项，不能把 Win32Extra 存在等同于 PC 绑定当前启用。[L2] |
| MAA v6.22.0 的控制单元下载脚本默认 Framework v5.13.0 | 本文据此选定底层源码版本；没有运行本地 DLL 的版本函数，也没有证明本地二进制与 tag 完全一致。[S8] |

搜索仅用于定位文件；以下实现结论均回到固定 tag 阅读，不以默认分支的搜索片段代替版本证据。没有将另一个项目 MaaEnd 的 ScreenDC 配置混同为 MAA 的 PC 默认行为。

## 2. 窗口发现、权限与绑定

`AsstAttachWindowConnect` 首先调用 `Bootstrapper.IsUserAdministrator()`。未通过时显示 Yes/No 对话框；拒绝则返回错误，同意则调用 `RestartAsAdmin()` 并结束本次绑定。重启路径使用 `UseShellExecute=true`、`Verb=runas`。检查的是 MAA 自身身份，不是游戏 token。[S1][S6]

随后根据客户端类型取得目标标题，`FindWindowsByName` 用 EnumWindows 遍历可见窗口、读取标题并作完整字符串相等比较。这段发现代码不按 UnityWndClass 或进程文件名筛选，也不打开游戏进程。找到多个时记录提示并使用首个 HWND，而非证明唯一；该策略不适合直接代替 Next 的唯一候选要求。[S1]

GUI 从 Win32Extra 读取截图、鼠标、键盘方法，经 AsstAttachWindow 进入核心；绑定前保存窗口布局，因为连接阶段已经可能截图并影响窗口。核心 `Controller::attach_window` 创建 Win32Controller、绑定 HWND、进行预览采集并建立缩放代理。Win32Controller 的 attach 则加载控制单元、create、connect，尝试截图读取分辨率。[S1][S2][S5]

**对 Next 的推论**：在 Next 的进程查询被拒绝时，换 FramePool/ScreenDC 不会自动绕过现有 OpenProcess 前置检查。MAA 的发现路径没有该检查，但其 GUI 又主动要求提升；仅凭源码无法断言普通权限 MAA 能在本机截到同一窗口。本研究不提出自动提权、跳过身份校验或改安全边界的实现。[S1][S6][L3]

## 3. DLL 与 ABI 边界

Win32ControlUnitLoader 调用 LoadLibraryW，解析 MaaWin32ControlUnitGetVersion、MaaWin32ControlUnitCreate、MaaWin32ControlUnitDestroy。create 接收 HWND 与三组方法位掩码；返回句柄被转为 MAA 声明的 MaaFwControlUnitAPI，通过虚函数 connect/screencap 等调用。接口含 std::string、cv::Mat 及虚表，属于内部 C++ ABI 适配，不能因工厂导出看起来像 C 函数就当作稳定正式 C API。[S2][S3][S4]

下载脚本还明确区分 Release/RelWithDebInfo 与 Debug 的 ABI 兼容条件。Next 固定的 Framework v5.15.0 正式 C API 是另一条接入路线；本次没有更换 DLL 或混用本地 MAA 的控制单元。[S8][L4]

## 4. 后端选择与像素采集

Framework `connect()` 设置线程 per-monitor-v2 DPI，校验 HWND，先初始化截图后端，再建立输入后端。`build_screencap_units()` 按传入位掩码建立候选；`speed_test()` 对候选进行一次预热采集和一次计时采集，从计时成功的方法中选最快者。预热失败只记录警告，后续计时仍可能成功。候选使用 unordered_map，不能从源码列举顺序推导优先级。连接本身已经执行截图，而非只准备对象。[S7]

| 方法 | 核心像素来源 | 工程边界 |
| --- | --- | --- |
| FramePool | WGC GraphicsCaptureItem，通过 CreateForWindow 绑定 HWND，D3D11 FramePool 取得纹理，复制到可读纹理并 Map；按客户区与 DWM 可视窗口边界计算 ROI，BGRA 转 BGR | 窗口绑定采集，不是读取桌面矩形；客户端裁切仍受纹理尺寸边界约束，不应预先承诺所有 DPI/全屏环境尺寸完全匹配。[S9] |
| DXGI_DesktopDup_Window | 基类通过 DuplicateOutput/AcquireNextFrame 获取显示器桌面图，再按该显示器坐标裁出窗口客户区 | 不是独立窗口纹理；裁切越出该输出图像时返回失败。由桌面来源可推知遮挡内容可能进入结果，但本机表现未实测。[S10][S11] |
| DXGI_DesktopDup | 上述桌面复制基类，取得显示器图像并转 BGR | 与窗口裁切版本区分，不应将两者混写为同一窗口截图方法。[S11] |
| ScreenDC | GetDC(nullptr) 取得桌面 DC，按客户区屏幕坐标用 BitBlt 复制 | 也是屏幕来源；遮挡内容可能被复制是源码推论，不是后台无遮挡保证。[S12] |
| GDI | GetDC(hwnd) 取得窗口 DC，通过 BitBlt 和 GetBitmapBits 读取，转 BGR | API 返回成功不等于所有游戏渲染路径都能正确捕获。[S13] |
| PrintWindow | 创建 DC/位图，使用 PW_CLIENTONLY 与 PW_RENDERFULLCONTENT 调用 PrintWindow，然后 GetDIBits 转出 BGRA/BGR | 源码还处理活动弹窗合成；“后台”标志不是任意游戏/独占全屏兼容保证。[S14] |

### FramePool 的等待、缓存与尺寸

每次采集先清空队列中已有帧，再以约 2ms 轮询等待新帧，最多 2000ms。无新帧但已有缓存时返回 cached_image 的 clone；没有缓存才失败。成功帧也被克隆到缓存。因此“清空旧帧队列”不等于“成功返回一定是本次产生的新画面”。该路线返回的是 cv::Mat，而不是带真实源帧时间戳的记录。[S9]

纹理宽高、行跨度和窗口尺寸分开处理：Map 结果以 RowPitch 建 Mat，再计算客户区 ROI 并转换颜色。这里有图像复制和转换成本；不能把 GPU 取纹理直接等同于最终可识别图像的零拷贝。[S9]

## 5. 截图过程附带的窗口与输入行为

- **FramePool/PrintWindow 包装层**：Manager 实际为它们创建带 PseudoMinimize 的实现。FramePool 包装构造时启动辅助线程，采集前调用 ensure_not_minimized；helper 在窗口最小化时可能修改扩展样式为 layered/transparent、设置透明度为0并 ShowWindow(SW_SHOWNOACTIVATE)，以保持渲染；停止时还涉及恢复样式和最小化状态。不能把“后台截图”解释成完全不改变窗口。[S7][S15][S16]
- **ScreenDC/DXGI窗口版**：采集前调用 ensure_foreground_with_cooldown 和 ensure_window_on_screen。前者可能 SetForegroundWindow、切换 TOPMOST，具有5000ms恢复冷却；后者可能按显示器工作区移动或缩小窗口。失败时这两个截图函数记录警告后仍继续尝试。这不是被动等待用户归还控制权，也不保证截图时已成功置前。[S10][S12][S17][S18]
- **MAA任务截图上层**：`screencap()` 调用 move_cursor=true 的分支；按输入模式可能挪光标或窗口，部分路径会 BlockInput、保存恢复真实光标。主界面识别分支等待300ms，其他一条路径等待34ms。`screencap_for_preview()` 则传 false，跳过该层挪动逻辑，但不因此取消底层后端的窗口行为。[S2]
- **结束绑定**：Win32Controller 还负责 inactive、恢复记录的窗口位置和用户最小化意图，说明它承担截图之外的控制生命周期管理。[S2]

**推论**：只将鼠标/键盘方法设为 None，不能从逻辑上推出底层截图绝不置前或改变几何，因为上述部分行为直接位于截图后端及辅助类中。不能将整套控制器不加拆分地迁入 Next 的只读组件。[S7][S10][S12][S15][S16]

## 6. MAA 的原图、识别缩放与耗时

后端提供原始尺寸 BGR Mat，不内置“游戏必须16:9”的统一限制；但 MAA 的 ControlScaleProxy 构造会拒绝低于识别基准的分辨率和非16:9比例。Controller 默认返回按 m_scale_size 经 cv::resize 的识别图，raw=true 路径克隆原始缓存。任意比例像素采集能力与 MAA 上层识别链可接受的比例必须分开讨论。[S5][S19]

Controller 用互斥锁维护任务帧和预览帧缓存；预览帧单独存储，不覆盖任务识别帧。Win32Controller 维护最多30次采集耗时，首次及后续每10次报告统计；计时覆盖 unit_screencap 调用，前置的光标等待及后续统计不在该计时区间内。因此日志中的截图 cost 不应被直接当作一次完整工具请求的端到端延迟。[S2][S5]

Next 所需的 PNG、metadata.json 逐次来源关联与发布语义，参见现有 capture.md；不能由 MAA 返回 Mat/提供预览缓存推导已具备相同存档契约。本次未研究或验证 MAA 的全部外部截图保存入口。[L4][S5]

## 7. 对本项目的可复用结论与未决项

1. 可参考：按 HWND 建立后端、物理客户区和 DPI 处理、WGC 纹理行跨度、DWM边界偏移、按显示器坐标裁切、资源释放及缓存语义。均需按采用的 Framework 版本复核，不能把 v5.13.0 细节直接视为 v5.15.0/后续版本不变。[S7][S9][S10]
2. 不宜整体复用：内部 C++ ABI、输入耦合的截图前处理、强制管理员 GUI 入口、自动置前/移动/伪最小化、首窗口选择和上层16:9限制。它们各自服务 MAA 的应用约束，是否适用于 Next 是单独的设计决定。[S1][S2][S4][S15][S17][S19]
3. 当前优先研究问题仍是“目标进程查询为何被拒绝、所需身份校验如何与执行环境兼容”，不是先认定某个像素后端坏了。Next 的检查位置见本地 windows.cpp；没有在本次复现游戏访问拒绝，也没有核验目标 token。[L3]
4. 单帧纹理转CPU图像、颜色转换和像素拷贝在固定像素格式下为 O(W×H) 时间/图像内存；控制器初始化、等待新帧等另外增加延迟。没有实测数据，不提出吞吐或耗时保证。[S9][S11][S13]

未验证：本地 MAA 实际选中后端、当前管理员身份、本地 DLL 与 tag 的逐一对应、普通权限和管理员权限下各后端的本机表现、全屏/HDR/DPI/多屏/遮挡与新鲜度。以上源码路径不构成本机成功截图证明。

## 一手来源索引

### 本地

- [L1] [本地发行包目录](C:/Users/lexin/OneDrive/Desktop/MAA-v6.22)；MAA.exe版本资源由本次静态读取。
- [L2] [保存的Win32配置](C:/Users/lexin/OneDrive/Desktop/MAA-v6.22/config/gui.new.json:302)，TouchMode见同文件315行；未修改配置。
- [L3] [Next窗口进程查询](../src/windows.cpp#L48)（本地编辑器定位：[windows.cpp](D:/Projects/Stronghold-Protocol-Next/src/windows.cpp:48)）。
- [L4] [Next截图设计](capture.md)、[依赖说明](third-party.md)；既有 MaaEnd 研究另见 [research-maaend-foreground-capture.md](research-maaend-foreground-capture.md)。

### MAA v6.22.0

- [S1] [AsstProxy.cs：窗口枚举与绑定入口](https://github.com/MaaAssistantArknights/MaaAssistantArknights/blob/v6.22.0/src/MaaWpfGui/Main/AsstProxy.cs#L3274-L3492)。
- [S2] [Win32Controller.cpp：attach、任务/预览截图与生命周期](https://github.com/MaaAssistantArknights/MaaAssistantArknights/blob/v6.22.0/src/MaaCore/Controller/Win32Controller.cpp#L57-L266)。
- [S3] [Win32ControlUnitLoader.cpp：DLL加载和导出](https://github.com/MaaAssistantArknights/MaaAssistantArknights/blob/v6.22.0/src/MaaCore/Controller/Win32ControlUnitLoader.cpp#L19-L59)。
- [S4] [MaaFwControlUnitInterface.h：内部C++ ABI接口](https://github.com/MaaAssistantArknights/MaaAssistantArknights/blob/v6.22.0/src/MaaCore/Controller/MaaFwControlUnitInterface.h#L11-L43)。
- [S5] [Controller.cpp：绑定、缩放、原图与缓存](https://github.com/MaaAssistantArknights/MaaAssistantArknights/blob/v6.22.0/src/MaaCore/Controller/Controller.cpp)。
- [S6] [Bootstrapper.cs：自身身份检查](https://github.com/MaaAssistantArknights/MaaAssistantArknights/blob/v6.22.0/src/MaaWpfGui/Main/Bootstrapper.cs#L1139)；[管理员重启](https://github.com/MaaAssistantArknights/MaaAssistantArknights/blob/v6.22.0/src/MaaWpfGui/Main/Bootstrapper.cs#L1690-L1704)。
- [S8] [控制单元下载脚本：v5.13.0及ABI说明](https://github.com/MaaAssistantArknights/MaaAssistantArknights/blob/v6.22.0/tools/maafw-control-unit-download.py#L1-L45)。
- [S19] [ControlScaleProxy.cpp：分辨率与16:9检查](https://github.com/MaaAssistantArknights/MaaAssistantArknights/blob/v6.22.0/src/MaaCore/Controller/ControlScaleProxy.cpp#L15-L57)。

### MaaFramework v5.13.0

- [S7] [Win32ControlUnitMgr.cpp：连接、后端构造及测速](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaWin32ControlUnit/Manager/Win32ControlUnitMgr.cpp#L43-L228)。
- [S9] [FramePoolScreencap.cpp：WGC、缓存、客户区和纹理映射](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaWin32ControlUnit/Screencap/FramePoolScreencap.cpp)。
- [S10] [DesktopDupWindowScreencap.cpp：置前、窗口调整和客户区ROI](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaWin32ControlUnit/Screencap/DesktopDupWindowScreencap.cpp#L11-L96)。
- [S11] [DesktopDupScreencap.cpp：桌面复制](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaWin32ControlUnit/Screencap/DesktopDupScreencap.cpp)。
- [S12] [ScreenDCScreencap.cpp](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaWin32ControlUnit/Screencap/ScreenDCScreencap.cpp)。
- [S13] [GdiScreencap.cpp](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaWin32ControlUnit/Screencap/GdiScreencap.cpp)。
- [S14] [PrintWindowScreencap.cpp](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaWin32ControlUnit/Screencap/PrintWindowScreencap.cpp)。
- [S15] [FramePoolWithPseudoMinimizeScreencap.h](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaWin32ControlUnit/Screencap/FramePoolWithPseudoMinimizeScreencap.h)。
- [S16] [PseudoMinimizeHelper.cpp](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaWin32ControlUnit/Screencap/PseudoMinimizeHelper.cpp)。
- [S17] [ForegroundUtils.h](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaWin32ControlUnit/Base/ForegroundUtils.h)。
- [S18] [HwndUtils.hpp](https://github.com/MaaXYZ/MaaFramework/blob/v5.13.0/source/MaaWin32ControlUnit/Screencap/HwndUtils.hpp#L60-L172)。
