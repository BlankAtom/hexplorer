#include "UpdateController.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <cstring>

namespace {
const QByteArray installer("MZ offline installer fixture");
QByteArray releaseJson(QString version = "65535.65535.65535", QByteArray payload = installer)
{
    const auto name = "HExplorer-" + version + "-windows-x64-setup.exe";
    return QJsonDocument(QJsonObject{
        {"tag_name", "v" + version}, {"draft", false}, {"prerelease", false},
        {"html_url", "https://github.com/BlankAtom/hexplorer/releases/tag/v" + version},
        {"body", "Release notes\n<not executable HTML>"},
        {"assets", QJsonArray{QJsonObject{
            {"name", name}, {"state", "uploaded"}, {"size", double(payload.size())},
            {"digest", "sha256:" + QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())},
            {"browser_download_url", "https://github.com/BlankAtom/hexplorer/releases/download/v" + version + "/" + name}
        }}}
    }).toJson();
}
struct Response {
    QByteArray data;
    int status = 200;
    QUrl redirect;
    bool stall = false;
};
class Reply : public QNetworkReply {
public:
    Reply(const QNetworkRequest &request, Response response, QObject *parent) : QNetworkReply(parent), m_response(response)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        open(QIODevice::ReadOnly);
        if (response.stall) return;
        QTimer::singleShot(0, this, [this] {
            if (isFinished()) return;
            if (!m_response.redirect.isEmpty()) {
                emit redirected(m_response.redirect);
                return;
            }
            if (m_response.status != 200)
                setError(QNetworkReply::ContentAccessDenied, "HTTP error");
            m_available = true;
            emit readyRead();
            if (!isFinished()) {
                setFinished(true);
                emit finished();
            }
        });
    }
    void abort() override {
        if (isFinished()) return;
        setError(QNetworkReply::OperationCanceledError, "Cancelled");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return (m_available ? m_response.data.size() - m_position : 0) + QNetworkReply::bytesAvailable();
    }
protected:
    qint64 readData(char *data, qint64 maximum) override {
        if (!m_available) return 0;
        const auto count = qMin(maximum, qint64(m_response.data.size()) - m_position);
        if (!count) return -1;
        std::memcpy(data, m_response.data.constData() + m_position, size_t(count));
        m_position += count;
        return count;
    }
private:
    Response m_response;
    qint64 m_position = 0;
    bool m_available = false;
};
class Network : public QNetworkAccessManager {
public:
    QList<Response> responses;
    QList<QNetworkRequest> requests;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        requests.append(request);
        if (responses.isEmpty()) qFatal("Unexpected network request");
        return new Reply(request, responses.takeFirst(), this);
    }
};
}

class UpdateTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_directory;
private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        QCoreApplication::setOrganizationName("HExplorerTests");
        QCoreApplication::setApplicationName("UpdateTests-" + QUuid::createUuid().toString(QUuid::Id128));
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_directory.path());
    }
    void init() { QSettings settings; settings.clear(); settings.sync(); }
    void versionComparison_data()
    {
        QTest::addColumn<QString>("left");
        QTest::addColumn<QString>("right");
        QTest::addColumn<int>("sign");
        QTest::addColumn<bool>("valid");
        QTest::newRow("numeric-not-lexical") << "0.10.0" << "0.9.9" << 1 << true;
        QTest::newRow("equal") << "1.2.3" << "1.2.3" << 0 << true;
        QTest::newRow("older") << "1.0.9" << "2.0.0" << -1 << true;
        QTest::newRow("prerelease") << "1.0.0-beta" << "1.0.0" << 0 << false;
        QTest::newRow("suffix") << "1.0.0oops" << "1.0.0" << 0 << false;
        QTest::newRow("short") << "1.0" << "1.0.0" << 0 << false;
        QTest::newRow("overflow") << "999999999999999999999.0.0" << "1.0.0" << 0 << false;
        QTest::newRow("leading-zero") << "01.0.0" << "1.0.0" << 0 << false;
    }
    void versionComparison()
    {
        QFETCH(QString, left); QFETCH(QString, right); QFETCH(int, sign); QFETCH(bool, valid);
        bool ok = false;
        const int result = UpdateController::compareVersions(left, right, &ok);
        QCOMPARE(ok, valid);
        QCOMPARE(result > 0 ? 1 : result < 0 ? -1 : 0, sign);
    }
    void metadataValidation()
    {
        UpdateController::Release release;
        QString error;
        QVERIFY(UpdateController::parseRelease(releaseJson("0.3.0"), release, error));
        QCOMPARE(release.version, QString("0.3.0"));
        QCOMPARE(release.size, qint64(installer.size()));
        QCOMPARE(release.sha256, QCryptographicHash::hash(installer, QCryptographicHash::Sha256));
        QVERIFY(release.notes.contains("<not executable HTML>"));
        auto object = QJsonDocument::fromJson(releaseJson("0.3.0")).object();
        for (const auto &field : {QString("draft"), QString("prerelease")}) {
            auto invalid = object;
            invalid.insert(field, true);
            QVERIFY(!UpdateController::parseRelease(QJsonDocument(invalid).toJson(), release, error));
        }
        for (const auto &url : {QString("http://github.com/x.exe"), QString("https://evil.example/x.exe"),
                               QString("https://github.com/Other/repo/releases/download/v0.3.0/x.exe")}) {
            auto invalid = object;
            auto assets = invalid.value("assets").toArray();
            auto asset = assets.first().toObject();
            asset.insert("browser_download_url", url);
            assets[0] = asset;
            invalid.insert("assets", assets);
            QVERIFY(!UpdateController::parseRelease(QJsonDocument(invalid).toJson(), release, error));
        }
        for (const auto &digest : {QString(), QString("md5:abc"), QString("sha256:abc")}) {
            auto invalid = object;
            auto asset = invalid.value("assets").toArray().first().toObject();
            asset.insert("digest", digest);
            invalid.insert("assets", QJsonArray{asset});
            QVERIFY(!UpdateController::parseRelease(QJsonDocument(invalid).toJson(), release, error));
        }
        object.insert("assets", QJsonArray{});
        QVERIFY(!UpdateController::parseRelease(QJsonDocument(object).toJson(), release, error));
        QVERIFY(!UpdateController::parseRelease("{broken", release, error));
        QVERIFY(!UpdateController::parseRelease(QByteArray(1024 * 1024 + 1, 'x'), release, error));
    }
    void redirectPolicy()
    {
        QVERIFY(UpdateController::trustedDownloadUrl(QUrl("https://release-assets.githubusercontent.com/file?signature=123")));
        QVERIFY(UpdateController::trustedDownloadUrl(QUrl("https://objects.githubusercontent.com/file")));
        QVERIFY(!UpdateController::trustedDownloadUrl(QUrl("http://github.com/file")));
        QVERIFY(!UpdateController::trustedDownloadUrl(QUrl("https://github.com.evil.example/file")));
        QVERIFY(!UpdateController::trustedDownloadUrl(QUrl("https://user:password@github.com/file")));
        QVERIFY(!UpdateController::trustedDownloadUrl(QUrl("https://github.com:123/file")));
    }
    void optInPersistsWithoutNetwork()
    {
        Network network;
        {
            UpdateController controller(&network, nullptr);
            QVERIFY(!controller.autoCheck());
            QVERIFY(network.requests.isEmpty());
            controller.setAutoCheck(true);
        }
        UpdateController restored(&network, nullptr);
        QVERIFY(restored.autoCheck());
        QVERIFY(network.requests.isEmpty());
    }
    void checksAndVerifiedDownload()
    {
        Network network;
        network.responses = {{releaseJson()}, {installer}};
        QString path;
        {
            UpdateController controller(&network, nullptr);
            controller.check();
            controller.check(); // no overlapping requests
            QTRY_COMPARE(controller.status(), QString("available"));
            QCOMPARE(network.requests.size(), 1);
            QCOMPARE(network.requests[0].url(), QUrl("https://api.github.com/repos/BlankAtom/hexplorer/releases/latest"));
            controller.download();
            QTRY_COMPARE(controller.status(), QString("ready"));
            QCOMPARE(controller.progress(), 1.0);
            QDirIterator iterator(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/updates", {"*.exe"}, QDir::Files);
            QVERIFY(iterator.hasNext());
            path = iterator.next();
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), installer);
            file.close();
            // Tampering after verification must block launch as well.
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            auto tampered = installer;
            tampered[0] = 'X';
            QCOMPARE(file.write(tampered), qint64(installer.size()));
            file.close();
            QVERIFY(!controller.launchInstaller());
            QCOMPARE(controller.status(), QString("error"));
            QVERIFY(controller.message().contains("changed"));
            QVERIFY(!QFileInfo::exists(path));
        }
        QVERIFY(!QFileInfo::exists(path));
    }
    void noDowngrade()
    {
        Network network;
        network.responses = {{releaseJson("0.1.0")}};
        UpdateController controller(&network, nullptr);
        controller.check();
        QTRY_COMPARE(controller.status(), QString("current"));
        controller.download();
        QCOMPARE(network.requests.size(), 1);
    }
    void equalVersionDoesNotOfferDownload()
    {
        Network network;
        UpdateController controller(&network, nullptr);
        network.responses = {{releaseJson(controller.currentVersion())}};
        controller.check();
        QTRY_COMPARE(controller.status(), QString("current"));
        controller.download();
        QCOMPARE(network.requests.size(), 1);
    }
    void missingHashAndOversizedMetadataFailClosed()
    {
        auto object = QJsonDocument::fromJson(releaseJson()).object();
        auto asset = object.value("assets").toArray().first().toObject();
        asset.remove("digest");
        object.insert("assets", QJsonArray{asset});
        Network network;
        network.responses = {{QJsonDocument(object).toJson()}, {QByteArray(1024 * 1024 + 1, ' ')}, {releaseJson()}};
        UpdateController controller(&network, nullptr);
        controller.check();
        QTRY_COMPARE(controller.status(), QString("error"));
        controller.download();
        QCOMPARE(network.requests.size(), 1);
        controller.check();
        QTRY_COMPARE(controller.status(), QString("error"));
        QVERIFY(controller.message().contains("size limit"));
        controller.check();
        QTRY_COMPARE(controller.status(), QString("available"));
    }
    void cacheWriteFailureIsExplicit()
    {
        Network network;
        network.responses = {{releaseJson()}};
        UpdateController controller(&network, nullptr);
        controller.check();
        QTRY_COMPARE(controller.status(), QString("available"));
        const auto cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QVERIFY(QDir().mkpath(cache));
        const auto updates = cache + "/updates";
        if (QDir(updates).exists()) QVERIFY(QDir().rmdir(updates));
        QFile blocker(updates);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        controller.download();
        QCOMPARE(controller.status(), QString("error"));
        QVERIFY(controller.message().contains("cache"));
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(blocker.remove());
    }
    void httpFailures_data()
    {
        QTest::addColumn<int>("code");
        QTest::newRow("no-release") << 404;
        QTest::newRow("rate-limit") << 403;
        QTest::newRow("too-many") << 429;
        QTest::newRow("server") << 500;
    }
    void httpFailures()
    {
        QFETCH(int, code);
        Network network;
        network.responses = {{"", code}};
        UpdateController controller(&network, nullptr);
        controller.check();
        QTRY_COMPARE(controller.status(), QString("error"));
        QVERIFY(!controller.message().isEmpty());
        QVERIFY(!controller.busy());
    }
    void rejectBadDownloads_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("truncated") << installer.left(4);
        QTest::newRow("too-large") << installer + "extra";
        QByteArray corrupt = installer;
        corrupt[0] = 'X';
        QTest::newRow("digest-mismatch") << corrupt;
    }
    void rejectBadDownloads()
    {
        QFETCH(QByteArray, bytes);
        Network network;
        network.responses = {{releaseJson()}, {bytes}};
        UpdateController controller(&network, nullptr);
        controller.check();
        QTRY_COMPARE(controller.status(), QString("available"));
        controller.download();
        QTRY_COMPARE(controller.status(), QString("error"));
        QVERIFY(!controller.launchInstaller());
    }
    void cancelAndRetry()
    {
        Network network;
        network.responses = {{"", 200, {}, true}, {releaseJson()}, {"", 200, {}, true}, {installer}};
        UpdateController controller(&network, nullptr);
        controller.check();
        QVERIFY(controller.busy());
        controller.cancel();
        QCOMPARE(controller.status(), QString("idle"));
        controller.check();
        QTRY_COMPARE(controller.status(), QString("available"));
        controller.download();
        QCOMPARE(controller.status(), QString("downloading"));
        controller.cancel();
        QCOMPARE(controller.status(), QString("available"));
        controller.download();
        QTRY_COMPARE(controller.status(), QString("ready"));
    }
    void rejectUntrustedRedirect()
    {
        Network network;
        network.responses = {{releaseJson()}, {"", 302, QUrl("https://evil.example/setup.exe")}};
        UpdateController controller(&network, nullptr);
        controller.check();
        QTRY_COMPARE(controller.status(), QString("available"));
        controller.download();
        QTRY_COMPARE(controller.status(), QString("error"));
        QVERIFY(controller.message().contains("untrusted"));
    }
    void timeoutIsExplicit()
    {
        Network network;
        network.responses = {{"", 200, {}, true}};
        UpdateController controller(&network, nullptr);
        controller.check();
        auto *deadline = controller.findChild<QTimer*>("updateDeadline");
        QVERIFY(deadline);
        QCOMPARE(deadline->interval(), 30000);
        QVERIFY(QMetaObject::invokeMethod(deadline, "timeout"));
        QVERIFY(!controller.busy());
        QCOMPARE(controller.status(), QString("error"));
        QVERIFY(controller.message().contains("timed out"));
    }
    void cleanupTestCase()
    {
        const auto cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QDir directory(cache);
        QVERIFY(!QDirIterator(cache, {"*.exe"}, QDir::Files, QDirIterator::Subdirectories).hasNext());
        if (directory.exists()) QVERIFY(directory.removeRecursively());
    }
};

QTEST_GUILESS_MAIN(UpdateTests)
#include "updatetests.moc"
