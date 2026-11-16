#include "measurementfile.h"

#include <QDateTime>
#include <QDir>
#include <QLocale>
#include <QRegularExpression>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ------------------------------------------------------------ формат

CsvFormat CsvFormat::fromMode(int mode)
{
    CsvFormat f;
    switch (mode) {
    case ExcelRu:       f.sep = ';';  f.dec = ','; break;
    case International: f.sep = ',';  f.dec = '.'; break;
    case Tab:           f.sep = '\t'; f.dec = '.'; f.ext = ".txt"; break;
    default: {
        // Excel берёт разделитель списка и десятичный знак из «Региональных стандартов» Windows
        QChar dec = QLocale::system().decimalPoint();
        QChar sep = (dec == ',') ? ';' : ',';
#ifdef Q_OS_WIN
        wchar_t buf[8];
        if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, buf, 8) > 1)
            dec = QChar(ushort(buf[0]));
        if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SLIST, buf, 8) > 1)
            sep = QChar(ushort(buf[0]));
#endif
        if (sep == dec)
            sep = (dec == ',') ? ';' : ',';
        f.sep = sep;
        f.dec = dec;
        break;
    }
    }
    return f;
}

QString CsvFormat::describe() const
{
    const QString s = sep == '\t' ? QString("табуляция") : QString("«%1»").arg(sep);
    return QString("столбцы: %1, десятичный знак: «%2»").arg(s, QString(dec));
}

// ------------------------------------------------------------ числа

QString MeasurementFile::uiNum(double v, int decimals)
{
    return QString::number(v, 'f', decimals).replace('.', ',');
}

QString MeasurementFile::num(double v, int decimals) const
{
    return QString::number(v, 'f', decimals).replace('.', m_fmt.dec);
}

QString MeasurementFile::numE(double v) const
{
    return QString::number(v, 'E', 4).replace('.', m_fmt.dec);
}

// Текст с разделителем внутри берём в кавычки (правило CSV)
QString MeasurementFile::cell(const QString &s) const
{
    if (s.contains(m_fmt.sep) || s.contains('"')) {
        QString q = s;
        q.replace("\"", "\"\"");
        return "\"" + q + "\"";
    }
    return s;
}

void MeasurementFile::writeRow(QFile &f, const QStringList &cells)
{
    QStringList out;
    for (const QString &c : cells)
        out << cell(c);
    f.write(out.join(m_fmt.sep).toUtf8());
    f.write("\r\n");
}

QStringList MeasurementFile::nextParamCells()
{
    if (m_paramNext < m_paramRows.size())
        return QStringList{QString()} + m_paramRows[m_paramNext++];
    return {};
}

// ------------------------------------------------------------ открыть, записать, закрыть

bool MeasurementFile::open(const QString &folder, const SweepParams &p, double ubias,
                           const CsvFormat &fmt, QString *error)
{
    close(QString());
    m_params = p;
    m_fmt = fmt;
    m_colKeys.clear();
    m_colTitle.clear();
    m_rowKeys.clear();
    m_wide.clear();
    m_widePath.clear();
    m_paramRows.clear();
    m_paramNext = 0;

    QDir dir(folder);
    if (folder.isEmpty() || !dir.exists()) {
        if (error) *error = "Папка для сохранения не существует: " + folder;
        return false;
    }

    QString safeName = p.name.trimmed();
    safeName.replace(QRegularExpression(R"([\\/:*?"<>|\s])"), "_");
    const QDateTime now = QDateTime::currentDateTime();
    const QString base = QString("%1_%2_T%3_%4")
        .arg(safeName, now.toString("yyyyMMdd_HHmmss"))
        .arg(p.transistor)
        .arg(p.kind == SweepParams::Transfer ? "PROH" : "VYH");

    m_file.setFileName(dir.filePath(base + m_fmt.ext));
    if (p.kind == SweepParams::Output)
        m_widePath = dir.filePath(base + "_wide" + m_fmt.ext);

    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = "Не удалось создать файл " + m_file.fileName() + ": " + m_file.errorString();
        return false;
    }

    // Блок параметров (выводится справа от таблицы)
    const bool tr = p.kind == SweepParams::Transfer;
    auto par = [this](const QString &a, const QString &b) { m_paramRows.append({a, b}); };
    par("Название", p.name.trimmed());
    par("Дата и время", now.toString("dd.MM.yyyy HH:mm:ss"));
    par("Характеристика", tr ? "Проходная" : "Выходная");
    par("Тип транзистора", QString(QChar(p.type)) + "-тип");
    par("Транзистор", QString::number(p.transistor));
    if (tr) {
        par("Uси, В", num(p.transUds, 4));
        par("Uзи начало, В", num(p.transUgsStart, 4));
        par("Uзи конец, В", num(p.transUgsStop, 4));
        par("Uзи шаг, В", num(std::fabs(p.transUgsStep), 4));
    } else {
        par("Uзи начало, В", num(p.outUgsStart, 4));
        par("Uзи конец, В", num(p.outUgsStop, 4));
        par("Uзи шаг, В", num(std::fabs(p.outUgsStep), 4));
        par("Uси начало, В", num(p.outUdsStart, 4));
        par("Uси конец, В", num(p.outUdsStop, 4));
        par("Uси шаг, В", num(std::fabs(p.outUdsStep), 4));
    }
    par("Задержка, мс", QString::number(p.delayMs));
    par("Окно усреднения, мс", QString::number(p.windowMs));
    par("Ограничение тока, А", numE(p.ilim));
    par("Обратная развёртка", p.reverse ? "да" : "нет");
    par("Диапазон", p.range == 0 ? QString("авто") : QString::number(p.range));
    par("Uсм, В", std::isnan(ubias) ? QString("нет данных") : num(ubias, 4));

    m_file.write("\xEF\xBB\xBF"); // BOM: Excel узнаёт кодировку UTF-8 и показывает русские буквы
    writeRow(m_file, QStringList{"N", "Проход", "Uзи, В", "Uси, В", "Iс, А", "|Iс|, А",
                                 "sqrt(|Iс|), А^0,5", "Диапазон", "Флаги", "", "Параметр", "Значение"});
    m_file.flush();
    return true;
}

void MeasurementFile::addPoint(int n, int pass, double ugs, double uds, double i, int range, int flags)
{
    if (!m_file.isOpen())
        return;
    const double a = std::fabs(i);
    QStringList row{QString::number(n), QString::number(pass), num(ugs, 4), num(uds, 4),
                    numE(i), numE(a), numE(std::sqrt(a)), QString::number(range), QString::number(flags)};
    writeRow(m_file, row + nextParamCells());
    m_file.flush(); // запись по мере прихода точек: при сбое данные не пропадут

    if (m_params.kind == SweepParams::Output) {
        const QString col = QString("%1|%2").arg(qRound64(ugs * 1e4)).arg(pass);
        if (!m_colTitle.contains(col)) {
            m_colKeys.append(col);
            m_colTitle.insert(col, QString("Iс, А при Uзи = %1 В%2")
                .arg(num(ugs, 2), pass == 2 ? " (обратно)" : ""));
        }
        const qint64 row = qRound64(uds * 1e4);
        if (!m_rowKeys.contains(row))
            m_rowKeys.append(row);
        m_wide[col].insert(row, i);
    }
}

void MeasurementFile::close(const QString &note)
{
    if (!m_file.isOpen())
        return;
    if (!note.isEmpty())
        m_paramRows.append({"Итог", note});
    // Если точек меньше, чем строк параметров, дописываем оставшиеся параметры
    QStringList empty;
    for (int k = 0; k < 9; ++k)
        empty << QString();
    while (m_paramNext < m_paramRows.size())
        writeRow(m_file, empty + nextParamCells());
    m_file.close();

    if (m_params.kind != SweepParams::Output || m_widePath.isEmpty() || m_colKeys.isEmpty())
        return;

    QFile w(m_widePath);
    if (!w.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    w.write("\xEF\xBB\xBF");
    QStringList head{"Uси, В"};
    for (const QString &c : m_colKeys)
        head << m_colTitle.value(c);
    writeRow(w, head);
    for (qint64 row : m_rowKeys) {
        QStringList cells{num(row / 1e4, 4)};
        for (const QString &c : m_colKeys) {
            const auto &col = m_wide.value(c);
            cells << (col.contains(row) ? numE(col.value(row)) : QString());
        }
        writeRow(w, cells);
    }
    w.close();
}
