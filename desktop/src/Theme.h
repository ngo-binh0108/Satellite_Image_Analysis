#pragma once
#include <QColor>
#include <QDir>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QString>
#include <QVector>

// Dark navy theme taken from the UI reference.
namespace Theme {
const QColor Background("#060e1a");
const QColor Card("#0b1728");
const QColor Border("#17304f");
const QColor Text("#e8f0fc");
const QColor Muted("#8094b4");
const QColor Accent("#2f8cff");
const QColor Grid("#1a2d47");

// Segoe MDL2 Assets glyphs (ships with Windows 10 and later).
namespace Icon {
const QChar Add(0xE710), Remove(0xE738), Target(0xE81D), Area(0xE739), Layers(0xE81E), Search(0xE721),
    Download(0xE896), Picture(0xEB9F), Play(0xE768), Globe(0xE774), Analysis(0xE9D9), Results(0xE9F9),
    Home(0xE80F), Models(0xE950), Folder(0xE8B7), Storage(0xEDA2), Settings(0xE713);
}

// Qt style sheets can only take sub-control glyphs from image files, so draw them once.
inline QString glyphFile(const QString &name, const QColor &color, const QVector<QPointF> &points)
{
    const QString path = QDir::temp().filePath("earthview_" + name + ".png");
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(points.constData(), points.size());
    p.end();
    pm.save(path);
    return QString(path).replace('\\', '/');
}

inline QString styleSheet()
{
    const QString arrow = glyphFile("arrow", QColor("#8094b4"), {{7, 12}, {16, 21}, {25, 12}});
    const QString tick = glyphFile("tick", QColor("#ffffff"), {{7, 17}, {13, 23}, {25, 9}});
    return QStringLiteral(R"(* { font-family: "Segoe UI"; font-size: 13px; color: #e8f0fc; }
QMainWindow, #root { background: #060e1a; }
QToolTip { background: #0b1728; color: #e8f0fc; border: 1px solid #2f8cff; padding: 4px 6px; }

#header { background: #081221; border-bottom: 1px solid #17304f; }
#appName { font-size: 19px; font-weight: 600; }
#appTag, .muted { color: #8094b4; }
#logo { font-family: "Segoe MDL2 Assets"; font-size: 26px; color: #2f8cff; }
#status { background: #0b1728; border: 1px solid #17304f; border-radius: 8px; padding: 6px 12px; color: #8094b4; }

#sidebar { background: #081221; border-right: 1px solid #17304f; }
QPushButton[kind="nav"] { background: transparent; border: none; border-radius: 9px; padding: 11px 12px; text-align: left; color: #c5d3ea; font-size: 14px; }
QPushButton[kind="nav"]:hover { background: #0d1d33; }
QPushButton[kind="nav"]:checked { color: white; font-weight: 600;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0e6f8f, stop:1 #123a7a); }
QPushButton[kind="primary"] { background: #1668c9; border-color: #1668c9; color: white; font-weight: 600; padding: 9px 16px; }
QPushButton[kind="primary"]:hover { background: #2f8cff; }
QPushButton[kind="plain"] { padding: 9px 16px; }
#pageTitle { font-size: 22px; font-weight: 600; }
#card, #mapCard { background: #0b1728; border: 1px solid #17304f; border-radius: 12px; }
#cardTitle { font-size: 15px; font-weight: 600; }
#cardIcon { font-family: "Segoe MDL2 Assets"; font-size: 18px; color: #2f8cff; }
QLabel[role="muted"] { color: #8094b4; }
QLabel[role="section"] { color: #c5d3ea; font-weight: 600; }
QLabel[role="error"] { color: #ff7b7b; }

QLineEdit, QComboBox, QSpinBox, QDateEdit {
    background: #0a1423; border: 1px solid #1d3a5f; border-radius: 8px; padding: 6px 10px;
    selection-background-color: #2f8cff; min-height: 20px;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDateEdit:focus { border-color: #2f8cff; }
#search { border-radius: 18px; padding: 8px 16px; background: #0b1a30; }
QComboBox::drop-down, QDateEdit::drop-down { border: none; width: 22px; }
QComboBox::down-arrow, QDateEdit::down-arrow { image: url(%ARROW%); width: 12px; height: 12px; margin-right: 8px; }
QComboBox QAbstractItemView { background: #0b1728; border: 1px solid #1d3a5f; selection-background-color: #1b4f8f; outline: 0; }
QSpinBox::up-button, QSpinBox::down-button, QDateEdit::up-button, QDateEdit::down-button { width: 0; }
QCalendarWidget QWidget { background: #0b1728; alternate-background-color: #0f2038; }
QCalendarWidget QAbstractItemView { selection-background-color: #2f8cff; }

QPushButton { background: #0f2038; border: 1px solid #1d3a5f; border-radius: 8px; padding: 7px 14px; }
QPushButton:hover { border-color: #2f8cff; }
QPushButton:disabled { color: #50627f; border-color: #14263f; }
QPushButton[kind="tab"] { background: transparent; border: 1px solid transparent; border-radius: 8px; padding: 9px 2px; color: #b5c5df; font-size: 12px; }
QPushButton[kind="tab"]:checked { background: #1668c9; color: white; font-weight: 600; }
QPushButton[kind="segment"] { background: transparent; border: none; border-radius: 6px; padding: 6px 12px; color: #b5c5df; }
QPushButton[kind="segment"]:checked { background: #1668c9; color: white; }
QPushButton[kind="flat"] { background: transparent; border: none; color: #b5c5df; padding: 7px 10px; }
QPushButton[kind="flat"]:hover { color: white; }
QPushButton[kind="flat"]:disabled { color: #50627f; }
#run { border: none; border-radius: 9px; padding: 12px; font-size: 14px; font-weight: 600; color: white;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #22a7f5, stop:0.6 #4d7cf7, stop:1 #9a5cf2); }
#run:disabled { background: #14263f; color: #50627f; }

#overlayBar { background: rgba(8, 18, 33, 225); border: 1px solid #1d3a5f; border-radius: 10px; }
QToolButton[kind="map"] { font-family: "Segoe MDL2 Assets"; font-size: 15px; background: rgba(8, 18, 33, 225);
    border: 1px solid #1d3a5f; border-radius: 8px; min-width: 36px; min-height: 36px; }
QToolButton[kind="map"]:hover { border-color: #2f8cff; }
QToolButton[kind="map"]:checked { background: #1668c9; border-color: #2f8cff; }

QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #2a4a75; border-radius: 4px; background: #0a1423; }
QCheckBox::indicator:checked { background: #2f8cff; border-color: #2f8cff; image: url(%TICK%); }
QSlider::groove:horizontal { height: 4px; background: #1a2d47; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #2f8cff; border-radius: 2px; }
QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; background: #5aa6ff; }

#tile { background: #0a1423; border: 1px solid #17304f; border-radius: 10px; }
#tileValue { font-size: 18px; font-weight: 600; }
#pill { border-radius: 10px; padding: 3px 10px; background: #0f2038; color: #8094b4; }
#pill[state="ok"] { background: #0c3a2b; color: #4fd99b; }
#pill[state="busy"] { background: #10305a; color: #7db8ff; }
#pill[state="error"] { background: #43161a; color: #ff8b8b; }

QTableWidget { background: transparent; border: none; gridline-color: transparent; outline: 0; }
QTableWidget::item { border-bottom: 1px solid #142844; padding: 6px 4px; }
QTableWidget::item:selected { background: #10305a; }
QHeaderView::section { background: #0a1423; color: #8094b4; border: none; padding: 7px 4px; }
QListWidget { background: transparent; border: none; outline: 0; }
QListWidget::item { background: #0a1423; border: 1px solid #17304f; border-radius: 8px; padding: 4px; margin-right: 8px; color: #c5d3ea; }
QListWidget::item:selected { border: 2px solid #35b8ff; background: #0d2340; color: white; }
QScrollBar:horizontal { height: 8px; background: transparent; }
QScrollBar:vertical { width: 8px; background: transparent; }
QScrollBar::handle { background: #1d3a5f; border-radius: 4px; min-width: 24px; min-height: 24px; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; border: none; width: 0; height: 0; }
)").replace("%ARROW%", arrow).replace("%TICK%", tick);
}
}  // namespace Theme
