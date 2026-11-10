#include "Workspace.h"
#include <QDir>
#include <functional>

Workspace::Workspace(QObject *parent) : QObject(parent) {}

void Workspace::setTagStore(TagStore *store)
{
    m_tags = store;
    for (auto *pane : m_panes) pane->setTagStore(store);
}

PaneController *Workspace::createPane()
{
    auto *pane = new PaneController(this);
    pane->setTagStore(m_tags);
    m_panes.append(pane);
    connect(pane, &PaneController::openFile, this, &Workspace::openFile);
    connect(pane, &PaneController::errorOccurred, this, &Workspace::errorOccurred);
    connect(pane, &PaneController::selectionChanged, this, [this, pane] {
        if (pane == m_active) emit selectionChanged();
    });
    return pane;
}
LayoutNode *Workspace::leaf(PaneController *pane)
{
    auto *node = new LayoutNode(this);
    node->pane = pane;
    return node;
}
LayoutNode *Workspace::branch(LayoutNode *a, LayoutNode *b, bool horizontal, double ratio)
{
    auto *node = new LayoutNode(this);
    node->first = a;
    node->second = b;
    node->orientation = horizontal ? Qt::Horizontal : Qt::Vertical;
    node->fraction = ratio;
    return node;
}
void Workspace::deleteTree(LayoutNode *node)
{
    if (!node) return;
    deleteTree(node->first);
    deleteTree(node->second);
    node->deleteLater();
}
QList<LayoutNode*> Workspace::dividers() const
{
    QList<LayoutNode*> result;
    std::function<void(LayoutNode*)> collect = [&](LayoutNode *node) {
        if (!node || node->pane) return;
        result.append(node);
        collect(node->first);
        collect(node->second);
    };
    collect(m_root);
    return result;
}
void Workspace::arrange(LayoutNode *node, QRectF area)
{
    if (!node) return;
    node->area = area;
    emit node->geometryChanged();
    if (node->pane) {
        node->pane->setRect(area);
    } else if (node->orientation == Qt::Horizontal) {
        const double width = area.width() * node->fraction;
        arrange(node->first, {area.x(), area.y(), width, area.height()});
        arrange(node->second, {area.x() + width, area.y(), area.width() - width, area.height()});
    } else {
        const double height = area.height() * node->fraction;
        arrange(node->first, {area.x(), area.y(), area.width(), height});
        arrange(node->second, {area.x(), area.y() + height, area.width(), area.height() - height});
    }
}
void Workspace::activate(PaneController *pane)
{
    if (!pane || !m_panes.contains(pane) || pane == m_active) return;
    m_active = pane;
    emit activePaneChanged();
    emit selectionChanged();
}
void Workspace::nextPane()
{
    if (!m_panes.isEmpty())
        activate(m_panes.at((m_panes.indexOf(m_active) + 1) % m_panes.size()));
}
void Workspace::preset(int count)
{
    if (count < 1 || count > 4) {
        emit errorOccurred(tr("Choose a layout with one to four panes."));
        return;
    }
    const auto location = m_active ? m_active->url() : QUrl::fromLocalFile(QDir::homePath());
    // Keep the active pane when reducing the layout.
    if (m_active && m_panes.indexOf(m_active) >= count)
        m_panes.swapItemsAt(0, m_panes.indexOf(m_active));
    while (m_panes.size() > count)
        m_panes.takeLast()->deleteLater();
    while (m_panes.size() < count)
        createPane()->navigate(location);
    deleteTree(m_root);
    if (count == 1)
        m_root = leaf(m_panes[0]);
    else if (count == 2)
        m_root = branch(leaf(m_panes[0]), leaf(m_panes[1]), true);
    else if (count == 3)
        m_root = branch(leaf(m_panes[0]), branch(leaf(m_panes[1]), leaf(m_panes[2]), true), true, 1.0 / 3);
    else
        m_root = branch(branch(leaf(m_panes[0]), leaf(m_panes[1]), true),
                        branch(leaf(m_panes[2]), leaf(m_panes[3]), true), false);
    arrange(m_root, {0, 0, 1, 1});
    if (!m_panes.contains(m_active))
        m_active = m_panes.first();
    emit structureChanged();
    emit activePaneChanged();
}
LayoutNode *Workspace::find(PaneController *pane, LayoutNode *node) const
{
    if (!node || node->pane == pane) return node;
    if (auto *found = find(pane, node->first)) return found;
    return find(pane, node->second);
}
void Workspace::split(PaneController *pane, bool horizontal)
{
    auto *node = find(pane, m_root);
    if (!node || !pane) return;
    if (m_panes.size() >= 16) {
        emit errorOccurred(tr("A workspace supports at most 16 panes."));
        return;
    }
    auto *added = createPane();
    added->navigate(pane->url());
    node->pane = nullptr;
    node->orientation = horizontal ? Qt::Horizontal : Qt::Vertical;
    node->first = leaf(pane);
    node->second = leaf(added);
    arrange(m_root, {0, 0, 1, 1});
    emit structureChanged();
    activate(added);
}
LayoutNode *Workspace::removeLeaf(LayoutNode *node, PaneController *pane)
{
    if (!node || node->pane) return node;
    if (node->first->pane == pane || node->second->pane == pane) {
        auto *removed = node->first->pane == pane ? node->first : node->second;
        auto *survivor = removed == node->first ? node->second : node->first;
        removed->deleteLater();
        node->deleteLater();
        return survivor;
    }
    node->first = removeLeaf(node->first, pane);
    node->second = removeLeaf(node->second, pane);
    return node;
}
void Workspace::closePane(PaneController *pane)
{
    if (m_panes.size() <= 1 || !m_panes.contains(pane)) return;
    m_root = removeLeaf(m_root, pane);
    m_panes.removeOne(pane);
    if (m_active == pane) m_active = m_panes.first();
    pane->deleteLater();
    arrange(m_root, {0, 0, 1, 1});
    emit structureChanged();
    emit activePaneChanged();
    emit selectionChanged();
}
void Workspace::resizeSplit(LayoutNode *node, double x, double y)
{
    if (!dividers().contains(node)) return;
    const auto area = node->area;
    const double fraction = node->horizontal() ? (x - area.x()) / area.width()
                                               : (y - area.y()) / area.height();
    node->fraction = qBound(0.15, fraction, 0.85);
    arrange(m_root, {0, 0, 1, 1});
}
QJsonObject Workspace::encode(LayoutNode *node) const
{
    if (!node) return {};
    if (node->pane)
        return {{"pane", node->pane->save()}, {"active", node->pane == m_active}};
    return {{"horizontal", node->horizontal()}, {"ratio", node->fraction},
            {"first", encode(node->first)}, {"second", encode(node->second)}};
}
QJsonObject Workspace::save() const { return encode(m_root); }
LayoutNode *Workspace::decode(const QJsonObject &object, int depth)
{
    if (depth > 15 || object.isEmpty() || m_panes.size() >= 16) return nullptr;
    if (object.contains("pane")) {
        auto *pane = createPane();
        pane->restore(object.value("pane").toObject());
        if (object.value("active").toBool()) m_active = pane;
        return leaf(pane);
    }
    auto *a = decode(object.value("first").toObject(), depth + 1);
    auto *b = decode(object.value("second").toObject(), depth + 1);
    if (!a || !b) {
        deleteTree(a);
        deleteTree(b);
        return nullptr;
    }
    return branch(a, b, object.value("horizontal").toBool(true),
                  qBound(0.15, object.value("ratio").toDouble(0.5), 0.85));
}
void Workspace::restore(const QJsonObject &object)
{
    deleteTree(m_root);
    for (auto *pane : m_panes) pane->deleteLater();
    m_panes.clear();
    m_active = nullptr;
    m_root = decode(object, 0);
    if (!m_root) {
        for (auto *pane : m_panes) pane->deleteLater();
        m_panes.clear();
        m_active = nullptr;
        preset(2);
        return;
    }
    if (!m_active) m_active = m_panes.first();
    arrange(m_root, {0, 0, 1, 1});
    emit structureChanged();
    emit activePaneChanged();
}
