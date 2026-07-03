# Windows Native 体积说明

本项目的轻量化目标是提供一个可直接运行的 Windows 串口工具，而不是把 Qt 或 .NET 运行库一起打包。

## 当前结果

v1.0.2 Win32 native 包：

| 项目 | 结果 |
|------|------|
| 包名 | `SerialValueMatcherNative-win32-native-x64.zip` |
| 主程序 | `svm-native-win32.exe` |
| zip 体积 | 以 package summary 为准 |
| 解压体积 | 以 package summary 为准 |
| 文件数 | 以 package summary 为准 |
| Qt 运行库 | 无 |
| SQLite 插件 | 无 |
| .NET/C# 运行库 | 无 |
| Gate status | passed |

## 体积策略

- 使用 Win32 native UI；
- 核心协议、Modbus、分析和报告逻辑放在 Qt-free 模块；
- native 存储不依赖 SQLite 插件；
- MSVC Release 使用静态运行库策略；
- 包内只放主程序、README 和必要文档。

## 门禁

第一阶段门禁：

- zip `<= 5 MB`；
- 解压 `<= 8 MB`；
- 不包含 Qt、SQLite 插件或 .NET 运行库。

当前 v1.0.2 已明显低于第一阶段门禁。

## 不追求的方向

不通过以下方式换体积：

- 牺牲中文可读性；
- 去掉必要错误提示；
- 把稳定性检查移出发布流程；
- 引入壳压缩导致杀软误报或调试困难。

后续若继续瘦身，应先确认不会损害可维护性和现场可用性。
