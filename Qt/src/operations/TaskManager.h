#pragma once

#include <QAbstractItemModel>
#include <QHash>
#include <QObject>
#include <QThreadPool>
#include <QUrl>
#include <QVariantList>
#include <memory>

class TaskListModel;
namespace FileOperations { struct Job; }

// GUI-thread API. Workers are serialized; model changes/signals stay on this thread.
// Operations: copy/move into an existing destination directory; recycle/delete
// sources; extract ZIP contents into an existing destination directory.
// Sources accept local QUrls, file: URL strings, or absolute/relative local paths.
// Policies: ask (default), overwrite, skip, rename. Existing directories merge;
// overwrite never replaces a directory with a file (or vice versa).
// Links/reparse points in source trees or destination ancestors are rejected.
// ZIP limits: 100,000 entries, 10 GiB total uncompressed data, 4,096-byte names.
// ZIP paths reject traversal, links, ADS, device names and ambiguous duplicates.
// Non-UTF-8 ZIP names and copy fallbacks with NTFS alternate streams are rejected
// rather than silently losing data. Absent-target, same-volume moves use native
// no-replace rename (including whole directories), retaining native metadata.
// Copies preserve contents, basic file permissions, and file/new-directory mtime;
// preservation errors fail the task. Copies do not preserve ACLs, other extended
// attributes or sparsity. Existing merged-directory timestamps are not restored.
// progress is [0,1] byte progress (-1 while scanning); skipped bytes are not counted.
// Additional model roles: processedBytes/totalBytes (quint64), bytesPerSecond
// (double, zero while queued/waiting/finished). These measure logical work, not
// physical disk traffic; a native rename accounts for the entire moved subtree.
// Status: queued/running/waiting/completed/partial/failed/cancelled.
// partial means skips or a failure after successful mutations, not atomic rollback.
// Each committed move file is removed from its source only after successful copy.
// Cancellation preserves completed work, discards the current uncommitted file,
// and cannot interrupt a single OS recycle/delete call. No pause/resume is offered.
// Callers must confirm deletion before submit; the recycle worker never shows UI.
class TaskManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* model READ model CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    explicit TaskManager(QObject *parent = nullptr);
    ~TaskManager() override;
    QAbstractItemModel *model() const;
    bool busy() const;
    Q_INVOKABLE QString submit(QString operation, QVariantList sources,
                               QUrl destination = {}, QString policy = QStringLiteral("ask"));
    Q_INVOKABLE void cancel(QString id);
    Q_INVOKABLE void resolveConflict(QString id, QString policy, bool applyToAll);
    Q_INVOKABLE void clearFinished();

signals:
    void busyChanged();
    void conflict(QString taskId, QString source, QString destination);
    void errorOccurred(QString message);
    void taskFinished();
    // Original, normalized source URLs; never emitted for partial/cancelled moves.
    void successfulMove(QString taskId, QVariantList urls);
    // Actual destinations, including keep-both names and moves in partial tasks.
    void pathRelocated(QString source, QString destination);

private:
    TaskListModel *m_model;
    QThreadPool m_pool;
    QHash<QString, std::shared_ptr<FileOperations::Job>> m_jobs;
};
