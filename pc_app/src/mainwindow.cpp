#include "mainwindow.h"
#include "ui_mainwindow.h"

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
#include <QInputDialog>
#include <QNetworkInterface>
#include <QUdpSocket>
#include <QTimer>
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
#include <QTabWidget>
#include <QRegularExpression>
#include <QTextStream>
#include <QFileInfo>
#include <QVBoxLayout>
#include <cmath>
#include <limits>

namespace {

bool toDouble(const QString &s, double *v)
{
    bool ok = false;
    *v = s.toDouble(&ok);
    return ok;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_ubias(std::numeric_limits<double>::quiet_NaN())
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

    // Сразу после запуска поискать плату в сети: если она одна, её адрес подставится сам
    QTimer::singleShot(500, this, [this] {
        if (m_mode == Mode::Offline && m_udp == nullptr) {
            m_autoFind = true;
            onFindBoard();
        }
    });
}

MainWindow::~MainWindow()
{
    m_dev.disconnect(this);
    delete ui;
}

// ---------------------------------------------------------------- интерфейс

void MainWindow::buildUi()
{
    // Внешний вид окна задаётся в mainwindow.ui (редактируется в Qt Designer).
    ui->setupUi(this);

    m_ip = ui->ipEdit;
    m_port = ui->portSpin;
    m_btnFind = ui->findButton;
    m_btnConnect = ui->connectButton;
    m_btnPing = ui->pingButton;
    m_led = ui->linkLed;

    m_lblMode = ui->modeLabel;
    m_lblState = ui->stateLabel;
    m_lblUbias = ui->ubiasValue;
    m_lblUgs = ui->ugsValue;
    m_lblUds = ui->udsValue;
    m_lblI = ui->currentValue;
    m_lblRange = ui->rangeValue;
    m_btnMeas = ui->measButton;

    m_type = ui->typeCombo;           // 0 = p-тип, 1 = n-тип
    m_transistor = ui->transistorSpin;
    m_btnSel = ui->selectButton;
    m_lblSelected = ui->selectedLabel;
    m_range = ui->rangeCombo;         // индекс 0 = авто, 1...7 = диапазон

    m_setUgs = ui->setUgsSpin;
    m_setUds = ui->setUdsSpin;
    m_btnSetV = ui->setVButton;
    m_btnZero = ui->zeroButton;

    m_tUds = ui->tUdsSpin;
    m_tUgsStart = ui->tUgsStartSpin;
    m_tUgsStop = ui->tUgsStopSpin;
    m_tUgsStep = ui->tUgsStepSpin;

    m_oUgsStart = ui->oUgsStartSpin;
    m_oUgsStop = ui->oUgsStopSpin;
    m_oUgsStep = ui->oUgsStepSpin;
    m_oUdsStart = ui->oUdsStartSpin;
    m_oUdsStop = ui->oUdsStopSpin;
    m_oUdsStep = ui->oUdsStepSpin;

    m_delay = ui->delaySpin;
    m_window = ui->windowSpin;
    m_ilim = ui->ilimSpin;
    m_reverse = ui->reverseCheck;
    m_name = ui->nameEdit;
    m_folder = ui->folderEdit;
    m_btnFolder = ui->folderButton;

    m_btnTrans = ui->transButton;
    m_btnOutp = ui->outpButton;
    m_btnStop = ui->stopButton;
    m_btnPause = ui->pauseButton;
    m_btnResume = ui->resumeButton;
    m_progress = ui->progressBar;
    m_log = ui->logEdit;
    m_csvFormat = ui->csvFormatCombo;
    m_tabs = ui->viewTabs;
    m_plot = ui->plotWidget;
    m_yMode = ui->yModeCombo;
    m_btnOpenFile = ui->openFileButton;
    m_btnSavePlot = ui->savePlotButton;

    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    QFont mono = m_log->font();
    mono.setStyleHint(QFont::Monospace);
    m_log->setFont(mono);

    // --- Сигналы
    connect(m_btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_btnFind, &QPushButton::clicked, this, [this] { m_autoFind = false; onFindBoard(); });
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
    connect(m_yMode, QOverload<int>::of(&QComboBox::currentIndexChanged), m_plot, &PlotWidget::setYMode);
    connect(m_btnOpenFile, &QPushButton::clicked, this, &MainWindow::onOpenFile);
    connect(m_btnSavePlot, &QPushButton::clicked, this, &MainWindow::onSavePlot);
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
    m_btnFind->setEnabled(!online && m_udp == nullptr);
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
             m_delay, m_window, m_ilim, m_reverse, m_name, m_folder, m_btnFolder,
             m_csvFormat, m_btnOpenFile})
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
    m_lblUgs->setText(MeasurementFile::uiNum(ugs, 4) + " В");
    m_lblUds->setText(MeasurementFile::uiNum(uds, 4) + " В");
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
    m_ip->setText(s.value("link/ip", QString()).toString());
    m_port->setValue(s.value("link/port", m_port->value()).toInt());
    const QString defFolder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    m_folder->setText(s.value("save/folder", defFolder).toString());
    m_name->setText(s.value("save/name").toString());

    m_type->setCurrentIndex(s.value("meas/type", "p").toString() == "n" ? 1 : 0);
    m_transistor->setValue(s.value("meas/transistor", 1).toInt());
    m_range->setCurrentIndex(qBound(0, s.value("meas/range", 0).toInt(), 7));
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
    m_csvFormat->setCurrentIndex(qBound(0, s.value("save/csv_format", 0).toInt(), 3));
    m_yMode->setCurrentIndex(qBound(0, s.value("ui/y_mode", 1).toInt(), 2));
    m_plot->setYMode(m_yMode->currentIndex());
    restoreGeometry(s.value("ui/geometry").toByteArray());
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue("link/ip", m_ip->text());
    s.setValue("link/port", m_port->value());
    s.setValue("save/folder", m_folder->text());
    s.setValue("save/name", m_name->text());
    s.setValue("meas/type", m_type->currentIndex() == 1 ? "n" : "p");
    s.setValue("meas/transistor", m_transistor->value());
    s.setValue("meas/range", m_range->currentIndex());
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
    s.setValue("save/csv_format", m_csvFormat->currentIndex());
    s.setValue("ui/y_mode", m_yMode->currentIndex());
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

// ---------------------------------------------------------------- поиск платы

// Плата слушает UDP-порт 5001 и на строку DISCOVER отвечает OFET;<IP>;<порт>.
static constexpr quint16 kDiscoverPort = 5001;

void MainWindow::onFindBoard()
{
    m_found.clear();
    m_udp = new QUdpSocket(this);
    if (!m_udp->bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
        log("Поиск: не удалось открыть UDP-сокет: " + m_udp->errorString());
        m_udp->deleteLater();
        m_udp = nullptr;
        return;
    }
    connect(m_udp, &QUdpSocket::readyRead, this, &MainWindow::onDiscoverReply);

    // Широковещательный запрос в каждую сеть ПК, общий broadcast и локальный эмулятор
    QList<QHostAddress> targets{QHostAddress::Broadcast, QHostAddress::LocalHost};
    for (const QNetworkInterface &nif : QNetworkInterface::allInterfaces()) {
        if (!(nif.flags() & QNetworkInterface::IsUp) || !(nif.flags() & QNetworkInterface::IsRunning))
            continue;
        for (const QNetworkAddressEntry &e : nif.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && !e.broadcast().isNull())
                targets.append(e.broadcast());
    }
    for (const QHostAddress &a : targets)
        m_udp->writeDatagram("DISCOVER\n", a, kDiscoverPort);

    log(m_autoFind ? "Автоматический поиск платы в сети..." : "Поиск платы в сети...");
    setState("Поиск платы...");
    setLed("#fdd835");
    updateUi();
    QTimer::singleShot(1500, this, &MainWindow::onDiscoverFinished);
}

void MainWindow::onDiscoverReply()
{
    while (m_udp && m_udp->hasPendingDatagrams()) {
        QByteArray d;
        d.resize(int(m_udp->pendingDatagramSize()));
        QHostAddress from;
        m_udp->readDatagram(d.data(), d.size(), &from);
        const QStringList f = QString::fromLatin1(d).trimmed().split(';');
        if (f.value(0) != "OFET" || f.size() < 3)
            continue;
        // Адрес берём из ответа; если он пустой, то адрес отправителя
        QHostAddress ip(f[1]);
        if (ip.isNull())
            ip = QHostAddress(from.toIPv4Address());
        const QString entry = ip.toString() + ":" + f[2];
        if (!m_found.contains(entry))
            m_found.append(entry);
    }
}

void MainWindow::onDiscoverFinished()
{
    if (m_udp) {
        onDiscoverReply();
        m_udp->deleteLater();
        m_udp = nullptr;
    }
    setLed(m_mode == Mode::Offline ? "#9e9e9e" : "#43a047");
    QString pick;
    const bool autoFind = m_autoFind;
    m_autoFind = false;
    if (m_found.isEmpty()) {
        log("Плата не найдена");
        if (autoFind)
            setState("Плата в сети не найдена. Подключите плату и нажмите «Найти плату» или впишите IP вручную");
        else
            setState("Плата не найдена. Проверьте кабель и питание платы, подождите 30 с после подключения и повторите", true);
    } else if (m_found.size() == 1) {
        pick = m_found.first();
    } else {
        bool ok = false;
        pick = QInputDialog::getItem(this, "Найдено несколько плат", "Выберите плату:", m_found, 0, false, &ok);
        if (!ok)
            pick.clear();
    }
    if (!pick.isEmpty()) {
        m_ip->setText(pick.section(':', 0, 0));
        m_port->setValue(pick.section(':', 1, 1).toInt());
        log("Найдена плата: " + pick);
        setState("Найдена плата " + pick + ". Нажмите «Подключиться»");
    }
    updateUi();
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
        m_dev.send(QString("RANGE;%1").arg(m_range->currentIndex()));
}

SweepParams MainWindow::collectParams(SweepParams::Kind kind) const
{
    SweepParams p;
    p.kind = kind;
    p.type = m_type->currentIndex() == 1 ? 'n' : 'p';
    p.transistor = m_transistor->value();
    p.range = m_range->currentIndex();
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
            m_lblUbias->setText(MeasurementFile::uiNum(ub, 4) + " В");
        }
        m_selected = f[3].toInt();
        m_lblSelected->setText(m_selected ? QString("выбран %1").arg(m_selected) : "не выбран");
        m_lblRange->setText(f[4] + (f.value(5) == "1" ? " (авто)" : ""));
        setState("Плата: " + f[1]);
    } else if (cmd == "MEAS" && f.size() >= 6) {
        double ub, ugs, uds, i;
        if (toDouble(f[1], &ub) && toDouble(f[2], &ugs) && toDouble(f[3], &uds) && toDouble(f[4], &i)) {
            m_ubias = ub;
            m_lblUbias->setText(MeasurementFile::uiNum(ub, 4) + " В");
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
                m_lblUgs->setText(MeasurementFile::uiNum(ugs, 4) + " В");
                m_lblUds->setText(MeasurementFile::uiNum(uds, 4) + " В");
            }
        } else if (what == "ZERO") {
            m_lblUgs->setText(MeasurementFile::uiNum(0, 4) + " В");
            m_lblUds->setText(MeasurementFile::uiNum(0, 4) + " В");
        } else if ((what == "TRANS" || what == "OUTP") && m_mode == Mode::Starting) {
            QString err;
            const CsvFormat fmt = CsvFormat::fromMode(m_csvFormat->currentIndex());
            if (!m_meas.open(m_folder->text().trimmed(), m_sweep, m_ubias, fmt, &err)) {
                log("Ошибка: " + err);
                setState(err, true);
                m_dev.send("STOP");
                setMode(Mode::Idle);
                return;
            }
            log("Запись в файл: " + QDir::toNativeSeparators(m_meas.path()) + " (" + fmt.describe() + ")");
            plotStart(m_sweep.kind, m_sweep.name.trimmed());
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
            plotPoint(f[2].toInt(), ugs, uds, i);
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

// ---------------------------------------------------------------- график

void MainWindow::plotStart(SweepParams::Kind kind, const QString &title)
{
    m_plotKind = kind;
    m_plotGroups.clear();
    m_plot->clear();
    m_plot->setTitle(title);
    m_plot->setXLabel(kind == SweepParams::Transfer ? "Uзи, В" : "Uси, В");
    m_tabs->setCurrentIndex(0);     // показать вкладку «График»
}

void MainWindow::plotPoint(int pass, double ugs, double uds, double i)
{
    const bool back = pass == 2;
    if (m_plotKind == SweepParams::Transfer) {
        m_plot->addPoint(0, back, "Iс(Uзи)", ugs, i);
        return;
    }
    // Выходная: своя кривая (свой цвет) на каждое Uзи
    const qint64 key = qRound64(ugs * 1e4);
    int g = m_plotGroups.indexOf(key);
    if (g < 0) {
        m_plotGroups.append(key);
        g = m_plotGroups.size() - 1;
    }
    m_plot->addPoint(g, back, "Uзи = " + MeasurementFile::uiNum(ugs, 2) + " В", uds, i);
}

// Открыть ранее сохранённый файл измерения и построить график
void MainWindow::onOpenFile()
{
    const QString path = QFileDialog::getOpenFileName(this, "Открыть файл измерения",
        m_folder->text(), "Измерения (*.csv *.txt);;Все файлы (*)");
    if (!path.isEmpty())
        loadMeasurementFile(path);
}

bool MainWindow::loadMeasurementFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Открыть файл", "Не удалось открыть файл:\n" + f.errorString());
        return false;
    }
    QString text = QString::fromUtf8(f.readAll());
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    const QStringList lines = text.split(QRegularExpression("\\r?\\n"));
    if (lines.isEmpty())
        return false;

    // Разделитель определяем по строке заголовков: «N», затем «Проход»
    QChar sep;
    for (QChar c : {QChar(';'), QChar('\t'), QChar(',')})
        if (lines[0].startsWith(QString("N") + c + "Проход")) { sep = c; break; }
    if (sep.isNull()) {
        QMessageBox::warning(this, "Открыть файл",
            "Это не файл измерения этой программы (первая строка должна начинаться с «N» и «Проход»).");
        return false;
    }
    const bool commaDecimal = sep != ',';
    auto toD = [commaDecimal](QString s, bool *ok) {
        if (commaDecimal) s.replace(',', '.');
        return s.toDouble(ok);
    };

    SweepParams::Kind kind = SweepParams::Transfer;
    struct Pt { int pass; double ugs, uds, i; };
    QVector<Pt> pts;
    for (int k = 1; k < lines.size(); ++k) {
        const QStringList c = lines[k].split(sep);
        if (c.size() > 11 && c[10] == "Характеристика" && c[11].startsWith("Выход"))
            kind = SweepParams::Output;
        bool ok1, ok2, ok3, ok4;
        if (c.size() < 5 || c[0].isEmpty())
            continue;
        const int pass = c[1].toInt(&ok1);
        const double ugs = toD(c[2], &ok2), uds = toD(c[3], &ok3), i = toD(c[4], &ok4);
        if (ok1 && ok2 && ok3 && ok4)
            pts.append({pass, ugs, uds, i});
    }
    if (pts.isEmpty()) {
        QMessageBox::warning(this, "Открыть файл", "В файле нет точек измерения.");
        return false;
    }
    plotStart(kind, QFileInfo(path).completeBaseName());
    for (const Pt &p : pts)
        plotPoint(p.pass, p.ugs, p.uds, p.i);
    log(QString("Открыт файл %1: %2 точек").arg(QDir::toNativeSeparators(path)).arg(pts.size()));
    return true;
}

void MainWindow::onSavePlot()
{
    if (m_plot->isEmpty()) {
        QMessageBox::information(this, "Сохранить картинку", "График пока пустой.");
        return;
    }
    const QString def = QDir(m_folder->text()).filePath(
        (m_name->text().trimmed().isEmpty() ? QString("grafik") : m_name->text().trimmed())
        + "_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png");
    const QString path = QFileDialog::getSaveFileName(this, "Сохранить картинку графика", def, "Картинка PNG (*.png)");
    if (path.isEmpty())
        return;
    if (m_plot->savePng(path))
        log("Картинка сохранена: " + QDir::toNativeSeparators(path));
    else
        QMessageBox::warning(this, "Сохранить картинку", "Не удалось сохранить файл.");
}
