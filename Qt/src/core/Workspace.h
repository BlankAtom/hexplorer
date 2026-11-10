#pragma once

#include "PaneController.h"
#include <QJsonObject>

class LayoutNode : public QObject {
    Q_OBJECT
    Q_PROPERTY(QRectF bounds READ bounds NOTIFY geometryChanged)
    Q_PROPERTY(double ratio READ ratio NOTIFY geometryChanged)
    Q_PROPERTY(bool horizontal READ horizontal CONSTANT)
public:
    explicit LayoutNode(QObject *parent = nullptr) : QObject(parent) {}
    QRectF bounds() const { return area; }
    double ratio() const { return fraction; }
    bool horizontal() const { return orientation == Qt::Horizontal; }
    PaneController *pane = nullptr;
    LayoutNode *first = nullptr;
    LayoutNode *second = nullptr;
    Qt::Orientation orientation = Qt::Horizontal;
    double fraction = 0.5;
    QRectF area;
signals:
    void geometryChanged();
};

class Workspace : public QObject {
    Q_OBJECT
    Q_PROPERTY(QList<PaneController*> panes READ panes NOTIFY structureChanged)
    Q_PROPERTY(QList<LayoutNode*> dividers READ dividers NOTIFY structureChanged)
    Q_PROPERTY(PaneController* activePane READ activePane NOTIFY activePaneChanged)
public:
    explicit Workspace(QObject *parent = nullptr);
    QList<PaneController*> panes() const { return m_panes; }
    QList<LayoutNode*> dividers() const;
    PaneController *activePane() const { return m_active; }
    Q_INVOKABLE void activate(PaneController *pane);
    Q_INVOKABLE void nextPane();
    Q_INVOKABLE void preset(int count);
    Q_INVOKABLE void split(PaneController *pane, bool horizontal);
    Q_INVOKABLE void closePane(PaneController *pane);
    Q_INVOKABLE void resizeSplit(LayoutNode *node, double x, double y);
    QJsonObject save() const;
    void setTagStore(TagStore *store);
    void restore(const QJsonObject &object);
signals:
    void structureChanged();
    void activePaneChanged();
    void selectionChanged();
    void openFile(QUrl url);
    void errorOccurred(QString error);
private:
    PaneController *createPane();
    LayoutNode *leaf(PaneController *pane);
    LayoutNode *branch(LayoutNode *first, LayoutNode *second, bool horizontal, double ratio = 0.5);
    void arrange(LayoutNode *node, QRectF rect);
    LayoutNode *find(PaneController *pane, LayoutNode *node) const;
    LayoutNode *removeLeaf(LayoutNode *node, PaneController *pane);
    QJsonObject encode(LayoutNode *node) const;
    LayoutNode *decode(const QJsonObject &object, int depth);
    void deleteTree(LayoutNode *node);
    QList<PaneController*> m_panes;
    LayoutNode *m_root = nullptr;
    PaneController *m_active = nullptr;
    TagStore *m_tags = nullptr;
};
