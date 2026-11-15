# 离线截图存档读取

入口：`load_capture_archive(directory)` 与 `inspect-archive --archive <directory>`。读取 schema 2 的原始 packed RGB8 PNG，不缩放、不裁剪、不重编码、不修改存档。返回 `frame_id`、完整 `Capture` 来源与 RGB 像素，以及原始 UTF-8 JSON（保留 archive-time metrics）。目录可重命名；关联键是元数据内的 `frame_id`。

请求 UTC (`request_start/end`) 与 WGC QPC (`source_frame.time`) 分开保留。QPC 字符串按 int64 精确解析，`process_created` 按 uint64 精确解析。离线读取不能利用这些 QPC 或原有 `frame_freshness` 判断当前新鲜度。CLI 显式输出 `observation_status: historical`。

## 支持与边界

- schema 2、WGC backend、RGB8、单帧原始 24-bit RGB/BGR PNG；不将 alpha、palette、灰度或其他格式自动转换为成功。
- 检查所有现有必需字段及类型、有限非负时长、整数范围、规范 UTC 时间和日历日期，raw 尺寸必须等于 source client 与 PNG 尺寸；检查 `performance.png_bytes` 与文件长度。
- 仅允许 `raw.path` 为字面值 `raw.png`。两个输入文件必须为普通文件且无 reparse point；不读取 metadata 指定的其他路径。调用者指定的目录可位于其选择的位置。
- metadata 最多 1 MiB；压缩 PNG 最多 128 MiB；单边最多 16384 像素、总量最多 33554432 像素（解码 RGB 最多 96 MiB）。WIC 自身还有解码内部开销。读取复杂度为 O(metadata bytes + PNG bytes + pixels)，主要应用内存为 O(metadata bytes + pixels)。上限用于限制离线输入，非性能目标。
- Windows.Data.Json 将 JSON number 表示为 double。数值型 HWND、occluding HWND 和性能计数仅接受 0..2^53-1 的整数，超过范围拒绝，避免静默舍入；pid 保持 uint32。schema 的时间/进程创建标识是十进制字符串，不受 double 精度限制。未知附加字段保留在原始 JSON 中。
- 本地文件读取期间不应由其他程序并发改写。尺寸、类型及文件检查不提供针对恶意并发替换文件的事务快照，也不提供密码学来源认证。

## 错误分类

`archive_metadata_missing` / `archive_image_missing`：必需文件缺失或不可访问。
`archive_metadata_invalid`：无效 JSON/UTF-8、缺失字段、类型/值非法。
`archive_version`：不兼容 schema。
`archive_path`：不支持的文件路径或 reparse point。
`archive_resource_limit`：输入资源超过上限。
`archive_image_invalid`：PNG 损坏、文件类型/帧数/长度不一致。
`archive_image_dimensions`：尺寸不一致。
`archive_image_format`：不支持的像素格式。
`archive_io`：读取或文件大小检查失败。

CLI 成功输出 JSON；这些输入失败经现有 Error 通道返回 `ok:false`，退出状态 2。

## 验证记录

仅使用 `save_capture` 生成的合成 2×1 RGB 图像与可控损坏副本，不读取真实 captures，不操作游戏，不重复 capture worker suite。

TDD vertical slices（经本地 Release 构建执行）：
1. RGB/来源 public seam：stub 返回 `not_implemented`，测试失败；实现后同一测试通过，RGB 期望字面值为 `[255,0,0,0,128,255]`，进程创建值 `133000000000000001` 精确保留。
2. 非法 UTC 日期：测试用 `2026-02-30...` 替换请求时间，最初失败 `Expected error: archive_metadata_invalid`；加入规范 UTC 与日历检查后通过。
3. 空 source 时间字符串：最初失败 `Expected error: archive_metadata_invalid`；区分 JSON null 与空字符串后通过。
4. CLI：原命令返回 `invalid_argument / Unknown command`；加入入口后不存在存档返回 `archive_metadata_missing`。

回归覆盖：不兼容版本、错误类型、路径逃逸、不支持 raw 格式、来源/raw 尺寸不一致、维度资源上限、大整数精度边界、损坏 JSON/UTF-8、metadata 上限、缺失两种文件、损坏 PNG、重复读取、目录重命名与原 metadata 不变。重复调用同时覆盖 WinRT factory cache 与自有 apartment 的生命周期。

未验证：真实游戏存档、识别准确率、其他 schema/图像格式、长期并发负载与恶意文件并发替换；本票不提供 classifier、OCR 或状态持久化。

最后局部检查：Release 构建与 `-TestRecognition` 通过；独立生成 RGB PNG + schema 2 JSON 的 CLI fixture 返回 `ok:true`、`historical`、2×1 / 6 RGB bytes；缺失目录返回 `archive_metadata_missing`。`git diff --check` 通过。
