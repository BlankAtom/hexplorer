#include "DirectoryModel.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QtConcurrent>
#include <filesystem>

DirectoryModel::DirectoryModel(QObject *parent) : QAbstractListModel(parent)
{
    m_pool.setMaxThreadCount(2);
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(250);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_refresh,
            qOverload<>(&QTimer::start));
    connect(&m_refresh, &QTimer::timeout, this, [this] { if (m_url.isLocalFile()) load(m_url, m_hidden); });
}

DirectoryModel::~DirectoryModel()
{
    if (m_cancel)
        m_cancel->store(true);
    m_pool.clear();
    m_pool.waitForDone();
}

int DirectoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant DirectoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const auto &entry = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return entry.name;
    case UrlRole: return QUrl::fromLocalFile(entry.path);
    case PathRole: return entry.path;
    case SizeRole: return entry.size;
    case SizeTextRole: return entry.directory ? QString() : QLocale().formattedDataSize(entry.size);
    case ModifiedRole: return entry.modified;
    case ModifiedTextRole: return QLocale().toString(entry.modified, QLocale::ShortFormat);
    case DirectoryRole: return entry.directory;
    case LinkRole: return entry.link;
    case IconRole: return "image://shell/" + QString::fromLatin1(QUrl::toPercentEncoding(entry.path));
    default: return {};
    }
}

QHash<int, QByteArray> DirectoryModel::roleNames() const
{
    return {{NameRole, "fileName"}, {UrlRole, "fileUrl"}, {PathRole, "filePath"},
            {SizeRole, "fileSize"}, {SizeTextRole, "sizeText"}, {ModifiedRole, "modified"},
            {ModifiedTextRole, "modifiedText"}, {DirectoryRole, "isDirectory"},
            {IconRole, "iconUrl"}, {LinkRole, "isLink"}};
}

void DirectoryModel::load(const QUrl &url, bool hidden)
{
    if (m_cancel)
        m_cancel->store(true);
    m_pool.clear();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_cancel;
    const quint64 generation = ++m_generation;
    m_url = url;
    m_hidden = hidden;
    m_error.clear();
    emit errorChanged();
    m_loading = true;
    emit loadingChanged();
    beginResetModel();
    m_entries.clear();
    endResetModel();
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    const QString path = url.toLocalFile();
    // The watcher is best-effort; manual refresh always remains available.
    if (!path.isEmpty())
        m_watcher.addPath(path);
    (void)QtConcurrent::run(&m_pool, [this, path, hidden, generation, cancel] {
        const QFileInfo root(path);
        QString error;
        if (path.isEmpty() || !root.exists() || !root.isDir())
            error = tr("This folder is no longer available.");
        else if (!root.isReadable())
            error = tr("You do not have permission to read this folder.");
        if (error.isEmpty()) {
            std::error_code scanError;
#ifdef Q_OS_WIN
            const std::filesystem::path nativePath(path.toStdWString());
#else
            const std::filesystem::path nativePath(path.toStdString());
#endif
            std::filesystem::directory_iterator iterator(nativePath, scanError);
            const std::filesystem::directory_iterator end;
            QList<FileEntry> batch;
            batch.reserve(256);
            while (!scanError && iterator != end && !cancel->load()) {
#ifdef Q_OS_WIN
                const QFileInfo info(QString::fromStdWString(iterator->path().native()));
#else
                const QFileInfo info(QString::fromStdString(iterator->path().native()));
#endif
                if (hidden || !info.isHidden())
                    batch.append({info.fileName(), info.absoluteFilePath(), info.size(),
                                  info.lastModified(), info.isDir(), info.isSymbolicLink() || info.isJunction()});
                if (batch.size() == 256) {
                    QMetaObject::invokeMethod(this, [this, generation, batch] { append(generation, batch); });
                    batch.clear();
                }
                iterator.increment(scanError);
            }
            if (scanError)
                error = tr("Could not read the folder: %1").arg(QString::fromStdString(scanError.message()));
            if (!batch.isEmpty() && !cancel->load())
                QMetaObject::invokeMethod(this, [this, generation, batch] { append(generation, batch); });
        }
        if (!cancel->load()) {
            QMetaObject::invokeMethod(this, [this, generation, error] {
                if (generation != m_generation)
                    return;
                m_loading = false;
                m_error = error;
                emit loadingChanged();
                emit errorChanged();
                emit scanFinished();
            });
        }
    });
}

void DirectoryModel::append(quint64 generation, QList<FileEntry> entries)
{
    if (generation != m_generation || entries.isEmpty())
        return;
    const int start = m_entries.size();
    beginInsertRows({}, start, start + entries.size() - 1);
    m_entries.append(entries);
    endInsertRows();
}

void DirectoryModel::loadTagged(const QStringList &paths)
{
    m_refresh.stop();
    if (m_cancel) m_cancel->store(true);
    m_pool.clear();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_cancel;
    const auto generation = ++m_generation;
    m_url = {};
    if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
    m_error.clear();
    emit errorChanged();
    m_loading = true;
    emit loadingChanged();
    beginResetModel();
    m_entries.clear();
    endResetModel();
    (void)QtConcurrent::run(&m_pool, [this, paths, cancel, generation] {
        QList<FileEntry> batch;
        int missing = 0;
        for (const auto &path : paths) {
            if (cancel->load()) return;
            const QFileInfo info(path);
            if (!info.exists()) { ++missing; continue; }
            batch.append({info.fileName(), info.absoluteFilePath(), info.size(), info.lastModified(),
                          info.isDir(), info.isSymbolicLink() || info.isJunction()});
            if (batch.size() == 256) {
                QMetaObject::invokeMethod(this, [this, generation, batch] { append(generation, batch); });
                batch.clear();
            }
        }
        QMetaObject::invokeMethod(this, [this, generation, batch, missing] {
            if (generation != m_generation) return;
            append(generation, batch);
            m_loading = false;
            m_error = missing ? tr("%1 tagged item(s) are unavailable. Their tags have been kept.").arg(missing) : QString();
            emit loadingChanged();
            emit errorChanged();
            emit scanFinished();
        });
    });
}

DirectoryProxy::DirectoryProxy(QObject *parent) : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    connect(this, &QAbstractItemModel::modelReset, this, &DirectoryProxy::countChanged);
    connect(this, &QAbstractItemModel::rowsInserted, this, &DirectoryProxy::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &DirectoryProxy::countChanged);
}

QVariant DirectoryProxy::data(const QModelIndex &index, int role) const
{
    if (role == TagColorsRole)
        return m_tags ? m_tags->colors(QSortFilterProxyModel::data(index, DirectoryModel::PathRole).toString()) : QStringList();
    if (role == SelectedRole)
        return m_selected.contains(QSortFilterProxyModel::data(index, DirectoryModel::PathRole).toString());
    return QSortFilterProxyModel::data(index, role);
}

QHash<int, QByteArray> DirectoryProxy::roleNames() const
{
    auto roles = QSortFilterProxyModel::roleNames();
    roles.insert(SelectedRole, "selected");
    roles.insert(TagColorsRole, "tagColors");
    return roles;
}

void DirectoryProxy::setTagStore(TagStore *store)
{
    if (m_tags == store) return;
    if (m_tags) disconnect(m_tags, nullptr, this, nullptr);
    m_tags = store;
    if (m_tags) connect(m_tags, &TagStore::changed, this, [this] {
        if (rowCount()) emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {TagColorsRole});
    });
}

QVariantList DirectoryProxy::selectedUrls() const
{
    QVariantList urls;
    for (const auto &path : m_selected)
        urls.append(QUrl::fromLocalFile(path));
    return urls;
}

void DirectoryProxy::setQuery(const QString &query)
{
    m_query = query;
    clearSelection();
    invalidateFilter();
    emit countChanged();
}

void DirectoryProxy::setSorting(int key, bool descending)
{
    m_sortKey = key;
    m_anchor = -1;
    sort(0, descending ? Qt::DescendingOrder : Qt::AscendingOrder);
}

QVariantMap DirectoryProxy::entry(int row) const
{
    QVariantMap entry;
    if (row < 0 || row >= rowCount())
        return entry;
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        entry.insert(QString::fromLatin1(it.value()), data(index(row, 0), it.key()));
    return entry;
}

void DirectoryProxy::notifySelection()
{
    if (rowCount() > 0)
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {SelectedRole});
    emit selectionChanged();
}

void DirectoryProxy::select(int row, int modifiers)
{
    if (row < 0 || row >= rowCount())
        return;
    if ((modifiers & Qt::ShiftModifier) && m_anchor >= 0) {
        selectRange(m_anchor, row, modifiers & Qt::ControlModifier);
        return;
    }
    const auto path = data(index(row, 0), DirectoryModel::PathRole).toString();
    if (!(modifiers & Qt::ControlModifier))
        m_selected.clear();
    if (m_selected.contains(path))
        m_selected.remove(path);
    else
        m_selected.insert(path);
    m_anchor = row;
    notifySelection();
}

void DirectoryProxy::selectRange(int first, int last, bool additive)
{
    if (!additive)
        m_selected.clear();
    for (int row = qMax(0, qMin(first, last)); row <= qMin(rowCount() - 1, qMax(first, last)); ++row)
        m_selected.insert(data(index(row, 0), DirectoryModel::PathRole).toString());
    notifySelection();
}

void DirectoryProxy::selectAll()
{
    selectRange(0, rowCount() - 1);
}

void DirectoryProxy::beginBoxSelection(bool additive)
{
    m_boxBase = additive ? m_selected : QSet<QString>();
    m_selected = m_boxBase;
    notifySelection();
}

void DirectoryProxy::selectBox(QRectF box, double cellWidth, double cellHeight, int columns)
{
    if (cellWidth <= 0 || cellHeight <= 0 || columns < 1)
        return;
    m_selected = m_boxBase;
    box = box.normalized();
    const int firstRow = qMax(0, int(box.top() / cellHeight));
    const int lastRow = qMin((rowCount() + columns - 1) / columns, int(box.bottom() / cellHeight));
    for (int row = firstRow; row <= lastRow; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int indexRow = row * columns + column;
            if (indexRow >= rowCount())
                break;
            const QRectF item(column * cellWidth + 2, row * cellHeight + 2,
                              cellWidth - 4, cellHeight - 4);
            if (box.intersects(item))
                m_selected.insert(data(index(indexRow, 0), DirectoryModel::PathRole).toString());
        }
    }
    notifySelection();
}

void DirectoryProxy::clearSelection()
{
    m_selected.clear();
    m_anchor = -1;
    notifySelection();
}

void DirectoryProxy::retainExistingSelection()
{
    QSet<QString> existing;
    for (int i = 0; i < rowCount(); ++i)
        existing.insert(data(index(i, 0), DirectoryModel::PathRole).toString());
    m_selected.intersect(existing);
    notifySelection();
}

bool DirectoryProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const bool leftDir = sourceModel()->data(left, DirectoryModel::DirectoryRole).toBool();
    const bool rightDir = sourceModel()->data(right, DirectoryModel::DirectoryRole).toBool();
    if (leftDir != rightDir)
        return sortOrder() == Qt::AscendingOrder ? leftDir : !leftDir;
    if (m_sortKey == 1) {
        const auto a = sourceModel()->data(left, DirectoryModel::SizeRole).toLongLong();
        const auto b = sourceModel()->data(right, DirectoryModel::SizeRole).toLongLong();
        if (a != b)
            return a < b;
    } else if (m_sortKey == 2) {
        const auto a = sourceModel()->data(left, DirectoryModel::ModifiedRole).toDateTime();
        const auto b = sourceModel()->data(right, DirectoryModel::ModifiedRole).toDateTime();
        if (a != b)
            return a < b;
    }
    thread_local QCollator collator = [] {
        QCollator c;
        c.setCaseSensitivity(Qt::CaseInsensitive);
        c.setNumericMode(true);
        return c;
    }();
    return collator.compare(sourceModel()->data(left, DirectoryModel::NameRole).toString(),
                            sourceModel()->data(right, DirectoryModel::NameRole).toString()) < 0;
}

bool DirectoryProxy::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    return sourceModel()->data(sourceModel()->index(row, 0, parent), DirectoryModel::NameRole)
        .toString().contains(m_query, Qt::CaseInsensitive);
}
