#pragma once

#include <QColor>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

// Простой график для ВАХ: несколько кривых, оси, сетка, легенда,
// три вида оси Y (ток линейно, |ток| в логарифме, корень из |тока|).
// Рисуется вручную через QPainter, дополнительных модулей Qt не требует.
class PlotWidget : public QWidget
{
    Q_OBJECT
public:
    enum YMode { Linear = 0, LogAbs = 1, Sqrt = 2 };

    explicit PlotWidget(QWidget *parent = nullptr);

    void clear();
    void setTitle(const QString &title);
    void setXLabel(const QString &label);
    void setYMode(int mode);

    // Добавить точку. group - номер цвета (одна кривая семейства),
    // dashed - пунктир (обратный ход), label - подпись в легенде.
    void addPoint(int group, bool dashed, const QString &label, double x, double current);

    bool savePng(const QString &path);
    bool isEmpty() const { return m_curves.isEmpty(); }

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    struct Curve {
        int group;
        bool dashed;
        QString label;
        QVector<QPointF> pts;   // x, ток в амперах
    };

    bool yValue(double current, double *y) const;   // ток -> значение по оси Y
    QString yLabel() const;
    QString formatY(double y) const;
    static QString formatSi(double v, const QString &unit);
    static QColor color(int group);

    QVector<Curve> m_curves;
    QString m_title;
    QString m_xLabel = "U, В";
    int m_yMode = Linear;

    // Последнее отображение (для подсказки под курсором)
    QRectF m_plot;
    double m_x0 = 0, m_x1 = 1, m_y0 = 0, m_y1 = 1;
    QPointF m_mouse{-1, -1};
};
