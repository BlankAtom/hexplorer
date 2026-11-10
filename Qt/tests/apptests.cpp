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
    void init()
    {
        QSettings settings;
        settings.clear();
        settings.sync();
    }
    static void writeFile(const QString &path)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("tag fixture"), qint64(11));
    }
    static int findRow(PaneController *pane, const QString &name)
    {
        for (int i = 0; i < pane->files()->count(); ++i)
            if (pane->files()->entry(i).value("fileName").toString() == name) return i;
        return -1;
    }
    void quickAccessPersistsUserChoices()
    {
        QTemporaryDir directory;
        {
            NavigationSettings navigation;
            QVERIFY(navigation.expanded());
            QVERIFY(navigation.visible());
            while (!navigation.places().isEmpty()) navigation.remove(0);
            navigation.setExpanded(false);
            navigation.setVisible(false);
            navigation.add("My work", directory.path());
            QCOMPARE(navigation.places().size(), 1);
            QSignalSpy errors(&navigation, &NavigationSettings::errorOccurred);
            navigation.add("Duplicate", directory.path());
            navigation.add("Invalid", directory.filePath("missing"));
            QCOMPARE(errors.size(), 2);
            QCOMPARE(navigation.places().size(), 1);
        }
        NavigationSettings restored;
        QVERIFY(!restored.expanded());
        QVERIFY(!restored.visible());
        QCOMPARE(restored.places().first().toMap().value("name").toString(), QString("My work"));
        restored.remove(0);
        NavigationSettings empty;
        QVERIFY(empty.places().isEmpty());
        empty.reset();
        QVERIFY(!empty.places().isEmpty());
    }
    void multipleTagsGlobalViewHistoryAndPersistence()
    {
        QTemporaryDir a;
        QTemporaryDir b;
        writeFile(a.filePath("MixedCase.txt"));
        QVERIFY(QDir(b.path()).mkdir("Folder"));
        const QVariantList urls{QUrl::fromLocalFile(a.filePath("MixedCase.txt")),
                                QUrl::fromLocalFile(b.filePath("Folder"))};
        {
            AppController app;
            QVERIFY(app.tags()->setTag(urls, "red", true));
            QVERIFY(app.tags()->setTag({urls.first()}, "blue", true));
            auto *pane = app.workspace()->activePane();
            pane->navigate(QUrl::fromLocalFile(a.path()));
            app.showTag("red");
            QTRY_VERIFY(!pane->loading());
            QCOMPARE(pane->files()->count(), 2);
            pane->files()->selectAll();
            QVERIFY(app.selectionTags().at(0).toMap().value("checked").toBool());
            QVERIFY(app.selectionTags().at(4).toMap().value("mixed").toBool());
            const auto row = findRow(pane, "MixedCase.txt");
            QVERIFY(row >= 0);
            QCOMPARE(pane->files()->entry(row).value("tagColors").toStringList().size(), 2);
            pane->setQuery("MixedCase");
            QCOMPARE(pane->files()->count(), 1);
            pane->files()->select(0);
            QVERIFY(app.selectionTags().at(0).toMap().value("checked").toBool());
            app.tagSelection("red", false);
            QTRY_VERIFY(!pane->loading());
            QCOMPARE(pane->files()->count(), 0); // name filter still applies
            pane->setQuery({});
            QCOMPARE(pane->files()->count(), 1);
            pane->back();
            QCOMPARE(pane->url(), QUrl::fromLocalFile(a.path()));
            pane->forward();
            QVERIFY(pane->tagView());
            app.saveSession();
        }
        AppController restored;
        QVERIFY(restored.workspace()->activePane()->tagView());
        QTRY_VERIFY(!restored.workspace()->activePane()->loading());
        QCOMPARE(restored.workspace()->activePane()->files()->count(), 1);
        QCOMPARE(restored.tags()->tags(a.filePath("MixedCase.txt")), QStringList{"blue"});
        QVERIFY(restored.tags()->paths("blue").contains(a.filePath("MixedCase.txt")));
    }
    void tagsFollowRenameAndDirectoryDescendants()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir("Original"));
        writeFile(dir.filePath("Original\\Inside.txt"));
        AppController app;
        QVERIFY(app.tags()->setTag({QUrl::fromLocalFile(dir.filePath("Original")),
                                   QUrl::fromLocalFile(dir.filePath("Original\\Inside.txt"))}, "green", true));
        auto *pane = app.workspace()->activePane();
        pane->navigate(QUrl::fromLocalFile(dir.path()));
        QTRY_VERIFY(!pane->loading());
        pane->files()->select(findRow(pane, "Original"));
        app.requestRename();
        app.acceptName("Renamed");
        QTRY_VERIFY(!app.busy());
        QVERIFY(QFileInfo::exists(dir.filePath("Renamed\\Inside.txt")));
        QVERIFY(app.tags()->tags(dir.filePath("Original")).isEmpty());
        QCOMPARE(app.tags()->tags(dir.filePath("Renamed")), QStringList{"green"});
        QCOMPARE(app.tags()->tags(dir.filePath("Renamed\\Inside.txt")), QStringList{"green"});
    }
    void tagsFollowKeepBothAndPartialMoves()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir("Target"));
        writeFile(dir.filePath("A.txt"));
        writeFile(dir.filePath("B.txt"));
        writeFile(dir.filePath("Target\\A.txt"));
        AppController app;
        QVERIFY(app.tags()->setTag({QUrl::fromLocalFile(dir.filePath("A.txt")),
                                   QUrl::fromLocalFile(dir.filePath("B.txt"))}, "purple", true));
        QSignalSpy moved(app.tasks(), &TaskManager::pathRelocated);
        app.tasks()->submit("move", {QUrl::fromLocalFile(dir.filePath("A.txt")),
                                     QUrl::fromLocalFile(dir.filePath("B.txt"))},
                            QUrl::fromLocalFile(dir.filePath("Target")), "skip");
        QTRY_VERIFY(!app.busy());
        QCOMPARE(moved.size(), 1);
        QCOMPARE(app.tags()->tags(dir.filePath("A.txt")), QStringList{"purple"});
        QCOMPARE(app.tags()->tags(dir.filePath("Target\\B.txt")), QStringList{"purple"});
        app.tasks()->submit("move", {QUrl::fromLocalFile(dir.filePath("A.txt"))},
                            QUrl::fromLocalFile(dir.filePath("Target")), "rename");
        QTRY_VERIFY(!app.busy());
        QCOMPARE(moved.size(), 2);
        const auto actual = moved.last().at(1).toString();
        QVERIFY(actual != dir.filePath("Target\\A.txt"));
        QCOMPARE(app.tags()->tags(actual), QStringList{"purple"});
        QVERIFY(app.tags()->tags(dir.filePath("A.txt")).isEmpty());
    }
    void unavailableTagsAreKeptAndInvalidInputRejected()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("File.txt"));
        AppController app;
        QVERIFY(app.tags()->setTag({QUrl::fromLocalFile(dir.filePath("File.txt"))}, "red", true));
        QVERIFY(QFile::remove(dir.filePath("File.txt")));
        app.showTag("red");
        auto *pane = app.workspace()->activePane();
        QTRY_VERIFY(!pane->loading());
        QCOMPARE(pane->files()->count(), 0);
        QVERIFY(!pane->error().isEmpty());
        QCOMPARE(app.tags()->paths("red").size(), 1);
        QSignalSpy errors(app.tags(), &TagStore::errorOccurred);
        QVERIFY(!app.tags()->setTag({QUrl("https://example.invalid")}, "red", true));
        QVERIFY(!app.tags()->setTag({QUrl::fromLocalFile(dir.path())}, "unknown", true));
        QCOMPARE(errors.size(), 2);
    }
    void tagsFollowDirectoryMergeAndOverwrite()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir("Source"));
        QVERIFY(QDir(dir.path()).mkdir("Target"));
        QVERIFY(QDir(dir.path()).mkdir("Target\\Source"));
        writeFile(dir.filePath("Source\\Child.txt"));
        writeFile(dir.filePath("Target\\Source\\Child.txt"));
        AppController app;
        QVERIFY(app.tags()->setTag({QUrl::fromLocalFile(dir.filePath("Source")),
                                   QUrl::fromLocalFile(dir.filePath("Source\\Child.txt"))}, "red", true));
        QVERIFY(app.tags()->setTag({QUrl::fromLocalFile(dir.filePath("Target\\Source")),
                                   QUrl::fromLocalFile(dir.filePath("Target\\Source\\Child.txt"))}, "blue", true));
        app.tasks()->submit("move", {QUrl::fromLocalFile(dir.filePath("Source"))},
                            QUrl::fromLocalFile(dir.filePath("Target")), "overwrite");
        QTRY_VERIFY(!app.busy());
        QVERIFY(!QFileInfo::exists(dir.filePath("Source")));
        QCOMPARE(app.tags()->tags(dir.filePath("Target\\Source\\Child.txt")), QStringList{"red"});
        const auto folderTags = app.tags()->tags(dir.filePath("Target\\Source"));
        QVERIFY(folderTags.contains("red"));
        QVERIFY(folderTags.contains("blue"));
        QCOMPARE(app.tags()->paths("red").size(), 2);
    }
    void completedMovesPersistTagsWhenEventLoopStops()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir("Target"));
        writeFile(dir.filePath("File.txt"));
        TagStore tags;
        QVERIFY(tags.setTag({QUrl::fromLocalFile(dir.filePath("File.txt"))}, "yellow", true));
        {
            TaskManager manager;
            connect(&manager, &TaskManager::pathRelocated, &tags, &TagStore::relocate);
            manager.submit("move", {QUrl::fromLocalFile(dir.filePath("File.txt"))},
                           QUrl::fromLocalFile(dir.filePath("Target")));
            QElapsedTimer timer;
            timer.start();
            // Deliberately do not deliver queued completion callbacks.
            while (QFileInfo::exists(dir.filePath("File.txt")) && timer.elapsed() < 5000)
                QThread::msleep(10);
            QVERIFY(QFileInfo::exists(dir.filePath("Target\\File.txt")));
        }
        QCOMPARE(tags.tags(dir.filePath("Target\\File.txt")), QStringList{"yellow"});
        QVERIFY(tags.tags(dir.filePath("File.txt")).isEmpty());
    }
    void tagViewRejectsFolderOnlyCommands()
    {
        AppController app;
        app.showTag("red");
        QSignalSpy errors(&app, &AppController::notification);
        app.requestFolder();
        app.paste();
        QCOMPARE(errors.size(), 2);
        QVERIFY(app.workspace()->activePane()->tagView());
    }
    void corruptTagMetadataIsNotOverwritten()
    {
        QSettings settings;
        settings.setValue("colorTags/records", QByteArray("{broken"));
        settings.sync();
        TagStore tags;
        QSignalSpy errors(&tags, &TagStore::errorOccurred);
        QVERIFY(!tags.setTag({QUrl::fromLocalFile(m_settings.path())}, "red", true));
        QVERIFY(!errors.isEmpty());
        settings.sync();
        QCOMPARE(settings.value("colorTags/records").toByteArray(), QByteArray("{broken"));
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
