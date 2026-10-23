#pragma once

#include <QDateTime>
#include <QString>

// File names for saved screenshots from a strftime-like pattern: %Y %m %d
// %H %M %S, plus %y %b %a %p and %% for a percent sign.
namespace FileNaming {

QString expand(const QString &pattern, const QDateTime &time);
// dir/<expanded pattern>.<suffix>, with -2, -3 … appended while the
// name is taken.
QString uniquePath(const QString &dir, const QString &pattern, const QString &suffix,
                   const QDateTime &time = QDateTime::currentDateTime());
// The same for a name already expanded: dir/<base>.<suffix>.
QString uniqueFile(const QString &dir, const QString &base, const QString &suffix);
// dir/<base> for a folder of its own, -2, -3 … appended while taken.
QString uniqueFolder(const QString &dir, const QString &base);

} // namespace FileNaming
