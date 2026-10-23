#include "FileNaming.h"

#include <QFileInfo>
#include <QLocale>

QString FileNaming::expand(const QString &pattern, const QDateTime &time)
{
    const QLocale locale = QLocale::c();
    QString out;
    for (int i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern[i];
        if (c != u'%' || i + 1 >= pattern.size()) {
            out += c;
            continue;
        }
        const QChar code = pattern[++i];
        switch (code.unicode()) {
        case u'Y': out += time.toString(QStringLiteral("yyyy")); break;
        case u'y': out += time.toString(QStringLiteral("yy")); break;
        case u'm': out += time.toString(QStringLiteral("MM")); break;
        case u'd': out += time.toString(QStringLiteral("dd")); break;
        case u'H': out += time.toString(QStringLiteral("HH")); break;
        case u'M': out += time.toString(QStringLiteral("mm")); break;
        case u'S': out += time.toString(QStringLiteral("ss")); break;
        case u'b': out += locale.toString(time, QStringLiteral("MMM")); break;
        case u'a': out += locale.toString(time, QStringLiteral("ddd")); break;
        case u'p': out += locale.toString(time, QStringLiteral("AP")); break;
        case u'%': out += u'%'; break;
        default: out += u'%'; out += code; break;
        }
    }
    // Slashes would make directories, which the pattern isn't for.
    out.replace(u'/', u'-');
    return out.isEmpty() ? QStringLiteral("screenshot") : out;
}

QString FileNaming::uniquePath(const QString &dir, const QString &pattern, const QString &suffix,
                               const QDateTime &time)
{
    return uniqueFile(dir, expand(pattern, time), suffix);
}

QString FileNaming::uniqueFile(const QString &dir, const QString &base, const QString &suffix)
{
    QString path = dir + u'/' + base + u'.' + suffix;
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = dir + u'/' + base + u'-' + QString::number(n) + u'.' + suffix;
    return path;
}

QString FileNaming::uniqueFolder(const QString &dir, const QString &base)
{
    QString path = dir + u'/' + base;
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = dir + u'/' + base + u'-' + QString::number(n);
    return path;
}
