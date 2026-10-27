#include "Charts.h"

#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QtMath>
#include <algorithm>
#include <cmath>

#include "Theme.h"

namespace {
const double RingWidth = 18;
const double GapDegrees = 2;

QString areaText(double value, const QString &unit) { return QLocale().toString(value, 'f', unit == "px" ? 0 : 2) + " " + unit; }
}  // namespace

DonutChart::DonutChart(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(150);
}

void DonutChart::setData(const QVector<ClassShare> &classes, const QString &unit)
{
    m_classes = classes;
    m_unit = unit;
    update();
}

QRectF DonutChart::ringRect() const
{
    const double side = qMin(height() - 16.0, width() * 0.42);
    return QRectF(8, (height() - side) / 2, side, side);
}

void DonutChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_classes.isEmpty()) {
        p.setPen(Theme::Muted);
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, tr("Run an index, land cover or change analysis to see the class distribution."));
        return;
    }
    const QRectF ring = ringRect().adjusted(RingWidth / 2, RingWidth / 2, -RingWidth / 2, -RingWidth / 2);
    const ClassShare *largest = &m_classes.first();
    double start = 90;  // 12 o'clock, clockwise
    for (const ClassShare &c : m_classes) {
        if (c.percent > largest->percent)
            largest = &c;
        const double span = 360.0 * c.percent / 100.0;
        if (span > GapDegrees) {
            p.setPen(QPen(c.color, RingWidth, Qt::SolidLine, Qt::FlatCap));
            p.drawArc(ring, qRound((start - GapDegrees / 2) * 16), -qRound((span - GapDegrees) * 16));
        }
        start -= span;
    }
    QFont big = font();
    big.setPixelSize(17);
    big.setBold(true);
    p.setFont(big);
    p.setPen(Theme::Text);
    p.drawText(ring.adjusted(0, -10, 0, -10), Qt::AlignCenter, QStringLiteral("%1%").arg(largest->percent, 0, 'f', 0));
    p.setFont(font());
    p.setPen(Theme::Muted);
    const QRectF nameBox = ring.adjusted(RingWidth, ring.height() / 2 + 2, -RingWidth, 0);
    p.drawText(nameBox, Qt::AlignHCenter | Qt::AlignTop,
               QFontMetrics(font()).elidedText(largest->label, Qt::ElideRight, int(nameBox.width())));

    // Legend: swatch carries the identity, text stays in ink colours.
    const double left = ringRect().right() + 18;
    const double rowHeight = qMin(24.0, (height() - 8.0) / m_classes.size());
    double y = (height() - rowHeight * m_classes.size()) / 2;
    for (const ClassShare &c : m_classes) {
        p.setPen(Qt::NoPen);
        p.setBrush(c.color);
        p.drawEllipse(QPointF(left + 5, y + rowHeight / 2), 4.5, 4.5);
        const QRectF text(left + 18, y, width() - left - 26, rowHeight);
        const QString pct = QStringLiteral("%1%").arg(c.percent, 0, 'f', 1);
        p.setPen(Theme::Text);
        p.drawText(text, Qt::AlignVCenter | Qt::AlignRight, pct);
        p.setPen(QColor("#c5d3ea"));
        p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(font()).elidedText(c.label, Qt::ElideRight, int(text.width()) - 54));
        y += rowHeight;
    }
}

void DonutChart::mouseMoveEvent(QMouseEvent *e)
{
    const QRectF ring = ringRect();
    const QPointF d = e->pos() - ring.center();
    const double radius = std::hypot(d.x(), d.y());
    if (m_classes.isEmpty() || radius > ring.width() / 2 || radius < ring.width() / 2 - RingWidth - 4) {
        QToolTip::hideText();
        return;
    }
    double clockwise = qRadiansToDegrees(qAtan2(d.x(), -d.y()));  // 0 at 12 o'clock
    if (clockwise < 0)
        clockwise += 360;
    double edge = 0;
    for (const ClassShare &c : m_classes) {
        edge += 360.0 * c.percent / 100.0;
        if (clockwise <= edge) {
            QToolTip::showText(e->globalPos(), QStringLiteral("%1: %2 (%3%)").arg(c.label, areaText(c.km2, m_unit)).arg(c.percent, 0, 'f', 1), this);
            return;
        }
    }
}

HistogramChart::HistogramChart(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(150);
}

void HistogramChart::setData(const QVector<double> &edges, const QVector<double> &km2, const QString &unit)
{
    m_unit = unit;
    m_edges = edges;
    m_km2 = km2;
    m_hover = -1;
    update();
}

QRectF HistogramChart::plotRect() const
{
    return QRectF(10, 10, width() - 20, height() - 34);
}

void HistogramChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (m_km2.isEmpty()) {
        p.setPen(Theme::Muted);
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, tr("A histogram is available for index and change results."));
        return;
    }
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF plot = plotRect();
    const double peak = qMax(1e-9, *std::max_element(m_km2.begin(), m_km2.end()));
    const double step = plot.width() / m_km2.size();
    p.setPen(Qt::NoPen);
    for (int i = 0; i < m_km2.size(); ++i) {
        const double h = (plot.height() - 4) * m_km2[i] / peak;
        if (h < 0.5)
            continue;
        p.setBrush(i == m_hover ? QColor("#7db8ff") : QColor("#3987e5"));
        QPainterPath bar;  // rounded top, square on the baseline
        const QRectF r(plot.left() + i * step + 1, plot.bottom() - h, step - 2, h);
        const double radius = qMin(2.0, h);
        bar.moveTo(r.bottomLeft());
        bar.lineTo(r.left(), r.top() + radius);
        bar.quadTo(r.topLeft(), QPointF(r.left() + radius, r.top()));
        bar.lineTo(r.right() - radius, r.top());
        bar.quadTo(r.topRight(), QPointF(r.right(), r.top() + radius));
        bar.lineTo(r.bottomRight());
        p.drawPath(bar);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QColor("#2a4263"));
    p.drawLine(plot.bottomLeft(), plot.bottomRight());
    p.setPen(Theme::Muted);
    QFont small = font();
    small.setPixelSize(11);
    p.setFont(small);
    for (double tick : {-1.0, -0.5, 0.0, 0.5, 1.0}) {
        const double x = plot.left() + (tick + 1) / 2 * plot.width();
        const int align = tick == -1.0 ? Qt::AlignLeft : tick == 1.0 ? Qt::AlignRight : Qt::AlignHCenter;
        const QRectF box(tick == -1.0 ? x : tick == 1.0 ? x - 40 : x - 20, plot.bottom() + 4, 40, 16);
        p.drawText(box, align | Qt::AlignTop, QString::number(tick));
    }
}

void HistogramChart::mouseMoveEvent(QMouseEvent *e)
{
    const QRectF plot = plotRect();
    int index = -1;
    if (!m_km2.isEmpty() && plot.adjusted(0, 0, 0, 20).contains(e->pos()))
        index = qBound(0, int((e->pos().x() - plot.left()) / plot.width() * m_km2.size()), m_km2.size() - 1);
    if (index != m_hover) {
        m_hover = index;
        update();
    }
    if (index >= 0 && index + 1 < m_edges.size())
        QToolTip::showText(e->globalPos(), QStringLiteral("%1 to %2: %3").arg(m_edges[index]).arg(m_edges[index + 1]).arg(areaText(m_km2[index], m_unit)), this);
    else
        QToolTip::hideText();
}

void HistogramChart::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}
