#include "UpdateController.h"
#include "Version.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>
#include <QVersionNumber>

namespace {
constexpr qint64 MaxJsonSize = 1024 * 1024;
constexpr qint64 MaxInstallerSize = 512 * 1024 * 1024;
QString repository() { return QStringLiteral(HEXPLORER_UPDATE_REPOSITORY); }
QString releasePath(const QString &version) { return "/" + repository() + "/releases/download/v" + version + "/"; }
bool stableVersion(const QString &text)
{
    static const QRegularExpression expression(R"(^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$)");
    if (!expression.match(text).hasMatch()) return false;
    const auto version = QVersionNumber::fromString(text);
    return version.segmentCount() == 3 && version.toString() == text
        && version.majorVersion() <= 65535 && version.minorVersion() <= 65535 && version.microVersion() <= 65535;
}
bool plainHttps(const QUrl &url)
{
    return url.isValid() && url.scheme() == "https" && url.userInfo().isEmpty()
        && (url.port() == -1 || url.port() == 443) && url.fragment().isEmpty();
}
}

UpdateController::UpdateController(QObject *parent)
    : UpdateController(nullptr, parent) {}

UpdateController::UpdateController(QNetworkAccessManager *manager, QObject *parent)
    : QObject(parent), m_manager(manager ? manager : &m_ownedManager)
{
    m_autoCheck = m_settings.value("updates/checkOnStartup", false).toBool();
    m_message = m_autoCheck ? tr("Startup update checks are enabled. Downloads and installation remain manual.")
                           : tr("Updates are checked only when requested. Automatic startup checks are off by default.");
    m_timeout.setSingleShot(true);
    m_timeout.setParent(this);
    m_timeout.setObjectName("updateDeadline");
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail(tr("The update request timed out. Please try again.")); });
}

UpdateController::~UpdateController()
{
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
    }
    m_output.reset();
    discardInstaller();
}

QString UpdateController::currentVersion() const { return QStringLiteral(HEXPLORER_VERSION); }

int UpdateController::compareVersions(const QString &left, const QString &right, bool *valid)
{
    const bool ok = stableVersion(left) && stableVersion(right);
    if (valid) *valid = ok;
    return ok ? QVersionNumber::compare(QVersionNumber::fromString(left), QVersionNumber::fromString(right)) : 0;
}

bool UpdateController::trustedDownloadUrl(const QUrl &url)
{
    if (!plainHttps(url)) return false;
    return url.host() == "github.com" || url.host() == "release-assets.githubusercontent.com"
        || url.host() == "objects.githubusercontent.com";
}

bool UpdateController::parseRelease(const QByteArray &json, Release &release, QString &error)
{
    error.clear();
    release = {};
    if (json.size() > MaxJsonSize) { error = tr("Release metadata is too large."); return false; }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = tr("GitHub returned invalid release metadata.");
        return false;
    }
    const auto object = document.object();
    const auto tag = object.value("tag_name").toString();
    if (!object.value("draft").isBool() || !object.value("prerelease").isBool()
        || object.value("draft").toBool() || object.value("prerelease").toBool()
        || !tag.startsWith('v') || !stableVersion(tag.mid(1))) {
        error = tr("Only published stable releases tagged vMAJOR.MINOR.PATCH are supported.");
        return false;
    }
    release.version = tag.mid(1);
    release.notes = object.value("body").toString().left(16000);
    release.page = QUrl(object.value("html_url").toString());
    if (!plainHttps(release.page) || release.page.host() != "github.com"
        || release.page.path() != "/" + repository() + "/releases/tag/" + tag
        || release.page.hasQuery()) {
        error = tr("The release page does not belong to the configured repository.");
        return false;
    }
    const QString name = "HExplorer-" + release.version + "-windows-x64-setup.exe";
    int matches = 0;
    for (const auto &value : object.value("assets").toArray()) {
        const auto asset = value.toObject();
        if (asset.value("name").toString() != name) continue;
        ++matches;
        release.installer = QUrl(asset.value("browser_download_url").toString());
        const double size = asset.value("size").toDouble(-1);
        const QString digest = asset.value("digest").toString();
        static const QRegularExpression hash(R"(^sha256:([a-fA-F0-9]{64})$)");
        const auto match = hash.match(digest);
        if (!plainHttps(release.installer) || release.installer.host() != "github.com"
            || release.installer.path() != releasePath(release.version) + name
            || release.installer.hasQuery() || size < 1 || size > MaxInstallerSize
            || size != static_cast<qint64>(size) || !match.hasMatch()
            || asset.value("state").toString() != "uploaded") {
            error = tr("The Windows installer is missing a trusted URL, valid size, or GitHub SHA-256 digest.");
            return false;
        }
        release.size = static_cast<qint64>(size);
        release.sha256 = QByteArray::fromHex(match.captured(1).toLatin1());
    }
    if (matches != 1) {
        error = tr("This release must contain exactly one verified Windows x64 installer. Try again after publishing finishes.");
        return false;
    }
    return true;
}

void UpdateController::setState(QString status, QString message)
{
    m_status = std::move(status);
    m_message = std::move(message);
    emit changed();
}

void UpdateController::setAutoCheck(bool value)
{
    if (value == m_autoCheck) return;
    m_settings.setValue("updates/checkOnStartup", value);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        fail(tr("Could not save your update-check preference."));
        return;
    }
    m_autoCheck = value;
    if (m_status == "idle")
        m_message = value ? tr("Startup update checks are enabled. Downloads and installation remain manual.")
                          : tr("Updates are checked only when requested. Automatic startup checks are off by default.");
    emit changed();
}

void UpdateController::discardInstaller()
{
    if (m_installerPath.isEmpty()) return;
    if (QFileInfo::exists(m_installerPath) && !QFile::remove(m_installerPath))
        qWarning("Could not remove the cached update installer: %s", qPrintable(m_installerPath));
    m_installerPath.clear();
}

void UpdateController::fail(QString message)
{
    m_timeout.stop();
    if (m_reply) {
        auto *reply = m_reply.data();
        m_reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    m_output.reset();
    discardInstaller();
    m_progress = 0;
    setState("error", std::move(message));
}

void UpdateController::startRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", ("HExplorer/" + currentVersion()).toUtf8());
    request.setRawHeader("Accept", m_status == "checking" ? "application/vnd.github+json" : "application/octet-stream");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    request.setTransferTimeout(30000);
    auto *reply = m_manager->get(request);
    m_reply = reply;
    reply->setReadBufferSize(256 * 1024);
    connect(reply, &QNetworkReply::redirected, this, [this, reply](const QUrl &target) {
        if (reply != m_reply) return;
        // The metadata endpoint must not redirect to unrelated APIs.
        if (m_status != "downloading" || !trustedDownloadUrl(target))
            fail(tr("The update server redirected to an untrusted destination."));
        else emit reply->redirectAllowed();
    });
    connect(reply, &QIODevice::readyRead, this, &UpdateController::consume);
    connect(reply, &QNetworkReply::finished, this, &UpdateController::finish);
    m_timeout.start(m_status == "checking" ? 30000 : 10 * 60 * 1000);
}

void UpdateController::check()
{
    if (busy()) return;
    discardInstaller();
    m_release = {};
    m_json.clear();
    m_progress = 0;
    setState("checking", tr("Checking GitHub Releases..."));
    startRequest(QUrl("https://api.github.com/repos/" + repository() + "/releases/latest"));
}

void UpdateController::consume()
{
    if (!m_reply) return;
    const auto statusCode = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // Do not mix redirect bodies into either metadata or installer bytes.
    if (statusCode != 200) { m_reply->readAll(); return; }
    if (m_status == "checking") {
        const auto declared = m_reply->header(QNetworkRequest::ContentLengthHeader);
        if (declared.isValid() && declared.toLongLong() > MaxJsonSize) {
            fail(tr("Release metadata exceeds the size limit."));
            return;
        }
        const auto bytes = m_reply->readAll();
        if (m_json.size() + bytes.size() > MaxJsonSize) { fail(tr("Release metadata exceeds the size limit.")); return; }
        m_json.append(bytes);
        return;
    }
    if (m_status != "downloading" || !m_output) return;
    const auto declared = m_reply->header(QNetworkRequest::ContentLengthHeader);
    if (declared.isValid() && declared.toLongLong() != m_release.size) {
        fail(tr("Installer Content-Length does not match the release metadata."));
        return;
    }
    while (m_reply && m_reply->bytesAvailable() > 0) {
        const auto bytes = m_reply->read(64 * 1024);
        if (bytes.isEmpty()) break;
        if (bytes.size() > m_release.size - m_received) { fail(tr("The installer exceeds its declared size.")); return; }
        if (m_output->write(bytes) != bytes.size()) { fail(tr("Could not write the update installer. Check disk space and permissions.")); return; }
        m_hash.addData(bytes);
        m_received += bytes.size();
    }
    m_progress = double(m_received) / double(m_release.size);
    emit changed();
}

void UpdateController::finish()
{
    if (!m_reply) return;
    consume();
    if (!m_reply) return;
    auto *reply = m_reply.data();
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (code != 200 || reply->error() != QNetworkReply::NoError) {
        QString message;
        if (code == 404 && m_status == "checking")
            message = tr("No published stable release is available yet.");
        else if (code == 403 || code == 429)
            message = tr("GitHub denied or rate-limited the request. Please try again later.");
        else message = tr("Update request failed (HTTP %1): %2").arg(code).arg(reply->errorString());
        fail(message);
        return;
    }
    m_reply = nullptr;
    reply->deleteLater();
    m_timeout.stop();
    if (m_status == "checking") {
        Release release;
        QString error;
        if (!parseRelease(m_json, release, error)) { fail(error); return; }
        m_json.clear();
        m_release = release;
        if (compareVersions(release.version, currentVersion()) > 0)
            setState("available", tr("Version %1 is available.").arg(release.version));
        else
            setState("current", tr("You are up to date (installed %1; latest release %2).").arg(currentVersion(), release.version));
    } else {
        if (m_received != m_release.size || m_hash.result() != m_release.sha256) {
            fail(tr("Installer size or SHA-256 verification failed. Nothing will be installed."));
            return;
        }
        if (!m_output->commit()) { fail(tr("Could not commit the downloaded installer.")); return; }
        m_output.reset();
        setState("ready", tr("Installer verified. Confirm installation to open the Windows setup wizard."));
    }
}

void UpdateController::download()
{
    if (m_status != "available" && m_status != "ready") return;
    discardInstaller();
    const auto cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/updates";
    if (!QDir().mkpath(cache)) { fail(tr("Cannot create the update cache directory.")); return; }
    m_installerPath = QDir(cache).filePath("HExplorer-" + m_release.version + "-"
        + QUuid::createUuid().toString(QUuid::WithoutBraces) + "-setup.exe");
    m_output = std::make_unique<QSaveFile>(m_installerPath);
    m_output->setDirectWriteFallback(false);
    if (!m_output->open(QIODevice::WriteOnly)) { fail(tr("Cannot create the update installer file.")); return; }
    m_hash.reset();
    m_received = 0;
    m_progress = 0;
    setState("downloading", tr("Downloading the Windows x64 installer..."));
    startRequest(m_release.installer);
}

void UpdateController::cancel()
{
    if (!busy()) return;
    m_timeout.stop();
    if (m_reply) {
        auto *reply = m_reply.data();
        m_reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    m_output.reset();
    discardInstaller();
    m_progress = 0;
    setState(m_release.installer.isEmpty() ? "idle" : "available", tr("Update request cancelled."));
}

bool UpdateController::verifyInstaller(QString &error) const
{
    const QFileInfo info(m_installerPath);
    QFile file(m_installerPath);
    if (m_status != "ready" || !info.isFile() || info.isSymbolicLink() || info.size() != m_release.size
        || !file.open(QIODevice::ReadOnly)) {
        error = tr("The verified installer is no longer available. Download it again.");
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || hash.result() != m_release.sha256) {
        error = tr("The downloaded installer changed. Installation was blocked.");
        return false;
    }
    return true;
}

bool UpdateController::launchInstaller()
{
#ifdef Q_OS_WIN
    QString error;
    if (!verifyInstaller(error)) { fail(error); return false; }
    // Interactive, per-user setup; no silent flags, elevation, or in-place updater.
    if (!QProcess::startDetached(m_installerPath, {}, QFileInfo(m_installerPath).absolutePath())) {
        fail(tr("Windows could not start the installer. No application files were changed."));
        return false;
    }
    m_installerPath.clear(); // The setup process still needs its executable.
    setState("installing", tr("The setup wizard was started. HExplorer will close to allow the update."));
    return true;
#else
    fail(tr("Installing updates is supported only on Windows."));
    return false;
#endif
}

void UpdateController::openRelease()
{
    if (m_release.page.isEmpty() || !QDesktopServices::openUrl(m_release.page))
        fail(tr("Could not open the release page."));
}
