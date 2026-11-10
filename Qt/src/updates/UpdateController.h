#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <memory>

class UpdateController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY changed)
    Q_PROPERTY(QUrl releaseUrl READ releaseUrl NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY changed)
public:
    struct Release {
        QString version;
        QString notes;
        QUrl page;
        QUrl installer;
        QByteArray sha256;
        qint64 size = 0;
    };
    explicit UpdateController(QObject *parent = nullptr);
    // Inject a manager only for deterministic offline tests.
    explicit UpdateController(QNetworkAccessManager *manager, QObject *parent);
    ~UpdateController() override;
    QString currentVersion() const;
    QString latestVersion() const { return m_release.version; }
    QString status() const { return m_status; }
    QString message() const { return m_message; }
    QString releaseNotes() const { return m_release.notes; }
    QUrl releaseUrl() const { return m_release.page; }
    double progress() const { return m_progress; }
    bool busy() const { return m_status == "checking" || m_status == "downloading"; }
    bool autoCheck() const { return m_autoCheck; }
    void setAutoCheck(bool value);
    Q_INVOKABLE void check();
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openRelease();
    // AppController must guard this with task state and explicit UI confirmation.
    bool launchInstaller();
    static int compareVersions(const QString &left, const QString &right, bool *valid = nullptr);
    static bool parseRelease(const QByteArray &json, Release &release, QString &error);
    static bool trustedDownloadUrl(const QUrl &url);
signals:
    void changed();
private:
    void setState(QString status, QString message);
    void fail(QString message);
    void startRequest(const QUrl &url);
    void consume();
    void finish();
    bool verifyInstaller(QString &error) const;
    void discardInstaller();
    QNetworkAccessManager m_ownedManager;
    QNetworkAccessManager *m_manager;
    QPointer<QNetworkReply> m_reply;
    QSettings m_settings;
    QTimer m_timeout;
    Release m_release;
    QString m_status = "idle";
    QString m_message;
    bool m_autoCheck = false;
    double m_progress = 0;
    QByteArray m_json;
    std::unique_ptr<QSaveFile> m_output;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    QString m_installerPath;
    qint64 m_received = 0;
};
