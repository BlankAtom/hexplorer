#include "AppController.h"
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QMimeData>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QtTest>

class AppTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void initTestCase()
    {
        QVERIFY(m_settings.isValid());
        QCoreApplication::setOrganizationName("HExplorerTests");
        QCoreApplication::setApplicationName("AppTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void externalDropNegotiation()
    {
        QTemporaryDir source;
        QTemporaryDir destination;
        AppController app;
        const QList<QUrl> urls{QUrl::fromLocalFile(source.path())};
        const auto target = QUrl::fromLocalFile(destination.path());
        QCOMPARE(app.dropAction(urls, target, Qt::ControlModifier, Qt::CopyAction | Qt::MoveAction), int(Qt::CopyAction));
        QCOMPARE(app.dropAction(urls, target, Qt::ShiftModifier, Qt::CopyAction | Qt::MoveAction), int(Qt::MoveAction));
        QCOMPARE(app.dropAction(urls, target, Qt::NoModifier, Qt::CopyAction | Qt::MoveAction), int(Qt::MoveAction));
        QCOMPARE(app.dropAction(urls, target, Qt::ControlModifier | Qt::ShiftModifier, Qt::CopyAction | Qt::MoveAction), int(Qt::IgnoreAction));
        QCOMPARE(app.dropAction(urls, target, Qt::NoModifier, Qt::MoveAction), int(Qt::IgnoreAction));
        QCOMPARE(app.dropAcknowledgement(Qt::MoveAction, Qt::CopyAction | Qt::MoveAction), int(Qt::CopyAction));
        QCOMPARE(app.dropAction({QUrl("https://example.invalid/file")}, target, 0, Qt::CopyAction), int(Qt::IgnoreAction));
    }
    void successfulCutClearsMatchingClipboard()
    {
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkdir("Target"));
        QFile file(directory.filePath("source.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("contents");
        file.close();
        AppController app;
        app.workspace()->activePane()->navigate(QUrl::fromLocalFile(directory.filePath("Target")));
        app.native()->setClipboard({QUrl::fromLocalFile(file.fileName())}, true);
        app.paste();
        QTRY_VERIFY_WITH_TIMEOUT(!app.busy(), 5000);
        QVERIFY(QFileInfo::exists(directory.filePath("Target\\source.txt")));
        QVERIFY(!QFileInfo::exists(file.fileName()));
        QVERIFY(app.native()->clipboardFiles().value("urls").toList().isEmpty());
    }
    void folderAndRenameRejectInvalidNames()
    {
        QTemporaryDir directory;
        AppController app;
        app.workspace()->activePane()->navigate(QUrl::fromLocalFile(directory.path()));
        QSignalSpy errors(&app, &AppController::notification);
        app.requestFolder();
        app.acceptName("../escape");
        QCOMPARE(errors.count(), 1);
        app.acceptName("Child");
        QTRY_VERIFY_WITH_TIMEOUT(!app.busy(), 5000);
        QVERIFY(QFileInfo(directory.filePath("Child")).isDir());
        app.acceptName("Child");
        QTRY_VERIFY_WITH_TIMEOUT(!app.busy(), 5000);
        QCOMPARE(errors.count(), 2);
    }
};

QTEST_MAIN(AppTests)
#include "apptests.moc"
