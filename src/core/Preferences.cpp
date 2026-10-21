#include "Preferences.h"

#include "FileNaming.h"
#include "KeyListener.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtQml/qqmlengine.h>

namespace {
const auto kSaveDirectory = "capture/saveDirectory";
const auto kFilenamePattern = "capture/filenamePattern";
const auto kCopyPath = "capture/copyPathAfterSave";
const auto kColor = "tools/color";
const auto kSize = "tools/size";
const auto kShowGrid = "editor/showGrid";
const auto kFrameRate = "recording/frameRate";
const auto kMicrophone = "recording/microphone";
const auto kQuality = "recording/quality";
const auto kEditAfter = "recording/editAfterRecording";
const auto kIncludeCursor = "capture/includeCursor";
const auto kShowKeys = "recording/showKeys";
const auto kDefaultPattern = "guckste_%Y-%m-%d_%H-%M-%S";
} // namespace

Preferences::Preferences(QObject *parent)
    : QObject(parent)
{
}

Preferences *Preferences::instance()
{
    static Preferences preferences;
    return &preferences;
}

Preferences *Preferences::create(QQmlEngine *, QJSEngine *)
{
    Preferences *p = instance();
    QQmlEngine::setObjectOwnership(p, QQmlEngine::CppOwnership);
    return p;
}

QString Preferences::saveDirectory() const
{
    const QString fallback = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                             + QStringLiteral("/Screenshots");
    return m_settings.value(kSaveDirectory, fallback).toString();
}

void Preferences::setSaveDirectory(const QString &dir)
{
    if (dir == saveDirectory())
        return;
    m_settings.setValue(kSaveDirectory, dir);
    emit changed();
}

QString Preferences::filenamePattern() const
{
    return m_settings.value(kFilenamePattern, QString::fromLatin1(kDefaultPattern)).toString();
}

void Preferences::setFilenamePattern(const QString &pattern)
{
    if (pattern == filenamePattern())
        return;
    m_settings.setValue(kFilenamePattern, pattern.isEmpty() ? QString::fromLatin1(kDefaultPattern) : pattern);
    emit changed();
}

QString Preferences::autostartPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart/guckste.desktop");
}

bool Preferences::launchAtLoginSupported() const
{
#if defined(Q_OS_LINUX)
    return true;
#else
    return false;
#endif
}

bool Preferences::launchAtLogin() const
{
    return launchAtLoginSupported() && QFile::exists(autostartPath());
}

void Preferences::setLaunchAtLogin(bool on)
{
    if (!launchAtLoginSupported() || on == launchAtLogin())
        return;
    const QString path = autostartPath();
    if (!on) {
        QFile::remove(path);
    } else {
        // An AppImage has to be started by its own path, an installed
        // guckste by its name.
        QString exec = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
        if (exec.isEmpty())
            exec = QCoreApplication::applicationFilePath();
        if (exec.startsWith(QStringLiteral("/usr/")) || exec.startsWith(QStringLiteral("/opt/")))
            exec = QStringLiteral("guckste");
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=Guckste\n"
                                      "Comment=Screenshot tool in the tray\nExec=\"%1\"\nIcon=guckste\n"
                                      "Terminal=false\nX-GNOME-Autostart-enabled=true\n")
                           .arg(exec)
                           .toUtf8());
        }
    }
    emit changed();
}

QColor Preferences::color() const
{
    return m_settings.value(kColor, QColor(255, 59, 48)).value<QColor>();
}

void Preferences::setColor(const QColor &color)
{
    if (color == this->color())
        return;
    m_settings.setValue(kColor, color);
    emit changed();
}

qreal Preferences::size() const
{
    return m_settings.value(kSize, 3.0).toReal();
}

void Preferences::setSize(qreal size)
{
    if (qFuzzyCompare(size, this->size()))
        return;
    m_settings.setValue(kSize, size);
    emit changed();
}

bool Preferences::showGrid() const
{
    return m_settings.value(kShowGrid, true).toBool();
}

void Preferences::setShowGrid(bool on)
{
    if (on == showGrid())
        return;
    m_settings.setValue(kShowGrid, on);
    emit changed();
}

bool Preferences::copyPathAfterSave() const
{
    return m_settings.value(kCopyPath, false).toBool();
}

void Preferences::setCopyPathAfterSave(bool on)
{
    if (on == copyPathAfterSave())
        return;
    m_settings.setValue(kCopyPath, on);
    emit changed();
}

int Preferences::frameRate() const
{
    return qBound(1, m_settings.value(kFrameRate, 30).toInt(), 120);
}

void Preferences::setFrameRate(int fps)
{
    if (fps == frameRate())
        return;
    m_settings.setValue(kFrameRate, fps);
    emit changed();
}

bool Preferences::recordMicrophone() const
{
    return m_settings.value(kMicrophone, false).toBool();
}

void Preferences::setRecordMicrophone(bool on)
{
    if (on == recordMicrophone())
        return;
    m_settings.setValue(kMicrophone, on);
    emit changed();
}

int Preferences::recordingQuality() const
{
    return qBound(0, m_settings.value(kQuality, 2).toInt(), 3);
}

void Preferences::setRecordingQuality(int quality)
{
    if (quality == recordingQuality())
        return;
    m_settings.setValue(kQuality, qBound(0, quality, 3));
    emit changed();
}

bool Preferences::editAfterRecording() const
{
    return m_settings.value(kEditAfter, true).toBool();
}

void Preferences::setEditAfterRecording(bool on)
{
    if (on == editAfterRecording())
        return;
    m_settings.setValue(kEditAfter, on);
    emit changed();
}

bool Preferences::includeCursor() const
{
    return m_settings.value(kIncludeCursor, true).toBool();
}

void Preferences::setIncludeCursor(bool on)
{
    if (on == includeCursor())
        return;
    m_settings.setValue(kIncludeCursor, on);
    emit changed();
}

bool Preferences::showKeys() const
{
    return m_settings.value(kShowKeys, false).toBool();
}

void Preferences::setShowKeys(bool on)
{
    if (on == showKeys())
        return;
    m_settings.setValue(kShowKeys, on);
    emit changed();
}

bool Preferences::showKeysSupported() const
{
    return KeyListener::supported();
}

QString Preferences::nextRecordingPath(const QString &suffix) const
{
    return FileNaming::uniquePath(saveDirectory(), filenamePattern(), suffix);
}

QString Preferences::nextSavePath() const
{
    return FileNaming::uniquePath(saveDirectory(), filenamePattern(), QStringLiteral("png"));
}

QString Preferences::previewFileName(const QString &pattern) const
{
    return FileNaming::expand(pattern, QDateTime::currentDateTime()) + QStringLiteral(".png");
}
