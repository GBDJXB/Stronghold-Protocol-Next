# 第三方来源与依赖

## 当前采集实现：Windows WGC 与微软示例

当前程序直接使用 Windows Graphics Capture、D3D11、DWM 与 WIC 系统接口，使用已安装 Windows SDK 中的 C++/WinRT 头文件。

WGC/Win32 HWND 接入参考 [Microsoft Windows.UI.Composition-Win32-Samples / ScreenCaptureforHWND](https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/tree/master/cpp/ScreenCaptureforHWND)。微软示例采用 MIT，许可副本保留于 [Microsoft-Win32-Capture-LICENSE.txt](../third_party/Microsoft-Win32-Capture-LICENSE.txt)。本项目的隔离工作进程、客户区校验、时间戳传递和存档为项目内封装，不迁入示例的 UI 或窗口操作流程。系统 SDK 的使用仍受其相应条款约束。

## 参考实现边界

[MAA 截图研究](research-maa-pc-capture.md)及其他 `research-*.md` 文档保留历史方案评估和上游来源链接，不代表当前构建或分发依赖。

本地 Stronghold-Protocol-Auto 用于参考原始尺寸、进程超时隔离与来源复核经验；Next 独立实现 C++ 接口，不迁入其 Python 运行时、输入控制或策略模块。

PNG 编码/解码使用 Windows WIC。
