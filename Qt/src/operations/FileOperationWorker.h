#pragma once

#include <QMutex>
#include <QStringList>
#include <QWaitCondition>
#include <atomic>
#include <functional>

namespace FileOperations {

struct Job {
    QString id;
    QString operation;
    QStringList sources;
    QString destination;
    QString policy;
    std::atomic_bool cancelled{false};
    QMutex mutex;
    QWaitCondition condition;
    bool waiting = false;
    bool decisionReady = false;
    QString decision;
};

struct Update {
    QString status;
    double progress = -1;
    QString currentFile;
    QString error;
    quint64 processedBytes = 0;
    quint64 totalBytes = 0;
    double bytesPerSecond = 0;
};

using Progress = std::function<void(const Update &)>;
using Conflict = std::function<void(const QString &, const QString &)>;
Update run(Job &job, const Progress &progress, const Conflict &conflict);
bool validPolicy(const QString &policy);

}
