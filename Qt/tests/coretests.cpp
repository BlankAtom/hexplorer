#include "DirectoryModel.h"
#include "PaneController.h"
#include "Workspace.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
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
