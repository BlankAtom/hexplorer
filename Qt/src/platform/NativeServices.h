#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class NativeServices final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dragActive READ ownsDrag NOTIFY dragActiveChanged)

public:
    explicit NativeServices(QObject *parent = nullptr);

    Q_INVOKABLE void open(QUrl url);
    Q_INVOKABLE void reveal(QUrl url);
    Q_INVOKABLE void properties(QUrl url);
    Q_INVOKABLE void contextMenu(QVariantList urls, double x, double y);
    Q_INVOKABLE void setClipboard(QVariantList urls, bool cut);
    Q_INVOKABLE QVariantMap clipboardFiles();
    Q_INVOKABLE void clearCutClipboard(QVariantList completedUrls);
    Q_INVOKABLE int startDrag(QVariantList urls);
    Q_INVOKABLE bool ownsDrag() const { return m_dragActive; }

signals:
    void errorOccurred(QString message);
    void dragActiveChanged();

private:
    QList<QUrl> localUrls(const QVariantList &values);
    bool m_dragActive = false;
};
