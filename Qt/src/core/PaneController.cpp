#include "PaneController.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QUuid>

PaneController::PaneController(QObject *parent)
    : QObject(parent), m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_proxy.setSourceModel(&m_model);
    m_proxy.setSorting(0, false);
    connect(&m_model, &DirectoryModel::loadingChanged, this, &PaneController::loadingChanged);
    connect(&m_model, &DirectoryModel::errorChanged, this, &PaneController::errorChanged);
    connect(&m_model, &DirectoryModel::scanFinished, &m_proxy, &DirectoryProxy::retainExistingSelection);
    connect(&m_proxy, &DirectoryProxy::selectionChanged, this, &PaneController::selectionChanged);
}

QString PaneController::path() const { return tagView() ? m_url.toString() : QDir::toNativeSeparators(m_url.toLocalFile()); }
QString PaneController::title() const
{
    if (tagView()) return tr("Tag: %1").arg(TagStore::tagName(tagId()));
    const auto name = QFileInfo(m_url.toLocalFile()).fileName();
    return name.isEmpty() ? path() : name;
}

void PaneController::navigate(QUrl url)
{
    if (!validLocation(url)) {
        emit errorOccurred(tr("Only local folders and network shares are supported."));
        return;
    }
    if (url.isLocalFile()) url = QUrl::fromLocalFile(QDir::cleanPath(url.toLocalFile()));
    if (url == m_url) {
        refresh();
        return;
    }
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();
    m_history.append(url);
    if (m_history.size() > 100)
        m_history.removeFirst();
    m_historyIndex = m_history.size() - 1;
    visit(url);
}

void PaneController::navigateText(QString text)
{
    text = text.trimmed();
    if (text.startsWith('"') && text.endsWith('"'))
        text = text.mid(1, text.size() - 2);
    if (text == "~")
        text = QDir::homePath();
    const QUrl candidate(text);
    if (candidate.isLocalFile() || candidate.scheme() == "hexplorer-tag") {
        navigate(candidate);
        return;
    }
    const QString path = QDir::fromNativeSeparators(text);
    if (path.isEmpty()) {
        emit errorOccurred(tr("Enter a folder path."));
        return;
    }
    navigate(QUrl::fromLocalFile(QDir::isAbsolutePath(path) ? path
                                : QDir(m_url.toLocalFile()).absoluteFilePath(path)));
}

void PaneController::visit(const QUrl &url)
{
    m_proxy.clearSelection();
    m_url = url;
    setQuery({});
    setScrollPosition(0);
    emit locationChanged();
    refresh();
}
void PaneController::back() { if (canBack()) visit(m_history.at(--m_historyIndex)); }
void PaneController::forward() { if (canForward()) visit(m_history.at(++m_historyIndex)); }
void PaneController::up()
{
    if (tagView()) { back(); return; }
    QDir dir(m_url.toLocalFile());
    if (dir.cdUp())
        navigate(QUrl::fromLocalFile(dir.absolutePath()));
}
void PaneController::refresh()
{
    if (tagView() && m_tags) m_model.loadTagged(m_tags->paths(tagId()));
    else if (m_url.isLocalFile()) m_model.load(m_url, m_hidden);
}

bool PaneController::validLocation(const QUrl &url)
{
    return (url.isLocalFile() && !url.toLocalFile().isEmpty())
        || (url.scheme() == "hexplorer-tag" && url.host().isEmpty()
            && url.query().isEmpty() && url.fragment().isEmpty() && TagStore::validTag(url.path().mid(1)));
}
void PaneController::showTag(QString tag)
{
    navigate(QUrl("hexplorer-tag:/" + tag));
}
void PaneController::setTagStore(TagStore *store)
{
    if (m_tags == store) return;
    if (m_tags) disconnect(m_tags, nullptr, this, nullptr);
    m_tags = store;
    m_proxy.setTagStore(store);
    if (store) connect(store, &TagStore::changed, this, [this] {
        if (tagView()) refresh();
    });
    if (tagView()) refresh();
}

void PaneController::activateEntry(int row)
{
    const auto entry = m_proxy.entry(row);
    if (entry.isEmpty())
        return;
    const auto url = entry.value("fileUrl").toUrl();
    if (entry.value("isDirectory").toBool())
        navigate(url);
    else
        emit openFile(url);
}

void PaneController::setRect(QRectF rect)
{
    if (m_rect != rect) {
        m_rect = rect;
        emit rectChanged();
    }
}
void PaneController::setShowHidden(bool value)
{
    if (value == m_hidden) return;
    m_hidden = value;
    emit optionsChanged();
    refresh();
}
void PaneController::setQuery(const QString &query)
{
    if (m_query == query) return;
    m_query = query;
    m_proxy.setQuery(query);
    emit optionsChanged();
}
void PaneController::setSortKey(int key)
{
    if (key < 0 || key > 2 || key == m_sortKey) return;
    m_sortKey = key;
    m_proxy.setSorting(m_sortKey, m_descending);
    emit optionsChanged();
}
void PaneController::setDescending(bool value)
{
    if (value == m_descending) return;
    m_descending = value;
    m_proxy.setSorting(m_sortKey, m_descending);
    emit optionsChanged();
}
void PaneController::setGrid(bool value)
{
    if (value == m_grid) return;
    m_grid = value;
    emit optionsChanged();
}
void PaneController::setScrollPosition(double position)
{
    if (qFuzzyCompare(position, m_scrollPosition)) return;
    m_scrollPosition = qMax(0.0, position);
    emit scrollPositionChanged();
}

QJsonObject PaneController::save() const
{
    QJsonArray history;
    for (const auto &url : m_history)
        history.append(url.toString());
    return {{"url", m_url.toString()}, {"history", history}, {"historyIndex", m_historyIndex},
            {"hidden", m_hidden}, {"sort", m_sortKey}, {"descending", m_descending},
            {"grid", m_grid}, {"scroll", m_scrollPosition}};
}

void PaneController::restore(const QJsonObject &state)
{
    m_hidden = state.value("hidden").toBool();
    m_sortKey = qBound(0, state.value("sort").toInt(), 2);
    m_descending = state.value("descending").toBool();
    m_grid = state.value("grid").toBool();
    m_proxy.setSorting(m_sortKey, m_descending);
    navigate(QUrl(state.value("url").toString(QUrl::fromLocalFile(QDir::homePath()).toString())));
    const auto history = state.value("history").toArray();
    if (!history.isEmpty() && history.size() <= 100) {
        QList<QUrl> valid;
        for (const auto &value : history) {
            const QUrl url(value.toString());
            if (validLocation(url))
                valid.append(url);
        }
        const int index = state.value("historyIndex").toInt(-1);
        if (index >= 0 && index < valid.size() && valid.at(index) == m_url) {
            m_history = valid;
            m_historyIndex = index;
        }
    }
    m_scrollPosition = qMax(0.0, state.value("scroll").toDouble());
    emit optionsChanged();
    emit locationChanged();
}
