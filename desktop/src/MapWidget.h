#pragma once
#include <QColor>
#include <QImage>
#include <QPolygonF>
#include <QTimer>
#include <QVector>
#include <QWidget>

class Backend;

// An outlined, labelled result region (image pixel coordinates), e.g. "Forest area, 2.34 km²".
struct MapRegion {
    QString label, detail;
    QColor color;
    QPolygonF polygon;
    QPointF anchor;  // a point inside the polygon, where the label goes
};

// Viewer for one opened image, in that image's pixel coordinates: pan, zoom, a drawable
// analysis area, result overlays and an A|B swipe. Zooming in fetches a sharper picture
// of just the visible part from the engine.
class MapWidget : public QWidget {
    Q_OBJECT
public:
    explicit MapWidget(Backend *backend, QWidget *parent = nullptr);

    void setImage(const QString &id, int width, int height, double pixelMetres);  // empty id shows nothing
    void reloadImage();  // the band assignment changed
    bool hasImage() const { return !m_imageId.isEmpty(); }

    void zoomBy(int steps);
    void fitImage();
    void fitArea();
    void centreOn(double col, double row);
    void setDrawMode(bool on);

    void setArea(int col0, int row0, int col1, int row1);
    bool hasArea() const { return m_hasArea; }
    QRect areaRect() const;        // the drawn area, or the whole image when none is drawn
    QString areaWindow() const;    // "col0,row0,col1,row1" of areaRect()

    // One image fills the area; with a second image the view becomes a swipe (A left, B right).
    void setOverlays(const QImage &a, const QImage &b = QImage());
    void setRegions(const QVector<MapRegion> &regions);
    void setOverlayOpacity(double opacity);
    void setOverlayVisible(bool visible);
    const QImage &overlay() const { return m_overlayB.isNull() ? m_overlayA : m_overlayB; }

signals:
    void areaChanged();
    void drawModeChanged(bool on);
    void clicked(double col, double row, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    enum class Drag { None, Pan, Draw, Swipe };

    QPointF toScreen(const QPointF &imagePoint) const;
    QPointF toImage(const QPointF &screenPoint) const;
    QRectF areaScreenRect() const;
    double swipeX() const;
    double fitScale() const;
    void viewChanged();
    void requestDetail();
    void drawHud(QPainter &p);
    void drawRegions(QPainter &p);

    Backend *m_backend;
    QString m_imageId;
    QSize m_imageSize;
    double m_pixelMetres = 0;
    QImage m_preview;        // the whole image, at most 2048 px on the long side
    QString m_previewError;
    QImage m_detail;         // the visible part at screen resolution, when zoomed in
    QRectF m_detailRect;
    QTimer m_detailTimer;
    int m_generation = 0;

    double m_scale = 1.0;    // screen pixels per image pixel
    QPointF m_centre;

    bool m_hasArea = false;
    QPointF m_areaA, m_areaB;  // opposite corners, image coordinates
    QImage m_overlayA, m_overlayB;
    QVector<MapRegion> m_regions;
    double m_opacity = 1.0;
    bool m_overlayVisible = true;
    double m_swipe = 0.5;

    Drag m_drag = Drag::None;
    bool m_drawMode = false;
    QPoint m_pressPos, m_lastPos;
    QPointF m_cursor{-1, -1};
};
