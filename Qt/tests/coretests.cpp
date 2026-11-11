#include "DirectoryModel.h"
#include "PaneController.h"
#include "Workspace.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QJsonArray>
#include <QtTest>

class CoreTests : public QObject {
    Q_OBJECT
private:
    static void writeFile(const QString &path, const QByteArray &contents = "hello")
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), contents.size());
    }
private slots:
    void asynchronousDirectoryAndSelection()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkdir("Folder"));
        writeFile(dir.filePath("file10.txt"));
        writeFile(dir.filePath("file2.txt"));
        PaneController pane;
        pane.navigate(QUrl::fromLocalFile(dir.path()));
        QTRY_VERIFY_WITH_TIMEOUT(!pane.loading(), 5000);
        QCOMPARE(pane.error(), QString());
        QCOMPARE(pane.files()->count(), 3);
        QVERIFY(pane.files()->entry(0).value("isDirectory").toBool());
        QCOMPARE(pane.files()->entry(1).value("fileName").toString(), QString("file2.txt"));
        pane.files()->select(1);
        pane.files()->select(2, Qt::ControlModifier);
        QCOMPARE(pane.files()->selectionCount(), 2);
        pane.files()->select(1, Qt::ControlModifier);
        QCOMPARE(pane.files()->selectionCount(), 1);
        pane.setQuery("file2");
        QCOMPARE(pane.files()->count(), 1);
        QCOMPARE(pane.files()->selectionCount(), 0);
        pane.files()->selectAll();
        QCOMPARE(pane.files()->selectionCount(), 1);
    }
    void navigationHistory()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir("Child"));
        PaneController pane;
        pane.navigate(QUrl::fromLocalFile(dir.path()));
        pane.navigateText("Child");
        QCOMPARE(pane.url(), QUrl::fromLocalFile(dir.filePath("Child")));
        QVERIFY(pane.canBack());
        pane.back();
        QCOMPARE(pane.url(), QUrl::fromLocalFile(dir.path()));
        QVERIFY(pane.canForward());
        pane.forward();
        pane.up();
        QCOMPARE(pane.url(), QUrl::fromLocalFile(dir.path()));
        QVERIFY(!pane.canForward());
    }
    void rectangularSelection()
    {
        QTemporaryDir dir;
        for (int i = 0; i < 8; ++i)
            writeFile(dir.filePath(QString::number(i)));
        PaneController pane;
        pane.navigate(QUrl::fromLocalFile(dir.path()));
        QTRY_VERIFY_WITH_TIMEOUT(!pane.loading(), 5000);
        pane.files()->beginBoxSelection(false);
        pane.files()->selectBox(QRectF(0, 0, 190, 190), 100, 100, 4);
        QCOMPARE(pane.files()->selectionCount(), 4);
        pane.files()->beginBoxSelection(true);
        pane.files()->selectBox(QRectF(290, 190, -80, -80), 100, 100, 4);
        QCOMPARE(pane.files()->selectionCount(), 5);
    }
    void staleScanDoesNotReplaceNewFolder()
    {
        QTemporaryDir a;
        QTemporaryDir b;
        for (int i = 0; i < 600; ++i)
            writeFile(a.filePath(QString::number(i)));
        writeFile(b.filePath("correct.txt"));
        PaneController pane;
        pane.navigate(QUrl::fromLocalFile(a.path()));
        pane.navigate(QUrl::fromLocalFile(b.path()));
        QTRY_VERIFY_WITH_TIMEOUT(!pane.loading(), 5000);
        QCOMPARE(pane.files()->count(), 1);
        QCOMPARE(pane.files()->entry(0).value("fileName").toString(), QString("correct.txt"));
    }
    void inaccessibleDirectoryReportsError()
    {
        QTemporaryDir dir;
        PaneController pane;
        pane.navigate(QUrl::fromLocalFile(dir.filePath("missing")));
        QTRY_VERIFY_WITH_TIMEOUT(!pane.loading(), 5000);
        QVERIFY(!pane.error().isEmpty());
        QCOMPARE(pane.files()->count(), 0);
    }
    void invalidNavigationIsRejected()
    {
        PaneController pane;
        QSignalSpy errors(&pane, &PaneController::errorOccurred);
        pane.navigate(QUrl("https://example.invalid/"));
        QCOMPARE(errors.count(), 1);
        QVERIFY(pane.url().isEmpty());
    }
    void tabsKeepIndependentNavigationSelectionAndOptions()
    {
        QTemporaryDir a;
        QTemporaryDir b;
        QVERIFY(QDir(a.path()).mkdir("Child"));
        writeFile(a.filePath("first.txt"));
        writeFile(b.filePath("second.txt"));
        PaneController pane;
        pane.navigate(QUrl::fromLocalFile(a.path()));
        pane.navigateText("Child");
        pane.back();
        QTRY_VERIFY(!pane.loading());
        pane.setQuery("first");
        pane.files()->selectAll();
        pane.setCurrentRow(0);
        pane.setScrollPosition(84);
        pane.setGrid(true);
        pane.setSortKey(2);
        pane.setDescending(true);
        auto *firstModel = pane.files();
        const auto firstId = pane.tabs().first().toMap().value("id");
        QSignalSpy switches(&pane, &PaneController::activeTabChanged);
        QSignalSpy selections(&pane, &PaneController::selectionChanged);
        pane.newTab(QUrl::fromLocalFile(b.path()));
        QCOMPARE(pane.activeTab(), 1);
        QCOMPARE(pane.tabs().size(), 2);
        QVERIFY(pane.files() != firstModel);
        QVERIFY(!pane.grid());
        QVERIFY(!pane.canBack());
        QVERIFY(pane.query().isEmpty());
        QCOMPARE(pane.files()->selectionCount(), 0);
        QTRY_VERIFY(!pane.loading());
        QCOMPARE(pane.files()->entry(0).value("fileName").toString(), QString("second.txt"));
        pane.files()->selectAll();
        pane.setScrollPosition(20);
        pane.switchTab(0);
        QCOMPARE(pane.files(), firstModel);
        QCOMPARE(pane.tabs().first().toMap().value("id"), firstId);
        QCOMPARE(pane.files()->selectionCount(), 1);
        QCOMPARE(pane.query(), QString("first"));
        QCOMPARE(pane.currentRow(), 0);
        QCOMPARE(pane.scrollPosition(), 84.0);
        QVERIFY(pane.grid());
        QVERIFY(pane.descending());
        QCOMPARE(pane.sortKey(), 2);
        QVERIFY(pane.canForward());
        QVERIFY(switches.count() >= 2);
        QVERIFY(selections.count() >= 2);
        pane.forward();
        QCOMPARE(pane.url(), QUrl::fromLocalFile(a.filePath("Child")));
        pane.switchTab(1);
        QCOMPARE(pane.files()->selectionCount(), 1);
        QCOMPARE(pane.scrollPosition(), 20.0);
        QCOMPARE(pane.url(), QUrl::fromLocalFile(b.path()));
    }
    void tabsCloseWithoutChangingOtherTabState()
    {
        QTemporaryDir dir;
        PaneController pane;
        pane.navigate(QUrl::fromLocalFile(dir.path()));
        pane.newTab();
        pane.newTab();
        auto *third = pane.files();
        const auto thirdId = pane.tabs().at(2).toMap().value("id");
        pane.closeTab(0);
        QCOMPARE(pane.tabs().size(), 2);
        QCOMPARE(pane.activeTab(), 1);
        QCOMPARE(pane.files(), third);
        QCOMPARE(pane.tabs().at(1).toMap().value("id"), thirdId);
        pane.nextTab();
        QCOMPARE(pane.activeTab(), 0);
        pane.nextTab(-1);
        QCOMPARE(pane.activeTab(), 1);
        pane.closeTab(1);
        QCOMPARE(pane.tabs().size(), 1);
        QCOMPARE(pane.activeTab(), 0);
        QVERIFY(pane.files() != third);
        pane.closeTab(0);
        QCOMPARE(pane.tabs().size(), 1); // Closing the final tab does not close its pane.
        QSignalSpy errors(&pane, &PaneController::errorOccurred);
        pane.newTab(QUrl("https://example.invalid"));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(pane.tabs().size(), 1);
        pane.switchTab(-1);
        pane.switchTab(100);
        pane.closeTab(100);
        QCOMPARE(pane.activeTab(), 0);
    }
    void multiTabSessionRestoresAndMigratesLegacyPane()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir("Child"));
        TagStore tags;
        Workspace workspace;
        workspace.setTagStore(&tags);
        workspace.preset(2);
        auto *pane = workspace.panes().first();
        pane->navigate(QUrl::fromLocalFile(dir.path()));
        pane->navigateText("Child");
        pane->setGrid(true);
        pane->setQuery("documents");
        pane->setScrollPosition(150);
        pane->newTab(QUrl("hexplorer-tag:/red"));
        workspace.panes().last()->newTab(QUrl::fromLocalFile(dir.filePath("Child")));
        workspace.activate(workspace.panes().last());
        const auto saved = workspace.save();
        Workspace restored;
        restored.setTagStore(&tags);
        restored.restore(saved);
        QCOMPARE(restored.save(), saved);
        QCOMPARE(restored.panes().first()->tabs().size(), 2);
        QCOMPARE(restored.activePane(), restored.panes().last());
        auto *restoredFirst = restored.panes().first();
        QCOMPARE(restoredFirst->tagId(), QString("red"));
        restoredFirst->switchTab(0);
        QTRY_VERIFY(!restoredFirst->loading());
        QCOMPARE(restoredFirst->url(), QUrl::fromLocalFile(dir.filePath("Child")));
        QCOMPARE(restoredFirst->query(), QString("documents"));
        QVERIFY(restoredFirst->grid());
        QCOMPARE(restoredFirst->scrollPosition(), 150.0);
        QVERIFY(restoredFirst->canBack());

        PaneController legacy;
        legacy.restore({{"url", QUrl::fromLocalFile(dir.path()).toString()}, {"grid", true},
                        {"history", QJsonArray{QUrl::fromLocalFile(dir.path()).toString()}}, {"historyIndex", 0}});
        QCOMPARE(legacy.tabs().size(), 1);
        QCOMPARE(legacy.url(), QUrl::fromLocalFile(dir.path()));
        QVERIFY(legacy.grid());
        QCOMPARE(legacy.save().value("tabs").toArray().size(), 1);
    }
    void closingTabDuringEnumerationKeepsSurvivorUsable()
    {
        QTemporaryDir a;
        QTemporaryDir b;
        for (int i = 0; i < 600; ++i) writeFile(a.filePath(QString::number(i)));
        writeFile(b.filePath("survivor.txt"));
        PaneController pane;
        pane.navigate(QUrl::fromLocalFile(b.path()));
        pane.newTab(QUrl::fromLocalFile(a.path()));
        pane.closeTab(1);
        QTRY_VERIFY(!pane.loading());
        QCOMPARE(pane.files()->count(), 1);
        QCOMPARE(pane.files()->entry(0).value("fileName").toString(), QString("survivor.txt"));
    }
    void layoutsAndPersistence()
    {
        QTemporaryDir dir;
        Workspace workspace;
        workspace.preset(1);
        workspace.activePane()->navigate(QUrl::fromLocalFile(dir.path()));
        auto *original = workspace.activePane();
        workspace.preset(4);
        QCOMPARE(workspace.panes().size(), 4);
        QCOMPARE(workspace.dividers().size(), 3);
        QCOMPARE(workspace.panes().first(), original);
        double area = 0;
        for (auto *pane : workspace.panes())
            area += pane->rect().width() * pane->rect().height();
        QVERIFY(qAbs(area - 1.0) < 0.000001);
        workspace.split(original, false);
        QCOMPARE(workspace.panes().size(), 5);
        auto *added = workspace.activePane();
        workspace.closePane(added);
        QCOMPARE(workspace.panes().size(), 4);
        auto *divider = workspace.dividers().first();
        workspace.resizeSplit(divider, 0.3, 0.3);
        const auto saved = workspace.save();
        Workspace restored;
        restored.restore(saved);
        QCOMPARE(restored.panes().size(), 4);
        QCOMPARE(restored.save(), saved);
        workspace.activate(workspace.panes().last());
        auto *active = workspace.activePane();
        workspace.preset(1);
        QCOMPARE(workspace.activePane(), active);
        QCOMPARE(workspace.panes().first(), active);
    }
    void invalidSavedLayoutRecovers()
    {
        Workspace workspace;
        workspace.restore({{"first", QJsonObject{{"pane", QJsonObject{}}}}});
        QCOMPARE(workspace.panes().size(), 2);
        QVERIFY(workspace.activePane());
        QCOMPARE(workspace.dividers().size(), 1);
    }
    void largeDirectoryKeepsEventLoopResponsive()
    {
        QTemporaryDir dir;
        for (int i = 0; i < 10000; ++i)
            writeFile(dir.filePath(QString("item-%1.txt").arg(i)));
        PaneController pane;
        int ticks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(1);
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
        heartbeat.start();
        pane.navigate(QUrl::fromLocalFile(dir.path()));
        QTRY_VERIFY_WITH_TIMEOUT(!pane.loading(), 20000);
        QCOMPARE(pane.files()->count(), 10000);
        QVERIFY(ticks > 0);
        pane.setQuery("item-999");
        QCOMPARE(pane.files()->count(), 11);
    }
};

QTEST_GUILESS_MAIN(CoreTests)
#include "coretests.moc"
