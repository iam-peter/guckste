#pragma once

#include <QColor>
#include <QObject>
#include <QRect>
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
    // A text's font size in pixels, apart from the stroke size.
    Q_PROPERTY(qreal textSize READ textSize WRITE setTextSize NOTIFY changed)
    Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY changed)
    // Drawn and moved marks snap to the editor's guides.
    Q_PROPERTY(bool snapToGuides READ snapToGuides WRITE setSnapToGuides NOTIFY changed)
    // The guides are drawn into copies and saved images.
    Q_PROPERTY(bool exportGuides READ exportGuides WRITE setExportGuides NOTIFY changed)
    Q_PROPERTY(bool copyPathAfterSave READ copyPathAfterSave WRITE setCopyPathAfterSave NOTIFY changed)
    Q_PROPERTY(int frameRate READ frameRate WRITE setFrameRate NOTIFY changed)
    Q_PROPERTY(bool recordMicrophone READ recordMicrophone WRITE setRecordMicrophone NOTIFY changed)
    Q_PROPERTY(int recordingQuality READ recordingQuality WRITE setRecordingQuality NOTIFY changed)
    Q_PROPERTY(bool editAfterRecording READ editAfterRecording WRITE setEditAfterRecording NOTIFY changed)
    Q_PROPERTY(bool includeCursor READ includeCursor WRITE setIncludeCursor NOTIFY changed)
    Q_PROPERTY(bool showKeys READ showKeys WRITE setShowKeys NOTIFY changed)
    Q_PROPERTY(bool showKeysSupported READ showKeysSupported CONSTANT)
    Q_PROPERTY(QRect lastRegion READ lastRegion WRITE setLastRegion NOTIFY changed)
    Q_PROPERTY(QString pickMode READ pickMode WRITE setPickMode NOTIFY changed)

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
    qreal textSize() const;
    void setTextSize(qreal size);
    bool showGrid() const;
    bool snapToGuides() const;
    void setSnapToGuides(bool on);
    bool exportGuides() const;
    void setExportGuides(bool on);
    void setShowGrid(bool on);
    bool copyPathAfterSave() const;
    void setCopyPathAfterSave(bool on);

    int frameRate() const;
    void setFrameRate(int fps);
    bool recordMicrophone() const;
    void setRecordMicrophone(bool on);
    // 0 low, 1 medium, 2 high, 3 very high: see Encoding::Quality.
    int recordingQuality() const;
    void setRecordingQuality(int quality);
    // Open a recording in the video editor rather than saving it at once.
    bool editAfterRecording() const;
    void setEditAfterRecording(bool on);
    // The pointer in screenshots and recordings.
    bool includeCursor() const;
    void setIncludeCursor(bool on);
    // Keys pressed, shown along the bottom of recordings.
    bool showKeys() const;
    void setShowKeys(bool on);
    bool showKeysSupported() const;
    // The region last captured or recorded, in screen coordinates, offered
    // again next time.
    QRect lastRegion() const;
    void setLastRegion(const QRect &region);
    // How the overlay picks: "screen", "window" or "region", as last set.
    QString pickMode() const;
    void setPickMode(const QString &mode);

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
