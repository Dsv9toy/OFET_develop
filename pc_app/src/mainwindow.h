#pragma once

#include <QMainWindow>
#include <QFile>

#include "deviceclient.h"
#include "measurementfile.h"
#include "sweepparams.h"

class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QProgressBar;
class QPlainTextEdit;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void onConnectClicked();
    void onConnected();
    void onDisconnected();
    void onSocketError(const QString &text);
    void onTimeout();
    void onLine(const QString &line, const QStringList &f);
    void onExchange(const QString &dir, const QString &line);

    void onSelect();
    void onSetV();
    void onZero();
    void onRangeChanged();
    void onStartTransfer();
    void onStartOutput();
    void onBrowseFolder();

private:
    enum class Mode { Offline, Idle, Starting, Running, Paused };

    void buildUi();
    void loadSettings();
    void saveSettings();
    void updateUi();
    void setMode(Mode m);
    void setLed(const QString &color);
    void setState(const QString &text, bool error = false);
    void log(const QString &text);
    void openLogFile();
    SweepParams collectParams(SweepParams::Kind kind) const;
    void startSweep(SweepParams::Kind kind);
    void finishSweep(const QString &note);
    void showIndicators(double ugs, double uds, double i, int range);

    static QString errText(int code);
    static QString fmtCurrent(double i);

    DeviceClient m_dev;
    MeasurementFile m_meas;
    QFile m_logFile;

    Mode m_mode = Mode::Offline;
    int m_selected = 0;        // выбранный транзистор (по ответу платы), 0 = нет
    double m_ubias;            // последнее Uсм от платы
    SweepParams m_sweep;       // параметры текущей развёртки
    int m_expectedTotal = 0;

    // Связь
    QLineEdit *m_ip = nullptr;
    QSpinBox *m_port = nullptr;
    QPushButton *m_btnConnect = nullptr;
    QPushButton *m_btnPing = nullptr;
    QLabel *m_led = nullptr;

    // Состояние и индикаторы
    QLabel *m_lblMode = nullptr;
    QLabel *m_lblState = nullptr;
    QLabel *m_lblUbias = nullptr;
    QLabel *m_lblUgs = nullptr;
    QLabel *m_lblUds = nullptr;
    QLabel *m_lblI = nullptr;
    QLabel *m_lblRange = nullptr;
    QPushButton *m_btnMeas = nullptr;

    // Транзистор
    QComboBox *m_type = nullptr;
    QSpinBox *m_transistor = nullptr;
    QPushButton *m_btnSel = nullptr;
    QLabel *m_lblSelected = nullptr;
    QComboBox *m_range = nullptr;

    // Ручная установка
    QDoubleSpinBox *m_setUgs = nullptr;
    QDoubleSpinBox *m_setUds = nullptr;
    QPushButton *m_btnSetV = nullptr;
    QPushButton *m_btnZero = nullptr;

    // Проходная
    QDoubleSpinBox *m_tUds = nullptr;
    QDoubleSpinBox *m_tUgsStart = nullptr;
    QDoubleSpinBox *m_tUgsStop = nullptr;
    QDoubleSpinBox *m_tUgsStep = nullptr;

    // Выходная
    QDoubleSpinBox *m_oUgsStart = nullptr;
    QDoubleSpinBox *m_oUgsStop = nullptr;
    QDoubleSpinBox *m_oUgsStep = nullptr;
    QDoubleSpinBox *m_oUdsStart = nullptr;
    QDoubleSpinBox *m_oUdsStop = nullptr;
    QDoubleSpinBox *m_oUdsStep = nullptr;

    // Общие параметры
    QSpinBox *m_delay = nullptr;
    QSpinBox *m_window = nullptr;
    QDoubleSpinBox *m_ilim = nullptr;   // мкА
    QCheckBox *m_reverse = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_folder = nullptr;
    QPushButton *m_btnFolder = nullptr;

    // Управление
    QPushButton *m_btnTrans = nullptr;
    QPushButton *m_btnOutp = nullptr;
    QPushButton *m_btnStop = nullptr;
    QPushButton *m_btnPause = nullptr;
    QPushButton *m_btnResume = nullptr;
    QProgressBar *m_progress = nullptr;
    QPlainTextEdit *m_log = nullptr;
};
