#pragma once

#include <QObject>
#include <QSettings>
#include <QVariantList>
#include <QUrl>

class NavigationSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool expanded READ expanded WRITE setExpanded NOTIFY changed)
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY changed)
    Q_PROPERTY(QVariantList places READ places NOTIFY changed)
public:
    explicit NavigationSettings(QObject *parent = nullptr);
    bool expanded() const { return m_expanded; }
    bool visible() const { return m_visible; }
    QVariantList places() const { return m_places; }
    void setExpanded(bool value);
    void setVisible(bool value);
    Q_INVOKABLE void add(QString name, QString path);
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void reset();
signals:
    void changed();
    void errorOccurred(QString message);
private:
    void save();
    QVariantList defaults() const;
    QSettings m_settings;
    QVariantList m_places;
    bool m_expanded = true;
    bool m_visible = true;
};
