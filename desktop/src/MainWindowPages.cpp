// Left navigation, the pages other than the image workspace, and the band dialog.
#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStackedWidget>
#include <QTableWidget>
#include <iterator>

#include "Backend.h"
#include "MainWindow.h"
#include "MapWidget.h"
#include "Theme.h"
#include "UiHelpers.h"

using namespace ui;

namespace {
// A page: title, one-line description, then content.
QVBoxLayout *page(QWidget *&host, const QString &title, const QString &subtitle)
{
    host = new QWidget;
    auto *layout = new QVBoxLayout(host);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(14);
    auto *heading = new QLabel(title);
    heading->setObjectName("pageTitle");
    layout->addWidget(heading);
    QLabel *sub = label(subtitle, "muted");
    sub->setWordWrap(true);
    layout->addWidget(sub);
    return layout;
}
}  // namespace

QWidget *MainWindow::buildSidebar()
{
    auto *bar = new QFrame;
    bar->setObjectName("sidebar");
    bar->setFixedWidth(212);
    auto *layout = new QVBoxLayout(bar);
    layout->setContentsMargins(10, 16, 10, 14);
    layout->setSpacing(4);

    m_nav = new QButtonGroup(this);
    const struct { QChar icon; QString text; int page; } items[] = {
        {Theme::Icon::Home, tr("Home"), PageHome},
        {Theme::Icon::Globe, tr("Satellite Imagery"), PageImagery},
        {Theme::Icon::Models, tr("AI Models"), PageModels},
        {Theme::Icon::Folder, tr("Projects"), PageProjects},
        {Theme::Icon::Storage, tr("Data Management"), PageData},
    };
    for (const auto &item : items) {
        QPushButton *b = button(item.text, "nav", true);
        b->setIcon(glyphIcon(item.icon, QColor("#9fb4d6")));
        b->setIconSize(QSize(18, 18));
        b->setChecked(item.page == PageImagery);
        m_nav->addButton(b, item.page);
        layout->addWidget(b);
    }
    connect(m_nav, &QButtonGroup::idClicked, this, &MainWindow::showPage);
    layout->addStretch();

    QFrame *source;
    QVBoxLayout *info = card(source, Theme::Icon::Storage, tr("Works offline"));
    info->setContentsMargins(12, 10, 12, 10);
    info->setSpacing(4);
    QLabel *details = label(tr("Images are opened from this computer and analysed on it. Nothing is sent or downloaded."), "muted");
    details->setWordWrap(true);
    info->addWidget(details);
    layout->addWidget(source);
    return bar;
}

void MainWindow::showPage(int index)
{
    m_pages->setCurrentIndex(index);
    m_nav->button(index)->setChecked(true);
    if (index == PageData)
        refreshCacheInfo();
}

QWidget *MainWindow::buildHomePage()
{
    QWidget *host;
    QVBoxLayout *layout = page(host, tr("Welcome to EarthView AI"),
        tr("View and analyse satellite or aerial images stored on this computer. The app works fully offline."));
    QFrame *steps;
    QVBoxLayout *list = card(steps, Theme::Icon::Play, tr("Quick start"));
    const QStringList lines = {
        tr("1.  Open Satellite Imagery and click Open image… to choose a GeoTIFF, JPEG2000, PNG or JPEG file. "
           "Several single-band files (for example Sentinel-2 B02, B03, B04, B08) can be opened together as one image."),
        tr("2.  Check Bands… if the colours look wrong or an analysis says a band is missing."),
        tr("3.  Optionally draw the area to analyse with the rectangle tool; otherwise the whole image is used."),
        tr("4.  Pick an analysis on the right and click Run Analysis. Indices and vessel detection need a "
           "near-infrared band; land cover and viewing work with any image."),
        tr("5.  To compare two dates, open both images and choose the second one as Image B."),
        tr("6.  Save the result as GeoTIFF or as a picture from the bar under the image."),
    };
    for (const QString &line : lines) {
        QLabel *l = label(line);
        l->setWordWrap(true);
        list->addWidget(l);
    }
    layout->addWidget(steps);
    QPushButton *open = button(tr("Open Satellite Imagery"), "primary");
    open->setFixedWidth(240);
    connect(open, &QPushButton::clicked, this, [this] { showPage(PageImagery); });
    layout->addWidget(open);
    layout->addStretch();
    return host;
}

QWidget *MainWindow::buildModelsPage()
{
    QWidget *host;
    QVBoxLayout *layout = page(host, tr("AI Models"),
        tr("The analysis methods available now. All run on this computer's CPU. No deep-learning model is installed yet; "
           "the detector below is rule-based."));
    const QStringList rows[] = {
        {tr("Spectral indices"), tr("Rule-based"), tr("NDVI, NDWI, NDBI and NBR from band ratios, with area per class. Need near-infrared (and SWIR for NDBI, NBR).")},
        {tr("Land cover"), tr("Unsupervised (K-means)"), tr("Clusters the available bands into 2-8 classes. Works with any image; class names are estimates.")},
        {tr("Change detection"), tr("Index differencing"), tr("Difference of an index between two images, split by a threshold.")},
        {tr("Vessel candidates"), tr("Rule-based anomaly detector"),
         tr("Compact objects brighter than the surrounding open water. Small islands, shallows and breaking waves can also be flagged.")},
    };
    auto *table = new QTableWidget(int(std::size(rows)), 3);
    table->setHorizontalHeaderLabels({tr("Method"), tr("Type"), tr("What it does")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->verticalHeader()->hide();
    table->setShowGrid(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->setWordWrap(true);
    for (int r = 0; r < int(std::size(rows)); ++r)
        for (int c = 0; c < 3; ++c)
            table->setItem(r, c, new QTableWidgetItem(rows[r][c]));
    table->verticalHeader()->setDefaultSectionSize(44);
    QFrame *frame;
    QVBoxLayout *inner = card(frame, Theme::Icon::Models, tr("Analysis methods"));
    inner->addWidget(table);
    layout->addWidget(frame, 1);
    return host;
}

QWidget *MainWindow::buildProjectsPage()
{
    QWidget *host;
    QVBoxLayout *layout = page(host, tr("Projects"),
        tr("A project file remembers which image files are open and their band assignment. "
           "It stores file locations, not the images themselves."));
    QFrame *frame;
    QVBoxLayout *inner = card(frame, Theme::Icon::Folder, tr("Project file"));
    m_projectInfo = label(tr("No project file is open."), "muted");
    m_projectInfo->setWordWrap(true);
    inner->addWidget(m_projectInfo);
    auto *row = new QHBoxLayout;
    QPushButton *open = button(tr("Open project…"), "primary");
    QPushButton *save = button(tr("Save project…"), "plain");
    connect(open, &QPushButton::clicked, this, &MainWindow::openProject);
    connect(save, &QPushButton::clicked, this, &MainWindow::saveProject);
    row->addWidget(open);
    row->addWidget(save);
    row->addStretch();
    inner->addLayout(row);
    layout->addWidget(frame);
    layout->addStretch();
    return host;
}

void MainWindow::saveProject()
{
    if (m_images.isEmpty()) {
        m_projectInfo->setText(tr("Open an image first; there is nothing to save yet."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Save project"), "project.evproj", tr("EarthView project (*.evproj)"));
    if (path.isEmpty())
        return;
    QJsonArray images;
    for (const QJsonObject &info : m_images)
        images.append(QJsonObject{{"paths", info["paths"]}, {"mapping", info["mapping"]}, {"pixel_m", info["pixel_m"]}});
    const QJsonObject project{{"images", images}};
    QFile file(path);
    const bool ok = file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(project).toJson()) > 0;
    m_projectInfo->setText(ok ? tr("Saved %1").arg(path) : tr("Could not write %1").arg(path));
}

void MainWindow::openProject()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open project"), QString(), tr("EarthView project (*.evproj)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    const QJsonObject project = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    const QJsonArray images = project["images"].toArray();
    if (images.isEmpty()) {
        m_projectInfo->setText(tr("%1 is not a valid project file.").arg(path));
        return;
    }
    QVector<QStringList> groups;
    for (const QJsonValue &image : images) {
        QStringList files;
        for (const QJsonValue &p : image.toObject()["paths"].toArray())
            files.append(p.toString());
        groups.append(files);
    }
    m_projectInfo->setText(tr("Opened %1").arg(path));
    showPage(PageImagery);
    openImages(groups, images);
}

QWidget *MainWindow::buildDataPage()
{
    QWidget *host;
    QVBoxLayout *layout = page(host, tr("Data Management"),
        tr("Parts of the opened images are kept in memory so that repeated analyses are fast. "
           "Releasing them frees memory; your files on disk are never changed."));
    QFrame *frame;
    QVBoxLayout *inner = card(frame, Theme::Icon::Storage, tr("Memory"));
    m_cacheInfo = label(tr("Open this page when the engine is ready to see memory use."), "muted");
    m_cacheInfo->setWordWrap(true);
    inner->addWidget(m_cacheInfo);
    auto *row = new QHBoxLayout;
    QPushButton *refresh = button(tr("Refresh"), "plain");
    QPushButton *clear = button(tr("Release memory"), "plain");
    connect(refresh, &QPushButton::clicked, this, &MainWindow::refreshCacheInfo);
    connect(clear, &QPushButton::clicked, this, [this] {
        m_backend->post("/api/cache/clear", {}, [this](bool ok, const QJsonObject &body, const QString &error) {
            showCacheInfo(ok, body, error);
        });
    });
    row->addWidget(refresh);
    row->addWidget(clear);
    row->addStretch();
    inner->addLayout(row);
    layout->addWidget(frame);
    layout->addStretch();
    return host;
}

void MainWindow::refreshCacheInfo()
{
    if (!m_engineReady)
        return;
    m_backend->getJson("/api/cache", {}, [this](bool ok, const QJsonObject &body, const QString &error) {
        showCacheInfo(ok, body, error);
    });
}

void MainWindow::showCacheInfo(bool ok, const QJsonObject &body, const QString &error)
{
    restyle(m_cacheInfo, "role", ok ? "muted" : "error");
    m_cacheInfo->setText(!ok ? error : tr("Images open: %1\nImage parts held in memory: %2")
                                           .arg(body["images"].toInt()).arg(body["bands_in_memory"].toInt()));
}

// Which band of the file is red, green, blue and so on, and the ground size of a pixel
// for images that carry no georeferencing.
void MainWindow::editBands()
{
    const QJsonObject info = currentImage();
    if (info.isEmpty())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Bands - %1").arg(info["name"].toString()));
    dialog.setStyleSheet(Theme::styleSheet() + "QDialog { background: #0b1728; }");
    auto *form = new QFormLayout(&dialog);
    form->setContentsMargins(20, 18, 20, 16);
    form->setVerticalSpacing(10);
    QLabel *intro = label(tr("Tell the app what each band of this image is. Leave a role at \"Not present\" if the image has no such band."), "muted");
    intro->setWordWrap(true);
    form->addRow(intro);

    const QJsonArray bands = info["bands"].toArray();
    const QJsonObject mapping = info["mapping"].toObject();
    const QPair<QString, QString> roles[] = {
        {"red", tr("Red")}, {"green", tr("Green")}, {"blue", tr("Blue")}, {"nir", tr("Near-infrared")},
        {"swir16", tr("Short-wave infrared 1 (1.6 µm)")}, {"swir22", tr("Short-wave infrared 2 (2.2 µm)")},
    };
    QVector<QComboBox *> boxes;
    for (const auto &role : roles) {
        auto *box = new QComboBox;
        box->addItem(tr("Not present"), -1);
        for (int i = 0; i < bands.size(); ++i)
            box->addItem(tr("Band %1: %2").arg(i + 1).arg(bands[i].toString()), i);
        box->setCurrentIndex(mapping[role.first].isNull() ? 0 : mapping[role.first].toInt() + 1);
        box->setMinimumWidth(260);
        form->addRow(role.second, box);
        boxes.append(box);
    }
    auto *pixel = new QDoubleSpinBox;
    pixel->setRange(0, 10000);
    pixel->setDecimals(2);
    pixel->setSuffix(tr(" m"));
    pixel->setSpecialValueText(tr("Unknown"));
    pixel->setValue(info["pixel_m"].toDouble());
    pixel->setEnabled(!info["georeferenced"].toBool());
    pixel->setToolTip(info["georeferenced"].toBool() ? tr("Read from the file's georeferencing")
                                                     : tr("Needed for areas in km² and for object detection"));
    form->addRow(tr("Ground size of one pixel"), pixel);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QJsonObject chosen;
    for (int i = 0; i < boxes.size(); ++i) {
        const int band = boxes[i]->currentData().toInt();
        chosen[roles[i].first] = band < 0 ? QJsonValue() : QJsonValue(band);
    }
    const QJsonObject body{{"mapping", chosen}, {"pixel_m", pixel->value() > 0 ? QJsonValue(pixel->value()) : QJsonValue()}};
    m_backend->post("/api/images/" + info["id"].toString() + "/bands", body,
                    [this](bool ok, const QJsonObject &updated, const QString &error) {
        if (!ok)
            return setHint(error, true);
        addImage(updated);  // redraws the viewer and the thumbnail with the new assignment
    });
}
