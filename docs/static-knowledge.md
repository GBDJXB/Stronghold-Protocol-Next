# 离线静态知识快照（tickets 01–03）

独立 Python 3 工具从显式提供的固定 PRTS 渲染 HTML 提取 manifest 声明的盟约。不会联网、读取截图、运行 OCR 或产生局面观测。支持盟约、干员及规范名称候选查询；通过 manifest 显式声明覆盖范围，不内置三加三样本筛选器。

## 环境与命令

已核验 Windows、Python 3.13.7，依赖固定在 `scripts/knowledge-requirements.txt`：Beautiful Soup 4.14.3、SoupSieve 2.8.3、typing_extensions 4.15.0。解析器显式使用 `html.parser`，不用环境自动选择解析器。默认测试只使用合成 HTML：

```powershell
python -X utf8 scripts/test_knowledge.py
```

离线导入和只读查询（需先准备本地材料与 manifest，store 建议放在被忽略的 `knowledge-local/`）：

```powershell
python -X utf8 scripts/knowledge.py build --source <固定HTML路径> --manifest <manifest路径> --store knowledge-local --snapshot-id <新标识>
python -X utf8 scripts/knowledge.py query --store knowledge-local --snapshot-id <标识> --kind covenant --name <规范名称>
python -X utf8 scripts/knowledge.py query --store knowledge-local --snapshot-id <标识> --kind operator --name <规范名称>
python -X utf8 scripts/knowledge.py candidates --store knowledge-local --snapshot-id <标识> --kind covenant
python -X utf8 scripts/knowledge.py candidates --store knowledge-local --snapshot-id <标识> --kind operator
```

命令成功输出 JSON 并返回 0；来源、结构、版本或完整性错误输出 `status: error` 并返回 1。缺少 CLI 参数由 argparse 返回 2。查询不到返回 `not_in_sample`，仅表示未收录。必须显式指定 snapshot_id，不推断当前版本。

## 手动导入、赛季重建和历史查询

1. 手动获取需要研究的固定渲染 HTML，保留原始字节，并记录原始页面 URL、实际获取时间和时区、页面修订、署名及已知依赖；工具自身不抓取页面。不要把重新保存文件的时间填写为原获取时间。可用 `Get-FileHash -Algorithm SHA256 <HTML路径>` 核对字节指纹，写入 manifest 时使用小写哈希。
2. 直接检查该 HTML 的标题、激活人数、完整效果、干员行、特质段及双向成员关系。manifest 声明本次全部覆盖的盟约和干员；页面修订以来源证据为准，赛季／序列和客户端构建分别填写，无证据用 null。历史依赖不完整需保留限制。名称候选或解析成功都不能证明当前客户端适用性。
3. 用新的 snapshot_id 执行 `build`，然后显式用同一标识执行三类查询，核对完整文本、来源 locator、指纹、coverage 和 applicability。相同材料允许使用另一个新标识重复构建；重复使用既有标识会报错，内容相同也不会覆盖。
4. 赛季或资料更新时，准备新的完整来源材料与 manifest，重新声明目标覆盖范围，使用新标识构建。例如先发布 `season-a-study`，更新后发布 `season-b-study`。无需指定旧快照；新快照只来自本次输入。旧条目若不再声明或不再存在于新的关联中，不会继承到新快照。仍被来源显式关联的样本外盟约名称会继续作为候选出现，效果为 not_in_sample。
5. 历史分析始终保存并使用原 snapshot_id：对同一 store 分别查询 `--snapshot-id season-a-study` 和 `--snapshot-id season-b-study`。没有默认或隐式 latest；标识名称、获取日期和修订号都不会自动选择游戏版本。保留整个旧快照目录，勿手工改写 manifest、HTML 或校验文件。客户端构建未知仍为 null，不能由新赛季名称推导。

构建失败先根据错误修正输入，再使用尚未发布的标识重试；此前成功快照可继续查询。查询错误分别指出 `snapshot_id not found`（标识不存在）、`incomplete snapshot archive`（目录存在但归档文件缺失）、`unsupported snapshot format_version`（不支持格式），以及 SHA256／重提取不一致等损坏信息。错误均返回 1；具体操作系统错误文字可能不同。

同标识并发构建只允许一个发布成功，另一方失败且不覆盖赢家；不同标识各自独立。强制终止留下的 `.pending-*` 不是合法 snapshot_id，不能查询，也不会自动恢复、采用或删除；确认没有构建进程仍在使用后，可由维护者清理残留临时目录。正常失败会清理本次 stage。未提供断电持久性、自动恢复、自动客户端版本探测、定时联网或对外分发能力。

## Manifest 格式 1

以下为合成示例，哈希必须替换为实际 HTML 字节的 SHA-256，名称来自来源正文；不使用预定义样本筛选器：

```json
{
  "format_version": 1,
  "covenants": ["Example"],
  "source": {
    "url": "https://example.invalid/source",
    "acquired_at": "2026-11-15T12:00:00Z",
    "sha256": "<64位小写SHA256>",
    "page_revision": null,
    "attribution": "来源作者及适用的权利说明",
    "dependency_revisions": null,
    "limitations": ["历史模板和间接依赖修订未完整固定"]
  },
  "applicability": {"season": null, "sequence": null, "client_build": null}
}
```

可选 `operators` 字段为规范名称列表（可为空），例如 `"operators": ["Operator A"]`。只有显式提供该字段时才启用干员与双向关系提取；省略时保留 ticket 01 的原始输出结构。列表不允许重复、空名、非字符串或两端空白。

`page_revision` 可为正整数；来源含 wgRevisionId 时必须与之相符，来源不含时必须为 null。获取时间需含时区。赛季、序列、客户端构建独立表达，未知使用 null。`dependency_revisions` 可记载已知依赖；为 null 时必须显式记录限制。来源 URL 和署名由材料提供者声明，工具不能证明其真实性。

## 存储与校验

`<store>/<snapshot_id>/` 包含 `snapshot.json`、`snapshot.sha256` 和固定字节的 `source.html`。JSON 保存来源、manifest、适用范围、提取器及 Python/依赖版本、覆盖范围和盟约记录。来源位置记录归档文件名、全页 table 的一基序号、规范标题、效果行与单元格序号。查询保留全部这些上下文；成员名单与赛季完整性当前均为 false。

提取要求唯一盟约标题、直接 tbody 的三行结构、正数激活人数、非空完整效果单元格及成员区标题；拒绝缺失、重复、哈希不符、修订矛盾及非法 UTF-8。激活人数校验完整标签字段，只接受无前导零的 ASCII 正整数；小数、指数、正负号、分数或尾随文字均拒绝，不截取数字前缀。效果保留文本和 `<br>` 换行、解码实体、去除 HTML 格式标记及各行边缘空白，不执行公式。仅支持已核验的固定渲染布局；不运行 JavaScript/CSS，也不承诺任意网页的浏览器视觉等价。

先在 store 内临时目录写入并重新校验，再在 Windows 同卷重命名发布；存在目标时拒绝覆盖。失败清理本次临时目录，不影响已发布快照。进程被强制终止可能留下 `.pending-*`，查询不会选择这些目录。这里的原子发布不等同于断电持久性保证。

读取时检查 JSON 重复键、版本、SHA-256，再从归档 HTML 和 manifest 重新提取并逐项比较（包含类型）。因此重新计算校验和也不能使错误字段通过语义校验。完整性校验不是数字签名，不能识别有权替换全部材料的攻击者。省略 `operators` 的 manifest 沿用提取器 `1.0.0` 和原有归档字段，旧 ticket 01 快照保持可读；显式 `operators` 使用额外能力标记 `extractor.capability: operators-1`。读取要求受支持的格式、提取器版本、解析器及能力，并严格校验提取器元数据的字段集合、类型和版本字符串。原构建 Python／依赖版本是历史来源记录，不要求与当前校验环境完全相同：当前环境重解析所得业务数据、来源和其余归档字段必须完全一致，否则拒绝读取。成功查询仍返回原始提取器及环境记录，不改写旧归档，也不把当前校验环境伪装为原构建环境；不支持的版本或能力明确报错。

设 HTML 字节数为 H、选定盟约数为 C、干员数为 O、每人最多 M 个盟约名称。在支持的固定嵌套布局下，提取的上界为 O(H + C×O×(O+M) + C log C + O log O)，额外关系校验来自遍历样本交叉组合；空间为 O(H+C×O+O×M)。小样本以明确校验优先，不宣称适合全赛季高频查询。当前读取也重解析 HTML，换取来源与业务字段一致性；尚未做大型数据集或高频查询性能验收。

## 验证记录（2026-11-15）

TDD 公共边界为 CLI：依次验证最小构建/查询、非法输入不发布、损坏快照拒绝、重复生成和旧记录保留。各新增行为先观察 red 再实现；另观察到浮点型激活人数可绕过 Python 字典比较的 red，改为保留 JSON 类型的比较后通过。最终 4 个合成端到端测试通过，覆盖重复标题/标识、缺失结构、空效果、非法 UTF-8、来源哈希、未知格式、快照字段损坏及重新计算校验和的情况。

真实材料通过显式 `--source` 选入：原项目 `.scratch/static-knowledge/exploration/round2/record.html`，SHA-256 为 `5c5c0e6c6cd8e34a26b10ceb6dbc0ac85cbd70ed31e9501a11e6fc8a3322da97`。执行者直接检查 HTML 第 347 行标题和人数、第 351 行完整两段效果（包括公式、阈值、范围与换行），独立转录 literal expectations 到忽略的本地 `build/knowledge-01/check_real.py`，未用探索 `data.json` 生成期望。来源 locator 为全页第 25 个 table、效果第 2 行第 1 格。页面修订 450448；赛季、序列及客户端构建均保守保留未知。

已实际运行：

```powershell
python -X utf8 build/knowledge-01/check_real.py --source D:/Projects/Stronghold-Protocol-Next/.scratch/static-knowledge/exploration/round2/record.html
```

两次真实 HTML 构建、指定快照重读、逐项 literal 比较、来源、未知名称及去掉 snapshot_id 后的完整查询结果一致性均通过。本地核对脚本、manifest、HTML、生成快照及原文期望不提交；该命令仅在持有这些私有材料的本地目录可复跑。真实材料获取时刻据探索报告分钟记录，未伪称秒级精度。此次没有独立复审或真实游戏验证，也不证明游戏当前规则、完整历史依赖或完整赛季覆盖。

## 干员、关系和候选语义

扩展提取通过直接三列的「盟约／特质」表头辨认干员表，再定位首列规范 `title` 链接及直接所属行，避免把盟约成员缩略表和外层嵌套表误当作干员条目。要求每个选定干员有唯一行、三格结构、非空无重复盟约名称、唯一类型标签及非空特质段；同一行含多个选定干员的歧义情况拒绝导入。盟约三行分别要求单格；对所有声明的盟约与干员逐对核验正反成员关系，任一方向冲突即整个构建失败。

盟约 `members` 仅包含声明覆盖的干员交集，逐项带成员区定位，`members_complete` 始终为 false。干员 `memberships` 保留来源顺序和样本外名称；查询时为每项增加 `effect_status`，只有 `found` 才带 `covenant` 规则记录，未收录效果为 `not_in_sample`。成员身份与特质分开，不产生层数贡献、公式执行结果或局面状态。

`trait.type_source_text` 独立保存特质类型，`trait.segments` 按来源箭头顺序保存完整文本及 `index_1based`；无箭头时只保留一个段。当前不把有序段擅自标为某游戏等级，亦不把单段复制成普通／精锐两段。换行及实体采用与效果文本相同的规则；仅保证本次核验布局的文本提取。

`candidates` 返回去重排序的名称、逐项 `locators`，并保留顶层 snapshot_id、来源、适用范围、提取器和 coverage。盟约候选包含已收录规则及仅由干员关联得知的名称，分别标记 `effect_status: found/not_in_sample`；`coverage.covenants` 只列出已收录效果的名称。干员候选只来自已收录干员记录。候选不是 OCR 纠正或当前画面证据，空查询仍只表示未收录。

## Ticket 02 验证记录（2026-11-15）

TDD 在已批准的离线 CLI 边界上依次观察 red→green：干员查询、候选命令、重复干员标识、嵌套与同类成员表误匹配、单行身份歧义及盟约行单格要求。其余回归覆盖正反关系冲突、重复成员／行／名称、缺少特质类型／内容／单元格、异常箭头、单段特质、篡改关系后重算哈希、确定性和旧票拒绝路径。合成源不包含游戏原文。

真实材料核对未使用探索生成数据作为期望。执行者直接阅读固定 HTML 的三项效果单元格及两处成员区域，并对照 revision.json 修订 450448 的三个参战干员模板参数，将完整效果、全部特质段、激活人数、关系及位置写成私有 literal expectations：三项盟约、三名干员、五条样本内关系，另保留维多利亚／乌萨斯的名称而不补效果。来源哈希与 ticket 01 相同。干员来源为全页第 118 表（承曦格雷伊，Ⅱ阶）和第 119 表（水灯心、奥尔佳，Ⅲ阶）。标签类型与顺序有证据，程序不推演游戏数值。私有核对脚本每次使用新临时目录，避免重复运行覆盖历史。

实际运行命令：

```powershell
python -X utf8 scripts/test_knowledge.py
python -X utf8 -m py_compile scripts/knowledge.py scripts/test_knowledge.py
python -X utf8 build/knowledge-02/check_real.py --source D:/Projects/Stronghold-Protocol-Next/.scratch/static-knowledge/exploration/round2/record.html
python -X utf8 scripts/knowledge.py query --store D:/Projects/Stronghold-Protocol-Next/.worktrees/static-knowledge-integration/build/knowledge-01/store --snapshot-id literal-b --kind covenant --name 灵巧
```

14 个合成 CLI 测试通过；真实材料两次完整构建、三项完整效果／人数、三名干员全部特质段／成员关系、候选、样本外名称、来源位置及去掉 snapshot_id 后的业务查询一致性通过。旧 ticket 01 已发布的 literal-b 快照在新实现下只读重开通过，未重写旧归档。私有 `build/knowledge-02/check_real.py`、manifest、来源和生成数据不提交；最后两条命令需要持有本地材料。没有真实游戏截图或游戏操作、没有 OCR／识别接入；不证明真实赛季兼容性或规则在当前客户端有效。

## Ticket 03 验证记录（2026-11-15）

在公共 CLI 边界先观察「不存在标识」测试 red：原错误仅给出 snapshot.json 的文件路径，无法明确区分标识不存在与已有归档缺文件。加入查询前置检查后 green；同一测试分别验证不存在、校验和损坏、不支持格式和缺文件的错误信息。

其余生命周期能力复用既有发布实现，新增验收测试直接 green，未为制造 red 添加替代实现。两个独立合成来源集合包含共同条目的效果和特质修改、新增盟约／干员、移除盟约／干员；验证新 coverage、候选和查询不残留旧条目，修订、指纹、赛季／序列变化且客户端构建未知，旧查询完整结果与来源保持相同。另验证哈希错误和矛盾关系拒绝发布、不破坏旧结果，以及重复标识拒绝覆盖。两个真实 CLI 子进程并发争用同标识，恰好一个成功，成功归档可完整查询。

中断测试使用不完整 `.pending-*` 目录作为文件系统故障输入，验证不可查询、不阻塞下一次构建且旧结果不变；未声称在特定写入指令处实际终止进程，也未验证断电恢复。省略 snapshot_id 的查询明确返回参数错误，不选择新旧任一快照。

实际执行 `python -X utf8 scripts/test_knowledge.py`，19 个合成 CLI 测试通过；`python -X utf8 -m py_compile scripts/knowledge.py scripts/test_knowledge.py` 和 `git diff --check` 通过。合成赛季只证明软件隔离行为，不证明真实新赛季来源结构兼容或当前游戏规则正确。此次未增加真实来源材料、游戏原文、生成数据、截图测试或 OCR／识别接入。

## 审阅修复验证（2026-11-15）

公共 CLI 回归先观察到小数人数 `2.5` 被错误接受的 red，再改为完整标签字段校验后 green；合成测试同时拒绝指数、符号、零、前导零、分数、全角数字及尾随文字。另先观察到有效历史 Python／依赖版本记录导致查询失败的 red；修复后基础和干员归档均可在重解析一致时保留原环境记录。未知提取器版本／能力、元数据缺失／额外字段／类型错误，以及修改原文后重算归档校验和仍明确拒绝。

实际执行 `python -X utf8 scripts/test_knowledge.py`，23 个合成 CLI 测试通过；`python -X utf8 -m py_compile scripts/knowledge.py scripts/test_knowledge.py` 和 `git diff --check` 通过。用修复后的 CLI 只读查询旧 ticket 01 的 `literal-b` 归档通过。私有 `build/knowledge-02/check_real.py` 两次真实构建及三盟约／三干员的原文、人数、关系和来源核对通过；`build/knowledge-01/check_real.py` 因 Windows 目录发布重命名返回 `WinError 5` 而未完整通过，需以集成工作树的最终复核结果为准。未提交私有材料或生成数据。

## 来源与使用边界

来源为 [PRTS 盟约记录](https://prts.wiki/w/卫戍协议·规程/PRTS盟约记录)，PRTS 编辑者提供玩家资料，所引游戏原文权利属于相应权利人。固定材料仅用于已授权的本地研究复核；原始 HTML、游戏原文及生成数据的分发权限未确认，因此不纳入提交或自动发布。工具不提供抓取、上传或分发命令。署名和限制随查询返回，不能把站点文字许可自动扩展到所有游戏材料。
