# 历史状态记录

## 使用

保存分析结果（不发送游戏输入）：

```powershell
./build/x64/Release/stronghold-capture.exe analyze-archive --archive <兼容截图目录> --maa-root <可信MAA安装目录> --state-output <状态目录>
./build/x64/Release/stronghold-capture.exe inspect-state --record <输出的state_record文件>
```

`--state-output` 可选；未指定时不写状态。每次分析生成独立 UUID JSON 文件，先写唯一临时文件、flush，再无覆盖发布；失败返回 `state_io`。既有状态文件和截图原图不会被覆盖。这里只提供本地文件持久化，不提供完整断点恢复或跨局推断。

## 契约

C++ public seam：`save_state_record(Analysis, output)` 返回文件路径；`load_state_record(file)` 返回不含 RGB 的 `StateRecord`。`state_record_json` 为 CLI 提供同一 schema 的 JSON。状态 schema 1 保存分类及原因、funds 值/status/confidence/reason/raw_text、分类模型和字段模型 SHA256、字典 SHA256、运行时与识别器版本、evidence、原存档绝对路径/frame_id/原始 metadata JSON。

重新读取只解析状态文件，不打开来源目录，因此原截图移动或暂时不可访问仍可查看历史结果。来源路径是历史关联；这不证明该路径当前仍对应原图。原 metadata 的请求起止 UTC 和来源 QPC 原样保留，UTC 请求时间不当作 QPC 来源帧的真实墙钟时间。`saved_at` 是创建状态记录的 UTC 时间，不是模型时间或来源帧时间。

所有保存/重读结果的 `observation_status` 固定为 `historical`。`funds.status=recognized` 与整数值一起保存，`unknown` 的值必须是 JSON null；合法的已识别 0 仍是数字 0。读取历史记录不把字段升级为当前确认状态。未知新结果保存在新文件，旧文件保留；没有“拿上次已知值覆盖未知”的路径。

读取要求完整字段和支持的 schema，检查字段类型、非负整数、有限且在 [0,1] 的置信度、已识别字段模型来源、状态和值一致性、真实 UTC 日期以及嵌入来源 frame_id/请求时间的一致性，并要求来源 QPC 时钟/单位/可空整数时间及窗口 hwnd/pid/可空创建时间类型完整。错误包括 `state_missing`、`state_path`、`state_io`、`state_version`、`state_invalid`、`state_resource_limit`；不返回部分已确认状态。输入必须是普通文件且不是 reparse point。JSON 上限 2 MiB，嵌入 metadata 上限 1 MiB；大文件在解析前拒绝，读取过程中也实施上限。

## 验证及边界

公开 save/load seam 的合成已识别字段 roundtrip 首先在未实现 stub 上 red（`not_implemented`），实现后 green。继续验证 unknown/null、先前历史不被覆盖、来源存档移动后仍能读取、版本/缺失/损坏/非法日期/矛盾字段/超限拒绝，以及无效写入不产生输出目录。默认 `scripts/build.ps1 -TestRecognition` 包含这些检查，无需加载神经模型。

显式开发 S015 fixture 的实际 funds=4 分析结果也经过保存/重读，字段模型来源与历史有效性保持；同时通过两个独立 CLI 进程验证保存和重新读取。只使用开发图，不读取预留图。测试支持持久化契约，不增加识别准确率或当前状态可靠性的宣称。

时间与额外空间复杂度均为 O(record bytes)，上限由固定文件限制约束；不复制 PNG 或 RGB。发布使用同目录临时文件和 Windows 无覆盖移动，不承诺断电情况下的完整目录事务或外部文件不可篡改。
