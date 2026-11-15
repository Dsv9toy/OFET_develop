#pragma once

#include <QFile>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "sweepparams.h"

// Запись результатов в CSV для русского Excel:
// UTF-8 с BOM, разделитель ';', десятичная запятая.
class MeasurementFile
{
public:
    // Открыть файл и записать блок параметров и заголовок таблицы.
    // ubias - последнее известное Uсм (для блока параметров).
    bool open(const QString &folder, const SweepParams &p, double ubias, QString *error);

    void addPoint(int n, int pass, double ugs, double uds, double i, int range, int flags);

    // Закрыть основной файл; для выходной характеристики записать файл _wide.
    // note - строка о том, как закончилось измерение (дописывается в конец).
    void close(const QString &note);

    bool isOpen() const { return m_file.isOpen(); }
    QString path() const { return m_file.fileName(); }
    QString widePath() const { return m_widePath; }

    static QString num(double v, int decimals);   // 1,2345
    static QString numE(double v);                // 1,2345E-11

private:
    void writeLine(QFile &f, const QString &line);

    QFile m_file;
    SweepParams m_params;
    QString m_widePath;

    // Данные для _wide: столбец = (Uзи, проход), строка = Uси
    QStringList m_colKeys;               // порядок столбцов
    QHash<QString, QString> m_colTitle;  // ключ столбца -> заголовок
    QVector<qint64> m_rowKeys;           // порядок строк (Uси * 1e4)
    QHash<QString, QHash<qint64, double>> m_wide;
};
