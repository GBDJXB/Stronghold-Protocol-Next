# Stronghold-Protocol-Next

独立的离线静态知识导入与盟约查询见 [静态知识快照](docs/static-knowledge.md)；与截图、OCR 和局面记录分离。

明日方舟 PC 端「卫戍协议-规程」研究工具。当前提供单次、只读的 Windows Graphics Capture (WGC) 窗口截图与原图归档；另提供有限范围的离线分类与字段识别；不发送游戏输入，不实现策略或周期调度。

## 构建与离线检查

环境为 Windows x64、Visual Studio 2022 MSVC v143、Windows SDK 10.0.26100.0 和 PowerShell。构建使用 Visual Studio Installer 的 `vswhere` 定位 MSBuild，不修改 PATH 或其他全局设置。

```powershell
.\scripts\build.ps1 -Test
```

`-Test` 执行合成图、模拟窗口状态和工作进程监督的离线检查，不读取屏幕或操作游戏；脚本还独立解析 10 份合成归档的 JSON。检查素材保存在 `build/` 下，命令结束时会显示目录。该检查不等价于实机截图验收。构建输出及默认截图目录 `captures/` 被 Git 忽略。

WGC 支持性探测：

```powershell
.\build\x64\Release\stronghold-capture.exe --check-backend
```

此命令只查询 Windows 是否支持 WGC，不绑定游戏窗口、不创建截图会话，也不采集图像。

最近本地验证：Release x64 构建与 127 项离线检查通过，独立解析 10 份合成归档；未操作游戏。当前工具执行环境的 `--check-backend` 返回 `wgc_hresult` / `0x80070005`，发生在创建采集会话之前。尚未确认普通桌面环境是否同样受限，也未证明新后端已成功捕获游戏；真实测试需在独立测试对话进行。

## 单次截图

```powershell
.\build\x64\Release\stronghold-capture.exe capture --output .\captures
```

可用参数：

- `--hwnd <十进制|0x十六进制>`：指定窗口句柄；仍须通过游戏窗口筛选。
- `--method auto|wgc`：`auto` 当前也使用 WGC；`wgc` 显式指定同一后端。

命令不会等待或激活前台窗口。窗口须未最小化、可见、完整位于实际显示器区域、判定为不透明且无已知遮挡。采集期间父进程约每秒复核，并在开始、采集后及发布前检查窗口条件；检测到条件变化即取消并清理未发布的临时目录。忙碌或 500 ms 启动间隔未到时立即返回 `capture_busy` / `capture_throttled` 和 `retry_after_ms`，不排队。这个限流只在当前进程内共享；独立 CLI 进程没有共同 gate，也没有应用单实例锁。

采集不改变游戏窗口焦点、位置、尺寸或透明度，不注入输入。WGC 的系统默认捕获边框策略保持不变，Windows 可能显示捕获提示边框；鼠标指针不进入图像。

每张完成采集的图像都会归档为 GUID 目录中的 `raw.png` 与 UTF-8 `metadata.json`，包括 PNG 编码期间画面年龄超过 1 秒的情形。metadata 的 `frame_age_ms_at_archive_check` / `frame_freshness` 在 PNG 编码后、写入 metadata 前检查；CLI 返回的 `frame_age_ms_at_delivery_check` / `frame_freshness_at_delivery_check` 在发布后再次检查。因此 metadata 仍可标 `fresh`，而 CLI 交付检查因后续元数据写入/复核/发布耗时已变为 `stale`。`fresh` 表示检查时年龄 ≤1000 ms；`stale` 表示超过该值；`unknown` 表示无可用年龄。过期或未知画面可用于历史存档，但不能直接作为已确认的当前状态驱动操作。当前生产 WGC 路径会拒绝无时间戳或过期帧；归档仍可能因编码耗时变 stale。

metadata 使用 schema 2。`source_frame.time` 是 WGC `SystemRelativeTime` 的 QPC 100 ns 计数，以十进制字符串保存，避免 JSON 浮点数精度损失。窗口身份不可完整验证时，路径/创建时间留为 `null` 并记录警告；若复核中身份从 verified 降为 limited，则拒绝继续。程序不会自动提权。性能字段包括 PNG 编码时间、归档检查耗时、PNG 字节数，以及父/工作进程各自记录的 working-set 峰值；它们不是 GPU 内存，也不能相加后代表截图相关进程总峰值。

详细行为、限制、metadata 字段及尚待实机验证项目见 [截图设计](docs/capture.md)；第三方来源与许可证说明见 [第三方说明](docs/third-party.md)。

## 离线存档检查

`build\x64\Release\stronghold-capture.exe inspect-archive --archive <存档目录>` 只读检查 schema 2 的 `metadata.json` 与 RGB8 `raw.png`，输出记录标识、尺寸和原始来源元数据。此检查命令不运行分类或字段识别；分析使用 analyze-archive。离线记录一律为 `historical`；元数据中的 `fresh` 仅代表原存档写入时的检查，不表示当前画面。

C++ 调用入口为 `stronghold::load_capture_archive`，见 `include/stronghold/recognition.hpp`。局部编译与合成存档检查：`pwsh -File scripts/build.ps1 -TestRecognition`（不会运行 capture worker suite）。格式限制、错误码与验证范围见 [离线存档读取](docs/archive-reader.md)。

## 离线画面分析（有限支持）

`build\x64\Release\stronghold-capture.exe analyze-archive --archive <存档目录> --maa-root <可信 MAA 安装绝对路径>` 使用本地 MAA PaddleOCR 神经模型与 ONNX Runtime，读取兼容原图并输出画面类别、文字证据、原始来源及模型/字典指纹。当前支持 1920×1200 的主菜单、难度页、普通休整/战斗/战后等待，以及特殊信息页/结果覆盖层（special_in_game，special_view 为 information/result）。加载分类尚不支持；证据不足画面返回 unknown。所有离线结果保持 historical。具体范围、保守映射、错误边界和开发 TDD 记录见 [画面分析](docs/screen-analysis.md)。

运行时和模型来自显式指定的可信本地安装，项目不重新分发这些文件。局部检查仍为 `pwsh -File scripts/build.ps1 -TestRecognition`；真实开发正例通过文档列出的环境变量显式选入，默认不读取真实 captures。独立样本评估使用 `scripts/evaluate-recognition.ps1`，开发与验证的用途分离，未知拒识不充当 loading 分类成功。

当前未通过完整识别验收：新增预留组入口／休整正确，加载按约定拒识，三张战斗均被拒识，ticket02仍未完成，见 [最新分类验证](docs/ticket02-validation-20261115.md)。此前加载误报及资金拒识的 [历史报告](docs/state-recognition-validation.md) 保留。阶段／时间／资金不会以旧值补齐。

历史状态持久化与重新读取见 [docs/state-records.md](docs/state-records.md)。

离线分类与普通局内字段分析现已提供有限的 1920×1200 支持范围（普通休整/战斗/战后等待、局外菜单/难度页、特殊信息页/结果覆盖层），参见 [核心字段识别](docs/core-field-recognition.md) 和 [历史状态记录](docs/state-records.md)。支持范围及尚未验证事项以对应文档为准。
