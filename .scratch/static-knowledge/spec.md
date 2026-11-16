# 卫戍协议静态知识库

Status: resolved

用户已确认本规格的公共测试边界与三票拆分，并在本轮授权实现及本地提交、合并。三票已完成并集成；当前交付为独立离线知识能力，未接入 OCR、截图、状态持久化或策略。

## Problem Statement

状态识别需要合法名称候选，后续策略需要可查询的规则及实体关联。将静态规则重复录入识别条件会增加维护成本，也容易把候选知识误当作画面证据。当前缺少带来源和赛季语境、可重建和查询的独立静态知识能力。

## Solution

从 PRTS 的固定来源材料建立可核验的知识快照，保留盟约效果原文、干员盟约成员关系及干员特质，提供独立只读查询。首版使用探索已验证的三个盟约和三名干员；不要求先覆盖完整赛季数据。

用户判断干员基础资料较稳定，卫戍协议赛季变化是主要维护边界。本规格据此采用按赛季或序列组织、发生变更时全量重建所声明覆盖范围的维护方式，而非逐条在线增量同步。资料稳定性不是正确性或当前适用性的证明；来源、覆盖与未知仍明确保留。

## User Stories

1. As a researcher, I want to import a fixed source snapshot, so that I can reproduce the knowledge without relying on a changing website.
2. As a researcher, I want covenant names and visible effect text preserved, so that I can check extracted rules against their source.
3. As a researcher, I want operator membership and traits represented separately, so that equipment traits are not mistaken for covenant stacks.
4. As a user, I want to query a covenant and its sampled operators, so that I can inspect rules and relationships.
5. As a user, I want to query an operator and its covered covenant effects, so that I can retrieve related knowledge.
6. As a user, I want names and records linked to snapshot provenance, so that future consumers can inspect where candidates came from.
7. As a user, I want uncollected names and unknown applicability identified, so that missing data is not interpreted as absence from the game.
8. As a maintainer, I want to rebuild a complete declared dataset after a season change, so that updates do not mix old and new records silently.
9. As a researcher, I want earlier snapshots retained and explicitly selectable, so that earlier analysis can still reference its original knowledge.
10. As a maintainer, I want invalid or inconsistent inputs rejected before publication, so that a failed rebuild does not replace usable knowledge.

## Implementation Decisions

- 独立能力，不改变现有状态识别规格、ticket 02 的条件、归属或依赖；初期不接入分类器、OCR、状态持久化或策略执行。
- 接口与存储独立于现有 Analysis 和 StateRecord；不修改二者格式。查询显式指定 snapshot_id，每项结果可追溯到具体来源位置，成员列表显式注明覆盖完整性。
- 首个来源为 PRTS 玩家资料。首版接受固定渲染 HTML 作为提取输入，记录来源 URL、获取时间、页面修订（可得时）、材料哈希、提取器版本及依赖版本。页面修订、赛季／序列语境、客户端构建分别表达；缺失为未知，不用推断值伪装确认。
- 已保存渲染快照足以支持本次提取重跑；不要求先从所有历史模板重建网页。已知依赖及历史依赖不完整的限制仍随来源记录。
- 小样本为灵巧、精准、独行，以及承曦格雷伊、水灯心、奥尔佳；保留样本外盟约名称关联，但未收录其效果时明确表达。样本内成员不代表完整成员名单。
- 盟约名称、激活人数、完整可见效果文本、干员名称、成员关系及特质分别表达。数值或条件结构化仅限明确可验证字段，不解释执行公式，不推算局面效果。
- 查询返回知识快照标识、来源关联和覆盖限制。快照内合法名称只是候选；查询不到只表示未收录。不得产生当前局面观测、分类概率或已激活状态。
- 全量更新指重建该快照声明的全部覆盖范围；本次三加三样本不是完整赛季库。每次重建形成独立的新快照，不覆盖历史；查询显式选择快照，不自动按日期推断当前游戏版本。
- 重建先提取与校验，成功后才发布完整产物；结构缺失、重复标识、矛盾成员关系、损坏来源或失败更新不能产生半成品可用快照，也不能破坏旧快照。具体目录、存储格式与语言在实现设计时确定，不直接将一次性原型升为正式接口。
- 首版支持显式提供本地来源材料的离线导入，获取与查询分离。自动联网更新、定时抓取及无人值守发布不属首版。
- 探索脚本只作为已验证行为的参考。按正式错误、校验、接口和测试要求评估可复用片段，不照搬硬编码样本筛选器作为通用实现。
- 保留署名与来源说明。本地复核使用与对外分发分别处理；原始 HTML、游戏原文及生成数据的分发权限未确认前，不纳入自动发布或分发范围。

## Testing Decisions

- 已确认一个公共测试边界：通过面向用户的离线命令入口，完成“固定来源材料→构建知识快照→指定快照查询”。复用项目已有 CLI JSON、临时目录与错误输入验证风格，但不调用截图／OCR 分析路径；具体命令名待实现设计。
- 验证三类查询、原文及字段分离、来源可追溯、样本外关联和未收录语义；两个固定材料构建的规范化业务数据一致。获取时间等运行元数据不作为业务一致性条件。
- 对探索样本逐项对照固定材料；需要记录人工核对依据，不能只验证解析器自产的期望。离线合成异常材料只证明拒绝路径，不证明游戏规则正确。
- 验证缺失关键结构、重复标识、双向关联冲突、来源哈希不符、损坏知识快照与不支持版本均明确报错。
- 用两个可区分的本地来源集合验证全量重建后的新增、修改及移除；旧快照结果保持原样，失败重建不损坏旧快照。不以合成赛季证明真实新赛季兼容性。
- 不需真实游戏截图；不运行模型或触碰独立验证素材，不以本库测试声明识别准确率提升。

## Out of Scope

- 修改状态识别 spec 或 ticket 02，分类器接入、候选纠错、OCR 改造、原有票重新验收。
- 地图、完整干员池、全量赛季数据、可执行规则引擎、阵容收益计算及策略执行。
- 游戏输入、截图采集、版本自动检测、跨赛季自动映射、定时抓取、全站抓取及增量同步。
- 完整历史网页依赖重建、对游戏规则的官方认证或未知内容的推测补全。

## Further Notes

设计证据见 [第二轮探索报告](exploration/round2/report.md)；固定快照小样本自动提取和重复生成已验证；当前正式实现、验证及限制见本文 Answer 与实现说明。探索第一轮仅人工材料转换的结论继续保留。

已发布三个端到端 tickets，依赖关系为 01→02→03；编号属于本静态知识库，不是 state-recognition 的票号：

- [01：单个盟约从固定来源到带来源查询](issues/01-import-and-query-covenant.md)
- [02：三盟约三干员的关系、特质及候选查询](issues/02-query-operators-and-relations.md)
- [03：全量重建与历史快照隔离](issues/03-rebuild-and-preserve-snapshots.md)

用户指出原 OCR 对话已承包现有状态识别全部 tickets。任何未来接入或改变 state-recognition ticket 02 关联任务，先与该对话沟通，再由用户确认范围并通过规格／ticket 记录；本规格不构成接入授权。

## Comments

- 2026-11-15：用户确认按所示公共测试边界和三票拆分发布。规格及三票设为 ready-for-agent；实现尚未启动。现有状态识别规格、票状态及验收范围保持不变。

- 2026-11-15：已与「实现 spec 与关联 tickets - OCR」（01a1dce6-9873-7bd0-8890-b4e12f19671c）沟通并收到回复：本独立范围无直接冲突，目前无需修改 ticket 02。其建议的独立接口／存储、显式 snapshot_id、版本分离、未收录与完整性语义、来源位置及规则与观测隔离已纳入草案。此回复是边界评估，不是用户对发布、实现或未来 OCR 接入的批准。双方均未因此修改现有票或产品代码。

## Answer

三票已在 `codex/static-knowledge` 完成。独立入口为 `python -X utf8 scripts/knowledge.py`，支持离线 build、指定快照 query 和 candidates。选用 Python／Beautiful Soup 固定解析器及带来源的 JSON 不可覆盖快照；完整覆盖重建不混入旧记录，历史选择显式进行。实现、已运行命令、来源核对和限制见 [实现说明](../../docs/static-knowledge.md)。审查发现的完整人数校验与历史环境元数据兼容问题均已修复并复核。实际源材料、原文期望及生成数据仅保留本地，不加入提交。
