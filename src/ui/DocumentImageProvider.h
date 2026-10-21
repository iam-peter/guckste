#pragma once

#include <QQuickImageProvider>

// Serves image://guckste/<document id>/<version> to QML Image items.
class DocumentImageProvider : public QQuickImageProvider
{
public:
    DocumentImageProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
