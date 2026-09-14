#include "AppController.h"

#include <QDir>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStyleHints>
#include <QDropEvent>
#include <QLocale>
#include <QtConcurrent>
#include <limits>

AppController::AppController(QObject *parent) : QObject(parent)
{
    QGuiApplication::instance()->installEventFilter(this);
    m_pool.setMaxThreadCount(1);
    m_tree.setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
    m_tree.setReadOnly(true);
    m_tree.setOption(QFileSystemModel::DontUseCustomDirectoryIcons);
    m_tree.setRootPath(QString());
    m_leftVisible = m_settings.value("sidebar/left", true).toBool();
    m_rightVisible = m_settings.value("sidebar/right", true).toBool();
    m_swapSides = m_settings.value("sidebar/swapped", false).toBool();
    m_leftWidth = qBound(160.0, m_settings.value("sidebar/leftWidth", 220).toDouble(), 600.0);
    m_rightWidth = qBound(200.0, m_settings.value("sidebar/rightWidth", 280).toDouble(), 600.0);
    setTheme(m_settings.value("theme", 0).toInt());
    connect(&m_workspace, &Workspace::openFile, &m_native, &NativeServices::open);
    connect(&m_workspace, &Workspace::errorOccurred, this, &AppController::notification);
    connect(&m_native, &NativeServices::errorOccurred, this, &AppController::notification);
    connect(&m_tasks, &TaskManager::errorOccurred, this, &AppController::notification);
    connect(&m_tasks, &TaskManager::taskFinished, this, &AppController::refreshAll);
    connect(&m_tasks, &TaskManager::successfulMove, this, [this](const QString &, const QVariantList &urls) {
        m_native.clearCutClipboard(urls);
    });
    connect(&m_tasks, &TaskManager::busyChanged, this, &AppController::busyChanged);
    connect(&m_workspace, &Workspace::selectionChanged, this, &AppController::updatePreview);
    connect(&m_workspace, &Workspace::activePaneChanged, this, &AppController::updatePreview);
    connect(&m_preview, &PreviewController::pinnedChanged, this, [this] {
        if (!m_preview.pinned()) updatePreview();
    });
    const auto savedWorkspace = m_settings.value("workspace").toByteArray();
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(savedWorkspace, &parseError);
    m_workspace.restore(document.object());
    if (!savedWorkspace.isEmpty() && (parseError.error != QJsonParseError::NoError || !document.isObject())) {
        m_settings.setValue("workspaceRecovery", savedWorkspace);
        QTimer::singleShot(0, this, [this] {
            emit notification(tr("The saved layout could not be read. A default layout was opened; the original data was kept in workspaceRecovery."));
        });
    }
    m_saveTimer.setInterval(5000);
    connect(&m_saveTimer, &QTimer::timeout, this, &AppController::saveSession);
    m_saveTimer.start();
}

bool AppController::eventFilter(QObject *watched, QEvent *event)
{
    // Qt 6.8's QML DragEvent does not expose keyboard modifiers.
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove || event->type() == QEvent::Drop)
        m_dragModifiers = static_cast<QDropEvent*>(event)->modifiers();
    else if (event->type() == QEvent::DragLeave)
        m_dragModifiers = Qt::NoModifier;
    return QObject::eventFilter(watched, event);
}

AppController::~AppController()
{
    saveSession();
    m_pool.waitForDone();
}

bool AppController::busy() const { return m_tasks.busy() || m_mutations > 0; }

QVariantList AppController::places() const
{
    QVariantList list;
    auto append = [&](const QString &name, const QString &path) {
        if (!path.isEmpty())
            list.append(QVariantMap{{"name", name}, {"url", QUrl::fromLocalFile(path)}});
    };
    append(tr("Home"), QDir::homePath());
    append(tr("Desktop"), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    append(tr("Documents"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    append(tr("Downloads"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    append(tr("Pictures"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    for (const auto &drive : QDir::drives())
        append(QDir::toNativeSeparators(drive.absoluteFilePath()), drive.absoluteFilePath());
    return list;
}
void AppController::setLeftVisible(bool value) { m_leftVisible = value; emit settingsChanged(); }
void AppController::setRightVisible(bool value) { m_rightVisible = value; emit settingsChanged(); }
void AppController::setSwapSides(bool value) { m_swapSides = value; emit settingsChanged(); }
void AppController::setLeftWidth(double value) { m_leftWidth = qBound(160.0, value, 600.0); emit settingsChanged(); }
void AppController::setRightWidth(double value) { m_rightWidth = qBound(200.0, value, 600.0); emit settingsChanged(); }
void AppController::setTheme(int value)
{
    m_theme = qBound(0, value, 2);
    QGuiApplication::styleHints()->setColorScheme(static_cast<Qt::ColorScheme>(m_theme));
    emit settingsChanged();
}
QVariantList AppController::selection() const
{
    auto *pane = m_workspace.activePane();
    return pane ? pane->files()->selectedUrls() : QVariantList();
}
void AppController::updatePreview()
{
    const auto urls = selection();
    m_preview.preview(urls.size() == 1 ? urls.first().toUrl() : QUrl());
}
void AppController::copy(bool cut)
{
    const auto urls = selection();
    if (!urls.isEmpty())
        m_native.setClipboard(urls, cut);
}
void AppController::paste()
{
    auto *pane = m_workspace.activePane();
    if (!pane) return;
    const auto clipboard = m_native.clipboardFiles();
    const auto urls = clipboard.value("urls").toList();
    if (urls.isEmpty()) {
        emit notification(tr("The clipboard does not contain local files."));
        return;
    }
    const bool cut = clipboard.value("cut").toBool();
    m_tasks.submit(cut ? "move" : "copy", urls, pane->url());
}
void AppController::requestDelete(bool permanent)
{
    m_deleteSources = selection();
    m_permanent = permanent;
    if (!m_deleteSources.isEmpty())
        emit deletionRequested(m_deleteSources.size(), permanent);
}
void AppController::confirmDelete()
{
    if (m_deleteSources.isEmpty()) return;
    m_tasks.submit(m_permanent ? "delete" : "recycle", m_deleteSources, {});
    m_deleteSources.clear();
}
void AppController::requestRename()
{
    const auto urls = selection();
    if (urls.size() != 1) {
        emit notification(tr("Select exactly one item to rename."));
        return;
    }
    m_createDirectory = false;
    m_nameSource = urls.first().toUrl().toLocalFile();
    m_nameDirectory = QUrl::fromLocalFile(QFileInfo(m_nameSource).absolutePath());
    emit nameRequested(tr("Rename"), QFileInfo(m_nameSource).fileName());
}
void AppController::requestFolder()
{
    auto *pane = m_workspace.activePane();
    if (!pane) return;
    m_createDirectory = true;
    m_nameSource.clear();
    m_nameDirectory = pane->url();
    emit nameRequested(tr("New folder"), tr("New folder"));
}
void AppController::acceptName(QString name)
{
    static const QRegularExpression invalid(R"([<>:"/\\|?*\x00-\x1f])");
    static const QRegularExpression reserved(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?$)",
                                             QRegularExpression::CaseInsensitiveOption);
    if (name.isEmpty() || name == "." || name == ".." || name.endsWith('.') || name.endsWith(' ')
        || name.contains(invalid) || reserved.match(name).hasMatch()) {
        emit notification(tr("This name is not valid on Windows. No changes were made."));
        return;
    }
    runMutation(m_nameSource, QDir(m_nameDirectory.toLocalFile()).filePath(name), m_createDirectory);
}
void AppController::runMutation(QString source, QString destination, bool createDirectory)
{
    ++m_mutations;
    emit busyChanged();
    (void)QtConcurrent::run(&m_pool, [this, source, destination, createDirectory] {
        QString error;
        if (QFileInfo::exists(destination))
            error = tr("An item with that name already exists.");
        else if (createDirectory) {
            if (!QDir().mkdir(destination))
                error = tr("Could not create the folder. Check permissions and the parent folder.");
        } else {
            QFile file(source);
            if (!file.rename(destination))
                error = file.errorString();
        }
        QMetaObject::invokeMethod(this, [this, error] {
            --m_mutations;
            emit busyChanged();
            if (!error.isEmpty()) emit notification(error);
            refreshAll();
        });
    });
}
void AppController::extract()
{
    auto *pane = m_workspace.activePane();
    const auto urls = selection();
    if (!pane || urls.isEmpty()) return;
    m_tasks.submit("extract", urls, pane->url());
}
void AppController::properties()
{
    const auto urls = selection();
    if (urls.size() == 1) m_native.properties(urls.first().toUrl());
    else if (auto *pane = m_workspace.activePane(); urls.isEmpty() && pane)
        m_native.properties(pane->url());
    else emit notification(tr("Select one item to show its properties."));
}
void AppController::contextMenu(double x, double y)
{
    auto urls = selection();
    if (urls.isEmpty() && m_workspace.activePane())
        urls.append(m_workspace.activePane()->url());
    m_native.contextMenu(urls, x, y);
}
void AppController::navigateTree(QModelIndex index)
{
    if (auto *pane = m_workspace.activePane(); pane && index.isValid())
        pane->navigate(QUrl::fromLocalFile(m_tree.filePath(index)));
}

int AppController::dropAction(QList<QUrl> urls, QUrl destination, int modifiers, int supportedActions)
{
    if (urls.isEmpty() || !destination.isLocalFile()) return Qt::IgnoreAction;
    if (!m_native.ownsDrag() && !(supportedActions & Qt::CopyAction))
        return Qt::IgnoreAction;
    for (const auto &url : urls)
        if (!url.isLocalFile()) return Qt::IgnoreAction;
    int action = Qt::CopyAction;
    if ((modifiers & Qt::ControlModifier) && (modifiers & Qt::ShiftModifier))
        return Qt::IgnoreAction; // Shortcut creation is not a file copy or move.
    if (modifiers & Qt::ControlModifier) action = Qt::CopyAction;
    else if (modifiers & Qt::ShiftModifier) action = Qt::MoveAction;
    else {
        const auto device = QStorageInfo(destination.toLocalFile()).device();
        bool sameVolume = !device.isEmpty();
        for (const auto &url : urls)
            sameVolume &= QStorageInfo(url.toLocalFile()).device() == device;
        action = sameVolume ? Qt::MoveAction : Qt::CopyAction;
        if (!(supportedActions & action))
            action = supportedActions & Qt::CopyAction ? Qt::CopyAction : Qt::MoveAction;
    }
    return (supportedActions & action) ? action : Qt::IgnoreAction;
}

int AppController::dropAcknowledgement(int action, int supportedActions) const
{
    // The worker owns the move. Do not let an external drag source delete its
    // files when this asynchronous operation has merely been queued.
    if (m_native.ownsDrag()) return action;
    return supportedActions & Qt::CopyAction ? Qt::CopyAction : Qt::IgnoreAction;
}

bool AppController::drop(QList<QUrl> urls, QUrl destination, int action)
{
    if (action != Qt::CopyAction && action != Qt::MoveAction) {
        emit notification(tr("This drop does not support copying or moving local files."));
        return false;
    }
    QVariantList sources;
    for (const auto &url : urls) sources.append(url);
    return !m_tasks.submit(action == Qt::CopyAction ? "copy" : "move", sources, destination).isEmpty();
}
void AppController::refreshAll()
{
    for (auto *pane : m_workspace.panes()) pane->refresh();
    updatePreview();
}
void AppController::saveSession()
{
    m_settings.setValue("workspace", QJsonDocument(m_workspace.save()).toJson(QJsonDocument::Compact));
    m_settings.setValue("sidebar/left", m_leftVisible);
    m_settings.setValue("sidebar/right", m_rightVisible);
    m_settings.setValue("sidebar/swapped", m_swapSides);
    m_settings.setValue("sidebar/leftWidth", m_leftWidth);
    m_settings.setValue("sidebar/rightWidth", m_rightWidth);
    m_settings.setValue("theme", m_theme);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError && !m_reportedSettingsError) {
        m_reportedSettingsError = true;
        emit notification(tr("Your workspace settings could not be saved. Check write permissions and available disk space."));
    }
}
void AppController::setWindowGeometry(int width, int height, bool maximized)
{
    m_settings.setValue("window/maximized", maximized);
    if (!maximized) {
        m_settings.setValue("window/width", width);
        m_settings.setValue("window/height", height);
    }
}
QVariantMap AppController::windowGeometry() const
{
    return {{"width", m_settings.value("window/width", 1440)},
            {"height", m_settings.value("window/height", 900)},
            {"maximized", m_settings.value("window/maximized", false)}};
}

QString AppController::transferSummary(qulonglong done, qulonglong total, double speed) const
{
    const auto format = [](qulonglong bytes) {
        return QLocale().formattedDataSize(static_cast<qint64>(
            qMin(bytes, static_cast<qulonglong>(std::numeric_limits<qint64>::max()))));
    };
    const auto amount = tr("%1 of %2").arg(format(done), format(total));
    return speed > 0 ? tr("%1  |  %2/s").arg(amount, format(static_cast<qulonglong>(qMin(speed, 1e18)))) : amount;
}
