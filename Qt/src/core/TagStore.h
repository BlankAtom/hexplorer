#pragma once

#include <QObject>
#include <QSettings>
#include <QMap>
#include <QUrl>
#include <QVariantList>

class TagStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList palette READ palette CONSTANT)
public:
    explicit TagStore(QObject *parent = nullptr);
    QVariantList palette() const;
    static bool validTag(const QString &tag);
    static QString tagName(const QString &tag);
    QStringList tags(const QString &path) const;
    QStringList colors(const QString &path) const;
    QStringList paths(const QString &tag) const;
    bool setTag(const QVariantList &urls, const QString &tag, bool enabled);
    bool clear(const QVariantList &urls);
    void relocate(const QString &source, const QString &destination);
signals:
    void changed();
    void errorOccurred(QString message);
private:
    static QString key(const QString &path);
    bool commit(const QMap<QString, QStringList> &records);
    QSettings m_settings;
    QMap<QString, QStringList> m_records;
    QMap<QString, QString> m_displayPaths;
    bool m_readable = true;
};
