#include "PaneController.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QUuid>

namespace {
QString tabTitle(const QUrl &url)
{
    if (url.scheme() == "hexplorer-tag")
        return PaneController::tr("Tag: %1").arg(TagStore::tagName(url.path().mid(1)));
    if (url.isEmpty()) return PaneController::tr("New tab");
    const auto name = QFileInfo(url.toLocalFile()).fileName();
    return name.isEmpty() ? QDir::toNativeSeparators(url.toLocalFile()) : name;
}
}

PaneController::PaneController(QObject *parent)
    : QObject(parent), m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    createTab();
}

PaneController::TabState *PaneController::createTab()
{
    auto tab = std::make_unique<TabState>();
    auto *state = tab.get();
    state->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    state->proxy.setSourceModel(&state->model);
    state->proxy.setSorting(0, false);
    state->proxy.setTagStore(m_tags);
    connect(&state->model, &DirectoryModel::loadingChanged, this, [this, state] {
        if (state == &current()) emit loadingChanged();
    });
    connect(&state->model, &DirectoryModel::errorChanged, this, [this, state] {
        if (state == &current()) emit errorChanged();
    });
    connect(&state->model, &DirectoryModel::scanFinished, &state->proxy, &DirectoryProxy::retainExistingSelection);
    connect(&state->proxy, &DirectoryProxy::selectionChanged, this, [this, state] {
        if (state == &current()) emit selectionChanged();
    });
    m_tabs.push_back(std::move(tab));
    return state;
}

QVariantList PaneController::tabs() const
{
    QVariantList result;
    for (const auto &tab : m_tabs)
        result.append(QVariantMap{{"id", tab->id}, {"title", tabTitle(tab->url)}, {"url", tab->url}});
    return result;
}

QString PaneController::path() const { return tagView() ? url().toString() : QDir::toNativeSeparators(url().toLocalFile()); }
QString PaneController::title() const { return tabTitle(url()); }

void PaneController::newTab(QUrl location)
{
    if (location.isEmpty()) location = url().isEmpty() ? QUrl::fromLocalFile(QDir::homePath()) : url();
    if (!validLocation(location)) {
        emit errorOccurred(tr("Only local folders, network shares and color tag views can be opened in a tab."));
        return;
    }
    emit activeTabAboutToChange();
    auto *tab = createTab();
    m_activeTab = int(m_tabs.size()) - 1;
    if (location.isLocalFile()) location = QUrl::fromLocalFile(QDir::cleanPath(location.toLocalFile()));
    tab->url = location;
    tab->history = {location};
    tab->historyIndex = 0;
    load(*tab);
    emit tabsChanged();
    notifyTabSwitch();
}

void PaneController::switchTab(int index)
{
    if (index < 0 || index >= int(m_tabs.size()) || index == m_activeTab) return;
    emit activeTabAboutToChange();
    m_activeTab = index;
    if (!current().loaded) load(current());
    notifyTabSwitch();
}

void PaneController::closeTab(int index)
{
    if (index < 0 || index >= int(m_tabs.size()) || m_tabs.size() == 1) return;
    const bool closingActive = index == m_activeTab;
    if (closingActive) emit activeTabAboutToChange();
    // Keep the removed state's models alive until observers have rebound.
    auto removed = std::move(m_tabs[size_t(index)]);
    m_tabs.erase(m_tabs.begin() + index);
    if (index < m_activeTab) --m_activeTab;
    else if (closingActive) m_activeTab = qMin(index, int(m_tabs.size()) - 1);
    if (closingActive && !current().loaded) load(current());
    emit tabsChanged();
    if (closingActive) notifyTabSwitch();
    else emit activeTabChanged();
}

void PaneController::nextTab(int direction)
{
    const int count = int(m_tabs.size());
    switchTab((m_activeTab + (direction < 0 ? count - 1 : 1)) % count);
}

void PaneController::notifyTabSwitch()
{
    emit activeTabChanged();
    emit locationChanged();
    emit optionsChanged();
    emit loadingChanged();
    emit errorChanged();
    emit scrollPositionChanged();
    emit currentRowChanged();
    emit selectionChanged();
}

void PaneController::setCurrentRow(int row)
{
    if (current().currentRow == row) return;
    current().currentRow = row;
    emit currentRowChanged();
}

void PaneController::navigate(QUrl location)
{
    if (!validLocation(location)) {
        emit errorOccurred(tr("Only local folders, network shares and color tag views are supported."));
        return;
    }
    if (location.isLocalFile()) location = QUrl::fromLocalFile(QDir::cleanPath(location.toLocalFile()));
    if (location == url()) { refresh(); return; }
    auto &tab = current();
    while (tab.history.size() > tab.historyIndex + 1) tab.history.removeLast();
    tab.history.append(location);
    if (tab.history.size() > 100) tab.history.removeFirst();
    tab.historyIndex = tab.history.size() - 1;
    visit(location);
}

void PaneController::navigateText(QString text)
{
    text = text.trimmed();
    if (text.startsWith('"') && text.endsWith('"')) text = text.mid(1, text.size() - 2);
    if (text == "~") text = QDir::homePath();
    const QUrl candidate(text);
    if (candidate.isLocalFile() || candidate.scheme() == "hexplorer-tag") { navigate(candidate); return; }
    const QString path = QDir::fromNativeSeparators(text);
    if (path.isEmpty()) { emit errorOccurred(tr("Enter a folder path.")); return; }
    navigate(QUrl::fromLocalFile(QDir::isAbsolutePath(path) ? path
                                : QDir(url().toLocalFile()).absoluteFilePath(path)));
}

void PaneController::visit(const QUrl &location)
{
    current().proxy.clearSelection();
    current().url = location;
    setQuery({});
    setScrollPosition(0);
    setCurrentRow(-1);
    emit locationChanged();
    emit tabsChanged();
    refresh();
}
void PaneController::back() { if (canBack()) visit(current().history.at(--current().historyIndex)); }
void PaneController::forward() { if (canForward()) visit(current().history.at(++current().historyIndex)); }
void PaneController::up()
{
    if (tagView()) { back(); return; }
    QDir dir(url().toLocalFile());
    if (dir.cdUp()) navigate(QUrl::fromLocalFile(dir.absolutePath()));
}
void PaneController::load(TabState &tab)
{
    if (tab.url.scheme() == "hexplorer-tag" && m_tags)
        tab.model.loadTagged(m_tags->paths(tab.url.path().mid(1)));
    else if (tab.url.isLocalFile()) tab.model.load(tab.url, tab.hidden);
    tab.loaded = true;
}
void PaneController::refresh() { load(current()); }
void PaneController::refreshAllTabs()
{
    for (auto &tab : m_tabs) if (tab->loaded) load(*tab);
}

bool PaneController::validLocation(const QUrl &location)
{
    return (location.isLocalFile() && !location.toLocalFile().isEmpty())
        || (location.scheme() == "hexplorer-tag" && location.host().isEmpty()
            && location.query().isEmpty() && location.fragment().isEmpty() && TagStore::validTag(location.path().mid(1)));
}
void PaneController::showTag(QString tag) { navigate(QUrl("hexplorer-tag:/" + tag)); }
void PaneController::setTagStore(TagStore *store)
{
    if (m_tags == store) return;
    if (m_tags) disconnect(m_tags, nullptr, this, nullptr);
    m_tags = store;
    for (auto &tab : m_tabs) tab->proxy.setTagStore(store);
    if (store) connect(store, &TagStore::changed, this, [this] {
        for (auto &tab : m_tabs)
            if (tab->loaded && tab->url.scheme() == "hexplorer-tag") load(*tab);
    });
    for (auto &tab : m_tabs)
        if (tab->loaded && tab->url.scheme() == "hexplorer-tag") load(*tab);
}
void PaneController::activateEntry(int row)
{
    const auto entry = current().proxy.entry(row);
    if (entry.isEmpty()) return;
    const auto location = entry.value("fileUrl").toUrl();
    if (entry.value("isDirectory").toBool()) navigate(location);
    else emit openFile(location);
}
void PaneController::setRect(QRectF rect)
{
    if (m_rect != rect) { m_rect = rect; emit rectChanged(); }
}
void PaneController::setShowHidden(bool value)
{
    if (value == current().hidden) return;
    current().hidden = value;
    emit optionsChanged();
    refresh();
}
void PaneController::setQuery(const QString &query)
{
    if (current().query == query) return;
    current().query = query;
    current().proxy.setQuery(query);
    emit optionsChanged();
}
void PaneController::setSortKey(int key)
{
    if (key < 0 || key > 2 || key == current().sortKey) return;
    current().sortKey = key;
    current().proxy.setSorting(key, descending());
    emit optionsChanged();
}
void PaneController::setDescending(bool value)
{
    if (value == descending()) return;
    current().descending = value;
    current().proxy.setSorting(sortKey(), value);
    emit optionsChanged();
}
void PaneController::setGrid(bool value)
{
    if (value == grid()) return;
    current().grid = value;
    emit optionsChanged();
}
void PaneController::setScrollPosition(double position)
{
    if (!qIsFinite(position) || qFuzzyCompare(position, scrollPosition())) return;
    current().scrollPosition = qMax(0.0, position);
    emit scrollPositionChanged();
}

QJsonObject PaneController::saveTab(const TabState &tab) const
{
    QJsonArray history;
    for (const auto &location : tab.history) history.append(location.toString());
    return {{"url", tab.url.toString()}, {"history", history}, {"historyIndex", tab.historyIndex},
            {"hidden", tab.hidden}, {"sort", tab.sortKey}, {"descending", tab.descending},
            {"grid", tab.grid}, {"scroll", tab.scrollPosition}, {"query", tab.query}};
}
QJsonObject PaneController::save() const
{
    QJsonArray tabs;
    for (const auto &tab : m_tabs) tabs.append(saveTab(*tab));
    return {{"tabs", tabs}, {"activeTab", m_activeTab}};
}
void PaneController::restoreTab(TabState &tab, const QJsonObject &state)
{
    tab.hidden = state.value("hidden").toBool();
    tab.sortKey = qBound(0, state.value("sort").toInt(), 2);
    tab.descending = state.value("descending").toBool();
    tab.grid = state.value("grid").toBool();
    tab.query = state.value("query").toString();
    tab.proxy.setSorting(tab.sortKey, tab.descending);
    tab.proxy.setQuery(tab.query);
    tab.url = QUrl(state.value("url").toString(QUrl::fromLocalFile(QDir::homePath()).toString()));
    if (!validLocation(tab.url)) {
        tab.url = QUrl::fromLocalFile(QDir::homePath());
        emit errorOccurred(tr("An invalid saved tab location was replaced with Home."));
    }
    const auto history = state.value("history").toArray();
    if (!history.isEmpty() && history.size() <= 100) {
        for (const auto &value : history) {
            const QUrl location(value.toString());
            if (validLocation(location)) tab.history.append(location);
        }
    }
    const int index = state.value("historyIndex").toInt(-1);
    if (index >= 0 && index < tab.history.size() && tab.history.at(index) == tab.url)
        tab.historyIndex = index;
    else {
        tab.history = {tab.url};
        tab.historyIndex = 0;
    }
    tab.scrollPosition = qMax(0.0, state.value("scroll").toDouble());
}
void PaneController::restore(const QJsonObject &state)
{
    emit activeTabAboutToChange();
    auto previous = std::move(m_tabs);
    m_activeTab = 0;
    const auto tabs = state.value("tabs").toArray();
    if (state.contains("tabs") && !tabs.isEmpty()) {
        for (const auto &value : tabs) restoreTab(*createTab(), value.toObject());
        m_activeTab = qBound(0, state.value("activeTab").toInt(), int(m_tabs.size()) - 1);
    } else {
        restoreTab(*createTab(), state);
        if (state.contains("tabs"))
            emit errorOccurred(tr("The saved pane had no valid tabs. A default tab was opened."));
    }
    load(current());
    emit tabsChanged();
    notifyTabSwitch();
}
