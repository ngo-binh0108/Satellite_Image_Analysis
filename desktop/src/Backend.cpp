#include "Backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>

namespace {
const int Port = 8765;
const int MaxAttempts = 120;         // x 500 ms = one minute for the engine to come up
const int RequestTimeoutMs = 180000; // first read of a scene plus clustering can take a while

QString errorText(QNetworkReply *reply, const QByteArray &body)
{
    const QString detail = QJsonDocument::fromJson(body).object().value("detail").toString();
    return detail.isEmpty() ? reply->errorString() : detail;
}
}  // namespace

Backend::Backend(QObject *parent) : QObject(parent)
{
    m_poll.setInterval(500);
    connect(&m_poll, &QTimer::timeout, this, &Backend::probe);
}

Backend::~Backend()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(3000);
    }
}

QUrl Backend::url(const QString &path, const QUrlQuery &query) const
{
    QUrl u(QStringLiteral("http://127.0.0.1:%1%2").arg(Port).arg(path));
    u.setQuery(query);
    return u;
}

QString Backend::findProjectRoot()
{
    const QString fromEnv = qEnvironmentVariable("SATIMG_ROOT");
    if (!fromEnv.isEmpty())
        return fromEnv;
    QDir dir(QCoreApplication::applicationDirPath());
    do {
        if (QFileInfo::exists(dir.filePath("satimg/app.py")))
            return dir.absolutePath();
    } while (dir.cdUp());
    return {};
}

void Backend::start()
{
    m_attempts = 0;
    probe();  // an engine may already be running
    m_poll.start();
}

void Backend::launch()
{
    m_launched = true;
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    // Installed copy: the packaged engine sits next to the app. Otherwise run it from the source tree.
    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath("engine/satimg-engine.exe");
    if (QFileInfo::exists(bundled)) {
        m_process.setWorkingDirectory(QFileInfo(bundled).absolutePath());
        m_process.start(bundled, {"--port", QString::number(Port)});
        return;
    }
    const QString root = findProjectRoot();
    if (root.isEmpty()) {
        m_poll.stop();
        emit failed(tr("Analysis engine not found. Set SATIMG_ROOT to the folder that contains 'satimg'."));
        return;
    }
    QString python = QDir(root).filePath(".conda-env/python.exe");
    if (!QFileInfo::exists(python))
        python = "python";
    m_process.setWorkingDirectory(root);
    m_process.start(python, {"-m", "satimg", "--port", QString::number(Port)});
}

void Backend::probe()
{
    if (m_ready)
        return;
    if (m_launched && m_process.state() == QProcess::NotRunning) {
        m_poll.stop();
        const QString log = QString::fromLocal8Bit(m_process.readAll()).trimmed().right(400);
        emit failed(tr("The analysis engine stopped: %1").arg(log.isEmpty() ? m_process.errorString() : log));
        return;
    }
    if (++m_attempts > MaxAttempts) {
        m_poll.stop();
        emit failed(tr("The analysis engine did not start in time."));
        return;
    }
    QNetworkRequest request(url("/api/products"));
    request.setTransferTimeout(2000);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (m_ready)
            return;
        if (reply->error() == QNetworkReply::NoError) {
            m_ready = true;
            m_poll.stop();
            emit ready();
        } else if (!m_launched) {
            launch();
        }
    });
}

void Backend::getBytes(const QString &path, const QUrlQuery &query, BytesHandler handler)
{
    QNetworkRequest request(url(path, query));
    request.setTransferTimeout(RequestTimeoutMs);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, handler] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() == QNetworkReply::NoError)
            handler(true, body, {});
        else
            handler(false, {}, errorText(reply, body));
    });
}

void Backend::getJson(const QString &path, const QUrlQuery &query, JsonHandler handler)
{
    getBytes(path, query, [handler](bool ok, const QByteArray &body, const QString &error) {
        handler(ok, ok ? QJsonDocument::fromJson(body).object() : QJsonObject(), error);
    });
}

void Backend::post(const QString &path, const QJsonObject &body, JsonHandler handler)
{
    QNetworkRequest request(url(path));
    request.setTransferTimeout(RequestTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, handler] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        handler(ok, ok ? QJsonDocument::fromJson(body).object() : QJsonObject(), ok ? QString() : errorText(reply, body));
    });
}