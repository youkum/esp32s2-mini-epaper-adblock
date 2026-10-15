# Domain Docs

工程类技能在探索代码库时，应如何使用本仓库的领域文档。

## 本仓库的布局

**multi-context** — 子项目各一份 `CONTEXT.md`，根目录一份 `CONTEXT-MAP.md` 指向它们。本结论在初始化时已与用户确认。

初始化阶段不创建 `CONTEXT-MAP.md` 与各子项目的 `CONTEXT.md`：留到第一次真正写下词条或决策时再建，届时由仓库根目录的 `CONTEXT-MAP.md` 指向它们。系统级决策放根 `docs/adr/`，上下文级决策放各子项目的 `docs/adr/`。

## 探索前先读这些

- 仓库根目录的 **`CONTEXT.md`**；
- 或仓库根目录的 **`CONTEXT-MAP.md`**（若存在）— 它指向每个上下文各自的 `CONTEXT.md`，读与当前主题相关的那些；
- **`docs/adr/`** — 读与即将工作的领域相关的 ADR。multi-context 仓库还要看 `src/<context>/docs/adr/` 里的上下文级决策。

这些文件若不存在，**静默继续**。不要指出它们缺失，也不要建议提前创建。`/domain-modeling` 技能（经由 `/grill-with-docs` 与 `/improve-codebase-architecture` 进入）会在真正敲定术语或决策时按需创建它们。

## 文件结构

单上下文仓库（多数仓库）：

```
/
├── CONTEXT.md
├── docs/adr/
│   ├── 0001-event-sourced-orders.md
│   └── 0002-postgres-for-write-model.md
└── src/
```

multi-context 仓库（根目录存在 `CONTEXT-MAP.md`）：

```
/
├── CONTEXT-MAP.md
├── docs/adr/                          ← 系统级决策
└── src/
    ├── ordering/
    │   ├── CONTEXT.md
    │   └── docs/adr/                  ← 上下文级决策
    └── billing/
        ├── CONTEXT.md
        └── docs/adr/
```

## 使用术语表中的词汇

当你的输出提到某个领域概念（issue 标题、重构提案、假设、测试名），一律使用 `CONTEXT.md` 里定义的术语，不要漂移到术语表明确避开的同义词。

如果你需要的概念还不在术语表里，这本身就是信号 —— 要么你在发明项目不用的语言（请重新考虑），要么确实存在空缺（记下来留给 `/domain-modeling`）。

## 标记与 ADR 冲突之处

如果你的输出与已有 ADR 矛盾，请显式指出，而不是默默覆盖：

> _与 ADR-0007（event-sourced orders）矛盾——但值得重新讨论，因为…_
