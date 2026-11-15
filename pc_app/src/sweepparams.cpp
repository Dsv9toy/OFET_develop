#include "sweepparams.h"

#include <cmath>

QVector<double> sweepPoints(double start, double stop, double step)
{
    QVector<double> pts;
    step = std::fabs(step);
    if (step <= 0.0)
        return pts;
    const double span = stop - start;
    const double dir = span >= 0 ? 1.0 : -1.0;
    const int n = static_cast<int>(std::floor(std::fabs(span) / step + 1e-9));
    for (int i = 0; i <= n; ++i)
        pts.append(start + dir * step * i);
    if (std::fabs(pts.last() - stop) > 1e-9)
        pts.append(stop);
    return pts;
}

QString fmtU(double u)
{
    return QString::number(u, 'f', 4);
}

QString fmtI(double i)
{
    return QString::number(i, 'E', 4);
}

static QString checkU(const QString &what, double u)
{
    if (u < kUserUMin || u > kUserUMax)
        return QString("%1 = %2 В вне предела от %3 до %4 В")
            .arg(what).arg(u).arg(kUserUMin).arg(kUserUMax);
    return QString();
}

static QString checkSweep(const QString &what, double start, double stop, double step)
{
    QString e = checkU(what + " (начало)", start);
    if (e.isEmpty()) e = checkU(what + " (конец)", stop);
    if (!e.isEmpty()) return e;
    if (std::fabs(step) < 1e-4)
        return QString("Шаг %1 не может быть равен 0").arg(what);
    if (std::fabs(stop - start) < 1e-9)
        return QString("Начало и конец развёртки %1 совпадают").arg(what);
    if (sweepPoints(start, stop, step).size() > 100000)
        return QString("Слишком много точек по %1, увеличьте шаг").arg(what);
    return QString();
}

QString SweepParams::validate() const
{
    if (name.trimmed().isEmpty())
        return "Не задано название измерения";
    if (transistor < 1 || transistor > 5)
        return "Номер транзистора должен быть от 1 до 5";
    if (delayMs < 100 || delayMs > 60000)
        return "Задержка должна быть от 100 до 60000 мс";
    if (windowMs < 20 || windowMs > 2000 || windowMs % 20 != 0)
        return "Окно усреднения должно быть кратно 20 мс, от 20 до 2000 мс";
    if (ilim <= 0.0 || ilim > 1.5e-3)
        return "Ограничение тока должно быть больше 0 и не больше 1,5 мА";

    QString e;
    if (kind == Transfer) {
        e = checkU("Uси", transUds);
        if (e.isEmpty()) e = checkSweep("Uзи", transUgsStart, transUgsStop, transUgsStep);
    } else {
        e = checkSweep("Uзи", outUgsStart, outUgsStop, outUgsStep);
        if (e.isEmpty()) e = checkSweep("Uси", outUdsStart, outUdsStop, outUdsStep);
    }
    return e;
}

int SweepParams::totalPoints() const
{
    const int k = reverse ? 2 : 1;
    if (kind == Transfer)
        return k * sweepPoints(transUgsStart, transUgsStop, transUgsStep).size();
    return k * sweepPoints(outUgsStart, outUgsStop, outUgsStep).size()
             * sweepPoints(outUdsStart, outUdsStop, outUdsStep).size();
}

QString SweepParams::toCommand() const
{
    const QString t = QString(QChar(type));
    const QString tail = QString("%1;%2;%3;%4")
        .arg(delayMs).arg(windowMs).arg(fmtI(ilim)).arg(reverse ? 1 : 0);
    if (kind == Transfer) {
        return QString("TRANS;%1;%2;%3;%4;%5;%6")
            .arg(t, fmtU(transUds), fmtU(transUgsStart), fmtU(transUgsStop),
                 fmtU(std::fabs(transUgsStep)), tail);
    }
    return QString("OUTP;%1;%2;%3;%4;%5;%6;%7;%8")
        .arg(t, fmtU(outUgsStart), fmtU(outUgsStop), fmtU(std::fabs(outUgsStep)),
             fmtU(outUdsStart), fmtU(outUdsStop), fmtU(std::fabs(outUdsStep)), tail);
}
