#include "TagStore.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>

namespace {
const QStringList ids{"red", "orange", "yellow", "green", "blue", "purple", "gray"};
const QStringList colorValues{"#d13438", "#da7b00", "#b89b00", "#218739", "#0067c0", "#8764b8", "#737373"};
}

QString TagStore::key(const QString &path)
{
    const auto clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    return clean.toCaseFolded();
#else
    return clean;
#endif
}
bool TagStore::validTag(const QString &tag) { return ids.contains(tag); }
QString TagStore::tagName(const QString &tag)
{
    const QStringList names{tr("Red"), tr("Orange"), tr("Yellow"), tr("Green"), tr("Blue"), tr("Purple"), tr("Gray")};
    const int index = ids.indexOf(tag);
    return index >= 0 ? names.at(index) : QString();
}
QVariantList TagStore::palette() const
{
    QVariantList result;
    for (int i = 0; i < ids.size(); ++i)
        result.append(QVariantMap{{"id", ids[i]}, {"name", tagName(ids[i])}, {"color", colorValues[i]}});
    return result;
}
TagStore::TagStore(QObject *parent) : QObject(parent)
{
    const auto saved = m_settings.value("colorTags/records").toByteArray();
    if (saved.isEmpty()) return;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(saved, &error);
    m_readable = error.error == QJsonParseError::NoError && document.isObject();
    if (m_readable) {
        const auto object = document.object();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (!QDir::isAbsolutePath(it.key()) || !it.value().isArray()) { m_readable = false; break; }
            QStringList values;
            for (const auto &value : it.value().toArray()) {
                if (!validTag(value.toString())) { m_readable = false; break; }
                if (!values.contains(value.toString())) values.append(value.toString());
            }
            m_records.insert(key(it.key()), values);
            m_displayPaths.insert(key(it.key()), it.key());
        }
    }
    if (!m_readable) {
        m_records.clear();
        QTimer::singleShot(0, this, [this] {
            emit errorOccurred(tr("Color tag metadata is invalid and has been preserved. Tag editing is disabled to avoid overwriting it."));
        });
    }
}
QStringList TagStore::tags(const QString &path) const { return m_records.value(key(path)); }
QStringList TagStore::colors(const QString &path) const
{
    QStringList result;
    for (const auto &tag : tags(path)) result.append(colorValues.at(ids.indexOf(tag)));
    return result;
}
QStringList TagStore::paths(const QString &tag) const
{
    QStringList result;
    for (auto it = m_records.begin(); it != m_records.end(); ++it)
        if (it.value().contains(tag)) result.append(m_displayPaths.value(it.key(), it.key()));
    return result;
}
bool TagStore::commit(const QMap<QString, QStringList> &records)
{
    if (!m_readable) {
        emit errorOccurred(tr("Tag metadata cannot be edited until the invalid stored data is repaired."));
        return false;
    }
    QJsonObject object;
    for (auto it = records.begin(); it != records.end(); ++it)
        if (!it.value().isEmpty())
            object.insert(m_displayPaths.value(it.key(), it.key()), QJsonArray::fromStringList(it.value()));
    m_settings.setValue("colorTags/records", QJsonDocument(object).toJson(QJsonDocument::Compact));
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        emit errorOccurred(tr("Could not save color tags. The file operation itself is not rolled back."));
        return false;
    }
    m_records = records;
    emit changed();
    return true;
}
bool TagStore::setTag(const QVariantList &urls, const QString &tag, bool enabled)
{
    if (!validTag(tag) || urls.isEmpty()) {
        emit errorOccurred(tr("Select files or folders and a valid color tag."));
        return false;
    }
    auto records = m_records;
    for (const auto &value : urls) {
        const auto url = value.toUrl();
        if (!url.isLocalFile() || !QFileInfo::exists(url.toLocalFile())) {
            emit errorOccurred(tr("A selected file or folder is no longer available. Tags were not changed."));
            return false;
        }
        auto &values = records[key(url.toLocalFile())];
        m_displayPaths.insert(key(url.toLocalFile()), QDir::cleanPath(url.toLocalFile()));
        values.removeAll(tag);
        if (enabled) values.append(tag);
    }
    return commit(records);
}
bool TagStore::clear(const QVariantList &urls)
{
    auto records = m_records;
    for (const auto &value : urls) {
        if (!value.toUrl().isLocalFile()) {
            emit errorOccurred(tr("Only local file tags can be removed."));
            return false;
        }
        records.remove(key(value.toUrl().toLocalFile()));
    }
    return commit(records);
}
void TagStore::relocate(const QString &source, const QString &destination)
{
    const auto from = key(source);
    const auto to = key(destination);
    auto records = m_records;
    QMap<QString, QStringList> moved;
    for (auto it = m_records.begin(); it != m_records.end(); ++it) {
        if (it.key() == from || it.key().startsWith(from + '/')) {
            moved.insert(to + it.key().mid(from.size()), it.value());
            const auto original = m_displayPaths.value(it.key(), it.key());
            m_displayPaths.insert(to + it.key().mid(from.size()),
                                  QDir::cleanPath(QFileInfo(destination).absoluteFilePath())
                                  + original.mid(QDir::cleanPath(QFileInfo(source).absoluteFilePath()).size()));
            records.remove(it.key());
        }
    }
    // An overwritten destination is a different item, even if the source had no tag.
    if (QFileInfo(destination).isDir()) {
        auto combined = records.value(to);
        for (const auto &tag : moved.value(to)) if (!combined.contains(tag)) combined.append(tag);
        if (!combined.isEmpty()) moved.insert(to, combined);
    }
    records.remove(to);
    for (auto it = moved.begin(); it != moved.end(); ++it) records.insert(it.key(), it.value());
    if (records != m_records || (from == to && !moved.isEmpty())) commit(records);
}
