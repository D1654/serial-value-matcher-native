# 架构说明

状态：过渡参考。当前 Win32 native 架构入口请优先阅读 [architecture-win32-native.md](architecture-win32-native.md)。Qt 历史路线请阅读 [legacy-qt-notes.md](legacy-qt-notes.md)。

当前仓库保留两条实现路线：Win32 native 轻量发布路线和 Qt 历史基线路线。v1.0.2 面向用户发布的是 Win32 native 包。

## Win32 native 发布路线

```text
svm-native-win32
  ├─ NativeMainWindow                 Win32 UI、菜单、布局、消息循环
  ├─ win32_serial                     CreateFileW / ReadFile / WriteFile 串口后端
  ├─ native_storage                   Qt-free 本地存储
  ├─ svm_slim_core
  │   ├─ protocol_core                字节解析、显示格式
  │   ├─ modbus_core                  Modbus RTU 请求、响应、扫描计划
  │   ├─ analysis_core                候选生成和规则验证核心
  │   └─ report_core                  Markdown 报告文本
  └─ utf8_win32                       UTF-8 / UTF-16 转换
```

特点：

- 不链接 Qt；
- MSVC Release 使用静态运行库策略；
- 包内主程序为 `svm-native-win32.exe`；
- GitHub Actions 对包体积、中文文本、禁止运行库文件做门禁检查。

## Qt 历史基线

```text
svm-native
  ├─ Qt Widgets MainWindow
  ├─ SerialPortService / QSerialPort
  ├─ CaptureBus
  ├─ SessionStore / SQLite
  ├─ ModbusScanWorker
  ├─ Matching / RuleVerifier
  └─ Report
```

Qt 代码仍用于历史能力参考和部分自动化测试。后续新增用户可见能力时，应优先评估是否能沉到 `src/core` 或 Qt-free 模块，避免重新把 Windows 发布包推回大体积依赖路线。

## 模块原则

- 串口后端只负责传输和错误诊断；
- UI 负责组织流程，不承载协议算法；
- 分析逻辑放在核心模块，可被 Qt 和 Win32 复用；
- 原始 TX/RX 记录优先进入存储，日志窗口只是可见工作集；
- 中文文案集中管理，避免乱码和散落字符串。

## 主要目录

```text
src/win32/           Win32 native UI 和串口后端
src/core/            Qt-free 核心能力
src/native_storage/  Win32 native 存储
src/app/             Qt Widgets 历史基线 UI
src/modbus/          Qt 路线 Modbus 组件
src/matching/        Qt 路线候选和规则验证组件
src/storage/         Qt 路线 SQLite 存储
scripts/             构建、打包、检查和 Wine UI 截图脚本
.github/workflows/   CI、Windows 出包和压力测试
```
