---
description: 查看所有 worktree Agent 的开发进度
---

查看项目中所有 worktree 的进度记录，汇总各 Agent 的工作状态。

请执行以下步骤：

1. 读取所有 worktree 下的 TASK.md 中"## 进度记录"部分
2. 汇总为一个清晰的全局进度报告

需要检查的文件：
- `.claude/worktrees/terrain-system/TASK.md`
- `.claude/worktrees/strategy-camera/TASK.md`
- `.claude/worktrees/core-framework/TASK.md`

输出格式：

对每个 worktree 列出：
- 分支名称
- 各文件的状态（未开始 / 进行中 / 已完成 / 阻塞）
- 阻塞项（如有）
- 总体完成度（已完成数 / 总数）

最后给出全局摘要：总文件数、已完成数、进行中数、阻塞数。
