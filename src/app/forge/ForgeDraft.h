#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

namespace drift::forge {

struct DraftResult
{
    bool ok = false;
    QString error;
    QStringList shaderLog;
    // True when GPU kinds could not be test-compiled (no GL); the draft is still installed.
    bool shaderSkipped = false;
};

// kind: "effect" | "transition" | "animation" | "audio_effect" (PackageInstaller's names).
// <AppData>/forge-drafts/<kind>/, outside every package search path.
QString draftDir(const QString &kind);

// Writes the package, parses it, test-compiles GPU shaders, and only then publishes it to the
// kind's draft slot. On failure the previous draft stays active.
DraftResult stageDraft(const QString &kind, const QJsonObject &manifest, const QMap<QString, QByteArray> &files);
void clearDraft(const QString &kind);

} // namespace drift::forge
