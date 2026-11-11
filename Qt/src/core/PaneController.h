#pragma once

#include "DirectoryModel.h"
#include <QRectF>
#include <QJsonObject>
#include <vector>

class PaneController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(DirectoryProxy* files READ files NOTIFY activeTabChanged)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(int activeTab READ activeTab NOTIFY activeTabChanged)
    Q_PROPERTY(int currentRow READ currentRow WRITE setCurrentRow NOTIFY currentRowChanged)
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
    Q_PROPERTY(bool tagView READ tagView NOTIFY locationChanged)
    Q_PROPERTY(QString tagId READ tagId NOTIFY locationChanged)
public:
    explicit PaneController(QObject *parent = nullptr);
    QString id() const { return m_id; }
    DirectoryProxy *files() { return &current().proxy; }
    QVariantList tabs() const;
    int activeTab() const { return m_activeTab; }
    int currentRow() const { return current().currentRow; }
    void setCurrentRow(int row);
    Q_INVOKABLE void newTab(QUrl url = {});
    Q_INVOKABLE void switchTab(int index);
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void nextTab(int direction = 1);
    void refreshAllTabs();
    QUrl url() const { return current().url; }
    QString path() const;
    QString title() const;
    bool canBack() const { return current().historyIndex > 0; }
    bool canForward() const { return current().historyIndex + 1 < current().history.size(); }
    bool loading() const { return current().model.loading(); }
    QString error() const { return current().model.error(); }
    bool showHidden() const { return current().hidden; }
    QString query() const { return current().query; }
    int sortKey() const { return current().sortKey; }
    bool descending() const { return current().descending; }
    bool grid() const { return current().grid; }
    double scrollPosition() const { return current().scrollPosition; }
    QRectF rect() const { return m_rect; }
    bool tagView() const { return url().scheme() == "hexplorer-tag"; }
    QString tagId() const { return tagView() ? url().path().mid(1) : QString(); }
    void setTagStore(TagStore *store);
    Q_INVOKABLE void showTag(QString tag);
    static bool validLocation(const QUrl &url);
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
    void tabsChanged();
    void activeTabAboutToChange();
    void activeTabChanged();
    void currentRowChanged();
private:
    struct TabState {
        QString id;
        DirectoryModel model;
        DirectoryProxy proxy;
        QUrl url;
        QList<QUrl> history;
        int historyIndex = -1;
        bool hidden = false;
        QString query;
        int sortKey = 0;
        bool descending = false;
        bool grid = false;
        double scrollPosition = 0;
        int currentRow = -1;
        bool loaded = false;
    };
    TabState &current() const { return *m_tabs.at(size_t(m_activeTab)); }
    TabState *createTab();
    void load(TabState &tab);
    void notifyTabSwitch();
    QJsonObject saveTab(const TabState &tab) const;
    void restoreTab(TabState &tab, const QJsonObject &state);
    void visit(const QUrl &url);
    QString m_id;
    std::vector<std::unique_ptr<TabState>> m_tabs;
    int m_activeTab = 0;
    QRectF m_rect;
    TagStore *m_tags = nullptr;
};
