#include "mainwindow.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <cmath>
#include <limits>

namespace {

QDoubleSpinBox *uSpin(double value)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(kUserUMin, kUserUMax);
    s->setDecimals(3);
    s->setSingleStep(0.1);
    s->setSuffix(" В");
    s->setValue(value);
    return s;
}

QDoubleSpinBox *stepSpin(double value)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(0.001, kUserUMax - kUserUMin);
    s->setDecimals(3);
    s->setSingleStep(0.01);
    s->setSuffix(" В");
    s->setValue(value);
    return s;
}

QLabel *valueLabel()
{
    auto *l = new QLabel("-");
    QFont f = l->font();
    f.setBold(true);
    f.setPointSize(f.pointSize() + 2);
    l->setFont(f);
    l->setMinimumWidth(110);
    return l;
}

bool toDouble(const QString &s, double *v)
{
    bool ok = false;
    *v = s.toDouble(&ok);
    return ok;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_ubias(std::numeric_limits<double>::quiet_NaN())
{
    buildUi();
    loadSettings();
    openLogFile();

    connect(&m_dev, &DeviceClient::connected, this, &MainWindow::onConnected);
    connect(&m_dev, &DeviceClient::disconnected, this, &MainWindow::onDisconnected);
    connect(&m_dev, &DeviceClient::errorText, this, &MainWindow::onSocketError);
    connect(&m_dev, &DeviceClient::timeout, this, &MainWindow::onTimeout);
    connect(&m_dev, &DeviceClient::lineReceived, this, &MainWindow::onLine);
    connect(&m_dev, &DeviceClient::exchange, this, &MainWindow::onExchange);

    setMode(Mode::Offline);
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------- интерфейс

void MainWindow::buildUi()
{
    setWindowTitle("Измерение ВАХ OFET");

    // --- Связь
    auto *gLink = new QGroupBox("Связь с платой");
    m_ip = new QLineEdit("192.168.1.10");
    m_port = new QSpinBox;
    m_port->setRange(1, 65535);
    m_port->setValue(5000);
    m_btnConnect = new QPushButton("Подключиться");
    m_btnPing = new QPushButton("Проверка связи");
    m_led = new QLabel;
    m_led->setFixedSize(18, 18);
    auto *lLink = new QGridLayout(gLink);
    lLink->addWidget(new QLabel("IP-адрес:"), 0, 0);
    lLink->addWidget(m_ip, 0, 1);
    lLink->addWidget(new QLabel("Порт:"), 0, 2);
    lLink->addWidget(m_port, 0, 3);
    lLink->addWidget(m_btnConnect, 1, 0, 1, 2);
    lLink->addWidget(m_btnPing, 1, 2);
    lLink->addWidget(m_led, 1, 3, Qt::AlignCenter);

    // --- Состояние
    auto *gState = new QGroupBox("Состояние");
    m_lblMode = new QLabel("-");
    m_lblState = new QLabel("-");
    m_lblState->setWordWrap(true);
    m_lblUbias = valueLabel();
    m_lblUgs = valueLabel();
    m_lblUds = valueLabel();
    m_lblI = valueLabel();
    m_lblRange = valueLabel();
    m_btnMeas = new QPushButton("Обновить показания");
    auto *lState = new QGridLayout(gState);
    lState->addWidget(new QLabel("Режим:"), 0, 0);
    lState->addWidget(m_lblMode, 0, 1, 1, 3);
    lState->addWidget(new QLabel("Состояние:"), 1, 0);
    lState->addWidget(m_lblState, 1, 1, 1, 3);
    lState->addWidget(new QLabel("Uсм:"), 2, 0);
    lState->addWidget(m_lblUbias, 2, 1);
    lState->addWidget(new QLabel("Диапазон:"), 2, 2);
    lState->addWidget(m_lblRange, 2, 3);
    lState->addWidget(new QLabel("Uзи:"), 3, 0);
    lState->addWidget(m_lblUgs, 3, 1);
    lState->addWidget(new QLabel("Uси:"), 3, 2);
    lState->addWidget(m_lblUds, 3, 3);
    lState->addWidget(new QLabel("Iс:"), 4, 0);
    lState->addWidget(m_lblI, 4, 1);
    lState->addWidget(m_btnMeas, 4, 2, 1, 2);

    // --- Транзистор
    auto *gTr = new QGroupBox("Транзистор и диапазон");
    m_type = new QComboBox;
    m_type->addItem("p-тип", "p");
    m_type->addItem("n-тип", "n");
    m_transistor = new QSpinBox;
    m_transistor->setRange(1, 5);
    m_btnSel = new QPushButton("Выбрать");
    m_lblSelected = new QLabel("не выбран");
    m_range = new QComboBox;
    m_range->addItem("Авто", 0);
    const char *scales[] = {"±1,5 мА", "±150 мкА", "±15 мкА", "±1,5 мкА",
                            "±150 нА", "±15 нА", "±1,5 нА"};
    for (int i = 0; i < 7; ++i)
        m_range->addItem(QString("%1 (%2)").arg(i + 1).arg(scales[i]), i + 1);
    auto *lTr = new QGridLayout(gTr);
    lTr->addWidget(new QLabel("Тип:"), 0, 0);
    lTr->addWidget(m_type, 0, 1);
    lTr->addWidget(new QLabel("Диапазон:"), 0, 2);
    lTr->addWidget(m_range, 0, 3);
    lTr->addWidget(new QLabel("Номер:"), 1, 0);
    lTr->addWidget(m_transistor, 1, 1);
    lTr->addWidget(m_btnSel, 1, 2);
    lTr->addWidget(m_lblSelected, 1, 3);

    // --- Ручная установка
    auto *gMan = new QGroupBox("Ручная установка напряжений");
    m_setUgs = uSpin(0.0);
    m_setUds = uSpin(0.0);
    m_btnSetV = new QPushButton("Установить");
    m_btnZero = new QPushButton("Обнулить напряжения");
    auto *lMan = new QGridLayout(gMan);
    lMan->addWidget(new QLabel("Uзи:"), 0, 0);
    lMan->addWidget(m_setUgs, 0, 1);
    lMan->addWidget(new QLabel("Uси:"), 0, 2);
    lMan->addWidget(m_setUds, 0, 3);
    lMan->addWidget(m_btnSetV, 1, 0, 1, 2);
    lMan->addWidget(m_btnZero, 1, 2, 1, 2);

    // --- Проходная
    auto *gT = new QGroupBox("Проходная характеристика Iс(Uзи)");
    m_tUds = uSpin(-5.0);
    m_tUgsStart = uSpin(0.0);
    m_tUgsStop = uSpin(-16.0);
    m_tUgsStep = stepSpin(0.1);
    auto *lT = new QGridLayout(gT);
    lT->addWidget(new QLabel("Uси:"), 0, 0);
    lT->addWidget(m_tUds, 0, 1);
    lT->addWidget(new QLabel("Uзи начало:"), 1, 0);
    lT->addWidget(m_tUgsStart, 1, 1);
    lT->addWidget(new QLabel("конец:"), 1, 2);
    lT->addWidget(m_tUgsStop, 1, 3);
    lT->addWidget(new QLabel("шаг:"), 1, 4);
    lT->addWidget(m_tUgsStep, 1, 5);

    // --- Выходная
    auto *gO = new QGroupBox("Выходная характеристика Iс(Uси)");
    m_oUgsStart = uSpin(0.0);
    m_oUgsStop = uSpin(-16.0);
    m_oUgsStep = stepSpin(4.0);
    m_oUdsStart = uSpin(0.0);
    m_oUdsStop = uSpin(-10.0);
    m_oUdsStep = stepSpin(0.1);
    auto *lO = new QGridLayout(gO);
    lO->addWidget(new QLabel("Uзи начало:"), 0, 0);
    lO->addWidget(m_oUgsStart, 0, 1);
    lO->addWidget(new QLabel("конец:"), 0, 2);
    lO->addWidget(m_oUgsStop, 0, 3);
    lO->addWidget(new QLabel("шаг:"), 0, 4);
    lO->addWidget(m_oUgsStep, 0, 5);
    lO->addWidget(new QLabel("Uси начало:"), 1, 0);
    lO->addWidget(m_oUdsStart, 1, 1);
    lO->addWidget(new QLabel("конец:"), 1, 2);
    lO->addWidget(m_oUdsStop, 1, 3);
    lO->addWidget(new QLabel("шаг:"), 1, 4);
    lO->addWidget(m_oUdsStep, 1, 5);

    // --- Общие параметры
    auto *gP = new QGroupBox("Параметры измерения");
    m_delay = new QSpinBox;
    m_delay->setRange(100, 60000);
    m_delay->setSingleStep(50);
    m_delay->setSuffix(" мс");
    m_delay->setValue(200);
    m_window = new QSpinBox;
    m_window->setRange(20, 2000);
    m_window->setSingleStep(20);
    m_window->setSuffix(" мс");
    m_window->setValue(100);
    m_ilim = new QDoubleSpinBox;
    m_ilim->setRange(0.001, 1500.0);
    m_ilim->setDecimals(3);
    m_ilim->setSuffix(" мкА");
    m_ilim->setValue(1000.0);
    m_reverse = new QCheckBox("Обратная развёртка (туда и обратно)");
    m_name = new QLineEdit;
    m_name->setPlaceholderText("например, Obrazec1");
    m_folder = new QLineEdit;
    m_btnFolder = new QPushButton("Обзор...");
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(m_folder);
    folderRow->addWidget(m_btnFolder);
    auto *lP = new QFormLayout(gP);
    lP->addRow("Задержка в точке:", m_delay);
    lP->addRow("Окно усреднения:", m_window);
    lP->addRow("Ограничение тока:", m_ilim);
    lP->addRow("", m_reverse);
    lP->addRow("Название измерения:", m_name);
    lP->addRow("Папка:", folderRow);

    // --- Кнопки управления
    m_btnTrans = new QPushButton("Проходная характеристика");
    m_btnOutp = new QPushButton("Выходная характеристика");
    m_btnStop = new QPushButton("СТОП");
    m_btnStop->setStyleSheet("QPushButton { background: #d32f2f; color: white; font-weight: bold; }"
                             "QPushButton:disabled { background: #e0a0a0; }");
    m_btnStop->setMinimumHeight(36);
    m_btnPause = new QPushButton("Пауза");
    m_btnResume = new QPushButton("Продолжить");
    auto *lBtn = new QGridLayout;
    lBtn->addWidget(m_btnTrans, 0, 0);
    lBtn->addWidget(m_btnOutp, 0, 1);
    lBtn->addWidget(m_btnPause, 1, 0);
    lBtn->addWidget(m_btnResume, 1, 1);
    lBtn->addWidget(m_btnStop, 2, 0, 1, 2);

    // --- Левая колонка
    auto *left = new QWidget;
    auto *lLeft = new QVBoxLayout(left);
    lLeft->addWidget(gLink);
    lLeft->addWidget(gTr);
    lLeft->addWidget(gT);
    lLeft->addWidget(gO);
    lLeft->addWidget(gP);
    lLeft->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setWidget(left);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(left->sizeHint().width() + 30);

    // --- Правая колонка
    m_progress = new QProgressBar;
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    m_log->setFont(mono);
    auto *right = new QWidget;
    auto *lRight = new QVBoxLayout(right);
    lRight->addWidget(gState);
    lRight->addLayout(lBtn);
    lRight->addWidget(gMan);
    lRight->addWidget(new QLabel("Ход измерения:"));
    lRight->addWidget(m_progress);
    lRight->addWidget(new QLabel("Журнал:"));
    lRight->addWidget(m_log, 1);

    auto *central = new QWidget;
    auto *lMain = new QHBoxLayout(central);
    lMain->addWidget(scroll);
    lMain->addWidget(right, 1);
    setCentralWidget(central);
    resize(1200, 820);

    // --- Сигналы
    connect(m_btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_btnPing, &QPushButton::clicked, this, [this] { m_dev.send("PING"); });
    connect(m_btnMeas, &QPushButton::clicked, this, [this] { m_dev.send("MEAS?"); });
    connect(m_btnSel, &QPushButton::clicked, this, &MainWindow::onSelect);
    connect(m_btnSetV, &QPushButton::clicked, this, &MainWindow::onSetV);
    connect(m_btnZero, &QPushButton::clicked, this, &MainWindow::onZero);
    connect(m_range, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::onRangeChanged);
    connect(m_btnTrans, &QPushButton::clicked, this, &MainWindow::onStartTransfer);
    connect(m_btnOutp, &QPushButton::clicked, this, &MainWindow::onStartOutput);
    connect(m_btnStop, &QPushButton::clicked, this, [this] { m_dev.send("STOP"); });
    connect(m_btnPause, &QPushButton::clicked, this, [this] { m_dev.send("PAUSE"); });
    connect(m_btnResume, &QPushButton::clicked, this, [this] { m_dev.send("RESUME"); });
    connect(m_btnFolder, &QPushButton::clicked, this, &MainWindow::onBrowseFolder);
    connect(m_folder, &QLineEdit::editingFinished, this, &MainWindow::openLogFile);
}

void MainWindow::updateUi()
{
    const bool online = m_mode != Mode::Offline;
    const bool idle = m_mode == Mode::Idle;
    const bool busy = m_mode == Mode::Starting || m_mode == Mode::Running || m_mode == Mode::Paused;

    m_btnConnect->setText(online ? "Отключиться" : "Подключиться");
    m_ip->setEnabled(!online);
    m_port->setEnabled(!online);
    m_btnPing->setEnabled(online);
    m_btnMeas->setEnabled(idle);
    m_btnSel->setEnabled(idle);
    m_range->setEnabled(!busy);
    m_btnSetV->setEnabled(idle);
    m_btnZero->setEnabled(online);
    m_btnTrans->setEnabled(idle);
    m_btnOutp->setEnabled(idle);
    m_btnStop->setEnabled(online);
    m_btnPause->setEnabled(m_mode == Mode::Running);
    m_btnResume->setEnabled(m_mode == Mode::Paused);

    // Параметры нельзя менять во время измерения
    for (QWidget *w : std::initializer_list<QWidget *>{
             m_type, m_transistor, m_tUds, m_tUgsStart, m_tUgsStop, m_tUgsStep,
             m_oUgsStart, m_oUgsStop, m_oUgsStep, m_oUdsStart, m_oUdsStop, m_oUdsStep,
             m_delay, m_window, m_ilim, m_reverse, m_name, m_folder, m_btnFolder})
        w->setEnabled(!busy);
}

void MainWindow::setMode(Mode m)
{
    m_mode = m;
    switch (m) {
    case Mode::Offline:  m_lblMode->setText("Нет связи"); setLed("#9e9e9e"); break;
    case Mode::Idle:     m_lblMode->setText("Ожидание"); setLed("#43a047"); break;
    case Mode::Starting: m_lblMode->setText("Запуск измерения..."); break;
    case Mode::Running:
        m_lblMode->setText(m_sweep.kind == SweepParams::Transfer
                               ? "Проходная характеристика" : "Выходная характеристика");
        break;
    case Mode::Paused:   m_lblMode->setText("Пауза"); break;
    }
    m_dev.setSweepActive(m == Mode::Running || m == Mode::Paused);
    updateUi();
}

void MainWindow::setLed(const QString &color)
{
    m_led->setStyleSheet(QString("background:%1; border-radius:9px; border:1px solid #555;").arg(color));
}

void MainWindow::setState(const QString &text, bool error)
{
    m_lblState->setText(text);
    m_lblState->setStyleSheet(error ? "color:#c62828; font-weight:bold;" : "");
}

QString MainWindow::fmtCurrent(double i)
{
    const double a = std::fabs(i);
    if (a >= 1e-3) return QString::number(i * 1e3, 'f', 4).replace('.', ',') + " мА";
    if (a >= 1e-6) return QString::number(i * 1e6, 'f', 4).replace('.', ',') + " мкА";
    if (a >= 1e-9) return QString::number(i * 1e9, 'f', 4).replace('.', ',') + " нА";
    return QString::number(i * 1e12, 'f', 3).replace('.', ',') + " пА";
}

void MainWindow::showIndicators(double ugs, double uds, double i, int range)
{
    m_lblUgs->setText(MeasurementFile::num(ugs, 4) + " В");
    m_lblUds->setText(MeasurementFile::num(uds, 4) + " В");
    m_lblI->setText(fmtCurrent(i));
    m_lblRange->setText(QString::number(range));
}

QString MainWindow::errText(int code)
{
    switch (code) {
    case 1: return "неверный формат или параметр";
    case 2: return "напряжение вне предела";
    case 3: return "нет ответа ЦАП по I2C";
    case 4: return "команда недоступна, идёт измерение";
    case 5: return "транзистор не выбран";
    default: return QString("неизвестная ошибка %1").arg(code);
    }
}

// ---------------------------------------------------------------- журнал

void MainWindow::openLogFile()
{
    const QString folder = m_folder->text().trimmed();
    const QString path = QDir(folder).filePath(
        "obmen_" + QDate::currentDate().toString("yyyyMMdd") + ".log");
    if (m_logFile.isOpen() && m_logFile.fileName() == path)
        return;
    m_logFile.close();
    if (folder.isEmpty() || !QDir(folder).exists())
        return;
    m_logFile.setFileName(path);
    m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void MainWindow::log(const QString &text)
{
    const QString line = QDateTime::currentDateTime().toString("HH:mm:ss.zzz") + "  " + text;
    m_log->appendPlainText(line);
    if (m_logFile.isOpen()) {
        m_logFile.write(QDate::currentDate().toString("yyyy-MM-dd ").toUtf8() + line.toUtf8() + "\n");
        m_logFile.flush();
    }
}

void MainWindow::onExchange(const QString &dir, const QString &line)
{
    log(dir + " " + line);
}

// ---------------------------------------------------------------- настройки

void MainWindow::loadSettings()
{
    QSettings s;
    m_ip->setText(s.value("link/ip", m_ip->text()).toString());
    m_port->setValue(s.value("link/port", m_port->value()).toInt());
    const QString defFolder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    m_folder->setText(s.value("save/folder", defFolder).toString());
    m_name->setText(s.value("save/name").toString());

    m_type->setCurrentIndex(qMax(0, m_type->findData(s.value("meas/type", "p"))));
    m_transistor->setValue(s.value("meas/transistor", 1).toInt());
    m_range->setCurrentIndex(qMax(0, m_range->findData(s.value("meas/range", 0))));
    m_delay->setValue(s.value("meas/delay", 200).toInt());
    m_window->setValue(s.value("meas/window", 100).toInt());
    m_ilim->setValue(s.value("meas/ilim_uA", 1000.0).toDouble());
    m_reverse->setChecked(s.value("meas/reverse", false).toBool());

    auto ld = [&s](QDoubleSpinBox *w, const char *key) {
        w->setValue(s.value(key, w->value()).toDouble());
    };
    ld(m_tUds, "trans/uds");
    ld(m_tUgsStart, "trans/ugs_start");
    ld(m_tUgsStop, "trans/ugs_stop");
    ld(m_tUgsStep, "trans/ugs_step");
    ld(m_oUgsStart, "outp/ugs_start");
    ld(m_oUgsStop, "outp/ugs_stop");
    ld(m_oUgsStep, "outp/ugs_step");
    ld(m_oUdsStart, "outp/uds_start");
    ld(m_oUdsStop, "outp/uds_stop");
    ld(m_oUdsStep, "outp/uds_step");
    restoreGeometry(s.value("ui/geometry").toByteArray());
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue("link/ip", m_ip->text());
    s.setValue("link/port", m_port->value());
    s.setValue("save/folder", m_folder->text());
    s.setValue("save/name", m_name->text());
    s.setValue("meas/type", m_type->currentData());
    s.setValue("meas/transistor", m_transistor->value());
    s.setValue("meas/range", m_range->currentData());
    s.setValue("meas/delay", m_delay->value());
    s.setValue("meas/window", m_window->value());
    s.setValue("meas/ilim_uA", m_ilim->value());
    s.setValue("meas/reverse", m_reverse->isChecked());
    s.setValue("trans/uds", m_tUds->value());
    s.setValue("trans/ugs_start", m_tUgsStart->value());
    s.setValue("trans/ugs_stop", m_tUgsStop->value());
    s.setValue("trans/ugs_step", m_tUgsStep->value());
    s.setValue("outp/ugs_start", m_oUgsStart->value());
    s.setValue("outp/ugs_stop", m_oUgsStop->value());
    s.setValue("outp/ugs_step", m_oUgsStep->value());
    s.setValue("outp/uds_start", m_oUdsStart->value());
    s.setValue("outp/uds_stop", m_oUdsStop->value());
    s.setValue("outp/uds_step", m_oUdsStep->value());
    s.setValue("ui/geometry", saveGeometry());
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_mode == Mode::Running || m_mode == Mode::Paused || m_mode == Mode::Starting) {
        const auto r = QMessageBox::question(this, "Выход",
            "Идёт измерение. Остановить его и выйти?");
        if (r != QMessageBox::Yes) {
            e->ignore();
            return;
        }
        m_dev.send("STOP");
        finishSweep("прервано: программа закрыта");
    }
    saveSettings();
    m_dev.disconnectFromDevice();
    e->accept();
}

void MainWindow::onBrowseFolder()
{
    const QString d = QFileDialog::getExistingDirectory(this, "Папка для сохранения", m_folder->text());
    if (!d.isEmpty()) {
        m_folder->setText(QDir::toNativeSeparators(d));
        openLogFile();
    }
}

// ---------------------------------------------------------------- связь

void MainWindow::onConnectClicked()
{
    if (m_mode != Mode::Offline || m_dev.isConnected()) {
        if (m_mode == Mode::Running || m_mode == Mode::Paused) {
            if (QMessageBox::question(this, "Отключение",
                    "Идёт измерение. Остановить его и отключиться?") != QMessageBox::Yes)
                return;
            m_dev.send("STOP");
            finishSweep("прервано: отключение от платы");
        }
        m_dev.disconnectFromDevice();
        return;
    }
    setLed("#fdd835");
    setState("Подключение к " + m_ip->text() + ":" + QString::number(m_port->value()) + "...");
    log("Подключение к " + m_ip->text() + ":" + QString::number(m_port->value()));
    m_dev.connectToDevice(m_ip->text().trimmed(), static_cast<quint16>(m_port->value()));
}

void MainWindow::onConnected()
{
    log("Подключено");
    setState("Подключено");
    saveSettings();
    setMode(Mode::Idle);
    m_dev.send("STAT?");
}

void MainWindow::onDisconnected()
{
    log("Соединение закрыто");
    if (m_meas.isOpen())
        finishSweep("прервано: потеряна связь с платой");
    m_selected = 0;
    m_lblSelected->setText("не выбран");
    setMode(Mode::Offline);
    setState("Соединение закрыто", true);
}

void MainWindow::onSocketError(const QString &text)
{
    log("Ошибка сети: " + text);
    setState("Ошибка сети: " + text, true);
    if (m_mode == Mode::Offline)
        setLed("#e53935");
}

void MainWindow::onTimeout()
{
    if (m_mode == Mode::Running) {
        log("Внимание: от платы нет сообщений дольше 2 с");
        setState("Нет сообщений от платы дольше 2 с", true);
    } else if (m_mode != Mode::Paused) {
        log("Нет ответа от платы за 2 с");
        setState("Нет ответа от платы", true);
        if (m_mode == Mode::Starting)
            setMode(Mode::Idle);
    }
    setLed("#e53935");
}

// ---------------------------------------------------------------- команды

void MainWindow::onSelect()
{
    m_dev.send(QString("SEL;%1").arg(m_transistor->value()));
}

void MainWindow::onSetV()
{
    m_dev.send("SETV;" + fmtU(m_setUgs->value()) + ";" + fmtU(m_setUds->value()));
}

void MainWindow::onZero()
{
    m_dev.send("ZERO");
}

void MainWindow::onRangeChanged()
{
    if (m_mode == Mode::Idle)
        m_dev.send(QString("RANGE;%1").arg(m_range->currentData().toInt()));
}

SweepParams MainWindow::collectParams(SweepParams::Kind kind) const
{
    SweepParams p;
    p.kind = kind;
    p.type = m_type->currentData().toString().at(0).toLatin1();
    p.transistor = m_transistor->value();
    p.range = m_range->currentData().toInt();
    p.transUds = m_tUds->value();
    p.transUgsStart = m_tUgsStart->value();
    p.transUgsStop = m_tUgsStop->value();
    p.transUgsStep = m_tUgsStep->value();
    p.outUgsStart = m_oUgsStart->value();
    p.outUgsStop = m_oUgsStop->value();
    p.outUgsStep = m_oUgsStep->value();
    p.outUdsStart = m_oUdsStart->value();
    p.outUdsStop = m_oUdsStop->value();
    p.outUdsStep = m_oUdsStep->value();
    p.delayMs = m_delay->value();
    p.windowMs = m_window->value();
    p.ilim = m_ilim->value() * 1e-6;
    p.reverse = m_reverse->isChecked();
    p.name = m_name->text();
    return p;
}

void MainWindow::onStartTransfer() { startSweep(SweepParams::Transfer); }
void MainWindow::onStartOutput() { startSweep(SweepParams::Output); }

void MainWindow::startSweep(SweepParams::Kind kind)
{
    const SweepParams p = collectParams(kind);
    QString err = p.validate();
    if (err.isEmpty() && !QDir(m_folder->text().trimmed()).exists())
        err = "Папка для сохранения не существует";
    if (err.isEmpty() && m_selected != p.transistor)
        err = QString("Сначала выберите транзистор %1 кнопкой «Выбрать»").arg(p.transistor);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, "Проверка параметров", err);
        return;
    }
    m_sweep = p;
    m_expectedTotal = p.totalPoints();
    m_progress->setRange(0, m_expectedTotal);
    m_progress->setValue(0);
    saveSettings();
    setMode(Mode::Starting);
    m_dev.send(QString("RANGE;%1").arg(p.range));
    m_dev.send(p.toCommand());
}

void MainWindow::finishSweep(const QString &note)
{
    if (m_meas.isOpen()) {
        m_meas.close(note);
        log("Файл сохранён: " + QDir::toNativeSeparators(m_meas.path()));
        if (!m_meas.widePath().isEmpty())
            log("Файл сохранён: " + QDir::toNativeSeparators(m_meas.widePath()));
    }
    if (m_mode != Mode::Offline)
        setMode(Mode::Idle);
}

// ---------------------------------------------------------------- ответы платы

void MainWindow::onLine(const QString &line, const QStringList &f)
{
    Q_UNUSED(line);
    const QString cmd = f.value(0);
    if (m_mode != Mode::Offline && cmd != "ERR")
        setLed("#43a047");

    if (cmd == "PONG") {
        setState("Связь в порядке");
    } else if (cmd == "STAT" && f.size() >= 5) {
        double ub;
        if (toDouble(f[2], &ub)) {
            m_ubias = ub;
            m_lblUbias->setText(MeasurementFile::num(ub, 4) + " В");
        }
        m_selected = f[3].toInt();
        m_lblSelected->setText(m_selected ? QString("выбран %1").arg(m_selected) : "не выбран");
        m_lblRange->setText(f[4] + (f.value(5) == "1" ? " (авто)" : ""));
        setState("Плата: " + f[1]);
    } else if (cmd == "MEAS" && f.size() >= 6) {
        double ub, ugs, uds, i;
        if (toDouble(f[1], &ub) && toDouble(f[2], &ugs) && toDouble(f[3], &uds) && toDouble(f[4], &i)) {
            m_ubias = ub;
            m_lblUbias->setText(MeasurementFile::num(ub, 4) + " В");
            showIndicators(ugs, uds, i, f[5].toInt());
        }
    } else if (cmd == "ACK") {
        const QString what = f.value(1);
        setState("Выполнено: " + what);
        if (what == "SEL") {
            m_selected = f.value(2).toInt();
            m_lblSelected->setText(QString("выбран %1").arg(m_selected));
        } else if (what == "SETV") {
            double ugs, uds;
            if (toDouble(f.value(2), &ugs) && toDouble(f.value(3), &uds)) {
                m_lblUgs->setText(MeasurementFile::num(ugs, 4) + " В");
                m_lblUds->setText(MeasurementFile::num(uds, 4) + " В");
            }
        } else if (what == "ZERO") {
            m_lblUgs->setText(MeasurementFile::num(0, 4) + " В");
            m_lblUds->setText(MeasurementFile::num(0, 4) + " В");
        } else if ((what == "TRANS" || what == "OUTP") && m_mode == Mode::Starting) {
            QString err;
            if (!m_meas.open(m_folder->text().trimmed(), m_sweep, m_ubias, &err)) {
                log("Ошибка: " + err);
                setState(err, true);
                m_dev.send("STOP");
                setMode(Mode::Idle);
                return;
            }
            log("Запись в файл: " + QDir::toNativeSeparators(m_meas.path()));
            setMode(Mode::Running);
        } else if (what == "PAUSE") {
            setMode(Mode::Paused);
        } else if (what == "RESUME") {
            setMode(Mode::Running);
        }
    } else if (cmd == "ERR") {
        const QString text = QString("Ошибка %1: %2").arg(f.value(1), errText(f.value(2).toInt()));
        setState(text, true);
        if ((f.value(1) == "TRANS" || f.value(1) == "OUTP") && m_mode == Mode::Starting)
            setMode(Mode::Idle);
    } else if (cmd == "DATA" && f.size() >= 8) {
        double ugs, uds, i;
        if (toDouble(f[3], &ugs) && toDouble(f[4], &uds) && toDouble(f[5], &i)) {
            const int n = f[1].toInt();
            const int range = f[6].toInt();
            m_meas.addPoint(n, f[2].toInt(), ugs, uds, i, range, f[7].toInt());
            showIndicators(ugs, uds, i, range);
            if (n <= m_progress->maximum())
                m_progress->setValue(n);
        }
    } else if (cmd == "PROG" && f.size() >= 3) {
        const int total = f[2].toInt();
        if (total > 0) {
            m_progress->setRange(0, total);
            m_progress->setValue(qMin(f[1].toInt(), total));
        }
    } else if (cmd == "DONE") {
        m_progress->setValue(m_progress->maximum());
        setState("Измерение завершено");
        finishSweep("завершено");
    } else if (cmd == "ABORT") {
        setState("Измерение прервано", true);
        finishSweep("прервано");
    } else if (cmd == "OVERI") {
        const QString text = QString("Превышение тока в точке %1: %2. Напряжения сняты.")
            .arg(f.value(1), f.value(2));
        log(text);
        setState(text, true);
        if (m_meas.isOpen())
            m_meas.close("остановлено: превышение тока, точка " + f.value(1) + ", I = " + f.value(2) + " А");
    }
}
