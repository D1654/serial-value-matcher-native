#include "win32/ui_text.h"

#if defined(_WIN32)

namespace svm::win32 {

const wchar_t* uiText(TextId id) {
    switch (id) {
    case TextId::WindowTitle: return L"\u4E32\u53E3\u503C\u5339\u914D\u5668 Win32 Native";
    case TextId::CreateWindowError: return L"\u65E0\u6CD5\u521B\u5EFA Win32 \u4E3B\u7A97\u53E3\u3002";
    case TextId::HexInvalidChar: return L"HEX \u8F93\u5165\u5305\u542B\u975E\u5341\u516D\u8FDB\u5236\u5B57\u7B26\u3002";
    case TextId::HexOddNibble: return L"HEX \u8F93\u5165\u7684\u534A\u5B57\u8282\u6570\u91CF\u4E3A\u5947\u6570\uFF0C\u8BF7\u8865\u9F50\u3002";
    case TextId::FileSaveProfileMenu: return L"\u4FDD\u5B58\u4E32\u53E3\u914D\u7F6E(&S)";
    case TextId::FileExitMenu: return L"\u9000\u51FA(&X)";
    case TextId::SerialRefreshMenu: return L"\u5237\u65B0\u7AEF\u53E3(&R)";
    case TextId::SerialConnectMenu: return L"\u8FDE\u63A5(&C)";
    case TextId::SerialDisconnectMenu: return L"\u65AD\u5F00(&D)";
    case TextId::SerialAutoReconnectMenu: return L"\u5207\u6362\u81EA\u52A8\u91CD\u8FDE(&A)";
    case TextId::ToolsSendMenu: return L"\u53D1\u9001(&E)";
    case TextId::ToolsPauseScrollMenu: return L"\u6682\u505C/\u6062\u590D\u6EDA\u52A8(&P)";
    case TextId::ToolsClearLogMenu: return L"\u6E05\u7A7A\u63A5\u6536\u533A(&L)";
    case TextId::AnalysisModbusScanMenu: return L"Modbus \u626B\u63CF(&M)";
    case TextId::AnalysisWorkspaceMenu: return L"\u5206\u6790\u5DE5\u4F5C\u533A(&W)";
    case TextId::AnalysisRuleVerifyMenu: return L"\u89C4\u5219\u9A8C\u8BC1(&V)";
    case TextId::AnalysisExportReportMenu: return L"\u5BFC\u51FA\u9A8C\u8BC1\u62A5\u544A(&O)";
    case TextId::HelpAboutMenu: return L"\u5173\u4E8E(&A)";
    case TextId::FileMenu: return L"\u6587\u4EF6(&F)";
    case TextId::SerialMenu: return L"\u4E32\u53E3(&S)";
    case TextId::ToolsMenu: return L"\u5DE5\u5177(&T)";
    case TextId::AnalysisMenu: return L"\u5206\u6790(&A)";
    case TextId::HelpMenu: return L"\u5E2E\u52A9(&H)";
    case TextId::ConnectionGroup: return L"\u8FDE\u63A5\u4E0E\u4E32\u53E3\u53C2\u6570";
    case TextId::SendGroup: return L"\u53D1\u9001\u63A7\u5236";
    case TextId::WorkflowGroup: return L"\u626B\u63CF\u4E0E\u5206\u6790\u5DE5\u4F5C\u6D41";
    case TextId::LogGroup: return L"\u901A\u4FE1\u65E5\u5FD7";
    case TextId::WorkflowHint: return L"\u6D41\u7A0B\uFF1A\u8FDE\u63A5\u8BBE\u5907 \u2192 \u626B\u63CF\u5BC4\u5B58\u5668 \u2192 \u751F\u6210\u5019\u9009 \u2192 \u9A8C\u8BC1\u89C4\u5219 \u2192 \u5BFC\u51FA\u62A5\u544A";
    case TextId::PortLabel: return L"\u4E32\u53E3";
    case TextId::RefreshButton: return L"\u5237\u65B0";
    case TextId::SaveProfileButton: return L"\u4FDD\u5B58\u914D\u7F6E";
    case TextId::BaudLabel: return L"\u6CE2\u7279\u7387";
    case TextId::DataBitsLabel: return L"\u6570\u636E\u4F4D";
    case TextId::ParityLabel: return L"\u6821\u9A8C";
    case TextId::StopBitsLabel: return L"\u505C\u6B62\u4F4D";
    case TextId::FlowControlLabel: return L"\u6D41\u63A7";
    case TextId::AutoReconnectCheck: return L"\u81EA\u52A8\u91CD\u8FDE";
    case TextId::ConnectButton: return L"\u8FDE\u63A5";
    case TextId::DisconnectButton: return L"\u65AD\u5F00";
    case TextId::SendButton: return L"\u53D1\u9001";
    case TextId::PauseScrollButton: return L"\u6682\u505C\u6EDA\u52A8";
    case TextId::ResumeScrollButton: return L"\u6062\u590D\u6EDA\u52A8";
    case TextId::ClearButton: return L"\u6E05\u7A7A";
    case TextId::ModbusScanButton: return L"Modbus \u626B\u63CF";
    case TextId::AnalysisButton: return L"\u5206\u6790\u751F\u6210";
    case TextId::RuleVerifyButton: return L"\u89C4\u5219\u9A8C\u8BC1";
    case TextId::ExportReportButton: return L"\u5BFC\u51FA\u62A5\u544A";
    case TextId::InitialStatus: return L"\u672A\u8FDE\u63A5";
    case TextId::NoParity: return L"\u65E0\u6821\u9A8C";
    case TextId::OddParity: return L"\u5947\u6821\u9A8C";
    case TextId::EvenParity: return L"\u5076\u6821\u9A8C";
    case TextId::MarkParity: return L"\u6807\u8BB0\u6821\u9A8C";
    case TextId::SpaceParity: return L"\u7A7A\u683C\u6821\u9A8C";
    case TextId::NoFlowControl: return L"\u65E0\u6D41\u63A7";
    case TextId::TextMode: return L"\u6587\u672C";
    case TextId::NoLineEnding: return L"\u65E0\u884C\u5C3E";
    case TextId::Fc03Holding: return L"FC03 \u4FDD\u6301\u5BC4\u5B58\u5668";
    case TextId::Fc04Input: return L"FC04 \u8F93\u5165\u5BC4\u5B58\u5668";
    case TextId::SendHistory: return L"\u53D1\u9001\u5386\u53F2";
    case TextId::TargetLabel: return L"\u76EE\u6807";
    case TextId::TargetValueDefault: return L"12.34";
    case TextId::TargetNameDefault: return L"\u76EE\u6807\u503C";
    case TextId::TargetUnitDefault: return L"\u5355\u4F4D";
    case TextId::ToleranceLabel: return L"\u5BB9\u5DEE";
    case TextId::ToleranceDefault: return L"0.01";
    case TextId::CandidatePlaceholder: return L"\u5019\u9009\u7ED3\u679C";
    case TextId::ScanSectionLabel: return L"\u626B\u63CF\u53C2\u6570";
    case TextId::ScanSlaveLabel: return L"\u4ECE\u7AD9";
    case TextId::ScanFunctionLabel: return L"\u529F\u80FD";
    case TextId::ScanStartLabel: return L"\u8D77\u59CB\u5730\u5740";
    case TextId::ScanEndLabel: return L"\u7ED3\u675F\u5730\u5740";
    case TextId::AnalysisSectionLabel: return L"\u76EE\u6807\u4E0E\u5019\u9009";
    case TextId::TargetNameLabel: return L"\u5B57\u6BB5";
    case TextId::TargetValueLabel: return L"\u76EE\u6807\u503C";
    case TextId::TargetUnitLabel: return L"\u5355\u4F4D";
    case TextId::ToleranceFieldLabel: return L"\u5BB9\u5DEE";
    case TextId::CandidateLabel: return L"\u5019\u9009";
    case TextId::ClearLogStatus: return L"\u63A5\u6536\u533A\u5DF2\u6E05\u7A7A\uFF0Cnative \u5B58\u50A8\u8BB0\u5F55\u4E0D\u4F1A\u5220\u9664\u3002";
    case TextId::PauseScrollStatus: return L"\u5DF2\u6682\u505C\u6EDA\u52A8\uFF1A\u4ECD\u7EE7\u7EED\u63A5\u6536\u548C\u5199\u5165 native \u5B58\u50A8\u3002";
    case TextId::ResumeScrollStatus: return L"\u5DF2\u6062\u590D\u6EDA\u52A8\u3002";
    case TextId::AutoReconnectEnabled: return L"\u5DF2\u5F00\u542F\u81EA\u52A8\u91CD\u8FDE\u3002";
    case TextId::AutoReconnectDisabled: return L"\u5DF2\u5173\u95ED\u81EA\u52A8\u91CD\u8FDE\u3002";
    case TextId::RefreshedPortsPrefix: return L"\u5DF2\u5237\u65B0\u4E32\u53E3\u5217\u8868\uFF0C\u5171 ";
    case TextId::PortsUnitSuffix: return L" \u4E2A\u3002";
    case TextId::NoPortsStatus: return L"\u672A\u53D1\u73B0\u4E32\u53E3\u8BBE\u5907\u3002";
    case TextId::RestoredProfilePrefix: return L"\u5DF2\u6062\u590D\u9ED8\u8BA4\u4E32\u53E3\u914D\u7F6E\uFF1A";
    case TextId::StorageSaveProfileClosed: return L"native \u5B58\u50A8\u672A\u6253\u5F00\uFF0C\u65E0\u6CD5\u4FDD\u5B58\u914D\u7F6E\u3002";
    case TextId::SavedProfilePrefix: return L"\u5DF2\u4FDD\u5B58\u9ED8\u8BA4\u4E32\u53E3\u914D\u7F6E\uFF1A";
    case TextId::AlreadyConnected: return L"\u4E32\u53E3\u5DF2\u7ECF\u8FDE\u63A5\u3002";
    case TextId::SystemConnectedPrefix: return L"[\u7CFB\u7EDF] \u5DF2\u8FDE\u63A5 ";
    case TextId::ConnectedStatus: return L"\u5DF2\u8FDE\u63A5\u3002";
    case TextId::SystemDisconnectedPrefix: return L"[\u7CFB\u7EDF] \u5DF2\u65AD\u5F00 ";
    case TextId::DisconnectedStatus: return L"\u5DF2\u65AD\u5F00\u3002";
    case TextId::SerialNotConnectedSend: return L"\u4E32\u53E3\u672A\u8FDE\u63A5\uFF0C\u65E0\u6CD5\u53D1\u9001\u3002";
    case TextId::EmptyPayload: return L"\u53D1\u9001\u5185\u5BB9\u4E3A\u7A7A\u3002";
    case TextId::SentPrefix: return L"\u5DF2\u53D1\u9001 ";
    case TextId::BytesSuffix: return L" \u5B57\u8282\u3002";
    case TextId::SystemSerialFailedPrefix: return L"[\u7CFB\u7EDF] \u4E32\u53E3\u5F02\u5E38\u65AD\u5F00\uFF1A";
    case TextId::ReconnectWaitingPrefix: return L"\u4E32\u53E3\u5F02\u5E38\u65AD\u5F00\uFF0C\u5DF2\u8FDB\u5165\u81EA\u52A8\u91CD\u8FDE\u7B49\u5F85\uFF1A";
    case TextId::WaitingReconnectPrefix: return L"\u7B49\u5F85\u81EA\u52A8\u91CD\u8FDE\uFF1A\u7AEF\u53E3 ";
    case TextId::PortNotReadySuffix: return L" \u5C1A\u672A\u6062\u590D\u3002";
    case TextId::AutoReconnectFailedPrefix: return L"\u81EA\u52A8\u91CD\u8FDE\u5931\u8D25\uFF1A";
    case TextId::SystemReconnectOkPrefix: return L"[\u7CFB\u7EDF] \u81EA\u52A8\u91CD\u8FDE\u6210\u529F ";
    case TextId::AutoReconnectOk: return L"\u81EA\u52A8\u91CD\u8FDE\u6210\u529F\u3002";
    case TextId::ScrollPausedPrefix: return L"\u6EDA\u52A8\u5DF2\u6682\u505C\uFF0C\u5DF2\u9690\u85CF ";
    case TextId::HiddenLinesSuffix: return L" \u6761\u65B0\u65E5\u5FD7\uFF1B\u6570\u636E\u4ECD\u5728\u63A5\u6536\u548C\u4FDD\u5B58\u3002";
    case TextId::LogLimitReset: return L"[\u7CFB\u7EDF] \u63A5\u6536\u65E5\u5FD7\u5DF2\u8FBE\u5230\u4E0A\u9650\uFF0C\u5DF2\u6E05\u7A7A\u4EE5\u4FDD\u62A4\u957F\u671F\u8FD0\u884C\u5185\u5B58\u3002\r\n";
    case TextId::ConnectBeforeModbus: return L"\u8BF7\u5148\u8FDE\u63A5\u4E32\u53E3\uFF0C\u518D\u6267\u884C Modbus \u626B\u63CF\u3002";
    case TextId::StorageModbusClosed: return L"native \u5B58\u50A8\u672A\u6253\u5F00\uFF0C\u65E0\u6CD5\u4FDD\u5B58 Modbus \u626B\u63CF\u7ED3\u679C\u3002";
    case TextId::ModbusInvalidTitle: return L"Modbus \u626B\u63CF\u53C2\u6570\u65E0\u6548";
    case TextId::SystemModbusStartPrefix: return L"[\u7CFB\u7EDF] \u5F00\u59CB Modbus \u626B\u63CF ";
    case TextId::ModbusRunning: return L"\u6B63\u5728\u6267\u884C Modbus \u626B\u63CF\uFF0C\u8BF7\u7B49\u5F85\u5F53\u524D\u8BF7\u6C42\u5B8C\u6210\u3002";
    case TextId::ModbusTimeout: return L"\u7B49\u5F85 Modbus \u54CD\u5E94\u8D85\u65F6\u3002";
    case TextId::ModbusSummaryPrefix: return L"Modbus \u626B\u63CF\u5DF2\u4FDD\u5B58\uFF1A\u6210\u529F\u5757 ";
    case TextId::ModbusFailedBlocks: return L"\uFF0C\u5931\u8D25\u5757 ";
    case TextId::ModbusObservations: return L"\uFF0C\u89C2\u6D4B ";
    case TextId::ChinesePeriod: return L"\u3002";
    case TextId::AnalysisNoStore: return L"native \u5B58\u50A8\u672A\u6253\u5F00\uFF0C\u65E0\u6CD5\u6267\u884C\u5019\u9009\u5206\u6790\u3002";
    case TextId::AnalysisNoScan: return L"\u6CA1\u6709\u53EF\u5206\u6790\u7684 Modbus \u626B\u63CF\u7ED3\u679C\u3002\u8BF7\u5148\u8FDE\u63A5\u8BBE\u5907\u5E76\u6267\u884C\u4E00\u6B21 Modbus \u626B\u63CF\u3002";
    case TextId::AnalysisNoObservations: return L"\u6700\u8FD1\u626B\u63CF\u6CA1\u6709\u5BC4\u5B58\u5668\u89C2\u6D4B\uFF0C\u65E0\u6CD5\u751F\u6210\u5019\u9009\u3002";
    case TextId::AnalysisInvalidTarget: return L"\u76EE\u6807\u503C\u4E0D\u662F\u6709\u6548\u6570\u5B57\u3002";
    case TextId::AnalysisNoCandidates: return L"\u6CA1\u6709\u751F\u6210\u5339\u914D\u5019\u9009\u3002\u8BF7\u8C03\u6574\u76EE\u6807\u503C\u3001\u5BB9\u5DEE\u6216\u91CD\u65B0\u626B\u63CF\u3002";
    case TextId::AnalysisSavedPrefix: return L"\u5019\u9009\u5206\u6790\u5B8C\u6210\uFF1A\u626B\u63CF ";
    case TextId::AnalysisSavedMid: return L"\uFF0C\u5019\u9009 ";
    case TextId::AnalysisSavedRun: return L" \u4E2A\uFF0C\u8FD0\u884C ";
    case TextId::RuleNoCandidates: return L"\u6CA1\u6709\u53EF\u786E\u8BA4\u7684\u5019\u9009\u3002\u8BF7\u5148\u70B9\u51FB\u201C\u5206\u6790\u751F\u6210\u201D\u3002";
    case TextId::RuleNoStore: return L"native \u5B58\u50A8\u672A\u6253\u5F00\uFF0C\u65E0\u6CD5\u4FDD\u5B58\u6216\u9A8C\u8BC1\u89C4\u5219\u3002";
    case TextId::RuleSavedPrefix: return L"\u5DF2\u4FDD\u5B58\u534F\u8BAE\u5B57\u6BB5\u89C4\u5219\uFF1A";
    case TextId::RuleVerifyNoScan: return L"\u6CA1\u6709\u626B\u63CF\u6570\u636E\uFF0C\u65E0\u6CD5\u6267\u884C\u89C4\u5219\u9A8C\u8BC1\u3002";
    case TextId::RuleVerifyNoRules: return L"\u6CA1\u6709\u534F\u8BAE\u5B57\u6BB5\u89C4\u5219\u3002\u8BF7\u5148\u751F\u6210\u5019\u9009\u5E76\u786E\u8BA4\u89C4\u5219\u3002";
    case TextId::RuleVerifyNoObservations: return L"\u6240\u9009\u626B\u63CF\u6CA1\u6709\u5BC4\u5B58\u5668\u89C2\u6D4B\uFF0C\u65E0\u6CD5\u9A8C\u8BC1\u89C4\u5219\u3002";
    case TextId::RuleVerifySavedPrefix: return L"\u89C4\u5219\u9A8C\u8BC1\u5B8C\u6210\uFF1A\u8FD0\u884C ";
    case TextId::RulesTotalPrefix: return L"\uFF0C\u89C4\u5219 ";
    case TextId::RulesVerifiedPrefix: return L" \u6761\uFF0C\u5DF2\u9A8C\u8BC1 ";
    case TextId::RulesMissingPrefix: return L" \u6761\uFF0C\u7F3A\u5C11 ";
    case TextId::RulesUnsupportedPrefix: return L" \u6761\uFF0C\u5931\u8D25/\u4E0D\u652F\u6301 ";
    case TextId::ExportNoRun: return L"\u6682\u65E0\u89C4\u5219\u9A8C\u8BC1\u7ED3\u679C\uFF0C\u8BF7\u5148\u70B9\u51FB\u201C\u89C4\u5219\u9A8C\u8BC1\u201D\u3002";
    case TextId::ExportDialogTitle: return L"\u5BFC\u51FA\u9A8C\u8BC1\u62A5\u544A";
    case TextId::ExportFilter: return L"Markdown \u6587\u4EF6 (*.md)\0*.md\0\u6240\u6709\u6587\u4EF6 (*.*)\0*.*\0";
    case TextId::ExportDefaultPrefix: return L"\u534F\u8BAE\u89C4\u5219\u9A8C\u8BC1\u62A5\u544A-";
    case TextId::ExportFailedPrefix: return L"\u5BFC\u51FA\u9A8C\u8BC1\u62A5\u544A\u5931\u8D25\uFF1A";
    case TextId::ExportOkPrefix: return L"\u5DF2\u5BFC\u51FA\u89C4\u5219\u9A8C\u8BC1\u62A5\u544A\uFF1A";
    case TextId::AboutTitle: return L"\u5173\u4E8E\u4E32\u53E3\u503C\u5339\u914D\u5668";
    case TextId::AboutText: return L"\u4E32\u53E3\u503C\u5339\u914D\u5668 Win32 Native\n\n\u4F5C\u8005\uFF1Aw\n\u9762\u5411\u4E2D\u6587\u7528\u6237\u7684 Windows \u539F\u751F\u4E32\u53E3\u8C03\u8BD5\u3001Modbus \u626B\u63CF\u548C\u503C\u5019\u9009\u5206\u6790\u5DE5\u5177\u3002\n\n\u5F53\u524D\u76EE\u6807\uFF1A\u4E0D\u4F9D\u8D56 C#/.NET/Qt \u8FD0\u884C\u5E93\uFF0C\u5E76\u9010\u6B65\u8865\u9F50 Qt baseline \u529F\u80FD\u3002";
    case TextId::SelfTestText: return L"\u4E32\u53E3\u503C\u5339\u914D\u5668";
    }
    return L"";
}

std::wstring uiString(TextId id) {
    return std::wstring(uiText(id));
}

} // namespace svm::win32

#endif
