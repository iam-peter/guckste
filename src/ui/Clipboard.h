#pragma once

#include <QClipboard>
#include <QGuiApplication>
#include <QObject>
#include <QtQml/qqmlregistration.h>

// The clipboard for QML: text in, text out.
class Clipboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    Q_INVOKABLE void setText(const QString &text) { QGuiApplication::clipboard()->setText(text); }
    Q_INVOKABLE QString text() const { return QGuiApplication::clipboard()->text(); }
};
