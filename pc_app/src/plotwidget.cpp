#include "plotwidget.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <cmath>
#include <limits>

PlotWidget::PlotWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(300, 220);
    setMouseTracking(true);
    setAutoFillBackground(false);
}

void PlotWidget::clear()
{
    m_curves.clear();
    update();
}

void PlotWidget::setTitle(const QString &title)
{
    m_title = title;
    update();
}

void PlotWidget::setXLabel(const QString &label)
{
    m_xLabel = label;
    update();
}

void PlotWidget::setYMode(int mode)
{
    m_yMode = mode;
    update();
}

void PlotWidget::addPoint(int group, bool dashed, const QString &label, double x, double current)
{
    for (Curve &c : m_curves) {
        if (c.group == group && c.dashed == dashed) {
            c.pts.append(QPointF(x, current));
            update();
            return;
        }
    }
    m_curves.append(Curve{group, dashed, label, {QPointF(x, current)}});
    update();
}

bool PlotWidget::savePng(const QString &path)
{
    QPixmap pm(size() * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::white);
    const QPointF saved = m_mouse;
    m_mouse = QPointF(-1, -1);          // без подсказки курсора на картинке
    render(&pm);
    m_mouse = saved;
    return pm.save(path, "PNG");
}

QColor PlotWidget::color(int group)
{
    static const QColor pal[] = {
        QColor("#1f6fd1"), QColor("#d1421f"), QColor("#2a9d3c"), QColor("#8e44ad"),
        QColor("#e08a00"), QColor("#00939b"), QColor("#c2185b"), QColor("#5d6d7e"),
        QColor("#7cb342"), QColor("#3949ab"),
    };
    return pal[qAbs(group) % 10];
}

bool PlotWidget::yValue(double current, double *y) const
{
    switch (m_yMode) {
    case LogAbs:
        if (std::fabs(current) < 1e-16) return false;
        *y = std::log10(std::fabs(current));
        return true;
    case Sqrt:
        *y = std::sqrt(std::fabs(current));
        return true;
    default:
        *y = current;
        return true;
    }
}

QString PlotWidget::yLabel() const
{
    switch (m_yMode) {
    case LogAbs: return "|Iс|, А (логарифм)";
    case Sqrt:   return "sqrt(|Iс|), А^0,5";
    default:     return "Iс, А";
    }
}

// 2.5e-7 -> "250 нА"
QString PlotWidget::formatSi(double v, const QString &unit)
{
    if (v == 0.0) return "0";
    struct P { double k; const char *s; };
    static const P pre[] = { {1, ""}, {1e-3, "м"}, {1e-6, "мк"}, {1e-9, "н"}, {1e-12, "п"}, {1e-15, "ф"} };
    const double a = std::fabs(v);
    for (const P &p : pre) {
        if (a >= p.k * 0.999) {
            QString s = QString::number(v / p.k, 'g', 3).replace('.', ',');
            return s + " " + p.s + unit;
        }
    }
    return QString::number(v, 'g', 3).replace('.', ',') + " " + unit;
}

QString PlotWidget::formatY(double y) const
{
    switch (m_yMode) {
    case LogAbs: return formatSi(std::pow(10.0, y), "А");
    case Sqrt:   return QString::number(y, 'g', 3).replace('.', ',');
    default:     return formatSi(y, "А");
    }
}

// «Красивый» шаг сетки: 1, 2 или 5 умножить на степень 10
static double niceStep(double range, int ticks)
{
    const double raw = range / ticks;
    const double p = std::pow(10.0, std::floor(std::log10(raw)));
    const double m = raw / p;
    return (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * p;
}

void PlotWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white);
    const QFontMetrics fm = p.fontMetrics();

    // --- Диапазоны данных
    double x0 = std::numeric_limits<double>::max(), x1 = -x0, y0 = x0, y1 = -x0;
    for (const Curve &c : m_curves)
        for (const QPointF &pt : c.pts) {
            double y;
            if (!yValue(pt.y(), &y)) continue;
            x0 = qMin(x0, pt.x()); x1 = qMax(x1, pt.x());
            y0 = qMin(y0, y);      y1 = qMax(y1, y);
        }
    const bool haveData = x0 <= x1;
    if (!haveData) { x0 = 0; x1 = 1; y0 = 0; y1 = 1; }
    if (x1 - x0 < 1e-9) { x0 -= 0.5; x1 += 0.5; }
    if (m_yMode == LogAbs) {
        y0 = std::floor(y0); y1 = std::ceil(y1);
        if (y1 - y0 < 1) y1 = y0 + 1;
    } else {
        if (y1 - y0 < 1e-18) { const double d = qMax(std::fabs(y0) * 0.1, 1e-12); y0 -= d; y1 += d; }
        const double pad = (y1 - y0) * 0.05;
        y0 -= pad; y1 += pad;
    }
    const double xs = niceStep(x1 - x0, 8);
    x0 = std::floor(x0 / xs + 1e-9) * xs;
    x1 = std::ceil(x1 / xs - 1e-9) * xs;

    // --- Легенда справа от поля графика, если кривых несколько
    int nSolid = 0, lw = 0;
    bool anyDashed = false;
    for (const Curve &c : m_curves) {
        anyDashed = anyDashed || c.dashed;
        if (!c.dashed) { nSolid++; lw = qMax(lw, fm.horizontalAdvance(c.label)); }
    }
    const bool legend = nSolid >= 2;
    const int legendW = legend ? qMax(lw + 40, fm.horizontalAdvance("пунктир:")) + 12 : 0;

    // --- Поле графика
    const int left = fm.horizontalAdvance("-999 мкА") + 16;
    const int top = fm.height() + 12;
    const int bottom = 2 * fm.height() + 14;
    const QRectF plot(left, top, width() - left - 16 - legendW, height() - top - bottom);
    m_plot = plot; m_x0 = x0; m_x1 = x1; m_y0 = y0; m_y1 = y1;
    auto X = [&](double x) { return plot.left() + (x - x0) / (x1 - x0) * plot.width(); };
    auto Y = [&](double y) { return plot.bottom() - (y - y0) / (y1 - y0) * plot.height(); };

    // --- Сетка и подписи
    p.setPen(QPen(QColor("#e3e3e3"), 1));
    QPen textPen(QColor("#333"));
    for (double x = x0; x <= x1 + xs * 0.01; x += xs) {
        p.setPen(QPen(QColor("#e3e3e3"), 1));
        p.drawLine(QPointF(X(x), plot.top()), QPointF(X(x), plot.bottom()));
        p.setPen(textPen);
        const QString s = QString::number(std::fabs(x) < xs * 1e-6 ? 0.0 : x, 'g', 4).replace('.', ',');
        p.drawText(QRectF(X(x) - 40, plot.bottom() + 3, 80, fm.height()), Qt::AlignHCenter, s);
    }
    if (m_yMode == LogAbs) {
        const int decades = int(y1 - y0);
        const int every = decades > 10 ? 2 : 1;
        for (int d = int(y0); d <= int(y1); ++d) {
            p.setPen(QPen(QColor("#e3e3e3"), 1));
            if (decades <= 6 && d < int(y1))     // промежуточные линии 2...9
                for (int k = 2; k <= 9; ++k) {
                    const double yy = d + std::log10(double(k));
                    p.setPen(QPen(QColor("#f1f1f1"), 1));
                    p.drawLine(QPointF(plot.left(), Y(yy)), QPointF(plot.right(), Y(yy)));
                }
            p.setPen(QPen(QColor("#d6d6d6"), 1));
            p.drawLine(QPointF(plot.left(), Y(d)), QPointF(plot.right(), Y(d)));
            if ((d - int(y0)) % every == 0) {
                p.setPen(textPen);
                p.drawText(QRectF(0, Y(d) - fm.height() / 2.0, left - 6, fm.height()),
                           Qt::AlignRight | Qt::AlignVCenter, formatY(d));
            }
        }
    } else {
        const double ys = niceStep(y1 - y0, 6);
        for (double y = std::ceil(y0 / ys) * ys; y <= y1; y += ys) {
            p.setPen(QPen(std::fabs(y) < ys * 1e-6 ? QColor("#b0b0b0") : QColor("#e3e3e3"), 1));
            p.drawLine(QPointF(plot.left(), Y(y)), QPointF(plot.right(), Y(y)));
            p.setPen(textPen);
            p.drawText(QRectF(0, Y(y) - fm.height() / 2.0, left - 6, fm.height()),
                       Qt::AlignRight | Qt::AlignVCenter, formatY(std::fabs(y) < ys * 1e-6 ? 0.0 : y));
        }
    }
    p.setPen(QPen(QColor("#777"), 1));
    p.drawRect(plot);

    // --- Подписи осей и заголовок
    p.setPen(textPen);
    p.drawText(QRectF(plot.left(), plot.bottom() + fm.height() + 6, plot.width(), fm.height()),
               Qt::AlignHCenter, m_xLabel);
    p.drawText(QRectF(4, 2, width() - 8, fm.height() + 4), Qt::AlignLeft | Qt::AlignVCenter, yLabel());
    if (!m_title.isEmpty())
        p.drawText(QRectF(4, 2, width() - 8, fm.height() + 4), Qt::AlignRight | Qt::AlignVCenter, m_title);

    if (!haveData) {
        p.setPen(QColor("#999"));
        p.drawText(plot, Qt::AlignCenter, "Нет данных. График строится по мере прихода точек.");
        return;
    }

    // --- Кривые
    p.save();
    p.setClipRect(plot.adjusted(-3, -3, 3, 3));
    for (const Curve &c : m_curves) {
        QPen pen(color(c.group), 1.8);
        if (c.dashed) pen.setStyle(Qt::DashLine);
        QPainterPath path;
        bool started = false;
        for (const QPointF &pt : c.pts) {
            double y;
            if (!yValue(pt.y(), &y)) { started = false; continue; }
            const QPointF q(X(pt.x()), Y(y));
            if (!started) { path.moveTo(q); started = true; }
            else path.lineTo(q);
        }
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.setBrush(color(c.group));
        p.setPen(Qt::NoPen);
        for (const QPointF &pt : c.pts) {
            double y;
            if (yValue(pt.y(), &y))
                p.drawEllipse(QPointF(X(pt.x()), Y(y)), 2.2, 2.2);
        }
    }
    p.restore();

    // --- Легенда (сплошные кривые; пунктир = обратный ход)
    if (legend) {
        int ly = int(plot.top());
        const int lx = int(plot.right()) + 12;
        for (const Curve &c : m_curves) {
            if (c.dashed) continue;
            p.setPen(QPen(color(c.group), 2));
            p.drawLine(QPointF(lx, ly + fm.height() / 2.0), QPointF(lx + 24, ly + fm.height() / 2.0));
            p.setPen(textPen);
            p.drawText(QPointF(lx + 30, ly + fm.ascent()), c.label);
            ly += fm.height() + 3;
        }
        if (anyDashed) {
            p.setPen(QColor("#555"));
            p.drawText(QPointF(lx, ly + 6 + fm.ascent()), "пунктир:");
            p.drawText(QPointF(lx, ly + 6 + fm.height() + fm.ascent()), "обратный ход");
        }
    } else if (anyDashed) {
        p.setPen(QColor("#555"));
        p.drawText(QRectF(plot.left(), 2, plot.width(), fm.height() + 4), Qt::AlignHCenter | Qt::AlignVCenter,
                   "сплошная: прямой ход, пунктир: обратный");
    }

    // --- Подсказка под курсором
    if (plot.contains(m_mouse)) {
        const double mx = x0 + (m_mouse.x() - plot.left()) / plot.width() * (x1 - x0);
        const double my = y0 + (plot.bottom() - m_mouse.y()) / plot.height() * (y1 - y0);
        p.setPen(QPen(QColor("#999"), 1, Qt::DotLine));
        p.drawLine(QPointF(m_mouse.x(), plot.top()), QPointF(m_mouse.x(), plot.bottom()));
        p.drawLine(QPointF(plot.left(), m_mouse.y()), QPointF(plot.right(), m_mouse.y()));
        const QString tip = QString("%1 В;  %2").arg(QString::number(mx, 'f', 3).replace('.', ','), formatY(my));
        const QRectF tr(m_mouse.x() + 10, m_mouse.y() - fm.height() - 8, fm.horizontalAdvance(tip) + 10, fm.height() + 4);
        p.setPen(QColor("#888"));
        p.setBrush(QColor(255, 255, 230));
        p.drawRect(tr);
        p.setPen(textPen);
        p.drawText(tr, Qt::AlignCenter, tip);
    }
}

void PlotWidget::mouseMoveEvent(QMouseEvent *e)
{
    m_mouse = e->pos();
    update();
}

void PlotWidget::leaveEvent(QEvent *)
{
    m_mouse = QPointF(-1, -1);
    update();
}
