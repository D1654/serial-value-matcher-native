# 串口值匹配器（SerialValueMatcher Native）规格草案

项目作者：`w`

串口值匹配器面向中文母语人群，优先服务现场串口调试、设备联调、Modbus RTU 扫描、私有协议字段识别和值候选分析场景。项目主线是 C++20 + Qt6 Windows 原生桌面软件，不依赖 C#/.NET Desktop Runtime；交付形态优先为解压即可运行的便携包。

## 1. 产品定位

串口值匹配器第一阶段不是“协议分析插件”，而是一个独立好用的中文串口调试工具。

第二阶段再接入 Analysis Extension，用于拟合扫描、字段分析、私有协议规则保存和证据导出。

## 2. 第一阶段：成熟串口调试工具

必须具备：

- 串口枚举、刷新、连接、断开、自动重连；
- 波特率、数据位、校验位、停止位、流控、DTR、RTS；
- 文本/HEX 双视图；
- RX/TX 方向区分；
- 时间戳；
- 暂停滚动、搜索、过滤；
- 发送历史、常用命令、宏、行尾设置；
- 原始日志保存、导入、回放；
- 中文错误诊断；
- 低资源占用、长时间运行稳定；
- Windows 免 .NET 运行时打包。

## 3. 第二阶段：Analysis Extension

在串口工具基础稳定后增加：

- 自动/半自动帧切分；
- 字段候选枚举；
- 校验识别；
- 编码识别；
- 用户输入真实值；
- 多样本拟合；
- 字段变化趋势分析；
- 候选字段评分；
- 证据链展示；
- 保存为协议规则；
- 导出复现报告。

## 4. 数据流核心

所有 RX/TX 原始数据先进入统一事件总线，再分别供 UI、日志、回放、分析扩展消费。

```cpp
struct RawIoEvent {
    QString sessionId;
    Direction direction; // Rx / Tx
    QDateTime timestamp;
    QString endpoint;
    QByteArray payload;
};
```

原则：Analysis Extension 不直接依赖 `MainWindow`、`QPlainTextEdit` 或 `QSerialPort`，只订阅 `CaptureBus`。

## 5. 首批协议能力

最低必须覆盖：

- Checksum：SUM8、XOR8、LRC8；
- CRC：CRC16/MODBUS BE/LE、CRC8 多变体；
- Numeric endian：UInt16 BE/LE、UInt32 BE/LE；
- Encodings：PackedBCD、Gray16BE、BitFlags8。

这些能力必须可执行、可保存、可测试、可用于匹配证据，而不是只出现在报告文字里。

## 6. 旧 .NET/WPF 项目定位

旧项目只作为：

- 需求参考；
- UI/流程经验参考；
- 算法参考；
- 测试向量来源；
- 文档/交付教训来源。

不再作为未来主线继续堆大功能。
