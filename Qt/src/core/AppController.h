#pragma once

#include "Workspace.h"
#include "TaskManager.h"
#include "NativeServices.h"
#include "PreviewController.h"
#include <QFileSystemModel>
#include <QSettings>
#include <QThreadPool>

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(Workspace* workspace READ workspace CONSTANT)
    Q_PROPERTY(TaskManager* tasks READ tasks CONSTANT)
    Q_PROPERTY(NativeServices* native READ native CONSTANT)
    Q_PROPERTY(PreviewController* preview READ preview CONSTANT)
    Q_PROPERTY(QFileSystemModel* tree READ tree CONSTANT)
    Q_PROPERTY(QVariantList places READ places CONSTANT)
    Q_PROPERTY(bool leftVisible READ leftVisible WRITE setLeftVisible NOTIFY settingsChanged)
    Q_PROPERTY(bool rightVisible READ rightVisible WRITE setRightVisible NOTIFY settingsChanged)
    Q_PROPERTY(bool swapSides READ swapSides WRITE setSwapSides NOTIFY settingsChanged)
    Q_PROPERTY(int theme READ theme WRITE setTheme NOTIFY settingsChanged)
    Q_PROPERTY(double leftWidth READ leftWidth WRITE setLeftWidth NOTIFY settingsChanged)
    Q_PROPERTY(double rightWidth READ rightWidth WRITE setRightWidth NOTIFY settingsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;
    Workspace *workspace() { return &m_workspace; }
    TaskManager *tasks() { return &m_tasks; }
    NativeServices *native() { return &m_native; }
    PreviewController *preview() { return &m_preview; }
    QFileSystemModel *tree() { return &m_tree; }
    QVariantList places() const;
    bool leftVisible() const { return m_leftVisible; }
    bool rightVisible() const { return m_rightVisible; }
    bool swapSides() const { return m_swapSides; }
    int theme() const { return m_theme; }
    double leftWidth() const { return m_leftWidth; }
    double rightWidth() const { return m_rightWidth; }
    bool busy() const;
    void setLeftVisible(bool value);
    void setRightVisible(bool value);
    void setSwapSides(bool value);
    void setTheme(int value);
    void setLeftWidth(double value);
    void setRightWidth(double value);
    Q_INVOKABLE void copy(bool cut = false);
    Q_INVOKABLE void paste();
    Q_INVOKABLE void requestDelete(bool permanent = false);
    Q_INVOKABLE void confirmDelete();
    Q_INVOKABLE void requestRename();
    Q_INVOKABLE void requestFolder();
    Q_INVOKABLE void acceptName(QString name);
    Q_INVOKABLE void extract();
    Q_INVOKABLE void properties();
    Q_INVOKABLE void contextMenu(double x, double y);
    Q_INVOKABLE void navigateTree(QModelIndex index);
    Q_INVOKABLE int dropAction(QList<QUrl> urls, QUrl destination, int modifiers, int supportedActions);
    Q_INVOKABLE bool drop(QList<QUrl> urls, QUrl destination, int action);
    Q_INVOKABLE int dropAcknowledgement(int action, int supportedActions) const;
    Q_INVOKABLE int dragModifiers() const { return m_dragModifiers; }
    Q_INVOKABLE void refreshAll();
    Q_INVOKABLE void saveSession();
    Q_INVOKABLE void setWindowGeometry(int width, int height, bool maximized);
    Q_INVOKABLE QVariantMap windowGeometry() const;
    Q_INVOKABLE QString transferSummary(qulonglong done, qulonglong total, double speed) const;
signals:
    void settingsChanged();
    void busyChanged();
    void notification(QString message);
    void deletionRequested(int count, bool permanent);
    void nameRequested(QString title, QString initialName);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    QVariantList selection() const;
    void updatePreview();
    void runMutation(QString source, QString destination, bool createDirectory);
    Workspace m_workspace;
    TaskManager m_tasks;
    NativeServices m_native;
    PreviewController m_preview;
    QFileSystemModel m_tree;
    QThreadPool m_pool;
    QSettings m_settings;
    QTimer m_saveTimer;
    QVariantList m_deleteSources;
    bool m_permanent = false;
    QString m_nameSource;
    QUrl m_nameDirectory;
    bool m_createDirectory = false;
    int m_mutations = 0;
    bool m_leftVisible = true;
    bool m_rightVisible = true;
    bool m_swapSides = false;
    int m_theme = 0;
    double m_leftWidth = 220;
    double m_rightWidth = 280;
    bool m_reportedSettingsError = false;
    int m_dragModifiers = Qt::NoModifier;
};
