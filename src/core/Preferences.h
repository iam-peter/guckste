#pragma once

#include <QColor>
#include <QObject>
#include <QSettings>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

// Settings, shared by the QML windows and the command line.
class Preferences : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString saveDirectory READ saveDirectory WRITE setSaveDirectory NOTIFY changed)
    Q_PROPERTY(QString filenamePattern READ filenamePattern WRITE setFilenamePattern NOTIFY changed)
    Q_PROPERTY(bool launchAtLogin READ launchAtLogin WRITE setLaunchAtLogin NOTIFY changed)
    Q_PROPERTY(bool launchAtLoginSupported READ launchAtLoginSupported CONSTANT)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
    Q_PROPERTY(qreal size READ size WRITE setSize NOTIFY changed)
    Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY changed)
    Q_PROPERTY(bool copyPathAfterSave READ copyPathAfterSave WRITE setCopyPathAfterSave NOTIFY changed)
    Q_PROPERTY(int frameRate READ frameRate WRITE setFrameRate NOTIFY changed)
    Q_PROPERTY(bool recordMicrophone READ recordMicrophone WRITE setRecordMicrophone NOTIFY changed)

public:
    static Preferences *instance();
    static Preferences *create(QQmlEngine *, QJSEngine *);

    QString saveDirectory() const;
    void setSaveDirectory(const QString &dir);
    QString filenamePattern() const;
    void setFilenamePattern(const QString &pattern);
    bool launchAtLogin() const;
    void setLaunchAtLogin(bool on);
    bool launchAtLoginSupported() const;
    QColor color() const;
    void setColor(const QColor &color);
    qreal size() const;
    void setSize(qreal size);
    bool showGrid() const;
    void setShowGrid(bool on);
    bool copyPathAfterSave() const;
    void setCopyPathAfterSave(bool on);

    int frameRate() const;
    void setFrameRate(int fps);
    bool recordMicrophone() const;
    void setRecordMicrophone(bool on);

    // Where the next screenshot goes: the directory, the pattern, .png.
    Q_INVOKABLE QString nextSavePath() const;
    // The same for a recording, with the video format's suffix.
    Q_INVOKABLE QString nextRecordingPath(const QString &suffix) const;
    Q_INVOKABLE QString previewFileName(const QString &pattern) const;

signals:
    void changed();

private:
    explicit Preferences(QObject *parent = nullptr);
    static QString autostartPath();

    mutable QSettings m_settings;
};
