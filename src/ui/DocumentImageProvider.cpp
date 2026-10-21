#include "DocumentImageProvider.h"

#include "core/Document.h"

DocumentImageProvider::DocumentImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage DocumentImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(requestedSize)
    const int documentId = id.section(u'/', 0, 0).toInt();
    Document *document = Document::byId(documentId);
    if (!document)
        return {};
    const QImage image = document->image();
    if (size)
        *size = image.size();
    return image;
}
