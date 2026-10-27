#include "MapWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QUrlQuery>
#include <QtMath>

#include "Backend.h"
#include "Theme.h"

namespace {
const double MaxScale = 32.0;
const int PreviewPixels = 2048;
}  // namespace

MapWidget::MapWidget(Backend *backend, QWidget *parent) : QWidget(parent), m_backend(backend)
{
    setMouseTracking(true);
    setMinimumSize(480, 320);
    m_detailTimer.setSingleShot(true);
    m_detailTimer.setInterval(250);
    connect(&m_detailTimer, &QTimer::timeout, this, &MapWidget::requestDetail);
}

QPointF MapWidget::toScreen(const QPointF &imagePoint) const
{
    return (imagePoint - m_centre) * m_scale + QPointF(width() / 2.0, height() / 2.0);
}

QPointF MapWidget::toImage(const QPointF &screenPoint) const
{
    return (screenPoint - QPointF(width() / 2.0, height() / 2.0)) / m_scale + m_centre;
}

double MapWidget::fitScale() const
{
    if (m_imageSize.isEmpty())
        return 1.0;
    return qMin(width() * 0.94 / m_imageSize.width(), height() * 0.94 / m_imageSize.height());
}

void MapWidget::setImage(const QString &id, int width, int height, double pixelMetres)
{
    m_imageId = id;
    m_imageSize = QSize(width, height);
    m_pixelMetres = pixelMetres;
    m_hasArea = false;
    m_overlayA = m_overlayB = QImage();
    m_regions.clear();
    fitImage();
    reloadImage();
}

void MapWidget::reloadImage()
{
    const int generation = ++m_generation;
    m_preview = m_detail = QImage();
    m_previewError.clear();
    update();
    if (!hasImage())
        return;
    QUrlQuery q;
    q.addQueryItem("image", m_imageId);
    q.addQueryItem("product", "true_color");
    q.addQueryItem("max_px", QString::number(PreviewPixels));
    m_backend->getBytes("/api/render.png", q, [this, generation](bool ok, const QByteArray &body, const QString &error) {
        if (generation != m_generation)
            return;
        if (ok)
            m_preview.loadFromData(body);
        else
            m_previewError = error;
        viewChanged();
    });
}

void MapWidget::viewChanged()
{
    m_detailTimer.start();
    update();
}

// When the view is zoomed past what the preview can show, fetch the visible part at screen resolution.
void MapWidget::requestDetail()
{
    if (!hasImage() || m_preview.isNull())
        return;
    const double previewScale = double(m_preview.width()) / m_imageSize.width();
    if (m_scale <= previewScale * 1.15) {
        m_detail = QImage();
        update();
        return;
    }
    const QRectF visible = QRectF(toImage(QPointF(0, 0)), toImage(QPointF(width(), height())))
                               .intersected(QRectF(QPointF(0, 0), QSizeF(m_imageSize)));
    const QRect window = visible.toAlignedRect();
    if (window.width() < 2 || window.height() < 2)
        return;
    const int generation = m_generation;
    QUrlQuery q;
    q.addQueryItem("image", m_imageId);
    q.addQueryItem("product", "true_color");
    q.addQueryItem("window", QStringLiteral("%1,%2,%3,%4").arg(window.left()).arg(window.top())
                                 .arg(window.left() + window.width()).arg(window.top() + window.height()));
    q.addQueryItem("max_px", QString::number(qBound(256, qMax(width(), height()), PreviewPixels)));
    m_backend->getBytes("/api/render.png", q, [this, generation, window](bool ok, const QByteArray &body, const QString &) {
        if (!ok || generation != m_generation)
            return;
        if (m_detail.loadFromData(body))
            m_detailRect = window;
        update();
    });
}

void MapWidget::zoomBy(int steps)
{
    m_scale = qBound(fitScale() * 0.5, m_scale * qPow(1.5, steps), MaxScale);
    viewChanged();
}

void MapWidget::fitImage()
{
    m_scale = fitScale();
    m_centre = QPointF(m_imageSize.width() / 2.0, m_imageSize.height() / 2.0);
    viewChanged();
}

void MapWidget::fitArea()
{
    if (!m_hasArea)
        return fitImage();
    const QRectF area = QRectF(m_areaA, m_areaB).normalized();
    m_centre = area.center();
    m_scale = qMin(MaxScale, qMin(width() * 0.7 / area.width(), height() * 0.7 / area.height()));
    viewChanged();
}

void MapWidget::centreOn(double col, double row)
{
    m_centre = QPointF(col, row);
    m_scale = qMax(m_scale, 4.0);
    viewChanged();
}

void MapWidget::setDrawMode(bool on)
{
    if (m_drawMode == on)
        return;
    m_drawMode = on;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    emit drawModeChanged(on);
}

void MapWidget::setArea(int col0, int row0, int col1, int row1)
{
    m_areaA = QPointF(col0, row0);
    m_areaB = QPointF(col1, row1);
    m_hasArea = true;
    m_overlayA = m_overlayB = QImage();
    m_regions.clear();
    emit areaChanged();
    update();
}

QRect MapWidget::areaRect() const
{
    const QRect whole(QPoint(0, 0), m_imageSize);
    return m_hasArea ? QRectF(m_areaA, m_areaB).normalized().toAlignedRect().intersected(whole) : whole;
}

QString MapWidget::areaWindow() const
{
    const QRect r = areaRect();
    return QStringLiteral("%1,%2,%3,%4").arg(r.left()).arg(r.top()).arg(r.left() + r.width()).arg(r.top() + r.height());
}

void MapWidget::setOverlays(const QImage &a, const QImage &b)
{
    m_overlayA = a;
    m_overlayB = b;
    m_regions.clear();
    m_swipe = 0.5;
    update();
}

void MapWidget::setRegions(const QVector<MapRegion> &regions)
{
    m_regions = regions;
    update();
}

void MapWidget::drawRegions(QPainter &p)
{
    p.setRenderHint(QPainter::Antialiasing);
    for (const MapRegion &region : m_regions) {
        QPolygonF outline;
        for (const QPointF &point : region.polygon)
            outline << toScreen(point);
        QColor fill = region.color;
        fill.setAlpha(45);
        p.setPen(QPen(region.color.lighter(125), 2));
        p.setBrush(fill);
        p.drawPolygon(outline);
    }
    // Labels on top of every outline: the colour is on the border, the text stays in ink colours.
    QFont bold = font();
    bold.setBold(true);
    const QFontMetrics fm(font()), fmBold(bold);
    for (const MapRegion &region : m_regions) {
        const int w = qMax(fmBold.horizontalAdvance(region.label), fm.horizontalAdvance(region.detail)) + 22;
        const QRectF box(toScreen(region.anchor) - QPointF(w / 2.0, 22), QSizeF(w, 44));
        p.setPen(QPen(region.color.lighter(125), 1.5));
        p.setBrush(QColor(8, 18, 33, 225));
        p.drawRoundedRect(box, 7, 7);
        p.setPen(Theme::Text);
        p.setFont(bold);
        p.drawText(box.adjusted(11, 5, -8, -22), Qt::AlignLeft | Qt::AlignVCenter, region.label);
        p.setFont(font());
        p.setPen(QColor("#c5d3ea"));
        p.drawText(box.adjusted(11, 22, -8, -5), Qt::AlignLeft | Qt::AlignVCenter, region.detail);
    }
}

void MapWidget::setOverlayOpacity(double opacity)
{
    m_opacity = opacity;
    update();
}

void MapWidget::setOverlayVisible(bool visible)
{
    m_overlayVisible = visible;
    update();
}

QRectF MapWidget::areaScreenRect() const
{
    const QRectF r = areaRect();
    return QRectF(toScreen(r.topLeft()), toScreen(r.bottomRight()));
}

double MapWidget::swipeX() const
{
    const QRectF r = areaScreenRect();
    return r.left() + r.width() * m_swipe;
}

void MapWidget::drawHud(QPainter &p)
{
    QStringList parts;
    const QPointF at = toImage(m_cursor);
    if (m_cursor.x() >= 0 && QRectF(QPointF(0, 0), QSizeF(m_imageSize)).contains(at))
        parts << tr("x %1, y %2").arg(int(at.x())).arg(int(at.y()));
    parts << tr("%1 × %2 px").arg(m_imageSize.width()).arg(m_imageSize.height());
    parts << tr("Zoom: %1%").arg(qRound(m_scale * 100));
    const QString text = parts.join(QStringLiteral("     "));

    // Scale bar, when the ground size of a pixel is known.
    double barMetres = 0;
    int barPx = 0;
    if (m_pixelMetres > 0) {
        const double metresPerScreenPixel = m_pixelMetres / m_scale;
        for (double scale = 0.1; scale <= 1e6; scale *= 10)
            for (double step : {1.0, 2.0, 5.0})
                if (step * scale / metresPerScreenPixel <= 100)
                    barMetres = qMax(barMetres, step * scale);
        barPx = qRound(barMetres / metresPerScreenPixel);
    }
    const QString barText = barMetres >= 1000 ? tr("%1 km").arg(barMetres / 1000) : tr("%1 m").arg(barMetres);

    const QFontMetrics fm(font());
    const int w = fm.horizontalAdvance(text) + 24 + (barPx ? barPx + fm.horizontalAdvance(barText) + 30 : 0);
    const QRectF box(12, height() - 46, w, 34);
    p.setPen(QColor("#1d3a5f"));
    p.setBrush(QColor(8, 18, 33, 225));
    p.drawRoundedRect(box, 8, 8);
    p.setPen(Theme::Text);
    p.drawText(box.adjusted(12, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    if (barPx) {
        const double x = box.left() + 12 + fm.horizontalAdvance(text) + 20, y = box.center().y() + 5;
        p.drawLine(QPointF(x, y), QPointF(x + barPx, y));
        p.drawLine(QPointF(x, y - 5), QPointF(x, y));
        p.drawLine(QPointF(x + barPx, y - 5), QPointF(x + barPx, y));
        p.drawText(QRectF(x + barPx + 8, box.top(), 200, box.height()), Qt::AlignVCenter | Qt::AlignLeft, barText);
    }
}

void MapWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor("#050b14"));
    if (!hasImage()) {
        p.setPen(Theme::Muted);
        QFont big = font();
        big.setPixelSize(16);
        p.setFont(big);
        p.drawText(rect(), Qt::AlignCenter, tr("Open an image to start"));
        return;
    }
    const QRectF imageRect(toScreen(QPointF(0, 0)), toScreen(QPointF(m_imageSize.width(), m_imageSize.height())));
    // Smooth when shrinking; show true pixels when magnified well past native size.
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_scale < 3.0);
    if (!m_preview.isNull())
        p.drawImage(imageRect, m_preview);
    if (!m_detail.isNull())
        p.drawImage(QRectF(toScreen(m_detailRect.topLeft()), toScreen(m_detailRect.bottomRight())), m_detail);
    if (m_preview.isNull()) {
        p.setPen(QPen(QColor("#1d3a5f"), 1));
        p.drawRect(imageRect);
        p.setPen(m_previewError.isEmpty() ? Theme::Muted : QColor("#ff8b8b"));
        p.drawText(rect().adjusted(60, 0, -60, 0), Qt::AlignCenter | Qt::TextWordWrap,
                   m_previewError.isEmpty() ? tr("Loading image…") : m_previewError);
    }

    const QRectF area = areaScreenRect();
    if (m_overlayVisible && !m_overlayA.isNull()) {
        p.setOpacity(m_opacity);
        if (m_overlayB.isNull()) {
            p.drawImage(area, m_overlayA);
        } else {
            const double split = swipeX();
            p.save();
            p.setClipRect(QRectF(area.left(), area.top(), split - area.left(), area.height()));
            p.drawImage(area, m_overlayA);
            p.setClipRect(QRectF(split, area.top(), area.right() - split, area.height()));
            p.drawImage(area, m_overlayB);
            p.restore();
        }
        p.setOpacity(1.0);
    }
    if (m_hasArea) {
        p.setPen(QPen(QColor("#4cc3ff"), 1.5));
        p.setBrush(m_overlayA.isNull() || !m_overlayVisible ? QColor(47, 140, 255, 40) : QColor(Qt::transparent));
        p.drawRect(area);
        p.setBrush(QColor("#4cc3ff"));
        for (const QPointF &corner : {area.topLeft(), area.topRight(), area.bottomLeft(), area.bottomRight()})
            p.drawRect(QRectF(corner - QPointF(3, 3), QSizeF(6, 6)));
    }
    if (!m_overlayB.isNull() && m_overlayVisible) {
        const double split = swipeX();
        p.setPen(QPen(Qt::white, 2));
        p.drawLine(QPointF(split, area.top()), QPointF(split, area.bottom()));
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(split, area.center().y()), 7, 7);
        p.setPen(Theme::Text);
        p.drawText(QRectF(area.left() + 8, area.top() + 6, 40, 20), "A");
        p.drawText(QRectF(area.right() - 20, area.top() + 6, 20, 20), "B");
    }
    if (m_overlayVisible)
        drawRegions(p);
    p.setRenderHint(QPainter::Antialiasing);
    drawHud(p);
}

void MapWidget::resizeEvent(QResizeEvent *)
{
    if (hasImage() && m_preview.isNull() && m_scale == 1.0)
        fitImage();
    viewChanged();
}

void MapWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || !hasImage())
        return;
    m_pressPos = m_lastPos = e->pos();
    const bool onDivider = !m_overlayB.isNull() && qAbs(e->pos().x() - swipeX()) < 8
                           && areaScreenRect().adjusted(-8, 0, 8, 0).contains(e->pos());
    if (m_drawMode) {
        m_drag = Drag::Draw;
        m_hasArea = true;
        m_areaA = m_areaB = toImage(e->pos());
        m_overlayA = m_overlayB = QImage();
        m_regions.clear();
    } else {
        m_drag = onDivider ? Drag::Swipe : Drag::Pan;
    }
}

void MapWidget::mouseMoveEvent(QMouseEvent *e)
{
    m_cursor = e->pos();
    if (m_drag == Drag::Draw) {
        m_areaB = toImage(e->pos());
    } else if (m_drag == Drag::Swipe) {
        const QRectF area = areaScreenRect();
        m_swipe = qBound(0.0, (e->pos().x() - area.left()) / area.width(), 1.0);
    } else if (m_drag == Drag::Pan) {
        m_centre -= QPointF(e->pos() - m_lastPos) / m_scale;
        m_detailTimer.start();
    } else if (!m_drawMode) {
        const bool onDivider = !m_overlayB.isNull() && qAbs(e->pos().x() - swipeX()) < 8
                               && areaScreenRect().adjusted(-8, 0, 8, 0).contains(e->pos());
        setCursor(onDivider ? Qt::SplitHCursor : Qt::ArrowCursor);
    }
    m_lastPos = e->pos();
    update();
}

void MapWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const Drag drag = m_drag;
    m_drag = Drag::None;
    const bool moved = (e->pos() - m_pressPos).manhattanLength() > 4;
    if (drag == Drag::Draw) {
        // A click without a drag, or a rectangle outside the image, clears the area (whole image again).
        m_hasArea = moved && areaRect().width() > 1 && areaRect().height() > 1;
        setDrawMode(false);
        emit areaChanged();
    } else if (drag == Drag::Pan && !moved) {
        const QPointF at = toImage(e->pos());
        emit clicked(at.x(), at.y(), e->globalPos());
    }
    update();
}

void MapWidget::wheelEvent(QWheelEvent *e)
{
    if (!hasImage())
        return;
    const QPointF anchor = toImage(e->position());  // keep the point under the cursor fixed
    m_scale = qBound(fitScale() * 0.5, m_scale * (e->angleDelta().y() > 0 ? 1.25 : 0.8), MaxScale);
    m_centre += anchor - toImage(e->position());
    viewChanged();
}

void MapWidget::leaveEvent(QEvent *)
{
    m_cursor = {-1, -1};
    update();
}
