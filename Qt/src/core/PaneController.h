#pragma once

#include "DirectoryModel.h"
#include <QRectF>
#include <QJsonObject>

class PaneController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(DirectoryProxy* files READ files CONSTANT)
    Q_PROPERTY(QUrl url READ url NOTIFY locationChanged)
    Q_PROPERTY(QString path READ path NOTIFY locationChanged)
    Q_PROPERTY(QString title READ title NOTIFY locationChanged)
    Q_PROPERTY(bool canBack READ canBack NOTIFY locationChanged)
    Q_PROPERTY(bool canForward READ canForward NOTIFY locationChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY optionsChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY optionsChanged)
    Q_PROPERTY(int sortKey READ sortKey WRITE setSortKey NOTIFY optionsChanged)
    Q_PROPERTY(bool descending READ descending WRITE setDescending NOTIFY optionsChanged)
    Q_PROPERTY(bool grid READ grid WRITE setGrid NOTIFY optionsChanged)
    Q_PROPERTY(double scrollPosition READ scrollPosition WRITE setScrollPosition NOTIFY scrollPositionChanged)
    Q_PROPERTY(QRectF rect READ rect NOTIFY rectChanged)
public:
    explicit PaneController(QObject *parent = nullptr);
    QString id() const { return m_id; }
    DirectoryProxy *files() { return &m_proxy; }
    QUrl url() const { return m_url; }
    QString path() const;
    QString title() const;
    bool canBack() const { return m_historyIndex > 0; }
    bool canForward() const { return m_historyIndex + 1 < m_history.size(); }
    bool loading() const { return m_model.loading(); }
    QString error() const { return m_model.error(); }
    bool showHidden() const { return m_hidden; }
    QString query() const { return m_query; }
    int sortKey() const { return m_sortKey; }
    bool descending() const { return m_descending; }
    bool grid() const { return m_grid; }
    double scrollPosition() const { return m_scrollPosition; }
    QRectF rect() const { return m_rect; }
    void setRect(QRectF rect);
    void setShowHidden(bool value);
    void setQuery(const QString &query);
    void setSortKey(int key);
    void setDescending(bool value);
    void setGrid(bool value);
    void setScrollPosition(double position);
    Q_INVOKABLE void navigate(QUrl url);
    Q_INVOKABLE void navigateText(QString text);
    Q_INVOKABLE void back();
    Q_INVOKABLE void forward();
    Q_INVOKABLE void up();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void activateEntry(int row);
    QJsonObject save() const;
    void restore(const QJsonObject &state);
signals:
    void locationChanged();
    void loadingChanged();
    void errorChanged();
    void optionsChanged();
    void scrollPositionChanged();
    void rectChanged();
    void openFile(QUrl url);
    void errorOccurred(QString error);
    void selectionChanged();
private:
    void visit(const QUrl &url);
    QString m_id;
    DirectoryModel m_model;
    DirectoryProxy m_proxy;
    QUrl m_url;
    QList<QUrl> m_history;
    int m_historyIndex = -1;
    bool m_hidden = false;
    QString m_query;
    int m_sortKey = 0;
    bool m_descending = false;
    bool m_grid = false;
    double m_scrollPosition = 0;
    QRectF m_rect;
};
