#pragma once
#include <QBoxLayout>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QVariant>

// Small widget factories shared by the window's source files.
namespace ui {

inline void restyle(QWidget *w, const char *property, const QVariant &value)
{
    w->setProperty(property, value);
    w->style()->unpolish(w);
    w->style()->polish(w);
}

inline QLabel *label(const QString &text, const char *role = nullptr)
{
    auto *l = new QLabel(text);
    if (role)
        l->setProperty("role", role);
    return l;
}

inline QPushButton *button(const QString &text, const char *kind, bool checkable = false)
{
    auto *b = new QPushButton(text);
    b->setProperty("kind", kind);
    b->setCheckable(checkable);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

// Card frame with an icon + title row; returns the layout to fill. `corner` sits at the right of the title.
inline QVBoxLayout *card(QFrame *&frame, QChar icon, const QString &title, QWidget *corner = nullptr)
{
    frame = new QFrame;
    frame->setObjectName("card");
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);
    auto *head = new QHBoxLayout;
    auto *glyph = new QLabel(QString(icon));
    glyph->setObjectName("cardIcon");
    auto *name = new QLabel(title);
    name->setObjectName("cardTitle");
    head->addWidget(glyph);
    head->addWidget(name);
    head->addStretch();
    if (corner)
        head->addWidget(corner);
    layout->addLayout(head);
    return layout;
}

// A Segoe MDL2 Assets glyph as an icon, for buttons whose text uses the normal font.
inline QIcon glyphIcon(QChar glyph, const QColor &color, int size = 18)
{
    QPixmap pm(size * 2, size * 2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    QFont font("Segoe MDL2 Assets");
    font.setPixelSize(size * 2 - 6);
    p.setFont(font);
    p.setPen(color);
    p.drawText(pm.rect(), Qt::AlignCenter, QString(glyph));
    return QIcon(pm);
}

inline QString number(double v, int decimals) { return QLocale().toString(v, 'f', decimals); }

}  // namespace ui
