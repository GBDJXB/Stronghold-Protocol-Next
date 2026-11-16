# 04: 拒绝嵌套激活人数容器截断

Type: task
Status: resolved

父规格：[卫戍协议静态知识库](../spec.md)。审阅 P2：最近 div 可能只包含人数前缀，外层可见后缀被忽略并错误发布。

## Scope

- 在已批准的 `scripts/knowledge.py` 公共 CLI 边界，先复现失败，再修复完整人数容器边界。
- 拒绝小数、指数、整数及文字后缀的多层 div 歧义；兼容正常格式和已核验真实来源。
- 验证拒绝发布、完整 CLI 回归、编译及差异检查；不触碰截图或 OCR。

## Comments

- 2026-11-15：开始处理，测试 seam 沿用已批准的离线 CLI。

## Answer

已修复审阅 P2：人数容器使用与标题分离的完整 DOM 分支，拒绝分支内部截断及标题行外部额外文本；不禁止实际来源的外层 div 包装。

- Red：`KnowledgeCLI.test_nested_activation_count_cannot_hide_a_suffix_outside_the_nearest_div` 1 test / 4 subcases 全部失败，实际返回 0、期望 1（`.5`、`e3`、`3`、` people`）。
- Green：新增后缀拒绝及正向包装兼容、外部文本拒绝、旧快照保留通过；完整 CLI suite 26 tests / 41.009 秒全部通过。
- 编译检查及 `git diff --check` 通过；私有真实来源两次构建、三盟约／三干员字面期望及来源核对通过。
- 局限：按 DOM 文本保守验证，标题行新增其他可见说明文字会拒绝；未声明任意 HTML/CSS 视觉等价或真实新赛季兼容。

详细依据与实际命令见 [实现说明](../../../docs/static-knowledge.md)。

### 最终复审与集成

- 功能提交：`4ddf0b4`，已快进合入 `codex/static-knowledge`。
- 独立 Standards／Spec 双轴复审均为 0 项可操作发现。Standards 检查仓库约定和可维护性；Spec 独立 CLI 验证四类后缀拒绝、正常包装兼容、失败快照不可查询及旧结果保留，并只读核对真实人数 2／2／1。
- 主代理集成验证：完整 CLI suite 26 tests / 42.614 秒全部通过，`py_compile` 和 `git diff --check` 通过。此处记录复审及集成执行反馈；文档收尾不重复运行 suite，不含其他实现变更。
