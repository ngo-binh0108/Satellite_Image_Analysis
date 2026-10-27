#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QUrlQuery>
#include <QVector>
#include <memory>

class Backend;
class DonutChart;
class HistogramChart;
class MapWidget;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QStackedWidget;
class QTableWidget;
class QToolButton;
class QVBoxLayout;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // `userName` is shown in the header with a Sign out button; empty hides both.
    explicit MainWindow(const QString &userName = QString(), QWidget *parent = nullptr);
    bool signedOut() const { return m_signedOut; }  // the window was closed with Sign out
    // --selftest: opens the given image files, drives the real workflow and saves window captures
    void runSelfTest(const QString &outputDir, const QStringList &imageFiles);

private:
    enum Mode { Imagery = 0, LandCover = 1, Change = 2, Detect = 3 };
    enum Page { PageHome, PageImagery, PageModels, PageProjects, PageData };
    struct Job;

    // Image workspace (MainWindow.cpp)
    QWidget *buildHeader();
    QWidget *buildMapCard();
    QWidget *buildImageryCard();
    QWidget *buildChartsCard();
    QWidget *buildActionBar();
    QWidget *buildAnalysisCard();
    QWidget *buildResultsCard();

    // Navigation, the other pages and dialogs (MainWindowPages.cpp)
    QWidget *buildSidebar();
    QWidget *buildHomePage();
    QWidget *buildModelsPage();
    QWidget *buildProjectsPage();
    QWidget *buildDataPage();
    void showPage(int index);
    void saveProject();
    void openProject();
    void refreshCacheInfo();
    void showCacheInfo(bool ok, const QJsonObject &body, const QString &error);
    void editBands();

    // Opening images. Each entry of `groups` is the file list of one image; `settings` may carry
    // a saved band assignment per group.
    void chooseImages();
    void openImages(const QVector<QStringList> &groups, const QJsonArray &settings = {}, int start = 0);
    void addImage(const QJsonObject &info);
    void selectImage(int index);
    void refreshThumbnail(const QString &imageId);
    QJsonObject currentImage() const;

    void onAreaChanged();
    void runAnalysis();
    void finishJob(const std::shared_ptr<Job> &job);
    void showResults(const QJsonObject &stats, const QString &product, qint64 elapsedMs);
    void showDetections(const QJsonObject &result);
    void showPointValue(double col, double row, const QPoint &globalPos);
    void exportGeoTiff();
    void exportPng();
    void setHint(const QString &text, bool error = false);
    void setPill(const QString &text, const char *state);
    void setBusy(bool busy);

    Backend *m_backend;
    MapWidget *m_map;
    bool m_engineReady = false;
    QString m_userName;
    bool m_signedOut = false;
    QVector<QJsonObject> m_images;  // as described by the engine, in the order of the Imagery strip

    QStackedWidget *m_pages;
    QButtonGroup *m_nav;
    QLabel *m_projectInfo, *m_cacheInfo;

    QLabel *m_status, *m_imageLabel;
    QPushButton *m_openButton, *m_bandsButton;
    QToolButton *m_drawButton;

    QListWidget *m_strip;
    QLabel *m_stripHint;
    QStackedWidget *m_chartStack;
    DonutChart *m_donut;
    HistogramChart *m_histogram;
    QPushButton *m_tifButton, *m_pngButton;
    QCheckBox *m_compare;

    QButtonGroup *m_tabs;
    QStackedWidget *m_params;
    QComboBox *m_sceneA, *m_sceneB, *m_product, *m_changeIndex, *m_landMethod;
    QSlider *m_classes, *m_threshold, *m_minContrast, *m_minLength;
    QCheckBox *m_fixedFilter;
    QPushButton *m_run;
    QLabel *m_hint;

    QLabel *m_pill, *m_when, *m_tileArea, *m_tileAreaCaption, *m_tileMean, *m_tileMeanCaption, *m_tileTime;
    QTableWidget *m_table;

    int m_generation = 0;        // analysis requests; stale answers are dropped
    QUrlQuery m_lastQuery;       // parameters of the layer on screen (export, point values)
    QString m_lastName, m_lastProduct;
    bool m_lastIsIndex = false;
    int m_finishedJobs = 0;
};
