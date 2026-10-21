#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

#include <memory>

class Document;
class QQuickWindow;
class QSystemTrayIcon;
class QMenu;
class QAction;

// The application: the tray icon, the windows, and what the command line
// and the tray ask for.
class App : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged)

public:
    explicit App(QQmlEngine *engine, QObject *parent = nullptr);
    ~App() override;

    static App *instance();
    static App *create(QQmlEngine *, QJSEngine *);

    void setupTray();
    bool capturing() const { return m_overlay != nullptr; }

    // "gui": pick a region; "full": all screens to clipboard and file;
    // "open <path>": the editor; "open": the editor with a file dialog.
    void handleCommand(const QString &command);

    Q_INVOKABLE void capture();
    Q_INVOKABLE void captureFull();
    Q_INVOKABLE void openImage();
    Q_INVOKABLE bool openEditor(const QString &path);
    // Hands the overlay's document, cropped to the region, to a new editor.
    Q_INVOKABLE void editInWindow(Document *document, const QRectF &region);
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void closeOverlay();
    Q_INVOKABLE void notify(const QString &title, const QString &text);
    Q_INVOKABLE void quit();

signals:
    void capturingChanged();

private:
    QQuickWindow *createWindow(const QString &type, const QVariantMap &properties);
    void showEditor(Document *document);

    QQmlEngine *m_engine;
    std::unique_ptr<QSystemTrayIcon> m_tray;
    std::unique_ptr<QMenu> m_trayMenu;
    QAction *m_launchAtLogin = nullptr;
    QPointer<QQuickWindow> m_overlay;
    QPointer<QQuickWindow> m_settings;
    QList<QPointer<QQuickWindow>> m_editors;
};
