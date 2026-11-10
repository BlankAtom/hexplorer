#include "NavigationSettings.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

NavigationSettings::NavigationSettings(QObject *parent) : QObject(parent)
{
    m_expanded = m_settings.value("quickAccess/expanded", true).toBool();
    m_visible = m_settings.value("quickAccess/visible", true).toBool();
    m_places = m_settings.contains("quickAccess/places")
        ? m_settings.value("quickAccess/places").toList() : defaults();
    for (const auto &place : m_places) {
        const auto item = place.toMap();
        if (item.value("name").toString().isEmpty() || !item.value("url").toUrl().isLocalFile()) {
            m_settings.setValue("quickAccess/recovery", m_settings.value("quickAccess/places"));
            m_places.clear();
            QTimer::singleShot(0, this, [this] {
                emit errorOccurred(tr("Quick Access settings are invalid. Use Restore defaults in navigation settings."));
            });
            break;
        }
    }
}

QVariantList NavigationSettings::defaults() const
{
    QVariantList result;
    auto add = [&](QString name, QString path) {
        if (!path.isEmpty()) result.append(QVariantMap{{"name", name}, {"url", QUrl::fromLocalFile(path)}});
    };
    add(tr("Home"), QDir::homePath());
    add(tr("Desktop"), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    add(tr("Documents"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    add(tr("Downloads"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    add(tr("Pictures"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    for (const auto &drive : QDir::drives())
        add(QDir::toNativeSeparators(drive.absoluteFilePath()), drive.absoluteFilePath());
    return result;
}

void NavigationSettings::save()
{
    m_settings.setValue("quickAccess/expanded", m_expanded);
    m_settings.setValue("quickAccess/visible", m_visible);
    m_settings.setValue("quickAccess/places", m_places);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError)
        emit errorOccurred(tr("Could not save navigation settings."));
    emit changed();
}
void NavigationSettings::setExpanded(bool value) { if (m_expanded != value) { m_expanded = value; save(); } }
void NavigationSettings::setVisible(bool value) { if (m_visible != value) { m_visible = value; save(); } }
void NavigationSettings::add(QString name, QString path)
{
    name = name.trimmed();
    const QUrl input(path.trimmed());
    const QString local = input.isLocalFile() ? input.toLocalFile() : QDir::fromNativeSeparators(path.trimmed());
    if (name.isEmpty() || !QDir::isAbsolutePath(local) || !QFileInfo(local).isDir()) {
        emit errorOccurred(tr("Enter a name and an existing absolute folder path."));
        return;
    }
    const auto url = QUrl::fromLocalFile(QDir::cleanPath(local));
    for (const auto &place : m_places) {
        if (place.toMap().value("url").toUrl().toLocalFile().compare(url.toLocalFile(),
#ifdef Q_OS_WIN
                Qt::CaseInsensitive
#else
                Qt::CaseSensitive
#endif
            ) == 0) {
            emit errorOccurred(tr("This folder is already in Quick Access."));
            return;
        }
    }
    m_places.append(QVariantMap{{"name", name}, {"url", url}});
    save();
}
void NavigationSettings::remove(int index)
{
    if (index < 0 || index >= m_places.size()) {
        emit errorOccurred(tr("This Quick Access entry is no longer available."));
        return;
    }
    m_places.removeAt(index);
    save();
}
void NavigationSettings::reset() { m_places = defaults(); save(); }
