#include "AppController.h"
#include "ShellIconProvider.h"
#include "Version.h"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QCommandLineParser>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QIcon>
#include <QQuickItem>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <cstdio>
#include <cstdlib>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static QQuickItem *findVisualItem(QQuickItem *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems()) {
        if (auto *found = findVisualItem(child, name)) return found;
    }
    return nullptr;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("HExplorer");
    QCoreApplication::setApplicationName("HExplorer");
    QCoreApplication::setApplicationVersion(HEXPLORER_VERSION);
    app.setWindowIcon(QIcon(":/assets/hexplorer.svg"));
    QCommandLineParser parser;
    parser.setApplicationDescription("Native multi-pane desktop file manager");
    parser.addHelpOption();
    parser.addOption({{"v", "version"}, "Display the application version without opening a GUI."});
    parser.addOption({"smoke-test", "Load the interface with isolated settings and exit."});
    parser.addOption({"smoke-screenshot", "Save a rendered image during the smoke test.", "path"});
    parser.addOption({"smoke-log", "Write smoke-test diagnostics to a file.", "path"});
    parser.addPositionalArgument("folder", "Initial folder to open.");
    parser.process(app);
    if (parser.isSet("version")) {
        std::fprintf(stdout, "HExplorer %s\n", HEXPLORER_VERSION);
        return 0;
    }
#ifdef Q_OS_WIN
    // Inno Setup checks this shared mutex before changing installed files.
    // Multiple application instances are allowed; each retains a handle.
    const auto closeMutex = [](void *handle) { if (handle) CloseHandle(handle); };
    std::unique_ptr<void, decltype(closeMutex)> runningMutex(
        parser.isSet("smoke-test") ? nullptr
                                 : CreateMutexW(nullptr, FALSE, L"Local\\HExplorer.Running.61B21CF7-348E-47DF-9D69-744A8F5DA8B7"),
        closeMutex);
    if (!parser.isSet("smoke-test") && !runningMutex) {
        qCritical("Cannot register the application installation lock (Windows error %lu).", GetLastError());
        return 3;
    }
#endif
    std::unique_ptr<QTemporaryDir> smokeDirectory;
    if (parser.isSet("smoke-test")) {
        static QString smokeLogPath;
        smokeLogPath = parser.value("smoke-log");
        if (!smokeLogPath.isEmpty()) {
            QFile log(smokeLogPath);
            if (!log.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 3;
        }
        qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &message) {
            const auto bytes = message.toLocal8Bit();
            std::fprintf(stderr, "%s\n", bytes.constData());
            if (!smokeLogPath.isEmpty()) {
                QFile log(smokeLogPath);
                if (log.open(QIODevice::WriteOnly | QIODevice::Append)) {
                    log.write(bytes);
                    log.write("\n");
                }
            }
            if (type == QtFatalMsg) std::abort();
        });
        smokeDirectory = std::make_unique<QTemporaryDir>();
        if (!smokeDirectory->isValid()) return 3;
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("HExplorer-Smoke");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, smokeDirectory->path());
        QDir(smokeDirectory->path()).mkdir("Pictures");
        QDir(smokeDirectory->path()).mkdir("Copies");
        QFile text(smokeDirectory->filePath("Welcome.txt"));
        if (!text.open(QIODevice::WriteOnly)) return 3;
        text.write("HExplorer smoke test\nLocal text preview.");
        text.close();
        QImage image(64, 64, QImage::Format_RGB32);
        image.fill(QColor("#0067c0"));
        if (!image.save(smokeDirectory->filePath("Pictures\\Sample.png"))) return 3;
    }
    QQuickStyle::setStyle("FluentWinUI3");
    qmlRegisterUncreatableType<DirectoryProxy>("HExplorer.Core", 1, 0, "DirectoryProxy", "Owned by a pane");
    qmlRegisterUncreatableType<PaneController>("HExplorer.Core", 1, 0, "PaneController", "Owned by a workspace");
    qmlRegisterUncreatableType<LayoutNode>("HExplorer.Core", 1, 0, "LayoutNode", "Owned by a workspace");
    qmlRegisterUncreatableType<Workspace>("HExplorer.Core", 1, 0, "Workspace", "Owned by the app");
    qmlRegisterUncreatableType<NavigationSettings>("HExplorer.Core", 1, 0, "NavigationSettings", "Owned by the app");
    qmlRegisterUncreatableType<TagStore>("HExplorer.Core", 1, 0, "TagStore", "Owned by the app");
    qmlRegisterUncreatableType<UpdateController>("HExplorer.Core", 1, 0, "UpdateController", "Owned by the app");
    qmlRegisterUncreatableType<TaskManager>("HExplorer.Core", 1, 0, "TaskManager", "Owned by the app");
    qmlRegisterUncreatableType<NativeServices>("HExplorer.Core", 1, 0, "NativeServices", "Owned by the app");
    qmlRegisterUncreatableType<PreviewController>("HExplorer.Core", 1, 0, "PreviewController", "Owned by the app");
    qmlRegisterUncreatableType<QFileSystemModel>("HExplorer.Core", 1, 0, "FileSystemModel", "Owned by the app");
    AppController controller;
    if (!parser.isSet("smoke-test") && controller.updates()->autoCheck())
        QTimer::singleShot(2000, controller.updates(), &UpdateController::check);
    if (smokeDirectory) {
        for (auto *pane : controller.workspace()->panes())
            pane->navigate(QUrl::fromLocalFile(smokeDirectory->path()));
        controller.workspace()->panes().last()->navigate(QUrl::fromLocalFile(smokeDirectory->filePath("Copies")));
    }
    if (!parser.positionalArguments().isEmpty())
        controller.workspace()->activePane()->navigateText(parser.positionalArguments().first());
    QQmlApplicationEngine engine;
    engine.addImageProvider("shell", new ShellIconProvider);
    engine.rootContext()->setContextProperty("appController", &controller);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    bool qmlWarning = false;
    QObject::connect(&engine, &QQmlEngine::warnings, &app, [&](const QList<QQmlError> &) { qmlWarning = true; });
    engine.loadFromModule("HExplorer", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    if (parser.isSet("smoke-test")) {
        QObject::connect(&controller, &AppController::notification, &app, [&](const QString &message) {
            qWarning("Application notification: %s", qPrintable(message));
            qmlWarning = true;
        });
        auto simulateDrop = [&](QQuickItem *target) {
            auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            if (!target || !window) {
                qWarning("The smoke drop target could not be found.");
                qmlWarning = true;
                return;
            }
            QMimeData mime;
            mime.setUrls({QUrl::fromLocalFile(smokeDirectory->filePath("Welcome.txt"))});
            const auto point = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
            QDragEnterEvent enter(point.toPoint(), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::ControlModifier);
            QCoreApplication::sendEvent(window, &enter);
            QDragMoveEvent move(point.toPoint(), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::ControlModifier);
            QCoreApplication::sendEvent(window, &move);
            QDropEvent drop(point, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::ControlModifier);
            QCoreApplication::sendEvent(window, &drop);
            qInfo("Drag position %.1f,%.1f enter=%d move=%d drop=%d action=%d",
                  point.x(), point.y(), enter.isAccepted(), move.isAccepted(), drop.isAccepted(), int(drop.dropAction()));
            if (!drop.isAccepted() || drop.dropAction() != Qt::CopyAction) {
                qWarning("File drop was not accepted as a safe copy.");
                qmlWarning = true;
            }
        };
        auto *steps = new QTimer(&app);
        steps->setInterval(150);
        QElapsedTimer deadline;
        deadline.start();
        QObject::connect(steps, &QTimer::timeout, &app, [&, simulateDrop, steps, deadline, stage = 0]() mutable {
            if (qmlWarning || deadline.elapsed() > 25000) {
                qWarning("Smoke test stopped in stage %d (warnings=%d).", stage, qmlWarning);
                steps->stop();
                app.exit(2);
                return;
            }
            auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            auto *first = controller.workspace()->panes().first();
            auto *pane = findVisualItem(window->contentItem(), "file-pane-" + first->id());
            if (stage == 0) {
                auto *commandBar = findVisualItem(window->contentItem(), "commandBar");
                auto *quickAccessTitle = findVisualItem(window->contentItem(), "quickAccessTitle");
                if (!commandBar || commandBar->height() != 48 || !quickAccessTitle
                    || quickAccessTitle->property("horizontalAlignment").toInt() != Qt::AlignLeft
                    || !findVisualItem(window->contentItem(), "quickAccessDivider")
                    || !findVisualItem(window->contentItem(), "folderTreeDivider")) {
                    qWarning("Compact toolbar or sidebar section styling is incorrect.");
                    qmlWarning = true;
                    return;
                }
                auto *item = findVisualItem(pane, "file-item-Welcome.txt");
                if (!item || first->loading()) return;
                const auto point = item->mapToScene({item->width() / 2, item->height() / 2});
                QMouseEvent press(QEvent::MouseButtonPress, point, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease, point, point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &press);
                QCoreApplication::sendEvent(window, &release);
                if (first->files()->selectionCount() != 1) {
                    qWarning("Clicking a file did not select it.");
                    qmlWarning = true;
                }
            } else if (stage == 1) {
                if (controller.preview()->kind() != "text") return;
                auto *last = controller.workspace()->panes().last();
                auto *target = findVisualItem(window->contentItem(), "file-pane-" + last->id());
                if (!target || last->loading()) return;
                simulateDrop(target);
            } else if (stage == 2) {
                if (controller.busy() || first->loading()
                    || !QFileInfo::exists(smokeDirectory->filePath("Copies\\Welcome.txt"))) return;
                auto *folder = findVisualItem(pane, "file-item-Pictures");
                if (!folder) return;
                simulateDrop(folder);
            } else if (stage == 3) {
                if (controller.busy() || !QFileInfo::exists(smokeDirectory->filePath("Pictures\\Welcome.txt"))) return;
                if (auto *popup = engine.rootObjects().first()->findChild<QObject*>("taskCenter"))
                    QMetaObject::invokeMethod(popup, "open");
                controller.workspace()->preset(4);
            } else if (stage == 4) {
                controller.workspace()->preset(3);
            } else if (stage == 5) {
                controller.workspace()->split(controller.workspace()->activePane(), false);
                controller.setSwapSides(true);
            } else if (stage == 6) {
                controller.workspace()->closePane(controller.workspace()->activePane());
                controller.setTheme(2);
                controller.preview()->preview(QUrl::fromLocalFile(smokeDirectory->filePath("Pictures\\Sample.png")));
            } else if (stage == 7) {
                if (controller.preview()->kind() != "image") return;
            } else if (stage == 8) {
                if (auto *popup = engine.rootObjects().first()->findChild<QObject*>("taskCenter"))
                    QMetaObject::invokeMethod(popup, "close");
                controller.navigation()->setExpanded(false);
                controller.navigation()->setVisible(false);
                controller.navigation()->add("Smoke custom folder", smokeDirectory->filePath("Copies"));
                if (auto *dialog = engine.rootObjects().first()->findChild<QObject*>("navigationDialog"))
                    QMetaObject::invokeMethod(dialog, "open");
                else qmlWarning = true;
                auto *tabs = engine.rootObjects().first()->findChild<QObject*>("settingsTabs");
                if (!tabs || !tabs->setProperty("currentIndex", 1)) qmlWarning = true;
                if (controller.updates()->currentVersion() != QStringLiteral(HEXPLORER_VERSION)
                    || controller.updates()->busy()) qmlWarning = true;
            } else if (stage == 9) {
                if (parser.isSet("smoke-screenshot")
                    && !window->grabWindow().save(parser.value("smoke-screenshot") + ".updates.png"))
                    qmlWarning = true;
                if (auto *dialog = engine.rootObjects().first()->findChild<QObject*>("navigationDialog"))
                    QMetaObject::invokeMethod(dialog, "close");
                controller.navigation()->setVisible(true);
                controller.workspace()->activate(first);
                if (!controller.tags()->setTag({QUrl::fromLocalFile(smokeDirectory->filePath("Welcome.txt")),
                                                QUrl::fromLocalFile(smokeDirectory->filePath("Pictures\\Sample.png"))},
                                               "red", true)) qmlWarning = true;
                first->showTag("red");
            } else if (stage == 10) {
                if (first->loading()) return;
                if (first->files()->count() != 2) { qmlWarning = true; return; }
                first->files()->selectAll();
                if (auto *menu = engine.rootObjects().first()->findChild<QObject*>("toolbarTagMenu"))
                    QMetaObject::invokeMethod(menu, "open");
                else qmlWarning = true;
            } else if (stage == 11) {
                auto *menu = engine.rootObjects().first()->findChild<QObject*>("toolbarTagMenu");
                auto *content = menu ? menu->property("contentItem").value<QQuickItem*>() : nullptr;
                auto *item = findVisualItem(content, "tag-action-blue");
                if (!item) { qmlWarning = true; return; }
                if (!QMetaObject::invokeMethod(item, "click")) qmlWarning = true;
            } else if (stage == 12) {
                if (controller.tags()->paths("blue").size() != 2) qmlWarning = true;
                auto *menu = engine.rootObjects().first()->findChild<QObject*>("toolbarTagMenu");
                if (menu && menu->property("visible").toBool()) return;
            } else if (stage == 13) {
                if (first->loading()) return;
                if (first->files()->count() != 2) qmlWarning = true;
                first->showTag("blue");
            } else if (stage == 14) {
                if (first->loading()) return;
                if (first->files()->count() != 2) qmlWarning = true;
                first->setGrid(true);
            } else if (stage == 15) {
                for (const auto &name : {QString("Welcome.txt"), QString("Sample.png")}) {
                    auto *item = findVisualItem(pane, "file-item-" + name);
                    auto *icon = findVisualItem(item, "file-icon");
                    if (!icon || icon->property("status").toInt() != 1) return;
                }
            } else if (stage == 16) {
                first->setGrid(false);
                controller.setSwapSides(false);
                controller.navigation()->setExpanded(true);
                controller.preview()->preview(QUrl::fromLocalFile(smokeDirectory->filePath("Pictures\\Sample.png")));
            } else if (stage == 17) {
                if (controller.preview()->kind() != "image") return;
                if (parser.isSet("smoke-screenshot")
                    && !window->grabWindow().save(parser.value("smoke-screenshot") + ".dark.png"))
                    qmlWarning = true;
                controller.setTheme(1);
            } else {
                if (parser.isSet("smoke-screenshot")
                    && !window->grabWindow().save(parser.value("smoke-screenshot")))
                    qmlWarning = true;
                qInfo("Smoke result: warnings=%d; selection, drops, previews, layouts, navigation settings and multi-color tag views exercised.", qmlWarning);
                steps->stop();
                app.exit(qmlWarning ? 2 : 0);
            }
            ++stage;
        });
        steps->start();
    }
    return app.exec();
}
