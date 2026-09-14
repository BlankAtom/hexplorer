#pragma once

#include <QObject>
#include <QUrl>

#include <atomic>
#include <memory>

class PreviewController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl url READ url NOTIFY previewChanged)
    Q_PROPERTY(QString kind READ kind NOTIFY previewChanged)
    Q_PROPERTY(QString text READ text NOTIFY previewChanged)
    Q_PROPERTY(QString title READ title NOTIFY previewChanged)
    Q_PROPERTY(QString details READ details NOTIFY previewChanged)
    Q_PROPERTY(QString error READ error NOTIFY previewChanged)
    Q_PROPERTY(bool pinned READ pinned WRITE setPinned NOTIFY pinnedChanged)

public:
    explicit PreviewController(QObject *parent = nullptr);
    ~PreviewController() override;
    QUrl url() const { return m_url; }
    QString kind() const { return m_kind; }
    // Render this untrusted file content with QML Text.PlainText, never AutoText.
    QString text() const { return m_text; }
    QString title() const { return m_title; }
    QString details() const { return m_details; }
    QString error() const { return m_error; }
    bool pinned() const { return m_pinned; }

    Q_INVOKABLE void setPinned(bool pinned);
    Q_INVOKABLE void preview(QUrl url);

signals:
    void previewChanged();
    void pinnedChanged();

private:
    QUrl m_url;
    QString m_kind = QStringLiteral("none");
    QString m_text;
    QString m_title;
    QString m_details;
    QString m_error;
    bool m_pinned = false;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};
