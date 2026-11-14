# 基础截图技术路线比较：研究快照

选型后状态：用户已选择原生 WGC 薄封装。下文保留选型时的比较与验证缺口；当前实现及验证状态以 [capture.md](capture.md) 为准。

研究日期：2026-11-13。三名 GPT-6 Luna 子代理分别调研成熟工具、MaaFramework 补丁和原生 Windows API；主代理复核关键差异及共有的可见性约束。仅新增研究文档，未修改截图实现、安装工具、编译新后端、操作游戏、提权、暂存或提交。需求以 [capture.md](capture.md) 为准；以下不是已采用的实现计划。

## 结论

目前没有查到一个可原样集成、同时实现全部需求的现成 C++ 截图库。值得讨论的三条主路线是：项目内薄封装原生 WGC、对现有 MaaFramework 做被动采集与时间戳扩展、通过小适配层复用 Rust `windows-capture`。它们都还需要项目侧的窗口条件检查、采集准入和原图/元数据存档；不能把截图工具本身的成功返回当作可见性或可决策性的证明。

若以保持 C++ 工程和缩小运行依赖为优先，原生 WGC 是优先讨论的候选；若以尽量复用第三方采集封装为优先，Rust 包装也值得讨论；框架补丁的优势是沿用当前接入结构，但维护范围已经超过“移除几个窗口操作”。这只是成本建议，最终方案由用户决定。[N][P][T]

## 方案比较

| 路线 | 能复用的部分 | 满足本项目仍要做什么 | 构建、维护及许可代价 |
| --- | --- | --- | --- |
| **原生 WGC，项目内薄封装** | Windows 系统采集设施及微软 MIT 示例；现有工作进程隔离、像素校验与存档 | 配对返回图像和 WGC `SystemRelativeTime`；客户区定位、尺寸变化、设备/会话生命周期；可见性与调度仍独立实现 | 使用现有 MSVC/Windows SDK；本机已存在必要 C++/WinRT/WGC/D3D/DXGI 头。无需引入完整媒体框架；完整编译、链接和实机兼容性尚未验证。自行维护小封装，复制样例保留许可。[N] |
| **MaaFramework v5.15.0 补丁及扩展** | 当前正式 C API 调用、控制器与图像缓冲接入 | 去掉 FramePool 伪最小化、DXGI-window 置前/移窗；关闭旧帧兜底；扩展图像与源帧时间的配对传播。仅去副作用不满足新鲜度信息要求 | 自行构建与管理修改版，依赖 MaaUtils、OpenCV、Boost、ZLIB 等；需要 CMake 3.28+。本机已有 MSVC/MSBuild，尚未在 PATH 或 VS 安装中找到 CMake/Ninja。LGPL；修改版标识、对应源码及许可材料需随分发处理。未验证混用自建/官方 DLL 的 ABI。[P] |
| **Rust `windows-capture` + 适配层** | 现成 WGC 会话、D3D 读回、缓冲与源时间戳封装；MIT | 提供项目可调用的 C ABI 或复用工作进程协议；准确裁出客户区并保留图像/时间配对，审查生命周期及无窗口副作用路径 | 引入 Rust/Cargo 与跨语言构建维护；API/源码版本需固定。比直接维护 WGC 细节更依赖上游封装，但尚未验证本机编译和游戏行为。[T] |
| **原生 DXGI Desktop Duplication** | 微软桌面复制示例；按输出返回桌面图像和 `LastPresentTime` | 客户区裁切、跨输出拼接、旋转/显示模式变化、设备恢复；输出更新时间不能声称是目标游戏窗口更新时间 | 主要使用 Windows 系统库，但多屏与跨适配器处理责任较多；可讨论为候选后端，不能因为可能包含遮挡就直接淘汰，也不预设必须实现双后端。[N] |
| **OBS / FFmpeg** | 成熟视频采集、设备管理与媒体管线 | 仍须独立做可见性；媒体 PTS 不能冒充目标源帧时间，需逐路径确定或扩展输出契约 | 面向媒体系统，集成接口和依赖范围较大；OBS GPL，FFmpeg 许可取决于构建。完整应用与核心库依赖不能混为一谈。低频原始 PNG/元数据任务缺少明显收益。[T] |
| **ScreenCaptureLite / 直接 GDI** | 小型 C++ 接口或 Win32 BitBlt/PrintWindow 路径 | 缺少可直接消费的源帧时间；目标渲染、客户区及透明处理还需验证 | ScreenCaptureLite 已查源码的窗口初始化会更改系统动画设置，不可原样采用；纯 GDI 依赖少，但不能以调用时间填补源时间证据。作为低复杂度对照可保留，不作为已满足基线的方案。[N][T] |

## 已核验的关键行为

1. **当前框架的窗口副作用已确认到固定版本。** `v5.15.0` 的 DXGI-window `screencap()` 调用 `ensure_foreground_with_cooldown` 和 `ensure_window_on_screen`，失败只告警后继续；FramePool 使用伪最小化包装。已通过 GitHub 连接器核对先前网页获取失败的 DXGI 文件。关闭键鼠不能关闭这些截图内部行为。[P1]
2. **当前正式 C API 没有与图像配对的源帧时间。** 去掉缓存返回路径无法补出这个字段；要保留现有接入结构就需要扩展实现和接口。[P]
3. **WGC 有窗口帧时间，DXGI 有输出帧时间，两者对象不同。** WGC `SystemRelativeTime` 是合成器渲染该帧的 QPC 时间，WinRT `TimeSpan` 单位为 100 ns；DXGI `LastPresentTime` 是输出桌面图像更新时间。它们均不是游戏逻辑事件时间。[N1][N2]
4. **延迟与帧年龄独立。** 等待 2 秒后取得一张刚生成的图，并不自动违反 1 秒帧龄；快速返回缓存也不证明画面新鲜。保存过程还会增加交付年龄。静止画面像素不变不能用于判定失效。[N][P]
5. **当前进程访问拒绝仍是独立阻断。** 更换像素后端不会自动消除现有 `OpenProcess` 前置条件；允许管理员运行也不代表已验证根因或兼容性。此前讨论的有限身份校验需要按最终授权单独实现并记录保证变化。[V]

## 所有路线共有的工作与验证缺口

- 可见性层必须区分已知遮挡/透明与检查未知；`IsWindowVisible` 不检测遮挡，`GetLayeredWindowAttributes` 不能全面读取逐像素透明。按矩形枚举无法证明任意 Windows 合成特效均已覆盖。任务栏、小浮窗等不能沿用 Chromium 为性能优化而忽略的窗口类别。[V]
- 非前台不再是拒绝采集的充分条件；条件应围绕客户区完整可见和目标自身不透明。仍须采集前后检查，采集中允许每秒检查；不靠抢焦点或恢复窗口满足条件。[需求]
- 截图组件可负责按需准入、500 ms 最小启动间隔、同一时刻一次采集、图像/源时间与全量成功存档。周期策略、容量 3 的待识别队列、暂停代打及显式恢复属于完整 pipeline，不应因研究而全部放入截图组件。[需求]
- 新后端必须做局部离线验证，再交独立测试对话确认本机窗口/无边框、DPI/多屏、尺寸变化、非前台、遮挡、透明、最小化与静止画面。源帧时间语义、帧龄、PNG/元数据交付耗时、进程总内存和游戏 FPS 影响均未实测。[需求]
- 每秒 1 张、常见 1 秒交付、首张 5 秒、总内存 512 MiB 和 FPS 影响等是观测优化目标，不能作为已达标结论或初期功能硬门槛。不重新增加截图 SHA-256。[需求]

选型时只需先确定主采集路线及愿意承担的依赖维护；不要默认打包实现所有后端。原有 PNG/元数据、错误输出和隔离机制可按需复用，性能与资源变化用测量决定后续优化。

## 分项报告与一手链接

- [需求] [已确认需求与当前能力](capture.md)。
- [N] [原生 WGC / DXGI / GDI](research-capture-native.md)。
- [P] [MaaFramework 补丁、API 与构建成本](research-capture-framework-patch.md)。
- [T] [成熟工具、依赖与筛选](research-capture-tools.md)。
- [V] [所有候选共有的可见性与新鲜度边界](research-capture-visibility.md)。
- [P1] [MaaFramework v5.15.0 DXGI-window 源码](https://github.com/MaaXYZ/MaaFramework/blob/v5.15.0/source/MaaWin32ControlUnit/Screencap/DesktopDupWindowScreencap.cpp)。
- [N1] [Microsoft：WGC SystemRelativeTime](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframe.systemrelativetime)。
- [N2] [Microsoft：DXGI_OUTDUPL_FRAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_outdupl_frame_info)。

版本边界：MaaFramework 副作用及 CMake 核验针对 v5.15.0；成熟工具中部分源码来自滚动分支，分项报告已注明。用户选定第三方路线后仍需固定版本/提交并核验完整调用链。本研究没有声称已完成第三方组件的全面安全或发行合规审查。
