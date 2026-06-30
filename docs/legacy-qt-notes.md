# 历史 Qt 说明

状态：历史/参考文档。本文不描述当前主发布包。

## 当前结论

仓库仍保留 Qt Widgets 路线源码，用于历史能力基线、部分自动化测试和迁移参考。当前面向用户的轻量发布物是 Win32 native 包，不携带 Qt DLL、SQLite 插件或 .NET 运行库。

## 历史路线范围

- `src/app/`：Qt Widgets 历史 UI。
- `src/storage/`：SQLite 存储路线。
- `src/modbus/`、`src/matching/`：Qt 路线中的 Modbus、候选和规则验证组件。

## 使用原则

- 不把 Qt 路线描述为当前发布路线。
- 不删除仍有测试价值的历史能力说明。
- 新增用户可见能力时，优先考虑 Qt-free core 和 Win32 native 路线。

## 相关旧文档

- [Win32 Native 与 Qt 基线对照](windows-native-parity.md)
- [过渡架构说明](architecture.md)
