# 第三方来源与依赖

## 当前采集实现：Windows WGC 与微软示例

当前程序直接使用 Windows Graphics Capture、D3D11、DWM 与 WIC 系统接口，使用已安装 Windows SDK 中的 C++/WinRT 头文件。

WGC/Win32 HWND 接入参考 [Microsoft Windows.UI.Composition-Win32-Samples / ScreenCaptureforHWND](https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/tree/master/cpp/ScreenCaptureforHWND)。微软示例采用 MIT，许可副本保留于 [Microsoft-Win32-Capture-LICENSE.txt](../third_party/Microsoft-Win32-Capture-LICENSE.txt)。本项目的隔离工作进程、客户区校验、时间戳传递和存档为项目内封装，不迁入示例的 UI 或窗口操作流程。系统 SDK 的使用仍受其相应条款约束。

## 参考实现边界

[MAA 截图研究](research-maa-pc-capture.md)及其他 `research-*.md` 文档保留历史方案评估和上游来源链接，不代表当前构建或分发依赖。

本地 Stronghold-Protocol-Auto 用于参考原始尺寸、进程超时隔离与来源复核经验；Next 独立实现 C++ 接口，不迁入其 Python 运行时、输入控制或策略模块。

PNG 编码/解码使用 Windows WIC。

## 离线识别：ONNX Runtime C API

只随项目分发微软 ONNX Runtime v1.24.4 的 `onnxruntime_c_api.h` 和 `onnxruntime_ep_c_api.h`，取自 [官方 v1.24.4](https://github.com/microsoft/onnxruntime/tree/v1.24.4/include/onnxruntime/core/session)，MIT 许可副本为 `third_party/onnxruntime-LICENSE.txt`。不复制第三方运行时 DLL 或模型。

推理时要求显式可信 MAA 安装目录，动态读取该目录的 `onnxruntime_maa.dll`（本地核查 1.29.0，可请求 C API 24）与 `resource/PaddleOCR/rec` 模型和字典。C API 版本不可用、模型错误与依赖缺失须明确报错，不替换成猜测分类。MAA 的任务文件 `resource/tasks/MiniGame/SPA.json` 文档标明「卫戍协议：盟约」，不能把该任务图、坐标或模板作为本项目「规程」的已验证规则。MAA 对通用 OCR 模型的封装和图像理解不等于本票分类验收；本票在自己的归档 RGB 上进行神经推理。

安装目录中的 MAA 组件、ONNX 模型及其字典各受原分发许可约束。当前实现引用用户已有安装，不将该安装的资源重新打包或宣称模型归本项目所有。未来若分发模型，应先单独核查相应模型许可。
