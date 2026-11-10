#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QSet>
#include <QRectF>
#include <QSortFilterProxyModel>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <atomic>
#include <memory>
#include "TagStore.h"

struct FileEntry {
    QString name;
    QString path;
    qint64 size = 0;
    QDateTime modified;
    bool directory = false;
    bool link = false;
};

class DirectoryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    enum Role { NameRole = Qt::UserRole + 1, UrlRole, PathRole, SizeRole,
                SizeTextRole, ModifiedRole, ModifiedTextRole, DirectoryRole,
                IconRole, LinkRole };
    explicit DirectoryModel(QObject *parent = nullptr);
    ~DirectoryModel() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    void load(const QUrl &url, bool hidden);
    void loadTagged(const QStringList &paths);
    QUrl url() const { return m_url; }
signals:
    void loadingChanged();
    void errorChanged();
    void scanFinished();
private:
    void append(quint64 generation, QList<FileEntry> entries);
    QList<FileEntry> m_entries;
    QUrl m_url;
    bool m_hidden = false;
    bool m_loading = false;
    QString m_error;
    quint64 m_generation = 0;
    QThreadPool m_pool;
    QFileSystemWatcher m_watcher;
    QTimer m_refresh;
    std::shared_ptr<std::atomic_bool> m_cancel;
};

class DirectoryProxy : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedUrls READ selectedUrls NOTIFY selectionChanged)
public:
    enum { SelectedRole = DirectoryModel::LinkRole + 1, TagColorsRole };
    explicit DirectoryProxy(QObject *parent = nullptr);
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return rowCount(); }
    int selectionCount() const { return m_selected.size(); }
    QVariantList selectedUrls() const;
    void setQuery(const QString &query);
    void setSorting(int key, bool descending);
    Q_INVOKABLE QVariantMap entry(int row) const;
    Q_INVOKABLE void select(int row, int modifiers = 0);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void selectRange(int first, int last, bool additive = false);
    Q_INVOKABLE void beginBoxSelection(bool additive);
    Q_INVOKABLE void selectBox(QRectF box, double cellWidth, double cellHeight, int columns);
    void retainExistingSelection();
    void setTagStore(TagStore *store);
signals:
    void countChanged();
    void selectionChanged();
protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
private:
    void notifySelection();
    QSet<QString> m_selected;
    QSet<QString> m_boxBase;
    QString m_query;
    int m_anchor = -1;
    int m_sortKey = 0;
    TagStore *m_tags = nullptr;
};
