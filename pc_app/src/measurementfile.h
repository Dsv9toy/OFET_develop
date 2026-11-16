#pragma once

#include <QFile>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "sweepparams.h"

// Формат файла: разделитель столбцов и десятичный знак.
struct CsvFormat
{
    enum Mode { Auto = 0, ExcelRu = 1, International = 2, Tab = 3 };

    QChar sep = ';';        // разделитель столбцов
    QChar dec = ',';        // десятичный знак
    QString ext = ".csv";   // расширение файла

    // Auto: как в настройках Windows (так же, как ожидает Excel на этом ПК)
    static CsvFormat fromMode(int mode);
    QString describe() const;
};

// Запись результатов измерения.
// Основной файл: первая строка - заголовки столбцов, далее точки (таблицу удобно сразу строить в Excel).
// Справа от таблицы, через пустой столбец, блок «Параметр / Значение».
class MeasurementFile
{
public:
    bool open(const QString &folder, const SweepParams &p, double ubias,
              const CsvFormat &fmt, QString *error);

    void addPoint(int n, int pass, double ugs, double uds, double i, int range, int flags);

    // Закрыть основной файл; для выходной характеристики записать файл _wide.
    void close(const QString &note);

    bool isOpen() const { return m_file.isOpen(); }
    QString path() const { return m_file.fileName(); }
    QString widePath() const { return m_widePath; }

    // Число для показа в окне программы (всегда с запятой, как принято в русском тексте)
    static QString uiNum(double v, int decimals);

private:
    QString num(double v, int decimals) const;
    QString numE(double v) const;
    QString cell(const QString &s) const;
    void writeRow(QFile &f, const QStringList &cells);
    QStringList nextParamCells();

    QFile m_file;
    SweepParams m_params;
    CsvFormat m_fmt;
    QString m_widePath;
    QVector<QStringList> m_paramRows;   // блок параметров справа от таблицы
    int m_paramNext = 0;

    // Данные для _wide: столбец = (Uзи, проход), строка = Uси
    QStringList m_colKeys;
    QHash<QString, QString> m_colTitle;
    QVector<qint64> m_rowKeys;
    QHash<QString, QHash<qint64, double>> m_wide;
};
