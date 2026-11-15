#pragma once

#include <QString>
#include <QVector>

// Пользовательские пределы напряжений Uзи и Uси, В (CLAUDE.md, п. 3.3)
constexpr double kUserUMin = -18.0;
constexpr double kUserUMax = 15.0;

// Параметры одной развёртки (проходной или выходной)
struct SweepParams
{
    enum Kind { Transfer, Output };

    Kind kind = Transfer;
    char type = 'p';          // 'n' или 'p'
    int transistor = 1;       // 1..5
    int range = 0;            // 0 = авто, 1..7

    // Проходная: Uси постоянно, развёртка Uзи
    double transUds = -5.0;
    double transUgsStart = 0.0;
    double transUgsStop = -16.0;
    double transUgsStep = 0.1;

    // Выходная: семейство Uзи, развёртка Uси
    double outUgsStart = 0.0;
    double outUgsStop = -16.0;
    double outUgsStep = 4.0;
    double outUdsStart = 0.0;
    double outUdsStop = -10.0;
    double outUdsStep = 0.1;

    int delayMs = 200;
    int windowMs = 100;
    double ilim = 1e-3;       // А
    bool reverse = false;
    QString name;

    // Строка команды TRANS;... или OUTP;... (без LF)
    QString toCommand() const;

    // Пустая строка, если всё верно, иначе текст ошибки
    QString validate() const;

    // Ожидаемое число точек (для полосы хода до прихода PROG)
    int totalPoints() const;
};

// Точки развёртки от start до stop с шагом |step|. Последняя точка всегда stop.
// Такое же правило должно быть в прошивке и в эмуляторе.
QVector<double> sweepPoints(double start, double stop, double step);

// Форматирование чисел для протокола: напряжение 4 знака после точки, ток экспоненциально
QString fmtU(double u);
QString fmtI(double i);
