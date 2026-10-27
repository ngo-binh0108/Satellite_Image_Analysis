#pragma once
#include <QColor>
#include <QVector>
#include <QWidget>

struct ClassShare {
    QString label;
    QColor color;
    double km2 = 0;
    double percent = 0;
};

// Donut of class shares with the largest class named in the middle and a legend on the right.
class DonutChart : public QWidget {
    Q_OBJECT
public:
    explicit DonutChart(QWidget *parent = nullptr);
    void setData(const QVector<ClassShare> &classes, const QString &unit = QStringLiteral("km²"));

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;

private:
    QRectF ringRect() const;
    QVector<ClassShare> m_classes;
    QString m_unit;
};

// Area (km²) per value bin of an index or change layer in the range -1..1.
class HistogramChart : public QWidget {
    Q_OBJECT
public:
    explicit HistogramChart(QWidget *parent = nullptr);
    void setData(const QVector<double> &edges, const QVector<double> &km2, const QString &unit = QStringLiteral("km²"));

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QRectF plotRect() const;
    QVector<double> m_edges, m_km2;
    int m_hover = -1;
    QString m_unit;
};
