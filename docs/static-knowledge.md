# 离线静态知识快照（ticket 01）

独立 Python 3 工具从显式提供的固定 PRTS 渲染 HTML 提取 manifest 声明的盟约。不会联网、读取截图、运行 OCR 或产生局面观测。当前仅提供盟约查询；成员、干员与特质由后续票实现。

## 环境与命令

已核验 Windows、Python 3.13.7，依赖固定在 `scripts/knowledge-requirements.txt`：Beautiful Soup 4.14.3、SoupSieve 2.8.3、typing_extensions 4.15.0。解析器显式使用 `html.parser`，不用环境自动选择解析器。默认测试只使用合成 HTML：

```powershell
python -X utf8 scripts/test_knowledge.py
```

离线导入和只读查询（需先准备本地材料与 manifest，store 建议放在被忽略的 `knowledge-local/`）：

```powershell
python -X utf8 scripts/knowledge.py build --source <固定HTML路径> --manifest <manifest路径> --store knowledge-local --snapshot-id <新标识>
python -X utf8 scripts/knowledge.py query --store knowledge-local --snapshot-id <标识> --kind covenant --name <规范名称>
```

命令成功输出 JSON 并返回 0；来源、结构、版本或完整性错误输出 `status: error` 并返回 1。缺少 CLI 参数由 argparse 返回 2。查询不到返回 `not_in_sample`，仅表示未收录。必须显式指定 snapshot_id，不推断当前版本。

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

`page_revision` 可为正整数；来源含 wgRevisionId 时必须与之相符，来源不含时必须为 null。获取时间需含时区。赛季、序列、客户端构建独立表达，未知使用 null。`dependency_revisions` 可记载已知依赖；为 null 时必须显式记录限制。来源 URL 和署名由材料提供者声明，工具不能证明其真实性。

## 存储与校验

`<store>/<snapshot_id>/` 包含 `snapshot.json`、`snapshot.sha256` 和固定字节的 `source.html`。JSON 保存来源、manifest、适用范围、提取器及 Python/依赖版本、覆盖范围和盟约记录。来源位置记录归档文件名、全页 table 的一基序号、规范标题、效果行与单元格序号。查询保留全部这些上下文；成员名单与赛季完整性当前均为 false。

提取要求唯一盟约标题、直接 tbody 的三行结构、正数激活人数、非空完整效果单元格及成员区标题；拒绝缺失、重复、哈希不符、修订矛盾及非法 UTF-8。效果保留文本和 `<br>` 换行、解码实体、去除 HTML 格式标记及各行边缘空白，不执行公式。仅支持已核验的固定渲染布局；不运行 JavaScript/CSS，也不承诺任意网页的浏览器视觉等价。

先在 store 内临时目录写入并重新校验，再在 Windows 同卷重命名发布；存在目标时拒绝覆盖。失败清理本次临时目录，不影响已发布快照。进程被强制终止可能留下 `.pending-*`，查询不会选择这些目录。这里的原子发布不等同于断电持久性保证。

读取时检查 JSON 重复键、版本、SHA-256，再从归档 HTML 和 manifest 重新提取并逐项比较（包含类型）。因此重新计算校验和也不能使错误字段通过语义校验。完整性校验不是数字签名，不能识别有权替换全部材料的攻击者。当前格式要求相同提取器、Python 与依赖版本，环境变更需显式迁移或在原环境读取，不静默重解释旧规则。

设 HTML 字节数为 H、选定盟约数为 C；一次全页扫描建立标题索引，解析与保存主成本为 O(H)，记录排序 O(C log C)，空间 O(H+C)。当前读取也重解析 HTML，换取来源与业务字段一致性；尚未做大型数据集或高频查询性能验收。

## 验证记录（2026-11-15）

TDD 公共边界为 CLI：依次验证最小构建/查询、非法输入不发布、损坏快照拒绝、重复生成和旧记录保留。各新增行为先观察 red 再实现；另观察到浮点型激活人数可绕过 Python 字典比较的 red，改为保留 JSON 类型的比较后通过。最终 4 个合成端到端测试通过，覆盖重复标题/标识、缺失结构、空效果、非法 UTF-8、来源哈希、未知格式、快照字段损坏及重新计算校验和的情况。

真实材料通过显式 `--source` 选入：原项目 `.scratch/static-knowledge/exploration/round2/record.html`，SHA-256 为 `5c5c0e6c6cd8e34a26b10ceb6dbc0ac85cbd70ed31e9501a11e6fc8a3322da97`。执行者直接检查 HTML 第 347 行标题和人数、第 351 行完整两段效果（包括公式、阈值、范围与换行），独立转录 literal expectations 到忽略的本地 `build/knowledge-01/check_real.py`，未用探索 `data.json` 生成期望。来源 locator 为全页第 25 个 table、效果第 2 行第 1 格。页面修订 450448；赛季、序列及客户端构建均保守保留未知。

已实际运行：

```powershell
python -X utf8 build/knowledge-01/check_real.py --source D:/Projects/Stronghold-Protocol-Next/.scratch/static-knowledge/exploration/round2/record.html
```

两次真实 HTML 构建、指定快照重读、逐项 literal 比较、来源、未知名称及去掉 snapshot_id 后的完整查询结果一致性均通过。本地核对脚本、manifest、HTML、生成快照及原文期望不提交；该命令仅在持有这些私有材料的本地目录可复跑。真实材料获取时刻据探索报告分钟记录，未伪称秒级精度。此次没有独立复审或真实游戏验证，也不证明游戏当前规则、完整历史依赖或完整赛季覆盖。

## 来源与使用边界

来源为 [PRTS 盟约记录](https://prts.wiki/w/卫戍协议·规程/PRTS盟约记录)，PRTS 编辑者提供玩家资料，所引游戏原文权利属于相应权利人。固定材料仅用于已授权的本地研究复核；原始 HTML、游戏原文及生成数据的分发权限未确认，因此不纳入提交或自动发布。工具不提供抓取、上传或分发命令。署名和限制随查询返回，不能把站点文字许可自动扩展到所有游戏材料。
