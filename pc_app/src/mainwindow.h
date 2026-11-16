#pragma once

#include <QMainWindow>
#include <QFile>

#include "deviceclient.h"
#include "measurementfile.h"
#include "plotwidget.h"
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
class QUdpSocket;
class QTabWidget;

namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Построить график по сохранённому файлу измерения
    bool loadMeasurementFile(const QString &path);

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
    void onFindBoard();
    void onDiscoverReply();
    void onDiscoverFinished();
    void onOpenFile();
    void onSavePlot();

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
    void plotStart(SweepParams::Kind kind, const QString &title);
    void plotPoint(int pass, double ugs, double uds, double i);

    static QString errText(int code);
    static QString fmtCurrent(double i);

    Ui::MainWindow *ui;

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
    QPushButton *m_btnFind = nullptr;
    QUdpSocket *m_udp = nullptr;
    QStringList m_found;      // "IP:порт" найденных плат
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
    QComboBox *m_csvFormat = nullptr;

    // График
    QTabWidget *m_tabs = nullptr;
    PlotWidget *m_plot = nullptr;
    QComboBox *m_yMode = nullptr;
    QPushButton *m_btnOpenFile = nullptr;
    QPushButton *m_btnSavePlot = nullptr;
    SweepParams::Kind m_plotKind = SweepParams::Transfer;
    QVector<qint64> m_plotGroups;   // Uзи * 1e4 для номера цвета кривой выходной характеристики
};
