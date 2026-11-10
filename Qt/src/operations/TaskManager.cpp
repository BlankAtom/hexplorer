#include "TaskManager.h"
#include "FileOperationWorker.h"

#include <QAbstractListModel>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QRunnable>
#include <QUuid>

class TaskListModel final : public QAbstractListModel {
public:
    enum Role { TaskId = Qt::UserRole + 1, Title, Status, Progress, CurrentFile, Error, Cancellable,
                ProcessedBytes, TotalBytes, BytesPerSecond };
    struct Row {
        QString id;
        QString title;
        FileOperations::Update update;
        bool cancellable = true;
    };
    explicit TaskListModel(QObject *parent) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
            return {};
        const auto &row = m_rows[index.row()];
        switch (role) {
        case TaskId: return row.id;
        case Title: return row.title;
        case Status: return row.update.status;
        case Progress: return row.update.progress;
        case CurrentFile: return row.update.currentFile;
        case Error: return row.update.error;
        case Cancellable: return row.cancellable;
        case ProcessedBytes: return QVariant::fromValue(row.update.processedBytes);
        case TotalBytes: return QVariant::fromValue(row.update.totalBytes);
        case BytesPerSecond: return row.update.bytesPerSecond;
        default: return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override {
        return {{TaskId, "taskId"}, {Title, "title"}, {Status, "status"},
                {Progress, "progress"}, {CurrentFile, "currentFile"},
                {Error, "error"}, {Cancellable, "cancellable"},
                {ProcessedBytes, "processedBytes"}, {TotalBytes, "totalBytes"},
                {BytesPerSecond, "bytesPerSecond"}};
    }
    void add(Row row) {
        const int last = rowCount();
        beginInsertRows({}, last, last);
        m_rows.append(std::move(row));
        endInsertRows();
    }
    void update(const QString &id, const FileOperations::Update &value, bool finished = false) {
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows[i].id != id)
                continue;
            m_rows[i].update = value;
            m_rows[i].cancellable = !finished;
            emit dataChanged(index(i), index(i));
            return;
        }
    }
    void clearFinished() {
        for (int i = rowCount() - 1; i >= 0; --i) {
            if (m_rows[i].cancellable)
                continue;
            beginRemoveRows({}, i, i);
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }
private:
    QList<Row> m_rows;
};

static QString localPath(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QUrl>()) {
        const auto url = value.toUrl();
        return url.isLocalFile() ? url.toLocalFile() : QString{};
    }
    if (value.metaType() != QMetaType::fromType<QString>())
        return {};
    const QString text = value.toString();
    if (text.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl url(text);
        return url.isLocalFile() ? url.toLocalFile() : QString{};
    }
    if (text.contains(QStringLiteral("://")))
        return {};
    return text;
}

TaskManager::TaskManager(QObject *parent)
    : QObject(parent), m_model(new TaskListModel(this))
{
    m_pool.setMaxThreadCount(1);
}

TaskManager::~TaskManager()
{
    for (const auto &job : m_jobs) {
        QMutexLocker locker(&job->mutex);
        job->cancelled = true;
        job->condition.wakeAll();
    }
    m_pool.waitForDone();
    // The event loop may already have stopped. Persist metadata for moves that
    // completed before cancellation even when their queued result cannot run.
    for (const auto &job : m_jobs)
        for (const auto &paths : job->relocated)
            emit pathRelocated(paths.first, paths.second);
}

QAbstractItemModel *TaskManager::model() const { return m_model; }
bool TaskManager::busy() const { return !m_jobs.isEmpty(); }

QString TaskManager::submit(QString operation, QVariantList sources, QUrl destination, QString policy)
{
    operation = operation.toLower();
    policy = policy.toLower();
    const QStringList supported{QStringLiteral("copy"), QStringLiteral("move"),
        QStringLiteral("recycle"), QStringLiteral("delete"), QStringLiteral("extract")};
    if (!supported.contains(operation) || !FileOperations::validPolicy(policy) || sources.isEmpty()) {
        emit errorOccurred(tr("Invalid file operation, collision policy, or empty source list."));
        return {};
    }
    auto job = std::make_shared<FileOperations::Job>();
    job->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job->operation = operation;
    job->policy = policy;
    for (const auto &source : sources) {
        const QString path = localPath(source);
        if (path.isEmpty() || path.contains(QChar::Null)) {
            emit errorOccurred(tr("Only nonempty local file paths are supported."));
            return {};
        }
        const auto absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        if (!job->sources.contains(absolute))
            job->sources.append(absolute);
    }
    if (operation == "copy" || operation == "move" || operation == "extract") {
        if (!destination.isLocalFile() || destination.toLocalFile().isEmpty()
            || destination.toLocalFile().contains(QChar::Null)) {
            emit errorOccurred(tr("The destination must be a local directory."));
            return {};
        }
        job->destination = QDir::cleanPath(QFileInfo(destination.toLocalFile()).absoluteFilePath());
    }
    const bool wasBusy = busy();
    m_jobs.insert(job->id, job);
    m_model->add({job->id, tr("%1 — %2 item(s)").arg(operation).arg(job->sources.size()),
                  {QStringLiteral("queued"), -1, {}, {}}, true});
    if (!wasBusy)
        emit busyChanged();
    m_pool.start(QRunnable::create([this, job] {
        const auto progress = [this, job](const FileOperations::Update &update) {
            QMetaObject::invokeMethod(this, [this, job, update] {
                m_model->update(job->id, update);
            }, Qt::QueuedConnection);
        };
        const auto conflictCallback = [this, job](const QString &source, const QString &target) {
            QMetaObject::invokeMethod(this, [this, job, source, target] {
                if (!job->cancelled)
                    emit conflict(job->id, source, target);
            }, Qt::QueuedConnection);
        };
        const auto result = FileOperations::run(*job, progress, conflictCallback);
        QMetaObject::invokeMethod(this, [this, job, result] {
            for (const auto &paths : job->relocated)
                emit pathRelocated(paths.first, paths.second);
            m_model->update(job->id, result, true);
            m_jobs.remove(job->id);
            if (!busy())
                emit busyChanged();
            if (!result.error.isEmpty() && result.status != "cancelled")
                emit errorOccurred(result.error);
            if (job->operation == "move" && result.status == "completed") {
                QVariantList urls;
                urls.reserve(job->sources.size());
                for (const auto &source : job->sources)
                    urls.append(QUrl::fromLocalFile(source));
                emit successfulMove(job->id, urls);
            }
            emit taskFinished();
        }, Qt::QueuedConnection);
    }));
    return job->id;
}

void TaskManager::cancel(QString id)
{
    const auto job = m_jobs.value(id);
    if (!job)
        return;
    QMutexLocker locker(&job->mutex);
    job->cancelled = true;
    job->condition.wakeAll();
}

void TaskManager::resolveConflict(QString id, QString policy, bool applyToAll)
{
    policy = policy.toLower();
    if (!FileOperations::validPolicy(policy) || policy == "ask") {
        emit errorOccurred(tr("Choose overwrite, skip, or rename for this conflict."));
        return;
    }
    const auto job = m_jobs.value(id);
    if (!job)
        return;
    QMutexLocker locker(&job->mutex);
    if (!job->waiting || job->cancelled || job->decisionReady)
        return;
    job->decision = policy;
    job->decisionReady = true;
    if (applyToAll)
        job->policy = policy;
    job->condition.wakeAll();
}

void TaskManager::clearFinished() { m_model->clearFinished(); }
