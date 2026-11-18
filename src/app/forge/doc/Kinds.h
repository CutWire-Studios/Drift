#pragma once

#include <QString>
#include <QStringList>

namespace drift::forge {

enum class Kind { Effect, Transition, Animation, Audio };

struct KindInfo
{
    QString name;
    QString label;
    QString withArticle;
    QString defaultName;
    QString defaultCategory;
    QStringList categories;
    QString packageRoot;
    QString manifestFile;
    QString previewFile;
    QString browserName;
    QString outputNode;
    bool clockIsTime = false;
};

const KindInfo &kindInfo(Kind kind);
bool kindFromName(const QString &name, Kind *out);

}
