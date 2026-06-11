#include "app/main_window.h"

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QStandardPaths>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QToolBar>
#include <QUuid>
#include <QVariant>
#include <QVBoxLayout>

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#include "app/modbus_scan_worker.h"
#include "matching/protocol_rule_interpretation.h"
#include "matching/protocol_rule_metadata.h"
#include "matching/protocol_rule_verifier.h"
#include "matching/scan_observation_adapter.h"
#include "modbus/modbus_read_request.h"
#include "modbus/modbus_scan_executor.h"
#include "modbus/modbus_scan_plan.h"
#include "report/rule_verification_report.h"
#include "report/text_file_writer.h"
#include "storage/scan_persistence_records.h"
#include "transport/serial_port_enumerator.h"
#include "transport/serial_port_selection.h"

namespace svm::app {
namespace {

class GuardedCloseDialog final : public QDialog {
public:
    explicit GuardedCloseDialog(QWidget* parent = nullptr)
        : QDialog(parent) {}

    void setCloseGuard(std::function<bool()> closeGuard) {
        closeGuard_ = std::move(closeGuard);
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        if (closeGuard_ && !closeGuard_()) {
            event->ignore();
            return;
        }
        QDialog::closeEvent(event);
    }

    void reject() override {
        if (closeGuard_ && !closeGuard_()) {
            return;
        }
        QDialog::reject();
    }

private:
    std::function<bool()> closeGuard_;
};

QString scanExecutionStatusName(modbus::ScanExecutionStatus status) {
    switch (status) {
    case modbus::ScanExecutionStatus::Completed:
        return QStringLiteral("Completed");
    case modbus::ScanExecutionStatus::CompletedWithErrors:
        return QStringLiteral("CompletedWithErrors");
    case modbus::ScanExecutionStatus::Failed:
        return QStringLiteral("Failed");
    }
    return QStringLiteral("Unknown");
}

QString scanAttemptStatusName(modbus::ScanAttemptStatus status) {
    switch (status) {
    case modbus::ScanAttemptStatus::Success:
        return QStringLiteral("Success");
    case modbus::ScanAttemptStatus::ModbusException:
        return QStringLiteral("ModbusException");
    case modbus::ScanAttemptStatus::ParseError:
        return QStringLiteral("ParseError");
    case modbus::ScanAttemptStatus::Timeout:
        return QStringLiteral("Timeout");
    case modbus::ScanAttemptStatus::TransportError:
        return QStringLiteral("TransportError");
    }
    return QStringLiteral("Unknown");
}

storage::ScanExecutionPersistenceRecord scanExecutionToPersistence(
    const QString& sessionId,
    const modbus::ScanExecutionResult& result) {
    storage::ScanExecutionPersistenceRecord persistence;
    persistence.session.sessionId = sessionId;
    persistence.session.slaveId = result.plan.slaveId;
    persistence.session.functionCode = result.plan.functionCode;
    persistence.session.startAddress = result.plan.range.startAddress;
    persistence.session.endAddress = result.plan.range.endAddress;
    persistence.session.blockSize = result.plan.blockSize;
    persistence.session.requestCount = result.plan.requestCount();
    persistence.session.status = scanExecutionStatusName(result.status);
    persistence.session.startedAtUtc = result.startedAtUtc;
    persistence.session.finishedAtUtc = result.finishedAtUtc;
    persistence.session.successBlockCount = result.successBlockCount;
    persistence.session.failedBlockCount = result.failedBlockCount;
    persistence.session.errorMessage = result.errorMessage;

    for (const auto& block : result.blocks) {
        for (const auto& attempt : block.attempts) {
            storage::ScanAttemptRecord record;
            record.sessionId = sessionId;
            record.blockIndex = attempt.blockIndex;
            record.attemptIndex = attempt.attemptIndex;
            record.startAddress = block.block.startAddress;
            record.quantity = block.block.quantity;
            record.status = scanAttemptStatusName(attempt.status);
            record.requestFrame = attempt.requestFrame;
            record.responseFrame = attempt.responseFrame;
            record.errorMessage = attempt.errorMessage;
            record.isModbusException = attempt.isModbusException;
            record.exceptionCode = attempt.exceptionCode;
            record.exceptionDescription = attempt.exceptionDescription;
            record.sentAtUtc = attempt.sentAtUtc;
            record.receivedAtUtc = attempt.receivedAtUtc;
            record.endpoint = attempt.endpoint;
            persistence.attempts.append(record);
        }

        for (const auto& observation : block.observations) {
            storage::ScanObservationRecord record;
            record.sessionId = sessionId;
            record.blockIndex = observation.blockIndex;
            record.attemptIndex = observation.attemptIndex;
            record.slaveId = observation.slaveId;
            record.functionCode = observation.functionCode;
            record.address = observation.address;
            record.value = observation.value;
            record.observedAtUtc = observation.observedAtUtc;
            persistence.observations.append(record);
        }
    }

    return persistence;
}

QString scanSummaryText(const QString& sessionId, const modbus::ScanExecutionResult& result) {
    int attemptCount = 0;
    for (const auto& block : result.blocks) {
        attemptCount += block.attempts.size();
    }

    return QStringLiteral("扫描会话：%1｜状态：%2｜请求块：%3｜尝试：%4｜成功块：%5｜失败块：%6｜寄存器观测：%7")
        .arg(sessionId)
        .arg(modbus::describeScanExecutionStatus(result.status))
        .arg(result.plan.requestCount())
        .arg(attemptCount)
        .arg(result.successBlockCount)
        .arg(result.failedBlockCount)
        .arg(result.observations.size());
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_serialService(m_captureBus, this), m_sessionStore(this), m_consoleModel(this) {
    setWindowTitle(QStringLiteral("SerialValueMatcher Native - 串口调试工作台"));
    resize(1100, 720);

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    auto* connectionLayout = new QHBoxLayout();
    m_portCombo = new QComboBox(central);
    m_baudCombo = new QComboBox(central);
    m_connectButton = new QPushButton(QStringLiteral("连接"), central);
    auto* refreshButton = new QPushButton(QStringLiteral("刷新端口"), central);
    auto* saveProfileButton = new QPushButton(QStringLiteral("保存配置"), central);
    saveProfileButton->setToolTip(QStringLiteral("把当前串口、波特率、数据位、校验位、停止位、流控、DTR/RTS 保存为默认 Profile。"));

    m_baudCombo->addItems({
        QStringLiteral("9600"),
        QStringLiteral("19200"),
        QStringLiteral("38400"),
        QStringLiteral("57600"),
        QStringLiteral("115200"),
        QStringLiteral("230400"),
        QStringLiteral("460800"),
        QStringLiteral("921600")
    });
    m_baudCombo->setCurrentText(QStringLiteral("115200"));

    m_dataBitsCombo = new QComboBox(central);
    m_dataBitsCombo->addItem(QStringLiteral("8"), static_cast<int>(QSerialPort::Data8));
    m_dataBitsCombo->addItem(QStringLiteral("7"), static_cast<int>(QSerialPort::Data7));
    m_dataBitsCombo->addItem(QStringLiteral("6"), static_cast<int>(QSerialPort::Data6));
    m_dataBitsCombo->addItem(QStringLiteral("5"), static_cast<int>(QSerialPort::Data5));

    m_parityCombo = new QComboBox(central);
    m_parityCombo->addItem(QStringLiteral("None"), static_cast<int>(QSerialPort::NoParity));
    m_parityCombo->addItem(QStringLiteral("Even"), static_cast<int>(QSerialPort::EvenParity));
    m_parityCombo->addItem(QStringLiteral("Odd"), static_cast<int>(QSerialPort::OddParity));
    m_parityCombo->addItem(QStringLiteral("Space"), static_cast<int>(QSerialPort::SpaceParity));
    m_parityCombo->addItem(QStringLiteral("Mark"), static_cast<int>(QSerialPort::MarkParity));

    m_stopBitsCombo = new QComboBox(central);
    m_stopBitsCombo->addItem(QStringLiteral("1"), static_cast<int>(QSerialPort::OneStop));
    m_stopBitsCombo->addItem(QStringLiteral("1.5"), static_cast<int>(QSerialPort::OneAndHalfStop));
    m_stopBitsCombo->addItem(QStringLiteral("2"), static_cast<int>(QSerialPort::TwoStop));

    m_flowControlCombo = new QComboBox(central);
    m_flowControlCombo->addItem(QStringLiteral("None"), static_cast<int>(QSerialPort::NoFlowControl));
    m_flowControlCombo->addItem(QStringLiteral("Hardware"), static_cast<int>(QSerialPort::HardwareControl));
    m_flowControlCombo->addItem(QStringLiteral("Software"), static_cast<int>(QSerialPort::SoftwareControl));

    m_dtrCheck = new QCheckBox(QStringLiteral("DTR"), central);
    m_rtsCheck = new QCheckBox(QStringLiteral("RTS"), central);
    m_autoReconnectCheck = new QCheckBox(QStringLiteral("自动重连"), central);
    m_autoReconnectCheck->setToolTip(QStringLiteral("设备异常断开后，等待原端口重新出现并按上次成功参数尝试一次重连。"));

    connectionLayout->addWidget(new QLabel(QStringLiteral("串口"), central));
    connectionLayout->addWidget(m_portCombo, 2);
    connectionLayout->addWidget(new QLabel(QStringLiteral("波特率"), central));
    connectionLayout->addWidget(m_baudCombo);
    connectionLayout->addWidget(refreshButton);
    connectionLayout->addWidget(saveProfileButton);
    connectionLayout->addWidget(m_connectButton);

    auto* serialOptionsLayout = new QHBoxLayout();
    serialOptionsLayout->addWidget(new QLabel(QStringLiteral("数据位"), central));
    serialOptionsLayout->addWidget(m_dataBitsCombo);
    serialOptionsLayout->addWidget(new QLabel(QStringLiteral("校验"), central));
    serialOptionsLayout->addWidget(m_parityCombo);
    serialOptionsLayout->addWidget(new QLabel(QStringLiteral("停止位"), central));
    serialOptionsLayout->addWidget(m_stopBitsCombo);
    serialOptionsLayout->addWidget(new QLabel(QStringLiteral("流控"), central));
    serialOptionsLayout->addWidget(m_flowControlCombo);
    serialOptionsLayout->addWidget(m_dtrCheck);
    serialOptionsLayout->addWidget(m_rtsCheck);
    serialOptionsLayout->addWidget(m_autoReconnectCheck);
    serialOptionsLayout->addStretch(1);

    auto* sendLayout = new QHBoxLayout();
    m_sendModeCombo = new QComboBox(central);
    m_sendModeCombo->addItem(QStringLiteral("文本"), static_cast<int>(protocol::PayloadMode::Text));
    m_sendModeCombo->addItem(QStringLiteral("HEX"), static_cast<int>(protocol::PayloadMode::Hex));

    m_lineEndingCombo = new QComboBox(central);
    m_lineEndingCombo->addItem(QStringLiteral("无行尾"), static_cast<int>(protocol::LineEnding::None));
    m_lineEndingCombo->addItem(QStringLiteral("CR"), static_cast<int>(protocol::LineEnding::Cr));
    m_lineEndingCombo->addItem(QStringLiteral("LF"), static_cast<int>(protocol::LineEnding::Lf));
    m_lineEndingCombo->addItem(QStringLiteral("CRLF"), static_cast<int>(protocol::LineEnding::CrLf));

    m_historyCombo = new QComboBox(central);
    m_historyCombo->addItem(QStringLiteral("发送历史"));

    m_sendEdit = new QLineEdit(central);
    m_sendEdit->setPlaceholderText(QStringLiteral("文本模式直接输入内容；HEX 模式示例：01 03 00 00"));
    auto* sendButton = new QPushButton(QStringLiteral("发送"), central);
    sendLayout->addWidget(new QLabel(QStringLiteral("发送"), central));
    sendLayout->addWidget(m_sendModeCombo);
    sendLayout->addWidget(m_lineEndingCombo);
    sendLayout->addWidget(m_historyCombo);
    sendLayout->addWidget(m_sendEdit, 1);
    sendLayout->addWidget(sendButton);

    m_console = new QPlainTextEdit(central);
    m_console->setReadOnly(true);
    m_console->setPlaceholderText(QStringLiteral("RX/TX 原始事件将在这里显示，并同步写入 SQLite。"));

    rootLayout->addLayout(connectionLayout);
    rootLayout->addLayout(serialOptionsLayout);
    rootLayout->addLayout(sendLayout);
    rootLayout->addWidget(m_console, 1);
    setCentralWidget(central);

    auto* toolbar = addToolBar(QStringLiteral("主工具栏"));
    m_pauseScrollAction = toolbar->addAction(QStringLiteral("暂停滚动"));
    m_pauseScrollAction->setCheckable(true);
    m_clearConsoleAction = toolbar->addAction(QStringLiteral("清空接收区"));
    toolbar->addAction(QStringLiteral("导入回放"));
    m_modbusScanAction = toolbar->addAction(QStringLiteral("Modbus 扫描"));
    m_modbusScanAction->setToolTip(QStringLiteral("按当前串口参数执行一次 Modbus RTU 只读扫描，并把扫描事实保存到 SQLite。"));
    m_analysisWorkspaceAction = toolbar->addAction(QStringLiteral("分析工作区"));
    m_analysisWorkspaceAction->setToolTip(QStringLiteral("查看最近一次目标值匹配的稳定候选、评分和证据链。"));

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(2000);

    initializeStorage();
    refreshPorts();
    applyLatestSerialProfile();

    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(saveProfileButton, &QPushButton::clicked, this, &MainWindow::saveCurrentSerialProfile);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::sendText);
    connect(m_sendEdit, &QLineEdit::returnPressed, this, &MainWindow::sendText);
    connect(m_historyCombo, qOverload<int>(&QComboBox::activated), this, &MainWindow::applySendHistory);
    connect(m_pauseScrollAction, &QAction::toggled, this, &MainWindow::setScrollPaused);
    connect(m_clearConsoleAction, &QAction::triggered, this, &MainWindow::clearConsole);
    connect(m_modbusScanAction, &QAction::triggered, this, &MainWindow::showModbusScanDialog);
    connect(m_analysisWorkspaceAction, &QAction::triggered, this, &MainWindow::showAnalysisWorkspace);
    connect(m_autoReconnectCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        m_reconnectPolicy.setEnabled(enabled);
        if (!enabled && m_reconnectTimer != nullptr) {
            m_reconnectTimer->stop();
            statusBar()->showMessage(QStringLiteral("已关闭自动重连。"));
        }
    });
    connect(m_reconnectTimer, &QTimer::timeout, this, &MainWindow::refreshPorts);
    connect(&m_serialService, &transport::SerialPortService::serialErrorOccurred, this, &MainWindow::handleSerialError);
    connect(&m_serialService, &transport::SerialPortService::opened, this, [this](const QString& portName) {
        rememberSuccessfulConnection(portName);
        saveCurrentSerialProfile();
        m_connectButton->setText(QStringLiteral("断开"));
        statusBar()->showMessage(QStringLiteral("已连接：%1").arg(portName));
    });
    connect(&m_serialService, &transport::SerialPortService::closed, this, [this]() {
        if (!m_reconnectPolicy.isWaiting()) {
            m_connectButton->setText(QStringLiteral("连接"));
            statusBar()->showMessage(QStringLiteral("已断开"));
        }
    });
    connect(&m_captureBus, &capture::CaptureBus::eventCaptured, &m_sessionStore, &storage::SessionStore::appendRawEvent);
    connect(&m_captureBus, &capture::CaptureBus::eventCaptured, &m_consoleModel, &session::ConsoleModel::appendEvent);
    connect(&m_consoleModel, &session::ConsoleModel::lineAdded, this, &MainWindow::appendConsoleLine);
}

void MainWindow::refreshPorts() {
    const QString current = m_portCombo->currentData().toString();
    const auto latestProfile = m_sessionStore.latestSerialProfile();
    const QString profilePort = latestProfile.has_value() ? latestProfile->options.portName : QString();

    m_portCombo->clear();

    QStringList availablePortNames;
    const auto ports = transport::SerialPortEnumerator::availablePorts();
    availablePortNames.reserve(ports.size());
    for (const auto& port : ports) {
        availablePortNames.append(port.portName);
        const QString label = port.description.isEmpty()
            ? port.portName
            : QStringLiteral("%1 — %2").arg(port.portName, port.description);
        m_portCombo->addItem(label, port.portName);
    }

    const auto selection = transport::SerialPortSelectionPolicy::choose(current, profilePort, availablePortNames);
    if (selection.hasSelection()) {
        const int index = m_portCombo->findData(selection.portName);
        if (index >= 0) {
            m_portCombo->setCurrentIndex(index);
        }
    }

    QString reasonText;
    switch (selection.reason) {
    case transport::SerialPortSelectionReason::KeepCurrent:
        reasonText = QStringLiteral("保留当前选择 %1").arg(selection.portName);
        break;
    case transport::SerialPortSelectionReason::RestoreProfile:
        reasonText = QStringLiteral("当前端口不可用，已恢复 Profile 端口 %1").arg(selection.portName);
        break;
    case transport::SerialPortSelectionReason::FirstAvailable:
        reasonText = QStringLiteral("当前端口和 Profile 都不可用，已选择第一个端口 %1").arg(selection.portName);
        break;
    case transport::SerialPortSelectionReason::NoneAvailable:
        reasonText = QStringLiteral("未发现可用串口");
        break;
    }

    statusBar()->showMessage(QStringLiteral("已刷新串口：%1 个；%2。").arg(ports.size()).arg(reasonText));
    tryReconnectIfReady();
}

void MainWindow::toggleConnection() {
    if (m_serialService.isOpen()) {
        m_serialService.close();
        return;
    }

    if (m_portCombo->currentData().toString().isEmpty()) {
        showError(QStringLiteral("没有可连接的串口。请插入设备后刷新端口。"));
        return;
    }

    m_serialService.open(currentOpenOptions());
}

void MainWindow::sendText() {
    const QString text = m_sendEdit->text();
    if (text.isEmpty()) {
        return;
    }

    const auto encoded = protocol::PayloadCodec::encode(text, currentPayloadMode(), currentLineEnding());
    if (!encoded.ok) {
        showError(encoded.errorMessage);
        return;
    }

    if (encoded.payload.isEmpty()) {
        return;
    }

    if (m_serialService.writeBytes(encoded.payload) > 0) {
        m_sessionStore.saveSendHistory(text, currentPayloadMode(), currentLineEnding());
        refreshSendHistory();
        m_sendEdit->clear();
    }
}

void MainWindow::appendConsoleLine(const session::ConsoleLine& line) {
    if (!m_scrollPaused) {
        m_console->appendPlainText(line.displayLine);
    }
    statusBar()->showMessage(QStringLiteral("通信事件：%1 条%2")
        .arg(m_sessionStore.rawEventCount())
        .arg(m_scrollPaused ? QStringLiteral("（滚动已暂停）") : QString()));
}

void MainWindow::setScrollPaused(bool paused) {
    m_scrollPaused = paused;
    m_pauseScrollAction->setText(paused ? QStringLiteral("恢复滚动") : QStringLiteral("暂停滚动"));
    statusBar()->showMessage(paused
        ? QStringLiteral("已暂停滚动：仍继续接收并写入 SQLite。")
        : QStringLiteral("已恢复滚动：新的通信事件会继续显示。"));
}

void MainWindow::clearConsole() {
    m_consoleModel.clear();
    m_console->clear();
    statusBar()->showMessage(QStringLiteral("接收区已清空，SQLite 原始记录不会删除。"));
}


void MainWindow::showModbusScanDialog() {
    GuardedCloseDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Modbus RTU 只读扫描"));

    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();

    auto* slaveSpin = new QSpinBox(&dialog);
    slaveSpin->setRange(1, 247);
    slaveSpin->setValue(1);

    auto* functionCombo = new QComboBox(&dialog);
    functionCombo->addItem(QStringLiteral("FC03 保持寄存器"), static_cast<int>(modbus::ModbusReadFunction::HoldingRegisters));
    functionCombo->addItem(QStringLiteral("FC04 输入寄存器"), static_cast<int>(modbus::ModbusReadFunction::InputRegisters));

    auto* startSpin = new QSpinBox(&dialog);
    startSpin->setRange(0, 65535);
    startSpin->setValue(0);

    auto* endSpin = new QSpinBox(&dialog);
    endSpin->setRange(0, 65535);
    endSpin->setValue(15);

    auto* blockSizeSpin = new QSpinBox(&dialog);
    blockSizeSpin->setRange(1, 64);
    blockSizeSpin->setValue(16);
    blockSizeSpin->setSuffix(QStringLiteral(" 寄存器/块"));

    auto* timeoutSpin = new QSpinBox(&dialog);
    timeoutSpin->setRange(50, 30000);
    timeoutSpin->setValue(1000);
    timeoutSpin->setSingleStep(100);
    timeoutSpin->setSuffix(QStringLiteral(" ms"));

    auto* retrySpin = new QSpinBox(&dialog);
    retrySpin->setRange(0, 5);
    retrySpin->setValue(0);

    auto* intervalSpin = new QSpinBox(&dialog);
    intervalSpin->setRange(0, 5000);
    intervalSpin->setValue(30);
    intervalSpin->setSingleStep(10);
    intervalSpin->setSuffix(QStringLiteral(" ms"));

    form->addRow(QStringLiteral("从站 ID"), slaveSpin);
    form->addRow(QStringLiteral("功能码"), functionCombo);
    form->addRow(QStringLiteral("起始地址"), startSpin);
    form->addRow(QStringLiteral("结束地址"), endSpin);
    form->addRow(QStringLiteral("块大小"), blockSizeSpin);
    form->addRow(QStringLiteral("响应超时"), timeoutSpin);
    form->addRow(QStringLiteral("重试次数"), retrySpin);
    form->addRow(QStringLiteral("请求间隔"), intervalSpin);
    layout->addLayout(form);

    auto* hintLabel = new QLabel(QStringLiteral(
        "说明：扫描会按当前主界面的串口、波特率、数据位、校验位、停止位、流控、DTR/RTS 参数打开串口。"
        "如果当前串口调试连接已打开，执行扫描前会先断开，扫描完成后请按需重新连接。"), &dialog);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    auto* statusLabel = new QLabel(QStringLiteral("请输入扫描参数，然后点击“执行扫描并保存”。"), &dialog);
    statusLabel->setWordWrap(true);
    layout->addWidget(statusLabel);

    auto* buttons = new QDialogButtonBox(&dialog);
    auto* executeButton = buttons->addButton(QStringLiteral("执行扫描并保存"), QDialogButtonBox::AcceptRole);
    auto* closeButton = buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    bool scanRunning = false;
    std::shared_ptr<std::atomic_bool> activeCancelFlag;
    QPointer<QThread> activeThread;

    auto resetScanUi = [&scanRunning, &activeCancelFlag, &activeThread, executeButton, closeButton]() {
        scanRunning = false;
        activeCancelFlag.reset();
        activeThread = nullptr;
        executeButton->setEnabled(true);
        closeButton->setText(QStringLiteral("关闭"));
        closeButton->setEnabled(true);
    };

    auto requestClose = [&scanRunning, &activeCancelFlag, statusLabel, closeButton]() {
        if (!scanRunning) {
            return true;
        }
        if (activeCancelFlag != nullptr) {
            activeCancelFlag->store(true, std::memory_order_relaxed);
        }
        closeButton->setEnabled(false);
        statusLabel->setText(QStringLiteral("正在取消扫描：当前串口请求结束后将停止后续扫描。"));
        return false;
    };
    dialog.setCloseGuard(requestClose);

    connect(buttons, &QDialogButtonBox::rejected, &dialog, [&dialog, requestClose]() {
        if (requestClose()) {
            dialog.accept();
        }
    });
    connect(executeButton, &QPushButton::clicked, this, [this, &dialog, slaveSpin, functionCombo, startSpin, endSpin, blockSizeSpin, timeoutSpin, retrySpin, intervalSpin, statusLabel, executeButton, closeButton, &scanRunning, &activeCancelFlag, &activeThread, &resetScanUi]() {
        if (scanRunning) {
            return;
        }

        modbus::ScanPlanOptions planOptions;
        planOptions.slaveId = slaveSpin->value();
        planOptions.functionCode = static_cast<quint8>(functionCombo->currentData().toInt());
        planOptions.range = {startSpin->value(), endSpin->value()};
        planOptions.blockSize = blockSizeSpin->value();
        planOptions.requestIntervalMs = intervalSpin->value();
        planOptions.retryCount = retrySpin->value();
        planOptions.safetyLevel = modbus::ScanSafetyLevel::Custom;

        const auto planResult = modbus::buildScanPlan(planOptions);
        if (!planResult.ok) {
            statusLabel->setText(planResult.errorMessage);
            QMessageBox::warning(&dialog, QStringLiteral("扫描参数无效"), planResult.errorMessage);
            return;
        }

        auto serialOptions = currentOpenOptions();
        if (serialOptions.portName.trimmed().isEmpty()) {
            const QString message = QStringLiteral("请先在主界面选择一个串口，再执行 Modbus 扫描。");
            statusLabel->setText(message);
            QMessageBox::warning(&dialog, QStringLiteral("缺少串口"), message);
            return;
        }

        if (m_serialService.isOpen()) {
            const auto choice = QMessageBox::question(
                &dialog,
                QStringLiteral("需要临时占用串口"),
                QStringLiteral("当前串口调试连接已打开。执行 Modbus 扫描需要临时断开当前连接，扫描完成后可手动重新连接。是否继续？"));
            if (choice != QMessageBox::Yes) {
                statusLabel->setText(QStringLiteral("扫描已取消：当前串口连接保持不变。"));
                return;
            }
            m_serialService.close();
            m_connectButton->setText(QStringLiteral("连接"));
        }

        const QString scanSessionId = QStringLiteral("scan-%1-%2")
            .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz")))
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
        serialOptions.sessionId = scanSessionId;

        executeButton->setEnabled(false);
        statusLabel->setText(QStringLiteral("正在扫描：%1，请等待响应或超时…").arg(scanSessionId));
        modbus::ScanExecutionOptions executionOptions;
        executionOptions.responseTimeoutMs = timeoutSpin->value();
        executionOptions.continueOnBlockError = true;
        executionOptions.retryOnTimeout = true;
        executionOptions.retryOnTransportError = true;

        ModbusScanWorkerRequest request;
        request.scanSessionId = scanSessionId;
        request.serialOptions = serialOptions;
        request.plan = planResult.plan;
        request.executionOptions = executionOptions;

        auto cancelFlag = std::make_shared<std::atomic_bool>(false);
        auto* thread = new QThread();
        thread->setObjectName(QStringLiteral("modbus-scan-%1").arg(scanSessionId));
        auto* worker = new ModbusScanWorker(std::move(request), cancelFlag);
        worker->moveToThread(thread);

        scanRunning = true;
        activeCancelFlag = cancelFlag;
        activeThread = thread;
        closeButton->setText(QStringLiteral("取消扫描"));
        closeButton->setEnabled(true);

        connect(thread, &QThread::started, worker, &ModbusScanWorker::execute);
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        connect(worker, &ModbusScanWorker::finished, thread, [thread](const QString&, const modbus::ScanExecutionResult&) {
            thread->quit();
        }, Qt::DirectConnection);
        connect(worker, &ModbusScanWorker::failed, thread, [thread](const QString&) {
            thread->quit();
        }, Qt::DirectConnection);
        connect(worker, &ModbusScanWorker::rawIoEventCaptured, &dialog, [this](const capture::RawIoEvent& event) {
            m_captureBus.publish(event);
        });
        connect(worker, &ModbusScanWorker::failed, &dialog, [this, &dialog, statusLabel, &resetScanUi](const QString& message) {
            resetScanUi();
            statusLabel->setText(message);
            QMessageBox::warning(&dialog, QStringLiteral("打开串口失败"), message);
        });
        connect(worker, &ModbusScanWorker::finished, &dialog, [this, &dialog, statusLabel, &resetScanUi](const QString& scanSessionId, const modbus::ScanExecutionResult& result) {
            const auto persistence = scanExecutionToPersistence(scanSessionId, result);
            if (!m_sessionStore.saveScanExecution(persistence)) {
                const QString message = QStringLiteral("扫描已执行，但保存扫描结果失败：%1").arg(m_sessionStore.lastErrorText());
                resetScanUi();
                statusLabel->setText(message);
                QMessageBox::warning(&dialog, QStringLiteral("保存失败"), message);
                return;
            }

            const QString summary = scanSummaryText(scanSessionId, result);
            const QString message = result.errorMessage.isEmpty()
                ? QStringLiteral("已保存 Modbus 扫描结果。%1").arg(summary)
                : QStringLiteral("已保存 Modbus 扫描结果。%1｜主要错误：%2").arg(summary, result.errorMessage);
            resetScanUi();
            statusLabel->setText(message);
            statusBar()->showMessage(message);
            QMessageBox::information(&dialog, QStringLiteral("扫描完成"), message);
        });

        thread->start();
    });

    dialog.resize(560, 360);
    dialog.exec();
    if (scanRunning) {
        if (activeCancelFlag != nullptr) {
            activeCancelFlag->store(true, std::memory_order_relaxed);
        }
        if (!activeThread.isNull()) {
            activeThread->wait();
        }
    }
}

void MainWindow::showAnalysisWorkspace() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("分析工作区 - 稳定候选"));
    auto* layout = new QVBoxLayout(&dialog);

    auto validateRuleTypeBeforeSave = [this](const QString& candidateType, int registerCount) -> bool {
        const QString validationError = matching::validateProtocolRuleTypeAndRegisterCount(candidateType, registerCount);
        if (!validationError.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("规则类型校验"), QStringLiteral("规则类型与寄存器数量不匹配，规则尚未保存：\n%1").arg(validationError));
            return false;
        }
        return true;
    };

    auto validateInterpretationMapBeforeSave = [this](const QString& candidateType, const QString& interpretationMap) -> bool {
        const matching::InterpretationMapValidationResult validation = matching::validateInterpretationMap(candidateType, interpretationMap);
        if (!validation.valid) {
            QString message = QStringLiteral("解释映射格式有误，规则尚未保存：\n- %1\n\nBitFlags 格式：bit=名称|未置位说明|置位说明，例如 0=运行允许|未允许|已允许\nEnumMap 格式：数值=中文含义，例如 1=运行")
                .arg(validation.errors.join(QStringLiteral("\n- ")));
            QMessageBox::warning(this, QStringLiteral("解释映射校验"), message);
            return false;
        }
        if (!interpretationMap.trimmed().isEmpty()) {
            statusBar()->showMessage(QStringLiteral("解释映射校验通过：%1").arg(validation.previewText), 8000);
        }
        return true;
    };

    const auto recentScanSessions = m_sessionStore.recentScanSessions(20);

    auto* generatorLayout = new QHBoxLayout();
    auto* scanSessionCombo = new QComboBox(&dialog);
    scanSessionCombo->setMinimumWidth(260);
    if (recentScanSessions.isEmpty()) {
        scanSessionCombo->addItem(QStringLiteral("暂无扫描会话"), QString());
    } else {
        for (const storage::ScanSessionRecord& session : recentScanSessions) {
            const QString timeText = session.finishedAtUtc.isValid()
                ? session.finishedAtUtc.toLocalTime().toString(QStringLiteral("MM-dd HH:mm:ss"))
                : QStringLiteral("时间未知");
            const QString label = QStringLiteral("%1｜从站 %2｜FC%3｜%4-%5｜%6")
                .arg(timeText)
                .arg(session.slaveId)
                .arg(session.functionCode)
                .arg(session.startAddress)
                .arg(session.endAddress)
                .arg(session.status);
            scanSessionCombo->addItem(label, session.sessionId);
        }
    }

    auto* targetValueSpin = new QDoubleSpinBox(&dialog);
    targetValueSpin->setRange(-1000000000.0, 1000000000.0);
    targetValueSpin->setDecimals(6);
    targetValueSpin->setValue(0.0);
    targetValueSpin->setToolTip(QStringLiteral("输入当前真实目标值，例如仪表显示值、传感器读数或人工确认值。"));

    auto* targetUnitEdit = new QLineEdit(&dialog);
    targetUnitEdit->setPlaceholderText(QStringLiteral("单位，可空"));
    targetUnitEdit->setMaximumWidth(120);

    auto* toleranceSpin = new QDoubleSpinBox(&dialog);
    toleranceSpin->setRange(0.0, 1000000000.0);
    toleranceSpin->setDecimals(6);
    toleranceSpin->setValue(0.01);
    toleranceSpin->setToolTip(QStringLiteral("绝对容差：候选工程值与目标值允许的最大差值。"));

    auto* generateButton = new QPushButton(QStringLiteral("基于所选扫描生成候选"), &dialog);
    generateButton->setEnabled(!recentScanSessions.isEmpty());
    auto* generationStatusLabel = new QLabel(recentScanSessions.isEmpty()
        ? QStringLiteral("候选生成：暂无扫描会话，请先完成并保存一次扫描。")
        : QStringLiteral("候选生成：请选择扫描会话并输入目标值。"), &dialog);
    generationStatusLabel->setWordWrap(true);

    generatorLayout->addWidget(new QLabel(QStringLiteral("扫描会话"), &dialog));
    generatorLayout->addWidget(scanSessionCombo, 2);
    generatorLayout->addWidget(new QLabel(QStringLiteral("目标值"), &dialog));
    generatorLayout->addWidget(targetValueSpin);
    generatorLayout->addWidget(new QLabel(QStringLiteral("单位"), &dialog));
    generatorLayout->addWidget(targetUnitEdit);
    generatorLayout->addWidget(new QLabel(QStringLiteral("绝对容差"), &dialog));
    generatorLayout->addWidget(toleranceSpin);
    generatorLayout->addWidget(generateButton);
    layout->addLayout(generatorLayout);
    layout->addWidget(generationStatusLabel);

    connect(generateButton, &QPushButton::clicked, this, [this, scanSessionCombo, targetValueSpin, targetUnitEdit, toleranceSpin, generationStatusLabel]() {
        const QString selectedSessionId = scanSessionCombo->currentData().toString();
        if (selectedSessionId.isEmpty()) {
            const QString message = QStringLiteral("请先选择一个扫描会话；如果列表为空，请先完成一次 Modbus 扫描并保存扫描结果。");
            generationStatusLabel->setText(message);
            QMessageBox::information(this, QStringLiteral("候选生成"), message);
            return;
        }

        const auto scanSession = m_sessionStore.scanSession(selectedSessionId);
        if (!scanSession.has_value()) {
            const QString message = QStringLiteral("扫描会话不存在或已被清理：%1。").arg(selectedSessionId);
            generationStatusLabel->setText(message);
            QMessageBox::information(this, QStringLiteral("候选生成"), message);
            return;
        }

        const auto observations = m_sessionStore.scanObservations(scanSession->sessionId);
        if (m_sessionStore.hasReadError()) {
            const QString message = m_sessionStore.lastReadErrorText();
            generationStatusLabel->setText(message);
            QMessageBox::warning(this, QStringLiteral("候选生成"), message);
            return;
        }
        if (observations.isEmpty()) {
            const QString message = QStringLiteral("所选扫描会话没有寄存器观测，无法生成候选：%1。").arg(scanSession->sessionId);
            generationStatusLabel->setText(message);
            QMessageBox::information(this, QStringLiteral("候选生成"), message);
            return;
        }

        const QList<matching::RegisterSample> samples = matching::registerSamplesFromScanObservations(observations);
        matching::TargetValue target;
        target.label = QStringLiteral("界面输入目标值");
        target.value = targetValueSpin->value();
        target.unit = targetUnitEdit->text().trimmed();
        target.sampledAtUtc = QDateTime::currentDateTimeUtc();

        matching::CandidateGenerationOptions options;
        options.tolerance.absolute = toleranceSpin->value();
        const matching::CandidateGenerationResult result = matching::generateValueCandidates(samples, target, options);
        if (!result.success) {
            generationStatusLabel->setText(result.errorMessage);
            QMessageBox::warning(this, QStringLiteral("候选生成"), result.errorMessage);
            return;
        }

        storage::MatchRunRecord run;
        run.runId = QStringLiteral("match-%1").arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz")));
        run.sourceScanSessionId = scanSession->sessionId;
        run.targetLabel = target.label;
        run.targetValue = target.value;
        run.targetUnit = target.unit;
        run.sampledAtUtc = target.sampledAtUtc;
        run.toleranceAbsolute = options.tolerance.absolute;
        run.toleranceRelativeRatio = options.tolerance.relativeRatio;
        run.createdAtUtc = QDateTime::currentDateTimeUtc();

        if (!m_sessionStore.saveMatchRun(run, result.candidates)) {
            generationStatusLabel->setText(m_sessionStore.lastErrorText());
            QMessageBox::warning(this, QStringLiteral("候选生成"), m_sessionStore.lastErrorText());
            return;
        }

        const QString message = QStringLiteral("已基于所选扫描 %1 生成并保存候选：%2 个，匹配运行：%3。")
            .arg(scanSession->sessionId)
            .arg(result.candidates.size())
            .arg(run.runId);
        generationStatusLabel->setText(message + QStringLiteral(" 多样本稳定性排序会在后续步骤接入。"));
        statusBar()->showMessage(message);
    });

    const auto latestRun = m_sessionStore.latestStabilityRun();
    if (!latestRun.has_value()) {
        auto* emptyLabel = new QLabel(QStringLiteral(
            "暂无稳定候选。\n\n"
            "请先完成扫描、目标值候选生成和多样本稳定性分析；完成后这里会显示候选字段、稳定性评分和证据链。"), &dialog);
        emptyLabel->setWordWrap(true);
        layout->addWidget(emptyLabel);
    } else {
        const auto candidates = m_sessionStore.stableCandidates(latestRun->stabilityRunId);
        auto* summaryLabel = new QLabel(QStringLiteral("最近稳定性分析：%1｜来源匹配 %2 次｜稳定候选 %3 个｜创建时间 %4")
            .arg(latestRun->stabilityRunId)
            .arg(latestRun->sourceMatchRunIds.size())
            .arg(candidates.size())
            .arg(latestRun->createdAtUtc.isValid() ? latestRun->createdAtUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QStringLiteral("未知")), &dialog);
        summaryLabel->setWordWrap(true);
        layout->addWidget(summaryLabel);

        auto* table = new QTableWidget(candidates.size(), 9, &dialog);
        table->setHorizontalHeaderLabels({
            QStringLiteral("置信"),
            QStringLiteral("稳定分"),
            QStringLiteral("样本"),
            QStringLiteral("类型"),
            QStringLiteral("地址"),
            QStringLiteral("寄存器"),
            QStringLiteral("字序/字节序"),
            QStringLiteral("平均/最大误差"),
            QStringLiteral("证据摘要")
        });
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setAlternatingRowColors(true);
        table->verticalHeader()->setVisible(false);

        for (int row = 0; row < candidates.size(); ++row) {
            const storage::StableCandidateRecord& candidate = candidates.at(row);
            const QString addressText = candidate.addresses.isEmpty()
                ? QString::number(candidate.startAddress)
                : (candidate.addresses.size() == 1
                    ? QString::number(candidate.addresses.first())
                    : QStringLiteral("%1-%2").arg(candidate.addresses.first()).arg(candidate.addresses.last()));
            const QString registerText = QStringLiteral("%1 个，从 %2 开始")
                .arg(candidate.registerCount)
                .arg(candidate.startAddress);
            const QString orderText = QStringLiteral("%1 / %2").arg(candidate.wordOrder, candidate.byteOrder);
            const QString errorText = QStringLiteral("%1 / %2")
                .arg(QString::number(candidate.meanAbsoluteError, 'g', 8), QString::number(candidate.maxAbsoluteError, 'g', 8));

            table->setItem(row, 0, new QTableWidgetItem(candidate.confidenceLevel));
            table->setItem(row, 1, new QTableWidgetItem(QString::number(candidate.stabilityScore, 'f', 1)));
            table->setItem(row, 2, new QTableWidgetItem(QString::number(candidate.sampleCount)));
            table->setItem(row, 3, new QTableWidgetItem(candidate.candidateType));
            table->setItem(row, 4, new QTableWidgetItem(addressText));
            table->setItem(row, 5, new QTableWidgetItem(registerText));
            table->setItem(row, 6, new QTableWidgetItem(orderText));
            table->setItem(row, 7, new QTableWidgetItem(errorText));
            table->setItem(row, 8, new QTableWidgetItem(candidate.evidenceSummary));
        }

        table->resizeColumnsToContents();
        table->horizontalHeader()->setStretchLastSection(true);
        layout->addWidget(table, 1);

        auto* detailText = new QPlainTextEdit(&dialog);
        detailText->setReadOnly(true);
        detailText->setMaximumHeight(190);
        detailText->setPlaceholderText(QStringLiteral("选择上方稳定候选后，这里显示 observation / block / attempt 证据链。"));
        layout->addWidget(detailText);

        auto* confirmRuleButton = new QPushButton(QStringLiteral("确认为协议字段规则"), &dialog);
        confirmRuleButton->setEnabled(!candidates.isEmpty());
        confirmRuleButton->setToolTip(QStringLiteral("把当前选中的稳定候选保存为可复用的协议字段规则。"));
        layout->addWidget(confirmRuleButton);

        auto renderCandidateDetails = [this, detailText, candidates](int row) {
            if (row < 0 || row >= candidates.size()) {
                detailText->clear();
                return;
            }

            const storage::StableCandidateRecord& candidate = candidates.at(row);
            QStringList lines;
            lines.append(QStringLiteral("稳定候选详情"));
            lines.append(QStringLiteral("- 置信等级：%1").arg(candidate.confidenceLevel));
            lines.append(QStringLiteral("- 稳定性评分：%1").arg(QString::number(candidate.stabilityScore, 'f', 1)));
            lines.append(QStringLiteral("- 样本数：%1").arg(candidate.sampleCount));
            lines.append(QStringLiteral("- 类型/字序/字节序：%1 / %2 / %3").arg(candidate.candidateType, candidate.wordOrder, candidate.byteOrder));
            lines.append(QStringLiteral("- 地址：%1；寄存器数量：%2").arg(candidate.startAddress).arg(candidate.registerCount));
            lines.append(QStringLiteral("- 来源 match runs：%1").arg(candidate.runIds.join(QStringLiteral(", "))));
            lines.append(QStringLiteral("- 来源扫描会话：%1").arg(candidate.sourceScanSessionIds.join(QStringLiteral(", "))));
            lines.append(QStringLiteral("- 证据摘要：%1").arg(candidate.evidenceSummary));
            lines.append(QString());
            lines.append(QStringLiteral("观测证据链："));

            const auto observations = m_sessionStore.scanObservationsByIds(candidate.observationIds);
            if (observations.isEmpty()) {
                lines.append(QStringLiteral("- 未能在数据库中找到对应 observation 记录。"));
            } else {
                for (const storage::ScanObservationRecord& observation : observations) {
                    const QString timeText = observation.observedAtUtc.isValid()
                        ? observation.observedAtUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                        : QStringLiteral("时间未知");
                    lines.append(QStringLiteral("- observation #%1｜session %2｜block %3｜attempt %4｜从站 %5｜FC%6｜地址 %7｜值 %8｜%9")
                        .arg(observation.id)
                        .arg(observation.sessionId)
                        .arg(observation.blockIndex)
                        .arg(observation.attemptIndex)
                        .arg(observation.slaveId)
                        .arg(observation.functionCode)
                        .arg(observation.address)
                        .arg(observation.value)
                        .arg(timeText));
                }
            }

            detailText->setPlainText(lines.join(QLatin1Char('\n')));
        };

        connect(confirmRuleButton, &QPushButton::clicked, this, [this, table, candidates, validateRuleTypeBeforeSave, validateInterpretationMapBeforeSave]() {
            const int row = table->currentRow();
            if (row < 0 || row >= candidates.size()) {
                QMessageBox::information(this, QStringLiteral("确认规则"), QStringLiteral("请先选择一个稳定候选。"));
                return;
            }

            const storage::StableCandidateRecord& candidate = candidates.at(row);
            const QString defaultName = QStringLiteral("字段_%1_%2").arg(candidate.startAddress).arg(candidate.candidateType);
            bool accepted = false;
            const QString fieldName = QInputDialog::getText(this,
                QStringLiteral("确认协议字段规则"),
                QStringLiteral("字段名称："),
                QLineEdit::Normal,
                defaultName,
                &accepted).trimmed();
            if (!accepted) {
                return;
            }
            if (fieldName.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("确认规则"), QStringLiteral("字段名称不能为空。"));
                return;
            }

            if (!validateRuleTypeBeforeSave(candidate.candidateType, candidate.registerCount)) {
                return;
            }

            QString interpretationMap;
            if (matching::protocolRuleTypeSupportsInterpretationMap(candidate.candidateType)) {
                interpretationMap = QInputDialog::getMultiLineText(this,
                    QStringLiteral("确认协议字段规则"),
                    QStringLiteral("解释映射（可留空）：\nBitFlags：bit=名称|未置位说明|置位说明，例如 0=运行允许|未允许|已允许\nEnumMap：数值=中文含义，例如 1=运行"),
                    QString(),
                    &accepted).trimmed();
                if (!accepted) {
                    return;
                }
            }

            if (!validateInterpretationMapBeforeSave(candidate.candidateType, interpretationMap)) {
                return;
            }

            storage::ProtocolFieldRuleRecord rule;
            rule.ruleId = QStringLiteral("rule-%1").arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz")));
            rule.fieldName = fieldName;
            rule.sourceStabilityRunId = candidate.stabilityRunId;
            rule.sourceStableCandidateId = candidate.id;
            rule.candidateType = candidate.candidateType;
            rule.wordOrder = candidate.wordOrder;
            rule.byteOrder = candidate.byteOrder;
            rule.slaveId = candidate.slaveId;
            rule.functionCode = candidate.functionCode;
            rule.startAddress = candidate.startAddress;
            rule.registerCount = candidate.registerCount;
            rule.scaleMultiplier = candidate.scaleMultiplier;
            rule.scaleOffset = candidate.scaleOffset;
            rule.confidenceLevel = candidate.confidenceLevel;
            rule.stabilityScore = candidate.stabilityScore;
            rule.evidenceSummary = candidate.evidenceSummary;
            rule.interpretationMap = interpretationMap;
            rule.createdAtUtc = QDateTime::currentDateTimeUtc();

            if (!m_sessionStore.saveProtocolFieldRule(rule)) {
                QMessageBox::warning(this, QStringLiteral("确认规则"), m_sessionStore.lastErrorText());
                return;
            }

            const QString message = QStringLiteral("已保存协议字段规则：%1（%2）。重新打开分析工作区可在规则列表查看。").arg(rule.fieldName, rule.ruleId);
            statusBar()->showMessage(message);
            QMessageBox::information(this, QStringLiteral("确认规则"), message);
        });

        connect(table, &QTableWidget::currentCellChanged, this, [renderCandidateDetails](int currentRow, int, int, int) {
            renderCandidateDetails(currentRow);
        });
        if (!candidates.isEmpty()) {
            table->setCurrentCell(0, 0);
            renderCandidateDetails(0);
        }
    }

    const auto protocolRules = m_sessionStore.recentProtocolFieldRules(20);
    const QString protocolRulesReadError = m_sessionStore.lastReadErrorText();
    auto* rulesTitle = new QLabel(QStringLiteral("已确认协议字段规则"), &dialog);
    layout->addWidget(rulesTitle);
    if (!protocolRulesReadError.isEmpty()) {
        auto* errorRulesLabel = new QLabel(QStringLiteral("读取已确认规则失败：%1").arg(protocolRulesReadError), &dialog);
        errorRulesLabel->setWordWrap(true);
        layout->addWidget(errorRulesLabel);
        statusBar()->showMessage(protocolRulesReadError);
    } else if (protocolRules.isEmpty()) {
        auto* emptyRulesLabel = new QLabel(QStringLiteral("暂无已确认规则。选择稳定候选并点击“确认为协议字段规则”后，规则会保存到这里。"), &dialog);
        emptyRulesLabel->setWordWrap(true);
        layout->addWidget(emptyRulesLabel);
    } else {
        auto* rulesTable = new QTableWidget(protocolRules.size(), 9, &dialog);
        rulesTable->setHorizontalHeaderLabels({
            QStringLiteral("字段名"),
            QStringLiteral("类型"),
            QStringLiteral("地址"),
            QStringLiteral("寄存器"),
            QStringLiteral("字序/字节序"),
            QStringLiteral("置信"),
            QStringLiteral("稳定分"),
            QStringLiteral("来源候选"),
            QStringLiteral("创建时间")
        });
        rulesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        rulesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        rulesTable->setAlternatingRowColors(true);
        rulesTable->verticalHeader()->setVisible(false);
        rulesTable->setMaximumHeight(190);

        for (int row = 0; row < protocolRules.size(); ++row) {
            const storage::ProtocolFieldRuleRecord& rule = protocolRules.at(row);
            auto* fieldNameItem = new QTableWidgetItem(rule.fieldName);
            fieldNameItem->setData(Qt::UserRole, rule.ruleId);
            rulesTable->setItem(row, 0, fieldNameItem);
            rulesTable->setItem(row, 1, new QTableWidgetItem(rule.candidateType));
            rulesTable->setItem(row, 2, new QTableWidgetItem(QString::number(rule.startAddress)));
            rulesTable->setItem(row, 3, new QTableWidgetItem(QString::number(rule.registerCount)));
            rulesTable->setItem(row, 4, new QTableWidgetItem(QStringLiteral("%1 / %2").arg(rule.wordOrder, rule.byteOrder)));
            rulesTable->setItem(row, 5, new QTableWidgetItem(rule.confidenceLevel));
            rulesTable->setItem(row, 6, new QTableWidgetItem(QString::number(rule.stabilityScore, 'f', 1)));
            rulesTable->setItem(row, 7, new QTableWidgetItem(QString::number(rule.sourceStableCandidateId)));
            rulesTable->setItem(row, 8, new QTableWidgetItem(rule.createdAtUtc.isValid()
                ? rule.createdAtUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                : QStringLiteral("未知")));
        }
        rulesTable->resizeColumnsToContents();
        rulesTable->horizontalHeader()->setStretchLastSection(true);
        layout->addWidget(rulesTable);

        auto* ruleActions = new QHBoxLayout();
        auto* editRuleButton = new QPushButton(QStringLiteral("编辑选中规则"), &dialog);
        auto* deleteRuleButton = new QPushButton(QStringLiteral("删除选中规则"), &dialog);
        editRuleButton->setToolTip(QStringLiteral("修改选中协议字段规则的字段名、单位、规则类型和解释映射。"));
        deleteRuleButton->setToolTip(QStringLiteral("删除误确认或不再需要的协议字段规则。"));
        ruleActions->addWidget(editRuleButton);
        ruleActions->addWidget(deleteRuleButton);
        ruleActions->addStretch(1);
        layout->addLayout(ruleActions);

        auto selectedRuleId = [rulesTable]() -> QString {
            const int row = rulesTable->currentRow();
            if (row < 0) {
                return QString();
            }
            QTableWidgetItem* item = rulesTable->item(row, 0);
            return item == nullptr ? QString() : item->data(Qt::UserRole).toString();
        };

        connect(editRuleButton, &QPushButton::clicked, this, [this, rulesTable, selectedRuleId, validateRuleTypeBeforeSave, validateInterpretationMapBeforeSave]() {
            const QString ruleId = selectedRuleId();
            if (ruleId.isEmpty()) {
                QMessageBox::information(this, QStringLiteral("编辑规则"), QStringLiteral("请先选择一个协议字段规则。"));
                return;
            }

            auto rule = m_sessionStore.protocolFieldRule(ruleId);
            if (!rule.has_value()) {
                const QString message = m_sessionStore.hasReadError()
                    ? m_sessionStore.lastReadErrorText()
                    : QStringLiteral("未找到选中的协议字段规则，请重新打开分析工作区。");
                QMessageBox::warning(this, QStringLiteral("编辑规则"), message);
                return;
            }

            bool accepted = false;
            const QString fieldName = QInputDialog::getText(this,
                QStringLiteral("编辑协议字段规则"),
                QStringLiteral("字段名称："),
                QLineEdit::Normal,
                rule->fieldName,
                &accepted).trimmed();
            if (!accepted) {
                return;
            }
            if (fieldName.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("编辑规则"), QStringLiteral("字段名称不能为空。"));
                return;
            }

            const QString unit = QInputDialog::getText(this,
                QStringLiteral("编辑协议字段规则"),
                QStringLiteral("单位（可留空）："),
                QLineEdit::Normal,
                rule->unit,
                &accepted).trimmed();
            if (!accepted) {
                return;
            }

            const QStringList supportedTypes = matching::supportedProtocolRuleTypeNames();
            const int currentTypeIndex = qMax(0, supportedTypes.indexOf(rule->candidateType));
            const QString candidateType = QInputDialog::getItem(this,
                QStringLiteral("编辑协议字段规则"),
                QStringLiteral("规则类型："),
                supportedTypes,
                currentTypeIndex,
                false,
                &accepted);
            if (!accepted) {
                return;
            }
            if (!validateRuleTypeBeforeSave(candidateType, rule->registerCount)) {
                return;
            }

            QString interpretationMap;
            if (matching::protocolRuleTypeSupportsInterpretationMap(candidateType)) {
                interpretationMap = QInputDialog::getMultiLineText(this,
                    QStringLiteral("编辑协议字段规则"),
                    QStringLiteral("解释映射（可留空）：\nBitFlags：bit=名称|未置位说明|置位说明，例如 0=运行允许|未允许|已允许\nEnumMap：数值=中文含义，例如 1=运行"),
                    rule->interpretationMap,
                    &accepted).trimmed();
                if (!accepted) {
                    return;
                }
            }

            if (!validateInterpretationMapBeforeSave(candidateType, interpretationMap)) {
                return;
            }

            rule->fieldName = fieldName;
            rule->unit = unit;
            rule->candidateType = candidateType;
            rule->interpretationMap = interpretationMap;
            if (!m_sessionStore.saveProtocolFieldRule(*rule)) {
                QMessageBox::warning(this, QStringLiteral("编辑规则"), m_sessionStore.lastErrorText());
                return;
            }

            const int row = rulesTable->currentRow();
            if (row >= 0 && rulesTable->item(row, 0) != nullptr) {
                rulesTable->item(row, 0)->setText(rule->fieldName);
            }
            const QString message = QStringLiteral("已更新协议字段规则：%1（%2）。").arg(rule->fieldName, rule->ruleId);
            statusBar()->showMessage(message);
            QMessageBox::information(this, QStringLiteral("编辑规则"), message);
        });

        connect(deleteRuleButton, &QPushButton::clicked, this, [this, rulesTable, selectedRuleId]() {
            const QString ruleId = selectedRuleId();
            if (ruleId.isEmpty()) {
                QMessageBox::information(this, QStringLiteral("删除规则"), QStringLiteral("请先选择一个协议字段规则。"));
                return;
            }

            const QString fieldName = rulesTable->currentRow() >= 0 && rulesTable->item(rulesTable->currentRow(), 0) != nullptr
                ? rulesTable->item(rulesTable->currentRow(), 0)->text()
                : ruleId;
            const auto answer = QMessageBox::question(this,
                QStringLiteral("删除协议字段规则"),
                QStringLiteral("确定删除协议字段规则“%1”吗？删除后不会影响原始扫描、候选和稳定性分析记录。")
                    .arg(fieldName));
            if (answer != QMessageBox::Yes) {
                return;
            }

            if (!m_sessionStore.deleteProtocolFieldRule(ruleId)) {
                QMessageBox::warning(this, QStringLiteral("删除规则"), m_sessionStore.lastErrorText());
                return;
            }

            const int row = rulesTable->currentRow();
            if (row >= 0) {
                rulesTable->removeRow(row);
            }
            const QString message = QStringLiteral("已删除协议字段规则：%1。").arg(fieldName);
            statusBar()->showMessage(message);
            QMessageBox::information(this, QStringLiteral("删除规则"), message);
        });

        auto* verificationTitle = new QLabel(QStringLiteral("规则验证结果"), &dialog);
        layout->addWidget(verificationTitle);
        auto* verificationActions = new QHBoxLayout();
        auto* verifyRulesButton = new QPushButton(QStringLiteral("使用规则验证所选扫描"), &dialog);
        verifyRulesButton->setEnabled(!recentScanSessions.isEmpty());
        verifyRulesButton->setToolTip(QStringLiteral("使用当前已确认规则，对顶部选择的扫描会话重新解码并验证。"));
        const auto latestVerificationRunForExport = m_sessionStore.latestRuleVerificationRun();
        const QString latestVerificationRunReadError = m_sessionStore.lastReadErrorText();
        auto* exportReportButton = new QPushButton(QStringLiteral("导出最近验证报告"), &dialog);
        exportReportButton->setEnabled(latestVerificationRunForExport.has_value());
        exportReportButton->setToolTip(QStringLiteral("把最近一次规则验证结果导出为 Markdown 报告文件。"));
        verificationActions->addWidget(verifyRulesButton);
        verificationActions->addWidget(exportReportButton);
        verificationActions->addStretch(1);
        layout->addLayout(verificationActions);

        auto* verificationStatusLabel = new QLabel(!latestVerificationRunReadError.isEmpty()
            ? QStringLiteral("规则验证：读取最近验证运行失败：%1").arg(latestVerificationRunReadError)
            : (recentScanSessions.isEmpty()
                ? QStringLiteral("规则验证：暂无扫描会话，请先完成一次扫描。")
                : QStringLiteral("规则验证：选择扫描会话后点击按钮。")), &dialog);
        verificationStatusLabel->setWordWrap(true);
        layout->addWidget(verificationStatusLabel);

        auto* verificationTable = new QTableWidget(0, 8, &dialog);
        verificationTable->setHorizontalHeaderLabels({
            QStringLiteral("结果"),
            QStringLiteral("字段"),
            QStringLiteral("工程值"),
            QStringLiteral("单位"),
            QStringLiteral("类型/地址"),
            QStringLiteral("原始寄存器"),
            QStringLiteral("解释"),
            QStringLiteral("证据")
        });
        verificationTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        verificationTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        verificationTable->setAlternatingRowColors(true);
        verificationTable->verticalHeader()->setVisible(false);
        verificationTable->setMaximumHeight(210);
        verificationTable->horizontalHeader()->setStretchLastSection(true);
        layout->addWidget(verificationTable);

        auto registerListText = [](const QList<quint16>& registers) {
            QStringList parts;
            parts.reserve(registers.size());
            for (quint16 value : registers) {
                parts.append(QStringLiteral("0x%1").arg(value, 4, 16, QLatin1Char('0')).toUpper().replace(QStringLiteral("0X"), QStringLiteral("0x")));
            }
            return parts.join(QStringLiteral(", "));
        };

        connect(exportReportButton, &QPushButton::clicked, this, [this, verificationStatusLabel]() {
            const auto latestRun = m_sessionStore.latestRuleVerificationRun();
            if (!latestRun.has_value()) {
                const QString message = m_sessionStore.hasReadError()
                    ? m_sessionStore.lastReadErrorText()
                    : QStringLiteral("暂无规则验证运行结果，请先点击“使用规则验证所选扫描”。");
                verificationStatusLabel->setText(message);
                QMessageBox::information(this, QStringLiteral("导出验证报告"), message);
                return;
            }

            const auto results = m_sessionStore.ruleVerificationResults(latestRun->verificationRunId);
            if (m_sessionStore.hasReadError()) {
                const QString message = m_sessionStore.lastReadErrorText();
                verificationStatusLabel->setText(message);
                QMessageBox::warning(this, QStringLiteral("导出验证报告"), message);
                return;
            }
            const QString defaultName = QStringLiteral("协议规则验证报告-%1.md").arg(latestRun->verificationRunId);
            const QString filePath = QFileDialog::getSaveFileName(this,
                QStringLiteral("导出验证报告"),
                QDir::home().filePath(defaultName),
                QStringLiteral("Markdown 文件 (*.md);;所有文件 (*)"));
            if (filePath.isEmpty()) {
                return;
            }

            const QString markdown = report::renderRuleVerificationMarkdownReport(*latestRun, results);
            const report::TextFileWriteResult writeResult = report::writeUtf8TextFile(filePath, markdown);
            if (!writeResult.success) {
                const QString message = QStringLiteral("导出验证报告失败：%1").arg(writeResult.errorMessage);
                verificationStatusLabel->setText(message);
                QMessageBox::warning(this, QStringLiteral("导出验证报告"), message);
                return;
            }

            const QString message = QStringLiteral("已导出规则验证报告：%1。" ).arg(filePath);
            verificationStatusLabel->setText(message);
            statusBar()->showMessage(message);
            QMessageBox::information(this, QStringLiteral("导出验证报告"), message);
        });

        connect(verifyRulesButton, &QPushButton::clicked, this, [this, scanSessionCombo, verificationStatusLabel, verificationTable, exportReportButton, registerListText]() {
            const QString selectedSessionId = scanSessionCombo->currentData().toString();
            if (selectedSessionId.isEmpty()) {
                const QString message = QStringLiteral("请先选择一个扫描会话；如果列表为空，请先完成一次 Modbus 扫描并保存扫描结果。");
                verificationStatusLabel->setText(message);
                QMessageBox::information(this, QStringLiteral("规则验证"), message);
                return;
            }

            const auto rules = m_sessionStore.recentProtocolFieldRules(200);
            if (m_sessionStore.hasReadError()) {
                const QString message = m_sessionStore.lastReadErrorText();
                verificationStatusLabel->setText(message);
                QMessageBox::warning(this, QStringLiteral("规则验证"), message);
                return;
            }
            if (rules.isEmpty()) {
                const QString message = QStringLiteral("暂无已确认协议字段规则，无法验证扫描数据。");
                verificationStatusLabel->setText(message);
                QMessageBox::information(this, QStringLiteral("规则验证"), message);
                return;
            }

            const auto observations = m_sessionStore.scanObservations(selectedSessionId);
            if (m_sessionStore.hasReadError()) {
                const QString message = m_sessionStore.lastReadErrorText();
                verificationStatusLabel->setText(message);
                QMessageBox::warning(this, QStringLiteral("规则验证"), message);
                return;
            }
            if (observations.isEmpty()) {
                const QString message = QStringLiteral("所选扫描会话没有寄存器观测，无法验证规则：%1。").arg(selectedSessionId);
                verificationStatusLabel->setText(message);
                QMessageBox::information(this, QStringLiteral("规则验证"), message);
                return;
            }

            const QList<matching::RegisterSample> samples = matching::registerSamplesFromScanObservations(observations);
            const matching::ProtocolRuleVerificationSummary summary = matching::verifyProtocolFieldRules(rules, samples);

            storage::RuleVerificationRunRecord verificationRun;
            verificationRun.verificationRunId = QStringLiteral("verify-%1").arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz")));
            verificationRun.sourceScanSessionId = selectedSessionId;
            verificationRun.createdAtUtc = QDateTime::currentDateTimeUtc();
            if (!m_sessionStore.saveRuleVerificationRun(verificationRun, summary)) {
                verificationStatusLabel->setText(m_sessionStore.lastErrorText());
                QMessageBox::warning(this, QStringLiteral("规则验证"), m_sessionStore.lastErrorText());
                return;
            }

            verificationTable->setRowCount(summary.results.size());
            for (int row = 0; row < summary.results.size(); ++row) {
                const matching::ProtocolRuleVerificationResult& result = summary.results.at(row);
                const QString valueText = result.verified ? QString::number(result.engineeringValue, 'g', 12) : QStringLiteral("-");
                const QString typeAddressText = QStringLiteral("%1｜从站 %2｜FC%3｜地址 %4｜%5 个")
                    .arg(result.candidateType)
                    .arg(result.slaveId)
                    .arg(result.functionCode)
                    .arg(result.startAddress)
                    .arg(result.registerCount);

                verificationTable->setItem(row, 0, new QTableWidgetItem(result.verified ? QStringLiteral("已验证") : result.statusText));
                verificationTable->setItem(row, 1, new QTableWidgetItem(result.fieldName));
                verificationTable->setItem(row, 2, new QTableWidgetItem(valueText));
                verificationTable->setItem(row, 3, new QTableWidgetItem(result.unit));
                verificationTable->setItem(row, 4, new QTableWidgetItem(typeAddressText));
                verificationTable->setItem(row, 5, new QTableWidgetItem(registerListText(result.rawRegisters)));
                verificationTable->setItem(row, 6, new QTableWidgetItem(result.interpretationText.isEmpty() ? QStringLiteral("-") : result.interpretationText));
                verificationTable->setItem(row, 7, new QTableWidgetItem(result.evidenceText));
            }
            verificationTable->resizeColumnsToContents();
            verificationTable->horizontalHeader()->setStretchLastSection(true);

            const QString message = QStringLiteral("规则验证完成并保存：扫描 %1｜运行 %2｜规则 %3 条｜已验证 %4 条｜缺少观测 %5 条｜暂不支持/失败 %6 条。")
                .arg(selectedSessionId)
                .arg(verificationRun.verificationRunId)
                .arg(summary.totalRules)
                .arg(summary.verifiedRules)
                .arg(summary.missingRules)
                .arg(summary.unsupportedRules);
            verificationStatusLabel->setText(message);
            exportReportButton->setEnabled(true);
            statusBar()->showMessage(message);
        });
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.resize(980, 560);
    dialog.exec();
}

void MainWindow::applySendHistory(int index) {
    if (index <= 0) {
        return;
    }

    m_sendEdit->setText(m_historyCombo->itemData(index, Qt::UserRole).toString());

    const int modeIndex = m_sendModeCombo->findData(m_historyCombo->itemData(index, Qt::UserRole + 1));
    if (modeIndex >= 0) {
        m_sendModeCombo->setCurrentIndex(modeIndex);
    }

    const int lineEndingIndex = m_lineEndingCombo->findData(m_historyCombo->itemData(index, Qt::UserRole + 2));
    if (lineEndingIndex >= 0) {
        m_lineEndingCombo->setCurrentIndex(lineEndingIndex);
    }
}

void MainWindow::showError(const QString& message) {
    statusBar()->showMessage(message);
    QMessageBox::warning(this, QStringLiteral("串口提示"), message);
}

void MainWindow::handleSerialError(QSerialPort::SerialPortError error, const QString& message) {
    if (m_reconnectAttemptInProgress) {
        m_reconnectAttemptInProgress = false;
        m_reconnectPolicy.clearWaiting();
        if (m_reconnectTimer != nullptr) {
            m_reconnectTimer->stop();
        }
        m_connectButton->setText(QStringLiteral("连接"));
        statusBar()->showMessage(QStringLiteral("自动重连失败，已停止等待：%1").arg(message));
        return;
    }

    if (m_reconnectPolicy.enterWaitingOnError(error)) {
        if (m_serialService.isOpen()) {
            m_serialService.close();
        }
        m_connectButton->setText(QStringLiteral("等待重连"));
        if (m_reconnectTimer != nullptr) {
            m_reconnectTimer->start();
        }
        statusBar()->showMessage(QStringLiteral("检测到设备断开，等待端口 %1 重新出现后自动重连。")
            .arg(m_reconnectPolicy.waitingPortName()));
        return;
    }

    showError(message);
}

QStringList MainWindow::currentPortNames() const {
    QStringList portNames;
    if (m_portCombo == nullptr) {
        return portNames;
    }

    portNames.reserve(m_portCombo->count());
    for (int index = 0; index < m_portCombo->count(); ++index) {
        const QString portName = m_portCombo->itemData(index).toString();
        if (!portName.trimmed().isEmpty()) {
            portNames.append(portName);
        }
    }
    return portNames;
}

void MainWindow::rememberSuccessfulConnection(const QString& portName) {
    auto options = currentOpenOptions();
    options.portName = portName;
    m_reconnectPolicy.recordSuccessfulOpen(options);
    m_reconnectAttemptInProgress = false;
    if (m_reconnectTimer != nullptr) {
        m_reconnectTimer->stop();
    }
}

void MainWindow::tryReconnectIfReady() {
    if (!m_reconnectPolicy.isWaiting()) {
        return;
    }

    const auto reconnectOptions = m_reconnectPolicy.markAttemptIfReady(currentPortNames());
    if (!reconnectOptions.has_value()) {
        statusBar()->showMessage(QStringLiteral("等待自动重连：端口 %1 尚未出现。")
            .arg(m_reconnectPolicy.waitingPortName()));
        return;
    }

    const int portIndex = m_portCombo->findData(reconnectOptions->portName);
    if (portIndex >= 0) {
        m_portCombo->setCurrentIndex(portIndex);
    }

    statusBar()->showMessage(QStringLiteral("检测到端口 %1 已恢复，正在按上次成功参数重连。")
        .arg(reconnectOptions->portName));
    m_reconnectAttemptInProgress = true;
    const bool opened = m_serialService.open(*reconnectOptions);
    if (!opened) {
        m_reconnectAttemptInProgress = false;
        m_reconnectPolicy.clearWaiting();
        if (m_reconnectTimer != nullptr) {
            m_reconnectTimer->stop();
        }
        m_connectButton->setText(QStringLiteral("连接"));
        statusBar()->showMessage(QStringLiteral("自动重连失败，已停止等待：%1").arg(m_serialService.lastErrorText()));
    }
}

void MainWindow::initializeStorage() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) {
        dir = QDir::homePath() + QStringLiteral("/.serial-value-matcher-native");
    }
    QDir().mkpath(dir);

    const QString dbPath = dir + QStringLiteral("/sessions.sqlite");
    if (!m_sessionStore.open(dbPath)) {
        statusBar()->showMessage(m_sessionStore.lastErrorText());
    } else {
        statusBar()->showMessage(QStringLiteral("会话数据库已就绪：%1").arg(dbPath));
        refreshSendHistory();
    }
}

transport::SerialOpenOptions MainWindow::currentOpenOptions() const {
    transport::SerialOpenOptions options;
    options.sessionId = QCoreApplication::applicationName().isEmpty()
        ? QStringLiteral("default-session")
        : QCoreApplication::applicationName();
    options.portName = m_portCombo->currentData().toString();
    options.baudRate = m_baudCombo->currentText().toInt();
    options.dataBits = static_cast<QSerialPort::DataBits>(m_dataBitsCombo->currentData().toInt());
    options.parity = static_cast<QSerialPort::Parity>(m_parityCombo->currentData().toInt());
    options.stopBits = static_cast<QSerialPort::StopBits>(m_stopBitsCombo->currentData().toInt());
    options.flowControl = static_cast<QSerialPort::FlowControl>(m_flowControlCombo->currentData().toInt());
    options.dataTerminalReady = m_dtrCheck->isChecked();
    options.requestToSend = m_rtsCheck->isChecked();
    return options;
}

void MainWindow::applyLatestSerialProfile() {
    const auto profile = m_sessionStore.latestSerialProfile();
    if (!profile.has_value()) {
        return;
    }

    const int baudIndex = m_baudCombo->findText(QString::number(profile->options.baudRate));
    if (baudIndex >= 0) {
        m_baudCombo->setCurrentIndex(baudIndex);
    }

    const int portIndex = m_portCombo->findData(profile->options.portName);
    if (portIndex >= 0) {
        m_portCombo->setCurrentIndex(portIndex);
    }

    const int dataBitsIndex = m_dataBitsCombo->findData(static_cast<int>(profile->options.dataBits));
    if (dataBitsIndex >= 0) {
        m_dataBitsCombo->setCurrentIndex(dataBitsIndex);
    }

    const int parityIndex = m_parityCombo->findData(static_cast<int>(profile->options.parity));
    if (parityIndex >= 0) {
        m_parityCombo->setCurrentIndex(parityIndex);
    }

    const int stopBitsIndex = m_stopBitsCombo->findData(static_cast<int>(profile->options.stopBits));
    if (stopBitsIndex >= 0) {
        m_stopBitsCombo->setCurrentIndex(stopBitsIndex);
    }

    const int flowControlIndex = m_flowControlCombo->findData(static_cast<int>(profile->options.flowControl));
    if (flowControlIndex >= 0) {
        m_flowControlCombo->setCurrentIndex(flowControlIndex);
    }

    m_dtrCheck->setChecked(profile->options.dataTerminalReady);
    m_rtsCheck->setChecked(profile->options.requestToSend);
}

void MainWindow::saveCurrentSerialProfile() {
    const auto options = currentOpenOptions();
    if (options.portName.trimmed().isEmpty()) {
        statusBar()->showMessage(QStringLiteral("当前没有可保存的串口配置：请先选择串口。"));
        return;
    }

    storage::SerialProfile profile;
    profile.name = QStringLiteral("default");
    profile.options = options;
    if (!m_sessionStore.saveSerialProfile(profile)) {
        statusBar()->showMessage(m_sessionStore.lastErrorText());
        return;
    }

    statusBar()->showMessage(QStringLiteral("已保存默认串口配置：%1，%2，数据位 %3，校验 %4，停止位 %5，流控 %6。")
        .arg(profile.options.portName)
        .arg(profile.options.baudRate)
        .arg(m_dataBitsCombo->currentText())
        .arg(m_parityCombo->currentText())
        .arg(m_stopBitsCombo->currentText())
        .arg(m_flowControlCombo->currentText()));
}

void MainWindow::refreshSendHistory() {
    if (m_historyCombo == nullptr) {
        return;
    }

    m_historyCombo->blockSignals(true);
    m_historyCombo->clear();
    m_historyCombo->addItem(QStringLiteral("发送历史"));

    const auto entries = m_sessionStore.recentSendHistory(20);
    for (const auto& entry : entries) {
        const QString mode = entry.payloadMode == protocol::PayloadMode::Hex ? QStringLiteral("HEX") : QStringLiteral("文本");
        QString preview = entry.content;
        if (preview.size() > 32) {
            preview = preview.left(32) + QStringLiteral("…");
        }
        const QString label = QStringLiteral("%1 | %2").arg(mode, preview);
        m_historyCombo->addItem(label);
        const int index = m_historyCombo->count() - 1;
        m_historyCombo->setItemData(index, entry.content, Qt::UserRole);
        m_historyCombo->setItemData(index, static_cast<int>(entry.payloadMode), Qt::UserRole + 1);
        m_historyCombo->setItemData(index, static_cast<int>(entry.lineEnding), Qt::UserRole + 2);
    }

    m_historyCombo->setCurrentIndex(0);
    m_historyCombo->blockSignals(false);
}

} // namespace svm::app


namespace svm::app {

protocol::PayloadMode MainWindow::currentPayloadMode() const {
    return static_cast<protocol::PayloadMode>(m_sendModeCombo->currentData().toInt());
}

protocol::LineEnding MainWindow::currentLineEnding() const {
    return static_cast<protocol::LineEnding>(m_lineEndingCombo->currentData().toInt());
}

} // namespace svm::app
