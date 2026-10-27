#pragma once
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QUrlQuery>
#include <functional>

// Starts the Python analysis engine (..\satimg) and talks to it over local HTTP.
class Backend : public QObject {
    Q_OBJECT
public:
    using JsonHandler = std::function<void(bool ok, const QJsonObject &body, const QString &error)>;
    using BytesHandler = std::function<void(bool ok, const QByteArray &body, const QString &error)>;

    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    void start();
    QUrl url(const QString &path, const QUrlQuery &query = {}) const;
    void getJson(const QString &path, const QUrlQuery &query, JsonHandler handler);
    void getBytes(const QString &path, const QUrlQuery &query, BytesHandler handler);
    void post(const QString &path, const QJsonObject &body, JsonHandler handler);
    QNetworkAccessManager *network() { return &m_network; }

signals:
    void ready();
    void failed(const QString &reason);

private:
    void probe();
    void launch();
    static QString findProjectRoot();

    QNetworkAccessManager m_network;
    QProcess m_process;
    QTimer m_poll;
    int m_attempts = 0;
    bool m_launched = false;
    bool m_ready = false;
};
