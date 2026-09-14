#pragma once

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

class ShellIconProvider final : public QQuickImageProvider
{
public:
    ShellIconProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QMutex m_mutex;
    QCache<QString, QImage> m_cache;
};
