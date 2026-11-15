#include "measurementfile.h"

#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <cmath>

QString MeasurementFile::num(double v, int decimals)
{
    return QString::number(v, 'f', decimals).replace('.', ',');
}

QString MeasurementFile::numE(double v)
{
    return QString::number(v, 'E', 4).replace('.', ',');
}

void MeasurementFile::writeLine(QFile &f, const QString &line)
{
    f.write(line.toUtf8());
    f.write("\r\n");
}

bool MeasurementFile::open(const QString &folder, const SweepParams &p, double ubias, QString *error)
{
    close(QString());
    m_params = p;
    m_colKeys.clear();
    m_colTitle.clear();
    m_rowKeys.clear();
    m_wide.clear();
    m_widePath.clear();

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

    m_file.setFileName(dir.filePath(base + ".csv"));
    if (p.kind == SweepParams::Output)
        m_widePath = dir.filePath(base + "_wide.csv");

    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = "Не удалось создать файл " + m_file.fileName() + ": " + m_file.errorString();
        return false;
    }

    m_file.write("\xEF\xBB\xBF"); // BOM
    const bool tr = p.kind == SweepParams::Transfer;
    writeLine(m_file, "Параметр;Значение");
    writeLine(m_file, "Название;" + p.name.trimmed());
    writeLine(m_file, "Дата и время;" + now.toString("dd.MM.yyyy HH:mm:ss"));
    writeLine(m_file, QString("Характеристика;") + (tr ? "Проходная" : "Выходная"));
    writeLine(m_file, QString("Тип транзистора;") + QChar(p.type) + "-тип");
    writeLine(m_file, QString("Транзистор;%1").arg(p.transistor));
    if (tr) {
        writeLine(m_file, "Uси, В;" + num(p.transUds, 4));
        writeLine(m_file, "Uзи начало, В;" + num(p.transUgsStart, 4));
        writeLine(m_file, "Uзи конец, В;" + num(p.transUgsStop, 4));
        writeLine(m_file, "Uзи шаг, В;" + num(std::fabs(p.transUgsStep), 4));
    } else {
        writeLine(m_file, "Uзи начало, В;" + num(p.outUgsStart, 4));
        writeLine(m_file, "Uзи конец, В;" + num(p.outUgsStop, 4));
        writeLine(m_file, "Uзи шаг, В;" + num(std::fabs(p.outUgsStep), 4));
        writeLine(m_file, "Uси начало, В;" + num(p.outUdsStart, 4));
        writeLine(m_file, "Uси конец, В;" + num(p.outUdsStop, 4));
        writeLine(m_file, "Uси шаг, В;" + num(std::fabs(p.outUdsStep), 4));
    }
    writeLine(m_file, QString("Задержка, мс;%1").arg(p.delayMs));
    writeLine(m_file, QString("Окно усреднения, мс;%1").arg(p.windowMs));
    writeLine(m_file, "Ограничение тока, А;" + numE(p.ilim));
    writeLine(m_file, QString("Обратная развёртка;") + (p.reverse ? "да" : "нет"));
    writeLine(m_file, "Диапазон;" + (p.range == 0 ? QString("авто") : QString::number(p.range)));
    writeLine(m_file, "Uсм, В;" + (std::isnan(ubias) ? QString("нет данных") : num(ubias, 4)));
    writeLine(m_file, "");
    writeLine(m_file, "N;Проход;Uзи, В;Uси, В;Iс, А;|Iс|, А;sqrt(|Iс|);Диапазон;Флаги");
    m_file.flush();
    return true;
}

void MeasurementFile::addPoint(int n, int pass, double ugs, double uds, double i, int range, int flags)
{
    if (!m_file.isOpen())
        return;
    const double a = std::fabs(i);
    writeLine(m_file, QString("%1;%2;%3;%4;%5;%6;%7;%8;%9")
        .arg(n).arg(pass)
        .arg(num(ugs, 4), num(uds, 4), numE(i), numE(a), numE(std::sqrt(a)))
        .arg(range).arg(flags));
    m_file.flush(); // запись по мере прихода точек

    if (m_params.kind == SweepParams::Output) {
        const QString col = QString("%1|%2").arg(qRound64(ugs * 1e4)).arg(pass);
        if (!m_colTitle.contains(col)) {
            m_colKeys.append(col);
            m_colTitle.insert(col, QString("Iс, А при Uзи = %1 В%2")
                .arg(num(ugs, 4), pass == 2 ? " (обратно)" : ""));
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
    if (!note.isEmpty()) {
        writeLine(m_file, "");
        writeLine(m_file, "Итог;" + note);
    }
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
    writeLine(w, head.join(';'));
    for (qint64 row : m_rowKeys) {
        QStringList cells{num(row / 1e4, 4)};
        for (const QString &c : m_colKeys) {
            const auto &col = m_wide.value(c);
            cells << (col.contains(row) ? numE(col.value(row)) : QString());
        }
        writeLine(w, cells.join(';'));
    }
    w.close();
}
