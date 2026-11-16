# Issue tracker：本地 Markdown

议题和规格存放在仓库的 `.scratch/` 中。

- 每个功能使用 `.scratch/<feature-slug>/`。
- 规格使用 `.scratch/<feature-slug>/spec.md`。
- 实现议题使用独立文件 `.scratch/<feature-slug>/issues/<NN>-<slug>.md`，从 `01` 编号。
- 议题文件顶部附近使用 `Status:` 记录分类状态；状态名称见 `triage-labels.md`。
- 评论和讨论追加在文件末尾的 `## Comments` 下。
- 发布到 issue tracker 时，创建上述对应文件；获取议题时，读取用户给出的路径或编号对应的文件。

## Wayfinding 操作

- Map 使用 `.scratch/<effort>/map.md`。
- 子议题使用 `.scratch/<effort>/issues/NN-<slug>.md`。
- `Type:` 记录 `research`、`prototype`、`grilling` 或 `task`；`Status:` 记录 `claimed` 或 `resolved`。
- `Blocked by: NN, NN` 列出前置议题；全部 resolved 后解除阻塞。
- Frontier 是 open、未阻塞且未领取的子议题，按编号选择。
- 开始前设置 `Status: claimed`；解决时在 `## Answer` 追加答案、设置 `Status: resolved`，并在 map 的 Decisions-so-far 中添加摘要和链接。
