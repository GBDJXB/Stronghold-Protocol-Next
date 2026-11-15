# 离线画面分析：首版范围

## 冻结的保守映射

本票有限首版输出 `ordinary_in_game`、`out_of_game`、`unknown`。当前加载分类尚不支持：安全契约要求真实 loading 返回 unknown；实际是否满足须由冻结后的独立样本核验，不能由有限文本规则推导普遍保证。返回 unknown 仅计拒识，不计 loading 分类正确。

- 信息页 S016、结果覆盖层 S034/S035、开局选择 S051–S054：unknown。
- 不稳定过渡 S018/S036：unknown；不借地图或 HUD 把过渡确认成普通局内。
- 这些映射是输出契约，不改变素材真实类别、分组、原图和人工字段语义。
- 未覆盖分辨率返回 unknown。首批只覆盖 1920×1200；近重复帧不代表独立场景。

## 输入与输出

`analyze-archive --archive <兼容存档目录> --maa-root <可信本地 MAA 安装目录>`。

C++ 入口 `analyze_capture_archive` 返回 Analysis，保留完整 LoadedCapture（frame_id、原目录、RGB、原始 metadata、请求 UTC 与来源 QPC）。输出一律 historical。分类不产生字段当前值，不发送游戏输入。

本地神经文本识别读取输入 RGB 的指定文本行，使用外部 MAA 通用 PaddleOCR rec ONNX；模型不包含本项目样本 ID，也不读取标注清单。分类只使用网络输出和固定文本锚点。没有从 manifest 查询预测结果的路径。样本清单仅供独立 runner 对照。

模型和字典不随项目复制；用户显式指定可信 MAA 根目录。运行时版本和模型 SHA256 随分析记录输出。加载外部 DLL 等同执行该可信安装中的代码；这不是任意来源插件发现机制。

## 验证规则

开发只允许 development-index 中 `use_for_model_development=true` 的图像。预留验证图片在模型冻结前不查看、不调参；评估一次后若据其像素或结果调参，原独立组须退役。

`scripts/evaluate-recognition.ps1` 校验 manifest 与索引的完整记录、显式布尔字段和重复 ID，输出逐图结果；只有显式 evaluation=true 的候选计入独立指标。加载和特殊画面误报普通局内单列。真实未知负例仍缺，不能据已知特殊页的拒识宣称开放集安全。

## TDD 与检查记录（开发期）

通过已批准的 archive-analysis public seam 逐个进行 red/green：

1. 合成 2×1 存档未知分类与来源保留：最初 stub 抛出 not_implemented，测试失败；只实现兼容存档读入和 unsupported_dimensions→unknown 后，同一测试与已有 archive suite 通过。
2. 真实开发难度页 S003：在仅返回 unknown 的实现上测试失败，错误为 Development difficulty screen was not locally recognized with source/model provenance。后续 neural 实现必须使该观察通过。

真实开发 fixture 通过 `STRONGHOLD_DEVELOPMENT_DIFFICULTY`（存档目录）与 `STRONGHOLD_MAA_ROOT` 显式选入；默认合成 suite 不暗中读取 captures 或预留图像。

初步开发探索使用 Python ONNX Runtime CPU 调用已安装模型，只用于核对神经推理可行性与文本行位置；产品路径为 C++ 调用原生 C API，不引入 Python 运行时。没有训练、fine-tune 或从样本 ID 到标签的查表预测。

## 当前已实现的正例边界

- 局外：难度页精确识别「请选择模拟难度」与「开始模拟」（各平均 CTC 字符置信度 ≥0.95）；主菜单同时识别「终端」（≥0.80）和「编队」（≥0.90）。
- 普通休整：必须同时识别回合标签、休息一下、准备就绪、刷新、收起（各 ≥0.95）；不从单一 HUD 存在下结论。
- 普通战斗：必须同时识别回合标签、形如数字/数字的敌人进度和当前布局的「坚守」盟约标签（各 ≥0.95）。这里不解析字段值；其他盟约布局可拒识，不宣称通用战斗分类。
- 在普通局内判定前读取开发结果层的两个独立文本区域。任一高置信「作战结束」或「完美作战」使结果 unknown，即便顶部 HUD 仍存在。
- 信息页、过渡缺少这些正向证据而拒识。未知加载设计、其他特殊覆盖、语言、地图与 UI 改版仍可能误判，不能把有限文本 veto 当成通用覆盖层检测器。

CTC confidence 是网络字符概率的均值，不是校准后的画面分类概率。evidence 包括实际文字与该均值，方便审计；不据开发置信度推导真实准确率。

开发数据检查：原生 Release 构建后 S003、S015、S020、S034、S001 的各自 public tests 全部 green；S016、S018、S035、S036 通过同一公开 CLI 均返回 unknown。S034 曾在加入结果层 veto 之前被误报 ordinary，public test 首先 red，再加入 veto 后 green。S015/S020/S001 各自在仅实现前一类时先 red 再 green。最后组合运行所有这些 public tests 与既有存档测试通过。

尚未运行预留验证；未宣称 ticket 02 四类完整验收通过。父集成对话可运行冻结候选评估，结果另行记录。离线真实存档检查不操作游戏，不等同独立实机采集验收。检查过程中一次合成存档发布出现 system code 5，未改动实现，下一次同一检查正常完成；这次系统临时目录错误不计为分类失败或通过证据。

## 资源与复杂度

神经计算使用 CPU、单线程 session，避免为当前离线路径引入 GPU/provider 选择。指定安装必须包含匹配的 rec 模型与完整字典；输出形状/字典长度不匹配、非有限概率、错误 crop 和运行时加载错误会明确失败。缺少资源不会返回猜测的已知画面。

单次分析只读单帧，时间成本为存档解码 O(pixels + metadata + PNG bytes)、固定数量 crop 的缩放 O(crop pixels) 与模型推理；推理复杂度依模型和各行宽度决定，不是常数时间承诺。额外内存含解码 RGB、模型 session 和一行 input/logits，输出分类自身 O(1)，完整来源为 O(pixels + metadata)。不缓存前一帧结果，也不覆盖原图。

开发组完整离线运行：`scripts/evaluate-recognition.ps1 -Split development` 对 35 张仅开发样本执行原生 CLI。13 张局外均正确；17 张稳定普通局内中 16 张识别、S019 战斗开始横幅拒识；2 张过渡与 3 张特殊信息/结果画面均 unknown；开发组加载样本为 0。加载/特殊/过渡误报 ordinary 为 0（不推导未见加载的安全性）。这些是开发回代结果，不能称为独立准确率，难度页含 12 张近重复。

完整来源与逐图证据报告保存在 `captures/state-recognition-20261115-050329/classifier-02-development.json`（不纳入代码提交）。正式评估候选尚未执行。S019 明确列为当前拒识边界，不为提高开发分数放宽普通战斗规则。
