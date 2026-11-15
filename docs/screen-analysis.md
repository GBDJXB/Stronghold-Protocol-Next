# 离线画面分析：首版范围

## 冻结的保守映射

当前有限实现输出 `ordinary_in_game`、`out_of_game`、`special_in_game`、`unknown`。加载分类尚不支持：安全契约要求真实 loading 返回 unknown。v5新增预留评估中两张加载符合拒识契约，但三张普通战斗被拒识，ticket02仍未通过，见 [最新验证](ticket02-validation-20261115.md)。旧S049/S050误报局外的 [历史报告](state-recognition-validation.md) 不变。返回 unknown 仅计拒识，不计 loading 分类正确。

- 开发信息页 S016、结果覆盖层 S034/S035：`special_in_game`，`special_view` 分别为 `information` / `result`。开局选择 S051–S054 尚不支持，目标仍为 unknown；未查看这些预留图片或据其结果调参。
- 不稳定过渡 S018/S036：unknown；不借地图或 HUD 把过渡确认成普通局内。
- 这些映射是输出契约，不改变素材真实类别、分组、原图和人工字段语义。
- 未覆盖分辨率返回 unknown。首批只覆盖 1920×1200；近重复帧不代表独立场景。

## 输入与输出

`analyze-archive --archive <兼容存档目录> --maa-root <可信本地 MAA 安装目录>`。

C++ 入口 `analyze_capture_archive` 返回 Analysis，保留完整 LoadedCapture（frame_id、原目录、RGB、原始 metadata、请求 UTC 与来源 QPC）。输出一律 historical。分类和字段均为历史观察，不产生当前确认值，不发送游戏输入。

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
- 普通战斗：必须同时识别回合标签、形如数字/数字的敌人进度和当前布局的「坚守」盟约标签（各 ≥0.95）。该分类门控之外，核心阶段/时间字段解析见 core-field-recognition.md；其他盟约布局可拒识，不宣称通用战斗分类。
- 在普通局内判定前读取开发结果层的两个独立文本区域。两者满足当前正例契约时输出特殊结果层；只有一个高置信锚点时仍 veto 为 unknown，即便顶部 HUD 仍存在。
- 信息页使用下述独立正例锚点；过渡缺少稳定普通局内正向证据而拒识。未知加载设计、其他特殊覆盖、语言、地图与 UI 改版仍可能误判，不能把有限文本 veto 当成通用覆盖层检测器。

CTC confidence 是网络字符概率的均值，不是校准后的画面分类概率。evidence 包括实际文字与该均值，方便审计；不据开发置信度推导真实准确率。

ticket 02 历史开发检查（特殊页 unknown 结果已由下述 ticket 06 正例支持取代）：原生 Release 构建后 S003、S015、S020、S034、S001 的各自 public tests 全部 green；S016、S018、S035、S036 通过同一公开 CLI 均返回 unknown。S034 曾在加入结果层 veto 之前被误报 ordinary，public test 首先 red，再加入 veto 后 green。S015/S020/S001 各自在仅实现前一类时先 red 再 green。最后组合运行所有这些 public tests 与既有存档测试通过。

尚未运行预留验证；未宣称 ticket 02 四类完整验收通过。父集成对话可运行冻结候选评估，结果另行记录。离线真实存档检查不操作游戏，不等同独立实机采集验收。检查过程中一次合成存档发布出现 system code 5，未改动实现，下一次同一检查正常完成；这次系统临时目录错误不计为分类失败或通过证据。

## 资源与复杂度

神经计算使用 CPU、单线程 session，避免为当前离线路径引入 GPU/provider 选择。指定安装必须包含匹配的 rec 模型与完整字典；输出形状/字典长度不匹配、非有限概率、错误 crop 和运行时加载错误会明确失败。缺少资源不会返回猜测的已知画面。

单次分析只读单帧，时间成本为存档解码 O(pixels + metadata + PNG bytes)、固定数量 crop 的缩放 O(crop pixels) 与模型推理；推理复杂度依模型和各行宽度决定，不是常数时间承诺。额外内存含解码 RGB、模型 session 和一行 input/logits，输出分类自身 O(1)，完整来源为 O(pixels + metadata)。不缓存前一帧结果，也不覆盖原图。

开发组完整离线运行：`scripts/evaluate-recognition.ps1 -Split development` 对 35 张仅开发样本执行原生 CLI。13 张局外均正确；17 张稳定普通局内中 16 张识别、S019 战斗开始横幅拒识；2 张过渡与 3 张特殊信息/结果画面均 unknown；开发组加载样本为 0。加载/特殊/过渡误报 ordinary 为 0（不推导未见加载的安全性）。这些是开发回代结果，不能称为独立准确率，难度页含 12 张近重复。

完整来源与逐图证据报告保存在 `captures/state-recognition-20261115-050329/classifier-02-development.json`（不纳入代码提交）。正式评估候选尚未执行。S019 明确列为当前拒识边界，不为提高开发分数放宽普通战斗规则。

ticket 02 历史评估报告（特殊页拒识统计已由 ticket 06 的开发结果取代）：评估报告 schema 2 将真实语义分类结果与首版安全契约分开：已知画面输出 unknown 一律记为 rejected，即便该画面的冻结目标就是 unknown；`frozen_contract_match` 单独记录是否符合保守目标。开发组按真实语义统计为 ordinary_in_game 正确 16、拒识 3（含两张过渡），out_of_game 正确 13，special_in_game 拒识 3。既有开发 JSON 已仅据保存的原始分析重新解释上述两项，未重读图像或重跑模型；报告保留原始 executable/model/source 指纹及解释更新说明。

## Ticket03：单个资金字段

Analysis.funds 为 NumericObservation：status（recognized / unknown）、optional<int> value、confidence、reason、raw_text、model_sha256 与 dictionary_sha256。字段来源直接使用同一 Analysis.source，包含 frame_id、存档路径、原始 metadata、请求 UTC 与来源 QPC；CLI funds JSON 与分类、metadata 一起输出。未知 value 必须为 null；零是有效数值。无跨帧缓存，不能复用上一次资金。

本版只在 stable_rest_text_anchors 的 1920×1200 普通休整布局尝试资金。战斗白色 C 是部署费用，不做资金；分类局外、未知、加载未支持及特殊页／过渡时资金保持 unknown。S036 人工资金标签5不改变，但因过渡分类 unknown 不输出已确认资金。

数字识别使用外部 MAA resource/PaddleCharOCR/rec/inference.onnx 和 keys.txt，经同一个原生 ONNX Runtime API24 后端完整 CTC 解码。未限制 logits 到数字子集、未把字母 O 转换为零。通用中文模型实际将 S017 读为0但字符置信度约0.706，低于0.95，故不能用降低阈值通过该例；CharOCR 原生实际为0、0.976443。分类保留通用模型哈希；资金单独保存字符模型和字典哈希，runtime_version 相同。模型仍由用户可信 MAA 安装提供，不复制模型／DLL；第三方许可与复用边界沿用 docs/third-party.md，不能由代码许可推断模型可再分发。

有限布局契约：只支持单个数字，读图 ROI x1776,y890,width44,height46，完整解码必须匹配 [0-9] 且平均字符概率≥0.95。黄色资金图标区域 x1730..1775,y890..935 至少200个候选黄色像素作为语义布局守卫；此外检查 x1772..1899,y890..935 中数字安全框 x1778..1815,y896..930 外是否存在亮金色像素，有则拒识 unsupported_funds_extent，避免窄 ROI 接受明显超出边界的金额片段。该守卫是显式颜色／布局假设，不替代神经识别，不证明任意字体、颜色或多位数截断都能拒识。多位余额尚不支持；当前样本仅验证0和4，不宣称0–9全部数字准确率或任意余额识别。

TDD 公开入口：S015资金4先在默认 unknown 时 red，再由原生识别 green；去掉黄色图标的独立临时存档先 red（仍接受4），加入布局守卫后 green；S017零因通用模型置信度不足 red，接入字符模型后 green；在窄 ROI 外添加亮金色笔画先 red（仍接受4），加入范围守卫后 green。黑掉数字区域但保留图标的临时存档仍 ordinary_in_game，字段 unknown/unreadable_funds，不变为零；输出关联该临时存档自身的 frame_id。全部修改通过 public save_capture 生成测试临时存档，不修改原始素材。

开发组合检查：S015=4、S017=0、S020战斗资金unknown、S036过渡分类和资金unknown、S001局外资金unknown，以及上述三个派生软件 fixture 和原有存档异常测试全部通过。真实开发 fixture 由 STRONGHOLD_DEVELOPMENT_FUNDS_FOUR / STRONGHOLD_DEVELOPMENT_FUNDS_ZERO 显式指定目录；默认 suite 不读游戏图片。不读取预留图片／结果调参，S055待父集成对话冻结后独立评估。自然不可读资金、其他数值／地图／布局／分辨率、独立场景统计准确率、真实游戏采集测试均未验收；ticket02加载分类仍未完整支持，不据本票宣称整体规格完成。

增加的固定区域扫描 O(crop pixels)，CTC 完整解码 O(T×C)，另含单次字符模型加载／推理；T 为输出步数、C 为字典类别数。内存含额外字符模型 session、输入／logits；NumericObservation 本身 O(text bytes)，不缓存额外帧。不为当前单次离线分析引入长生命周期模型缓存。


## Ticket06：有限特殊画面与状态隔离

本轮冻结映射将 development 信息页与结果覆盖层支持为 special_in_game；其他过渡、开局选择和未支持加载仍目标 unknown。不改写原始人工标注、manifest、分组或 expected_classifier_output=null。评估 runner 的派生政策 `ticket06-information-result-v2` 仅将 candidate_special_in_game 中信息页／战斗结果语义映射为 special_in_game；报告 schema3 包含特殊类别计数。真实类别的 unknown 仍计 rejected；frozen_contract_match 单独表达安全契约，不把拒识充当正确分类。

信息页要求三个独立神经文本精确匹配，平均 CTC 字符概率各≥0.95：核心盟约（108,192,165,49）、附加盟约（108,507,165,48）、敌方领袖（1594,485,160,42）。开发 S016 实际为0.973579、0.999140、0.999504。不是根据地图缩略图判定，也不声称已提取盟约、Boss 或敌人数据。

结果覆盖层要求现有两个区域同时提供证据：作战结束精确匹配、另一行包含完美作战，各≥0.8。S035 实际0.999253与0.997958。任一旧 veto 锚点存在而另一锚点不足时返回 unknown，禁止落入残留普通 HUD 的判定路径。

特殊页在普通字段提取前返回。资金保持 unknown/null，无历史值搬运；每份 Analysis 自带独立 LoadedCapture。`special_view` 在 CLI、StateRecord 和状态 JSON 中保留，非特殊类别须为空，特殊类别只接受 information/result。状态 schema2 显式要求该字段，旧 schema1 明确 state_version 拒绝，不静默补造字段。本轮与 ticket05 属同一未发布 schema2 扩展，最终字段以合并后的契约为准。

TDD 使用已批准公开 analyze/save/load seam：开发 S016 在分类实现前首先失败；加入三锚点后分类通过。随后同一真实开发信息页保存测试因 state_invalid category 首先失败，加入类别、子类型严格读写后通过。S034/S035 同一公开测试通过；特殊快照重读保持 historical、自身 frame_id 与 unknown 资金，并断言先前资金4的快照及旧 frame_id 不变。默认合成存档/状态异常测试亦通过。环境变量 STRONGHOLD_DEVELOPMENT_SPECIAL 显式选择开发存档，不扫描预留组。

未覆盖：干员选中偏移视角、敌人待命地图、其他信息页／结果类型、开局选择2/3与3/3、未知负例、自然模糊遮挡、其他布局/分辨率、加载分类。没有素材就保留不支持，不构造虚假验收。当前仅支持上述固定1920×1200布局；这些开发检查不是独立准确率或实机采集验收。无游戏输入。

新增三个固定文本 crop，每帧仍为单帧解码、固定数量 crop 与模型推理；复杂度沿用前述 O(pixels+metadata+PNG bytes) 及模型推理成本。状态快照为不可变历史，无前帧缓存或覆盖写入。


本轮35张开发存档通过逐图公开CLI运行（只读 development-index）：16 ordinary_in_game、13 out_of_game、3 special_in_game（S016 information、S034/S035 result）、3 unknown（S018/S019/S036）。三个特殊页资金均 unknown。报告保存在实现 worktree 的 build/ticket06-development.json，不纳入提交；不是独立评估。没有读取预留图像或结果。

## Ticket02 补充开发素材修复（v5）

仅使用 20261115-095728 已人工复核的26张开发代表图；不读取未复核序列或验证像素，不修改素材标签和划分。`maa-text-core-special-v5` 保持1920×1200有限布局、历史来源关联和既有字段持久化契约。

新增局外入口要求「协议数据库」和「关键目标」两处精确文本，各≥0.95。加载仍输出unknown；先检查底部「正在提交反馈」前缀（ROI 1086,1095,280,45，平均字符置信度≥0.90），避免后面的难度页正例锚点覆盖加载反馈。此为已见反馈布局的拒识守卫，不宣称支持loading分类或所有加载动画。

普通战斗保留回合/敌人进度门控，允许旧「坚守」或新「精准」盟约位置各≥0.95；并在普通判定前拒识开发已见盟约说明中的「层数」。普通休整仍要求回合、休息一下、准备就绪；展开商店要求刷新和收起，折叠商店要求展开。折叠商店资金保持unknown/shop_collapsed，不读取隐藏金额。新增拒识锚点为攻击范围（选中干员）、天空盒（资源说明）、保护目标（目标说明），各≥0.95；均是固定位置可读文本，不能保证其他说明层或任意偏移布局安全。

滚动后的完整本局信息页使用右侧「敌方领袖」「特训敌人·元素」「特训敌人·隐匿」三处精确锚点各≥0.95，输出既有information子类型，普通字段仍unknown；此正例要求已见敌方特训组合，不泛化所有信息页。暂离确认、开局选择、过渡和Loading标题图在当前已见开发图缺少普通正例锚点而拒识。

TDD继续使用获准的analyze_capture_archive公开入口。入口、精准战斗、折叠休整、滚动信息页依次unknown→预期类别；精准战斗放宽后盟约说明曾ordinary→新增层数守卫后unknown，折叠休整放宽后天空盒/选中干员曾ordinary→各自守卫后unknown。保护目标说明在旧实现已误报ordinary，守卫后unknown。每项保留显式开发路径和预期类别的公开fixture入口STRONGHOLD_DEVELOPMENT_CLASSIFICATION / STRONGHOLD_DEVELOPMENT_EXPECTED_CATEGORY，检查自身来源和非普通字段隔离。合成回归将已复核开发加载反馈条复制到已复核开发难度图，保留难度页文字；先误报out_of_game，再由加载守卫拒识。该合成图只证明该门控优先级，不充当真实加载准确率证据。

复杂度仍为单帧解码O(pixels+metadata+PNG bytes)、固定数量crop预处理及神经推理；新增固定crop增加推理成本，不作实时性能承诺。额外内存沿用单帧RGB和模型session，无跨帧状态。资金、时间数字及stage推断未扩展识别语义，不据新分类结果宣称后续字段验收通过。

本轮26张已复核开发回代：4张局外、9张普通局内、2张完整信息/结果页语义分类正确；11张按保守契约拒识（loading2、special_setup3、transition1、special_in_game说明/选中/暂离5），0误判，26/26冻结契约匹配，0不适当ordinary。此为调参开发回代，不是独立准确率；报告位于实现worktree的build/ticket02-reviewed-development.json。Release和TestRecognition（合成存档/持久化/CLI异常检查）通过；默认系统临时目录曾有rename AccessDenied，改为worktree build/temp执行通过，不改产品实现。新验证由父集成对话冻结后执行，旧验证失败记录不被本次开发回代替代。
