#include "engine/projdir/ProjectGuide.h"

#include <QFile>

namespace drift::projdir {

QString projectFolderGuide()
{
    QFile file(QStringLiteral(":/docs/project-folder.md"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

} // namespace drift::projdir
