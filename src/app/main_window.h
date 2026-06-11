#pragma once

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>

#include "capture/capture_bus.h"
#include "session/console_model.h"
#include "storage/session_store.h"
#include "protocol/payload_codec.h"
#include "transport/serial_port_service.h"
#include "transport/serial_reconnect_policy.h"

namespace svm::app {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void refreshPorts();
    void toggleConnection();
    void sendText();
    void appendConsoleLine(const session::ConsoleLine& line);
    void setScrollPaused(bool paused);
    void clearConsole();
    void applySendHistory(int index);
    void showError(const QString& message);
    void handleSerialError(QSerialPort::SerialPortError error, const QString& message);
    void showModbusScanDialog();
    void showAnalysisWorkspace();

private:
    void initializeStorage();
    transport::SerialOpenOptions currentOpenOptions() const;
    protocol::PayloadMode currentPayloadMode() const;
    protocol::LineEnding currentLineEnding() const;
    void refreshSendHistory();
    void applyLatestSerialProfile();
    void saveCurrentSerialProfile();
    QStringList currentPortNames() const;
    void rememberSuccessfulConnection(const QString& portName);
    void tryReconnectIfReady();

    capture::CaptureBus m_captureBus;
    transport::SerialPortService m_serialService;
    storage::SessionStore m_sessionStore;
    session::ConsoleModel m_consoleModel;

    QComboBox* m_portCombo = nullptr;
    QComboBox* m_baudCombo = nullptr;
    QComboBox* m_dataBitsCombo = nullptr;
    QComboBox* m_parityCombo = nullptr;
    QComboBox* m_stopBitsCombo = nullptr;
    QComboBox* m_flowControlCombo = nullptr;
    QComboBox* m_sendModeCombo = nullptr;
    QComboBox* m_lineEndingCombo = nullptr;
    QComboBox* m_historyCombo = nullptr;
    QPushButton* m_connectButton = nullptr;
    QCheckBox* m_dtrCheck = nullptr;
    QCheckBox* m_rtsCheck = nullptr;
    QCheckBox* m_autoReconnectCheck = nullptr;
    QAction* m_pauseScrollAction = nullptr;
    QAction* m_clearConsoleAction = nullptr;
    QAction* m_modbusScanAction = nullptr;
    QAction* m_analysisWorkspaceAction = nullptr;
    QLineEdit* m_sendEdit = nullptr;
    QPlainTextEdit* m_console = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    transport::SerialReconnectPolicy m_reconnectPolicy;
    bool m_scrollPaused = false;
    bool m_reconnectAttemptInProgress = false;
};

} // namespace svm::app
