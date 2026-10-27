#include "MainWindow.h"

#include <QApplication>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <functional>

#include "Backend.h"
#include "Charts.h"
#include "MapWidget.h"
#include "Theme.h"
#include "UiHelpers.h"

using namespace ui;

namespace {
const QStringList SeriesColors = {"#3987e5", "#d95926", "#199e70", "#c98500", "#d55181", "#008300", "#9085e9", "#e66767"};
const QStringList IndexProducts = {"ndvi", "ndwi", "ndbi", "nbr"};
const QSize ThumbSize(128, 92);

QIcon swatch(const QColor &color)
{
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(1, 1, 10, 10);
    return QIcon(pm);
}

QColor classColor(const QJsonObject &c, int index, bool change)
{
    if (c.contains("color"))
        return QColor(c["color"].toString());
    if (change)
        return QColor(index == 0 ? "#e66767" : index == 1 ? "#5d6f8c" : "#3987e5");  // decrease, stable, increase
    return QColor(SeriesColors[index % SeriesColors.size()]);
}

// An area in km², or a pixel count for images whose ground pixel size is unknown.
QString areaText(double value, const QString &unit)
{
    return number(value, unit == "px" ? 0 : 1) + " " + unit;
}
}  // namespace

struct MainWindow::Job {
    int generation = 0;
    int remaining = 0;
    QImage a, b;
    QJsonObject stats;
    QString error;
    QUrlQuery query;
    QString product, name;
    QElapsedTimer timer;
};

MainWindow::MainWindow(const QString &userName, QWidget *parent)
    : QMainWindow(parent), m_backend(new Backend(this)), m_userName(userName)
{
    setWindowTitle(tr("EarthView AI - Satellite Image Analysis & Processing"));
    resize(1680, 980);
    setStyleSheet(Theme::styleSheet());

    auto *root = new QWidget;
    root->setObjectName("root");
    auto *outer = new QVBoxLayout(root);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(buildHeader());

    auto *workspace = new QWidget;
    auto *body = new QHBoxLayout(workspace);
    body->setContentsMargins(14, 14, 14, 14);
    body->setSpacing(14);
    auto *centre = new QVBoxLayout;
    centre->setSpacing(12);
    centre->addWidget(buildMapCard(), 1);
    auto *lower = new QHBoxLayout;
    lower->setSpacing(12);
    lower->addWidget(buildImageryCard(), 3);
    lower->addWidget(buildChartsCard(), 2);
    centre->addLayout(lower);
    centre->addWidget(buildActionBar());
    auto *side = new QVBoxLayout;
    side->setSpacing(12);
    side->addWidget(buildAnalysisCard());
    side->addWidget(buildResultsCard(), 1);
    auto *sideHost = new QWidget;
    sideHost->setLayout(side);
    side->setContentsMargins(0, 0, 0, 0);
    sideHost->setFixedWidth(460);
    body->addLayout(centre, 1);
    body->addWidget(sideHost);

    m_pages = new QStackedWidget;  // same order as enum Page
    m_pages->addWidget(buildHomePage());
    m_pages->addWidget(workspace);
    m_pages->addWidget(buildModelsPage());
    m_pages->addWidget(buildProjectsPage());
    m_pages->addWidget(buildDataPage());
    m_pages->setCurrentIndex(PageImagery);
    auto *main = new QHBoxLayout;
    main->setContentsMargins(0, 0, 0, 0);
    main->setSpacing(0);
    main->addWidget(buildSidebar());
    main->addWidget(m_pages, 1);
    outer->addLayout(main, 1);
    setCentralWidget(root);

    connect(m_map, &MapWidget::areaChanged, this, &MainWindow::onAreaChanged);
    connect(m_map, &MapWidget::drawModeChanged, m_drawButton, &QToolButton::setChecked);
    connect(m_map, &MapWidget::clicked, this, &MainWindow::showPointValue);
    connect(m_backend, &Backend::ready, this, [this] {
        m_engineReady = true;
        m_status->setText(tr("Engine ready · offline · CPU"));
        setBusy(false);
    });
    connect(m_backend, &Backend::failed, this, [this](const QString &reason) {
        m_status->setText(tr("Engine unavailable"));
        setHint(reason, true);
    });

    setBusy(false);
    setHint(tr("Open an image to start."));
    m_backend->start();
}

// ---------------------------------------------------------------- layout ----

QWidget *MainWindow::buildHeader()
{
    auto *header = new QFrame;
    header->setObjectName("header");
    header->setFixedHeight(68);
    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(20, 0, 20, 0);
    row->setSpacing(14);

    auto *logo = new QLabel(QString(Theme::Icon::Globe));
    logo->setObjectName("logo");
    auto *names = new QVBoxLayout;
    names->setSpacing(0);
    auto *name = new QLabel("EarthView AI");
    name->setObjectName("appName");
    auto *tag = new QLabel(tr("Satellite Image Analysis & Processing"));
    tag->setObjectName("appTag");
    names->addStretch();
    names->addWidget(name);
    names->addWidget(tag);
    names->addStretch();

    m_status = new QLabel(tr("Starting analysis engine…"));
    m_status->setObjectName("status");
    m_status->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    m_status->setToolTip(tr("All processing runs on this computer. The app does not use the internet."));

    row->addWidget(logo);
    row->addLayout(names);
    row->addStretch();
    row->addWidget(m_status);
    if (!m_userName.isEmpty()) {
        QLabel *user = label(m_userName, "section");
        user->setToolTip(tr("Signed in on this computer"));
        QPushButton *signOut = button(tr("Sign out"), "plain");
        connect(signOut, &QPushButton::clicked, this, [this] {
            m_signedOut = true;
            close();
        });
        row->addSpacing(6);
        row->addWidget(user);
        row->addWidget(signOut);
    }
    return header;
}

QWidget *MainWindow::buildMapCard()
{
    auto *frame = new QFrame;
    frame->setObjectName("mapCard");
    auto *frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(1, 1, 1, 1);
    m_map = new MapWidget(m_backend);
    frameLayout->addWidget(m_map);

    // File bar (top left)
    auto *bar = new QFrame;
    bar->setObjectName("overlayBar");
    auto *barRow = new QHBoxLayout(bar);
    barRow->setContentsMargins(6, 6, 12, 6);
    barRow->setSpacing(8);
    m_openButton = button(tr("Open image…"), "primary");
    m_openButton->setToolTip(tr("Open GeoTIFF, JPEG2000, PNG or JPEG files from this computer"));
    connect(m_openButton, &QPushButton::clicked, this, &MainWindow::chooseImages);
    m_bandsButton = button(tr("Bands…"), "plain");
    m_bandsButton->setToolTip(tr("Choose which band is red, green, blue, near-infrared and so on"));
    connect(m_bandsButton, &QPushButton::clicked, this, &MainWindow::editBands);
    m_imageLabel = label(tr("No image open"), "muted");
    barRow->addWidget(m_openButton);
    barRow->addWidget(m_bandsButton);
    barRow->addWidget(m_imageLabel);

    // Tools (left edge)
    auto *tools = new QVBoxLayout;
    tools->setSpacing(6);
    const auto tool = [&](QChar icon, const QString &tip, bool checkable = false) {
        auto *b = new QToolButton;
        b->setText(icon);
        b->setToolTip(tip);
        b->setProperty("kind", "map");
        b->setCheckable(checkable);
        b->setCursor(Qt::PointingHandCursor);
        tools->addWidget(b);
        return b;
    };
    connect(tool(Theme::Icon::Add, tr("Zoom in")), &QToolButton::clicked, this, [this] { m_map->zoomBy(1); });
    connect(tool(Theme::Icon::Remove, tr("Zoom out")), &QToolButton::clicked, this, [this] { m_map->zoomBy(-1); });
    connect(tool(Theme::Icon::Target, tr("Show the whole image")), &QToolButton::clicked, this, [this] { m_map->fitImage(); });
    tools->addSpacing(10);
    m_drawButton = tool(Theme::Icon::Area, tr("Draw the analysis area: drag a rectangle on the image. "
                                               "Click once without dragging to analyse the whole image again."), true);
    connect(m_drawButton, &QToolButton::clicked, m_map, &MapWidget::setDrawMode);
    QToolButton *layersButton = tool(Theme::Icon::Layers, tr("Show or hide the result layer"), true);
    layersButton->setChecked(true);
    connect(layersButton, &QToolButton::toggled, m_map, &MapWidget::setOverlayVisible);
    tools->addStretch();

    auto *grid = new QGridLayout(m_map);
    grid->setContentsMargins(12, 12, 12, 58);  // the bottom strip is the viewer's own status line
    grid->addWidget(bar, 0, 0, Qt::AlignLeft | Qt::AlignTop);
    grid->addLayout(tools, 1, 0, Qt::AlignLeft);
    grid->setRowStretch(1, 1);
    grid->setColumnStretch(1, 1);
    return frame;
}

QWidget *MainWindow::buildImageryCard()
{
    QFrame *frame;
    m_stripHint = label(tr("No images open"), "muted");
    QVBoxLayout *layout = card(frame, Theme::Icon::Picture, tr("Imagery"), m_stripHint);
    m_strip = new QListWidget;
    m_strip->setViewMode(QListView::IconMode);
    m_strip->setFlow(QListView::LeftToRight);
    m_strip->setWrapping(false);
    m_strip->setMovement(QListView::Static);
    m_strip->setIconSize(ThumbSize);
    m_strip->setFixedHeight(ThumbSize.height() + 76);
    m_strip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_strip->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(m_strip, &QListWidget::currentRowChanged, this, &MainWindow::selectImage);
    layout->addWidget(m_strip);
    return frame;
}

QWidget *MainWindow::buildChartsCard()
{
    QFrame *frame;
    auto *kind = new QComboBox;
    kind->addItems({tr("Class distribution"), tr("Value histogram")});
    QVBoxLayout *layout = card(frame, Theme::Icon::Analysis, tr("Analysis Charts"), kind);
    m_chartStack = new QStackedWidget;
    m_donut = new DonutChart;
    m_histogram = new HistogramChart;
    m_chartStack->addWidget(m_donut);
    m_chartStack->addWidget(m_histogram);
    connect(kind, QOverload<int>::of(&QComboBox::currentIndexChanged), m_chartStack, &QStackedWidget::setCurrentIndex);
    layout->addWidget(m_chartStack, 1);
    return frame;
}

QWidget *MainWindow::buildActionBar()
{
    auto *frame = new QFrame;
    frame->setObjectName("card");
    auto *row = new QHBoxLayout(frame);
    row->setContentsMargins(12, 6, 16, 6);
    m_tifButton = button(tr("Save result as GeoTIFF"), "flat");
    m_pngButton = button(tr("Save result image"), "flat");
    connect(m_tifButton, &QPushButton::clicked, this, &MainWindow::exportGeoTiff);
    connect(m_pngButton, &QPushButton::clicked, this, &MainWindow::exportPng);
    auto *opacity = new QSlider(Qt::Horizontal);
    opacity->setRange(0, 100);
    opacity->setValue(100);
    opacity->setFixedWidth(120);
    connect(opacity, &QSlider::valueChanged, this, [this](int v) { m_map->setOverlayOpacity(v / 100.0); });
    m_compare = new QCheckBox(tr("Compare A | B"));
    m_compare->setToolTip(tr("Swipe between image A and image B (Imagery & Indices)"));
    connect(m_compare, &QCheckBox::toggled, this, [this] {
        if (!m_map->overlay().isNull())
            runAnalysis();
    });
    row->addWidget(m_tifButton);
    row->addWidget(m_pngButton);
    row->addStretch();
    row->addWidget(label(tr("Layer opacity"), "muted"));
    row->addWidget(opacity);
    row->addSpacing(18);
    row->addWidget(m_compare);
    return frame;
}

QWidget *MainWindow::buildAnalysisCard()
{
    QFrame *frame;
    QVBoxLayout *layout = card(frame, Theme::Icon::Analysis, tr("Analysis & Processing"));
    layout->addWidget(label(tr("Choose your analysis type and configure parameters"), "muted"));

    auto *tabRow = new QHBoxLayout;
    tabRow->setSpacing(4);
    m_tabs = new QButtonGroup(this);
    const QStringList tabNames = {tr("Imagery && Indices"), tr("Land Cover"), tr("Change"), tr("Detection")};
    for (int i = 0; i < tabNames.size(); ++i) {
        QPushButton *b = button(tabNames[i], "tab", true);
        b->setChecked(i == 0);
        m_tabs->addButton(b, i);
        tabRow->addWidget(b);
    }
    layout->addLayout(tabRow);

    auto *scenes = new QGridLayout;
    scenes->setHorizontalSpacing(10);
    m_sceneA = new QComboBox;
    m_sceneB = new QComboBox;
    m_sceneB->addItem(tr("None"), QString());
    for (QComboBox *box : {m_sceneA, m_sceneB})
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    scenes->addWidget(label(tr("Image A"), "section"), 0, 0);
    scenes->addWidget(label(tr("Image B (compare)"), "section"), 0, 1);
    scenes->addWidget(m_sceneA, 1, 0);
    scenes->addWidget(m_sceneB, 1, 1);
    scenes->setColumnStretch(0, 1);
    scenes->setColumnStretch(1, 1);
    layout->addLayout(scenes);
    connect(m_sceneA, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { m_strip->setCurrentRow(i); });

    m_params = new QStackedWidget;
    {   // Imagery & Indices
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(0, 0, 0, 0);
        m_product = new QComboBox;
        m_product->addItem(tr("True colour"), "true_color");
        m_product->addItem(tr("False colour, vegetation in red (needs near-infrared)"), "false_color");
        m_product->addItem(tr("Short-wave infrared (needs SWIR)"), "swir");
        m_product->insertSeparator(3);
        m_product->addItem(tr("NDVI - vegetation (red, near-infrared)"), "ndvi");
        m_product->addItem(tr("NDWI - open water (green, near-infrared)"), "ndwi");
        m_product->addItem(tr("NDBI - built-up (near-infrared, SWIR)"), "ndbi");
        m_product->addItem(tr("NBR - burn ratio (near-infrared, SWIR)"), "nbr");
        v->addWidget(label(tr("Product"), "section"));
        v->addWidget(m_product);
        v->addStretch();
        m_params->addWidget(page);
    }
    const auto sliderRow = [this](QVBoxLayout *v, const QString &title, QSlider *&slider, int lo, int hi, int value,
                                  std::function<QString(int)> format) {
        auto *valueLabel = new QLabel(format(value));
        valueLabel->setObjectName("pill");
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(lo, hi);
        slider->setValue(value);
        connect(slider, &QSlider::valueChanged, valueLabel, [valueLabel, format](int x) { valueLabel->setText(format(x)); });
        auto *row = new QHBoxLayout;
        row->addWidget(slider, 1);
        row->addWidget(valueLabel);
        v->addWidget(label(title, "section"));
        v->addLayout(row);
    };
    {   // Land cover
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(0, 0, 0, 0);
        m_landMethod = new QComboBox;
        m_landMethod->addItem(tr("Named classes: forest, urban, water… (trained model)"), "classes");
        m_landMethod->addItem(tr("Unsupervised clusters (K-means)"), "landcover");
        v->addWidget(label(tr("Method"), "section"));
        v->addWidget(m_landMethod);
        sliderRow(v, tr("Number of clusters (K-means only)"), m_classes, 2, 8, 6, [](int x) { return QString::number(x); });
        QLabel *note = label(tr("The trained model outlines the largest forest, urban, farmland and water areas. "
                                "It is most reliable with near- and short-wave infrared bands."), "muted");
        note->setWordWrap(true);
        v->addWidget(note);
        v->addStretch();
        m_params->addWidget(page);
    }
    {   // Change detection
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(0, 0, 0, 0);
        m_changeIndex = new QComboBox;
        m_changeIndex->addItem(tr("NDVI - vegetation"), "ndvi");
        m_changeIndex->addItem(tr("NDWI - open water"), "ndwi");
        m_changeIndex->addItem(tr("NDBI - built-up"), "ndbi");
        m_changeIndex->addItem(tr("NBR - burn ratio"), "nbr");
        m_changeIndex->addItem(tr("Forest loss / gain (land cover model)"), "forest_change");
        v->addWidget(label(tr("What to compare between image A and image B"), "section"));
        v->addWidget(m_changeIndex);
        sliderRow(v, tr("Change threshold"), m_threshold, 5, 90, 20, [](int x) { return QString::number(x / 100.0, 'f', 2); });
        v->addStretch();
        m_params->addWidget(page);
    }
    {   // Object detection
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(4);
        v->addWidget(label(tr("Vessel candidates: compact bright objects on open water (rule-based)."), "muted"));
        sliderRow(v, tr("Minimum contrast against the water"), m_minContrast, 3, 30, 8, [](int x) { return QString::number(x); });
        sliderRow(v, tr("Minimum length"), m_minLength, 5, 200, 20, [](int x) { return QStringLiteral("%1 m").arg(x); });
        m_fixedFilter = new QCheckBox(tr("Drop fixed objects also present in image B (islands, platforms)"));
        v->addWidget(m_fixedFilter);
        v->addStretch();
        m_params->addWidget(page);
    }
    m_params->setFixedHeight(172);
    connect(m_tabs, &QButtonGroup::idClicked, m_params, &QStackedWidget::setCurrentIndex);
    layout->addWidget(m_params);

    m_run = new QPushButton(QStringLiteral("▶   ") + tr("Run Analysis"));
    m_run->setObjectName("run");
    m_run->setCursor(Qt::PointingHandCursor);
    connect(m_run, &QPushButton::clicked, this, &MainWindow::runAnalysis);
    layout->addWidget(m_run);

    m_hint = label(QString(), "muted");
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);
    return frame;
}

QWidget *MainWindow::buildResultsCard()
{
    QFrame *frame;
    auto *corner = new QWidget;
    auto *cornerRow = new QHBoxLayout(corner);
    cornerRow->setContentsMargins(0, 0, 0, 0);
    m_pill = new QLabel(tr("No results"));
    m_pill->setObjectName("pill");
    m_when = label(QString(), "muted");
    m_when->setMinimumWidth(38);
    cornerRow->addWidget(m_pill);
    cornerRow->addWidget(m_when);
    QVBoxLayout *layout = card(frame, Theme::Icon::Results, tr("Processing Results"), corner);

    auto *tiles = new QHBoxLayout;
    tiles->setSpacing(8);
    const auto tile = [&](const QString &caption, QLabel *&value, QLabel **captionOut = nullptr) {
        auto *t = new QFrame;
        t->setObjectName("tile");
        auto *v = new QVBoxLayout(t);
        v->setContentsMargins(12, 9, 12, 9);
        v->setSpacing(2);
        QLabel *c = label(caption, "muted");
        value = new QLabel(QStringLiteral("—"));
        value->setObjectName("tileValue");
        v->addWidget(c);
        v->addWidget(value);
        if (captionOut)
            *captionOut = c;
        tiles->addWidget(t);
    };
    tile(tr("Usable area"), m_tileArea, &m_tileAreaCaption);
    tile(tr("Mean value"), m_tileMean, &m_tileMeanCaption);
    tile(tr("Processing time"), m_tileTime);
    layout->addLayout(tiles);

    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels({tr("Class"), tr("Area (km²)"), tr("Share")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setFocusPolicy(Qt::NoFocus);
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int) {
        const QVariant position = m_table->item(row, 0)->data(Qt::UserRole);  // set for detected objects only
        if (position.isValid())
            m_map->centreOn(position.toPointF().x(), position.toPointF().y());
    });
    layout->addWidget(m_table, 1);
    return frame;
}

// --------------------------------------------------------------- images ----

void MainWindow::chooseImages()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Open image"), QString(),
        tr("Images (*.tif *.tiff *.jp2 *.png *.jpg *.jpeg *.img *.vrt);;All files (*)"));
    if (files.isEmpty())
        return;
    QVector<QStringList> groups;
    if (files.size() > 1) {
        // Several files are either separate images, or the bands of one (e.g. Sentinel-2 B02, B03, B04, B08).
        QMessageBox box(QMessageBox::Question, tr("Open image"),
                        tr("You selected %1 files. How should they be opened?").arg(files.size()), QMessageBox::NoButton, this);
        QPushButton *separate = box.addButton(tr("As %1 separate images").arg(files.size()), QMessageBox::AcceptRole);
        QPushButton *bands = box.addButton(tr("As the bands of one image"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == bands)
            groups.append(files);
        else if (box.clickedButton() != separate)
            return;
    }
    if (groups.isEmpty())
        for (const QString &file : files)
            groups.append(QStringList{file});
    openImages(groups);
}

// Opens one image at a time, so they appear in the order given.
void MainWindow::openImages(const QVector<QStringList> &groups, const QJsonArray &settings, int start)
{
    if (!m_engineReady)
        return setHint(tr("The analysis engine is still starting. Try again in a moment."), true);
    if (start >= groups.size())
        return;
    const QJsonObject saved = settings.at(start).toObject();
    const QJsonObject body{{"paths", QJsonArray::fromStringList(groups[start])}};
    const auto next = [this, groups, settings, start] { openImages(groups, settings, start + 1); };
    m_backend->post("/api/open", body, [this, saved, next](bool ok, const QJsonObject &info, const QString &error) {
        if (!ok) {
            setHint(error, true);
            return next();
        }
        if (!saved.contains("mapping")) {
            addImage(info);
            return next();
        }
        // A project file carries the band assignment that was in use when it was saved.
        const QJsonObject bands{{"mapping", saved["mapping"]}, {"pixel_m", saved["pixel_m"]}};
        m_backend->post("/api/images/" + info["id"].toString() + "/bands", bands,
                        [this, info, next](bool applied, const QJsonObject &updated, const QString &) {
            addImage(applied ? updated : info);
            next();
        });
    });
}

void MainWindow::addImage(const QJsonObject &info)
{
    const QString id = info["id"].toString();
    int index = -1;
    for (int i = 0; i < m_images.size(); ++i)
        if (m_images[i]["id"].toString() == id)
            index = i;
    const QString detail = tr("%1 × %2 px · %3 bands").arg(info["width"].toInt()).arg(info["height"].toInt())
                               .arg(info["bands"].toArray().size());
    if (index < 0) {
        index = m_images.size();
        m_images.append(info);
        QPixmap placeholder(ThumbSize);
        placeholder.fill(QColor("#0f2038"));
        auto *item = new QListWidgetItem(QIcon(placeholder), QString(), m_strip);
        item->setSizeHint(QSize(ThumbSize.width() + 22, ThumbSize.height() + 54));
        m_sceneA->addItem(QString(), id);
        m_sceneB->addItem(QString(), id);
    } else {
        m_images[index] = info;
    }
    const QString name = info["name"].toString();
    m_strip->item(index)->setText(QFontMetrics(font()).elidedText(name, Qt::ElideMiddle, ThumbSize.width() + 8) + "\n"
                                  + tr("%1 × %2").arg(info["width"].toInt()).arg(info["height"].toInt()));
    m_strip->item(index)->setToolTip(info["paths"].toArray().first().toString() + "\n" + detail);
    m_sceneA->setItemText(index, name);
    m_sceneB->setItemText(index + 1, name);
    m_stripHint->setText(tr("%1 open · click one to display it").arg(m_images.size()));
    refreshThumbnail(id);
    if (m_strip->currentRow() == index)
        selectImage(index);  // refreshes the viewer after a band change
    else
        m_strip->setCurrentRow(index);
}

void MainWindow::refreshThumbnail(const QString &imageId)
{
    QUrlQuery q;
    q.addQueryItem("image", imageId);
    q.addQueryItem("product", "true_color");
    q.addQueryItem("max_px", "256");
    m_backend->getBytes("/api/render.png", q, [this, imageId](bool ok, const QByteArray &bytes, const QString &) {
        QPixmap pm;
        if (!ok || !pm.loadFromData(bytes))
            return;  // no red/green/blue assigned yet: keep the placeholder
        for (int i = 0; i < m_images.size(); ++i)
            if (m_images[i]["id"].toString() == imageId)
                m_strip->item(i)->setIcon(QIcon(pm.scaled(ThumbSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
                                                    .copy(QRect(QPoint(0, 0), ThumbSize))));
    });
}

QJsonObject MainWindow::currentImage() const
{
    return m_images.value(m_sceneA->currentIndex());
}

void MainWindow::selectImage(int index)
{
    if (index < 0 || index >= m_images.size())
        return;
    ++m_generation;
    m_sceneA->setCurrentIndex(index);
    const QJsonObject info = m_images[index];
    m_map->setImage(info["id"].toString(), info["width"].toInt(), info["height"].toInt(), info["pixel_m"].toDouble());
    const double pixel = info["pixel_m"].toDouble();
    m_imageLabel->setText(info["name"].toString() + QStringLiteral("  ·  ")
                          + (pixel > 0 ? tr("%1 m per pixel").arg(number(pixel, pixel < 10 ? 2 : 1)) : tr("pixel size unknown")));
    setBusy(false);
    setHint(tr("Analysing the whole image. To analyse part of it, draw an area with the rectangle tool."));
}

void MainWindow::onAreaChanged()
{
    ++m_generation;
    setBusy(false);
    if (!m_map->hasImage())
        return;
    const QRect area = m_map->areaRect();
    setHint(m_map->hasArea() ? tr("Area: %1 × %2 px. Choose an analysis and click Run Analysis.").arg(area.width()).arg(area.height())
                             : tr("Analysing the whole image. To analyse part of it, draw an area with the rectangle tool."));
}

// --------------------------------------------------------------- behaviour ----

void MainWindow::setHint(const QString &text, bool error)
{
    m_hint->setText(text);
    restyle(m_hint, "role", error ? "error" : "muted");
}

void MainWindow::setPill(const QString &text, const char *state)
{
    m_pill->setText(text);
    restyle(m_pill, "state", state);
}

void MainWindow::setBusy(bool busy)
{
    m_openButton->setEnabled(m_engineReady);
    m_bandsButton->setEnabled(m_engineReady && m_map->hasImage());
    m_run->setEnabled(m_engineReady && !busy && m_map->hasImage());
    const bool hasLayer = !m_map->overlay().isNull();
    m_tifButton->setEnabled(hasLayer && !busy && m_lastProduct != "vessels");  // detections have no raster export
    m_pngButton->setEnabled(hasLayer && !busy);
}

void MainWindow::runAnalysis()
{
    if (!m_engineReady)
        return;
    const QString a = currentImage()["id"].toString(), b = m_sceneB->currentData().toString();
    if (a.isEmpty())
        return setHint(tr("Open an image first."), true);
    const int mode = m_tabs->checkedId();
    const bool hasB = !b.isEmpty() && b != a;
    if (mode == Change && !hasB)
        return setHint(tr("Change detection needs an image B that differs from image A. Open a second image and pick it as image B."), true);

    auto job = std::make_shared<Job>();
    job->generation = ++m_generation;
    job->product = mode == LandCover ? m_landMethod->currentData().toString() : mode == Detect ? "vessels"
                   : mode == Change ? m_changeIndex->currentData().toString() : m_product->currentData().toString();
    job->name = mode == LandCover ? tr("Land cover") : mode == Detect ? tr("Vessel candidates")
                : job->product == "forest_change" ? tr("Forest change")
                : job->product.toUpper() + (mode == Change ? tr(" change") : QString());
    job->query.addQueryItem("image", a);
    job->query.addQueryItem("window", m_map->areaWindow());
    job->query.addQueryItem("product", job->product);
    job->query.addQueryItem("k", QString::number(m_classes->value()));
    if (mode == Change)
        job->query.addQueryItem("image_b", b);
    if (mode == Detect) {
        job->query.addQueryItem("min_contrast", QString::number(m_minContrast->value()));
        job->query.addQueryItem("min_length", QString::number(m_minLength->value()));
        job->query.addQueryItem("max_px", "2048");  // detection works best at full resolution
        if (m_fixedFilter->isChecked() && hasB)
            job->query.addQueryItem("image_b", b);
    }
    const bool swipe = mode == Imagery && m_compare->isChecked() && hasB;
    const bool wantStats = mode != Imagery || IndexProducts.contains(job->product);
    job->remaining = 1 + (swipe ? 1 : 0) + (wantStats ? 1 : 0);
    job->timer.start();

    setBusy(true);
    setPill(tr("Processing…"), "busy");
    m_when->clear();
    setHint(tr("Working…"));

    const auto done = [this, job](const QString &error) {
        if (!error.isEmpty() && job->error.isEmpty())
            job->error = error;
        if (--job->remaining == 0 && job->generation == m_generation)
            finishJob(job);
    };
    m_backend->getBytes("/api/render.png", job->query, [job, done](bool ok, const QByteArray &body, const QString &error) {
        if (ok && !job->a.loadFromData(body))
            return done(QObject::tr("The engine returned an unreadable image."));
        done(error);
    });
    if (swipe) {
        // Image B is drawn on image A's grid, so the two halves of the swipe line up.
        QUrlQuery qb;
        qb.addQueryItem("image", b);
        qb.addQueryItem("grid", a);
        qb.addQueryItem("window", m_map->areaWindow());
        qb.addQueryItem("product", job->product);
        m_backend->getBytes("/api/render.png", qb, [job, done](bool ok, const QByteArray &body, const QString &error) {
            if (ok)
                job->b.loadFromData(body);
            done(error);
        });
    }
    if (wantStats) {
        QUrlQuery qs = job->query;
        qs.addQueryItem("threshold", QString::number(m_threshold->value() / 100.0, 'f', 2));
        m_backend->getJson(mode == Detect ? "/api/detect" : "/api/stats", qs, [job, done](bool, const QJsonObject &body, const QString &error) {
            job->stats = body;
            done(error);
        });
    }
}

void MainWindow::finishJob(const std::shared_ptr<Job> &job)
{
    ++m_finishedJobs;
    if (!job->error.isEmpty()) {
        setPill(tr("Failed"), "error");
        setHint(job->error, true);
        setBusy(false);
        return;
    }
    m_map->setOverlays(job->a, job->b);
    // Outlined, labelled regions (named land cover and forest change only).
    const QString unit = job->stats["area_unit"].toString("km²");
    QVector<MapRegion> regions;
    for (const QJsonValue &value : job->stats["regions"].toArray()) {
        const QJsonObject r = value.toObject();
        MapRegion region{r["label"].toString(), number(r["km2"].toDouble(), unit == "px" ? 0 : 2) + " " + unit,
                         QColor(r["color"].toString()), {}, {}};
        for (const QJsonValue &point : r["polygon"].toArray())
            region.polygon << QPointF(point.toArray()[0].toDouble(), point.toArray()[1].toDouble());
        region.anchor = QPointF(r["anchor"].toArray()[0].toDouble(), r["anchor"].toArray()[1].toDouble());
        regions.append(region);
    }
    m_map->setRegions(regions);
    m_lastQuery = job->query;
    m_lastName = job->name;
    m_lastProduct = job->product;
    m_lastIsIndex = IndexProducts.contains(job->product) && job->b.isNull();
    showResults(job->stats, job->product, job->timer.elapsed());
    setPill(tr("Completed"), "ok");
    m_when->setText(QDateTime::currentDateTime().toString("HH:mm"));
    const bool picture = job->stats.isEmpty() && job->b.isNull();  // true colour and the other composites
    setHint(m_lastIsIndex ? tr("Click inside the area to read the value at a point.")
            : job->product == "vessels" ? tr("Click a row to centre the view on that object.")
            : job->product == "forest_change" ? tr("Seasonal leaf-off can look like forest loss: compare images from the same season.")
            : picture ? tr("This product is a picture, shown on the image. For measured results choose an index, "
                           "Land Cover, Change or Detection.")
            : QString());
    setBusy(false);
}

void MainWindow::showResults(const QJsonObject &stats, const QString &product, qint64 elapsedMs)
{
    m_tileTime->setText(tr("%1 s").arg(number(elapsedMs / 1000.0, 1)));
    const QString unit = stats["area_unit"].toString("km²");
    const QString areaHeader = unit == "px" ? tr("Pixels") : tr("Area (km²)");
    const bool vessels = product == "vessels", named = product == "classes";
    const QStringList headers = vessels ? QStringList{tr("Object"), tr("Length (m)"), tr("Contrast")}
                                : named ? QStringList{tr("Class"), tr("Count"), areaHeader, tr("Confidence")}
                                        : QStringList{tr("Class"), areaHeader, tr("Share")};
    m_table->setColumnCount(headers.size());
    m_table->setHorizontalHeaderLabels(headers);
    for (int column = 0; column < headers.size(); ++column)
        m_table->horizontalHeader()->setSectionResizeMode(column, column == 0 ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
    if (vessels)
        return showDetections(stats);

    const QJsonArray classes = stats["classes"].toArray();
    m_tileAreaCaption->setText(tr("Usable area"));
    m_tileArea->setText(stats.contains("valid_km2") ? areaText(stats["valid_km2"].toDouble(), unit) : QStringLiteral("—"));
    m_tileArea->setToolTip(stats.contains("valid_pct") ? tr("%1% of the area has data").arg(stats["valid_pct"].toDouble()) : QString());
    m_tileMean->setToolTip(QString());
    if (named) {
        m_tileMeanCaption->setText(tr("Model accuracy"));
        m_tileMean->setText(QStringLiteral("%1%").arg(qRound(stats["model"].toObject()["holdout_accuracy"].toDouble() * 100)));
        m_tileMean->setToolTip(tr("Share of pixels this model classified correctly on test sites it was not trained on "
                                  "(reference: ESA WorldCover). Your image may differ."));
    } else if (product == "forest_change") {
        m_tileMeanCaption->setText(tr("Forest loss"));
        m_tileMean->setText(areaText(classes.at(0).toObject()["km2"].toDouble(), unit));
    } else if (product == "landcover") {
        m_tileMeanCaption->setText(tr("Classes"));
        m_tileMean->setText(QString::number(classes.size()));
    } else {
        m_tileMeanCaption->setText(stats["change"].toBool() ? tr("Mean change") : tr("Mean value"));
        m_tileMean->setText(stats.contains("mean") ? number(stats["mean"].toDouble(), 3) : QStringLiteral("—"));
    }

    QVector<ClassShare> shares;
    m_table->setRowCount(classes.size());
    const auto cell = [this](int row, int column, const QString &text) {
        auto *item = new QTableWidgetItem(text);
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, column, item);
    };
    for (int i = 0; i < classes.size(); ++i) {
        const QJsonObject c = classes[i].toObject();
        const ClassShare share{c["label"].toString(), classColor(c, i, stats["change"].toBool()),
                               c["km2"].toDouble(), c["pct"].toDouble()};
        shares.append(share);
        m_table->setItem(i, 0, new QTableWidgetItem(swatch(share.color), share.label));
        const QString area = number(share.km2, unit == "px" ? 0 : 2);
        if (named) {
            cell(i, 1, QString::number(c["count"].toInt()));
            cell(i, 2, area);
            cell(i, 3, c["confidence"].isNull() ? QStringLiteral("—") : number(c["confidence"].toDouble(), 2));
        } else {
            cell(i, 1, area);
            cell(i, 2, number(share.percent, 1) + " %");
        }
    }
    m_donut->setData(shares, unit);

    QVector<double> edges, km2;
    const QJsonObject histogram = stats["histogram"].toObject();
    for (const QJsonValue &v : histogram["edges"].toArray())
        edges.append(v.toDouble());
    for (const QJsonValue &v : histogram["km2"].toArray())
        km2.append(v.toDouble());
    m_histogram->setData(edges, km2, unit);
}
void MainWindow::showDetections(const QJsonObject &result)
{
    m_tileAreaCaption->setText(tr("Open water"));
    m_tileArea->setText(areaText(result["water_km2"].toDouble(), "km²"));
    m_tileArea->setToolTip(result["removed_fixed"].toInt() > 0
        ? tr("%1 fixed objects were dropped using image B").arg(result["removed_fixed"].toInt()) : QString());
    m_tileMeanCaption->setText(tr("Detected objects"));
    m_tileMean->setText(QString::number(result["count"].toInt()));

    const QJsonArray objects = result["objects"].toArray();
    m_table->setRowCount(objects.size());
    for (int i = 0; i < objects.size(); ++i) {
        const QJsonObject o = objects[i].toObject();
        // Geographic position when the image is georeferenced, otherwise the pixel position.
        QString where = tr("x %1, y %2").arg(qRound(o["col"].toDouble())).arg(qRound(o["row"].toDouble()));
        if (o.contains("lon")) {
            const double lon = o["lon"].toDouble(), lat = o["lat"].toDouble();
            where = QStringLiteral("%1° %2, %3° %4").arg(qAbs(lat), 0, 'f', 4).arg(lat >= 0 ? "N" : "S")
                        .arg(qAbs(lon), 0, 'f', 4).arg(lon >= 0 ? "E" : "W");
        }
        auto *name = new QTableWidgetItem(QStringLiteral("#%1   %2").arg(o["id"].toInt()).arg(where));
        name->setData(Qt::UserRole, QPointF(o["col"].toDouble(), o["row"].toDouble()));
        auto *length = new QTableWidgetItem(QString::number(o["length_m"].toInt()));
        auto *contrast = new QTableWidgetItem(number(o["contrast"].toDouble(), 1));
        length->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        contrast->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(i, 0, name);
        m_table->setItem(i, 1, length);
        m_table->setItem(i, 2, contrast);
    }
    QVector<ClassShare> shares;
    const QJsonArray classes = result["classes"].toArray();
    for (int i = 0; i < classes.size(); ++i) {
        const QJsonObject c = classes[i].toObject();
        shares.append({tr("%1: %2").arg(c["label"].toString()).arg(c["count"].toInt()), QColor(SeriesColors[i]),
                       c["km2"].toDouble(), c["pct"].toDouble()});
    }
    m_donut->setData(result["count"].toInt() > 0 ? shares : QVector<ClassShare>());
    m_histogram->setData({}, {});
}

void MainWindow::showPointValue(double col, double row, const QPoint &globalPos)
{
    if (!m_lastIsIndex || m_map->overlay().isNull())
        return;
    QUrlQuery q = m_lastQuery;
    q.addQueryItem("col", QString::number(col, 'f', 2));
    q.addQueryItem("row", QString::number(row, 'f', 2));
    const int generation = m_generation;
    m_backend->getJson("/api/point", q, [this, generation, globalPos](bool ok, const QJsonObject &body, const QString &) {
        if (!ok || generation != m_generation)
            return;  // outside the area: nothing to show
        const QJsonValue value = body["value"];
        QToolTip::showText(globalPos, m_lastName + ": " + (value.isNull() ? tr("no data") : QString::number(value.toDouble(), 'f', 4)), m_map);
    });
}

void MainWindow::exportGeoTiff()
{
    const QString suggested = m_lastName.toLower().replace(' ', '_') + ".tif";
    const QString path = QFileDialog::getSaveFileName(this, tr("Save result as GeoTIFF"), suggested, tr("GeoTIFF (*.tif)"));
    if (path.isEmpty())
        return;
    setHint(tr("Preparing GeoTIFF…"));
    m_backend->getBytes("/api/export.tif", m_lastQuery, [this, path](bool ok, const QByteArray &body, const QString &error) {
        QFile file(path);
        if (!ok)
            return setHint(error, true);
        if (!file.open(QIODevice::WriteOnly) || file.write(body) != body.size())
            return setHint(tr("Could not write %1").arg(path), true);
        setHint(tr("Saved %1").arg(path));
    });
}

void MainWindow::exportPng()
{
    const QString suggested = m_lastName.toLower().replace(' ', '_') + ".png";
    const QString path = QFileDialog::getSaveFileName(this, tr("Save result image"), suggested, tr("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    if (m_map->overlay().save(path))
        setHint(tr("Saved %1").arg(path));
    else
        setHint(tr("Could not write %1").arg(path), true);
}

// ---------------------------------------------------------------- self-test ----

void MainWindow::runSelfTest(const QString &outputDir, const QStringList &imageFiles)
{
    QDir().mkpath(outputDir);
    auto *timer = new QTimer(this);
    auto step = std::make_shared<int>(0);
    auto seen = std::make_shared<int>(0);
    auto ticks = std::make_shared<int>(0);
    auto wait = std::make_shared<int>(0);
    const auto capture = [this, outputDir](const QString &name) {
        grab().save(QDir(outputDir).filePath(name));
        fprintf(stdout, "%s | %s | area %s | value %s | time %s | rows %d | hint: %s\n", qPrintable(name),
                qPrintable(m_pill->text()), qPrintable(m_tileArea->text()), qPrintable(m_tileMean->text()),
                qPrintable(m_tileTime->text()), m_table->rowCount(), qPrintable(m_hint->text()));
        fflush(stdout);
    };
    const auto run = [this](int tab, const QString &product = QString()) {
        m_tabs->button(tab)->click();
        if (!product.isEmpty())
            m_product->setCurrentIndex(m_product->findData(product));
        runAnalysis();
    };
    connect(timer, &QTimer::timeout, this, [=] {
        if (++*ticks > 600) {  // five minutes
            capture("timeout.png");
            return qApp->exit(2);
        }
        const bool jobDone = m_finishedJobs > *seen;
        switch (*step) {
        case 0: {
            if (!m_engineReady)
                return;
            QVector<QStringList> groups;
            for (const QString &file : imageFiles)
                groups.append(QStringList{file});
            openImages(groups);
            break;
        }
        case 1:  // all images listed
            if (m_images.size() < imageFiles.size())
                return;
            m_strip->setCurrentRow(0);
            m_sceneB->setCurrentIndex(qMin(2, m_sceneB->count() - 1));
            *wait = 0;
            break;
        case 2:  // a moment for the preview and thumbnails to arrive
            if (++*wait < 8)
                return;
            capture("1_opened.png");
            run(Imagery, "ndvi");
            break;
        case 3:
            if (!jobDone)
                return;
            capture("2_ndvi.png");
            run(LandCover);
            break;
        case 4:
            if (!jobDone)
                return;
            capture("3_land_cover.png");
            m_changeIndex->setCurrentIndex(m_changeIndex->findData("forest_change"));
            run(Change);
            break;
        case 5:
            if (!jobDone)
                return;
            capture("4_forest_change.png");
            m_compare->blockSignals(true);
            m_compare->setChecked(true);
            m_compare->blockSignals(false);
            run(Imagery, "true_color");  // runs as an A | B swipe
            break;
        case 6: {
            if (!jobDone)
                return;
            capture("5_swipe.png");
            m_compare->blockSignals(true);
            m_compare->setChecked(false);
            m_compare->blockSignals(false);
            const QRect whole = m_map->areaRect();
            m_map->setArea(whole.width() / 4, whole.height() / 4, whole.width() * 3 / 4, whole.height() * 3 / 4);
            m_map->fitArea();
            run(Detect);
            break;
        }
        case 7:
            if (!jobDone)
                return;
            *wait = 0;
            break;
        case 8:  // a moment for the sharper zoomed-in picture
            if (++*wait < 5)
                return;
            capture("6_detection_zoomed.png");
            if (m_images.size() < 3)
                return qApp->exit(0);
            m_strip->setCurrentRow(2);  // a plain picture without infrared: the index must be refused clearly
            run(Imagery, "ndvi");
            break;
        case 9:
            if (!jobDone)
                return;
            capture("7_picture_ndvi_refused.png");
            run(LandCover);
            break;
        case 10:
            if (!jobDone)
                return;
            capture("8_picture_land_cover.png");
            showPage(PageHome);
            grab().save(QDir(outputDir).filePath("9_home_page.png"));
            return qApp->exit(0);
        }
        *seen = m_finishedJobs;
        ++*step;
    });
    timer->start(500);
}
