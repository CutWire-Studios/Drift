#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

class AppController;

// Writes, validates, installs and exports user-authored GPU/audio packages under
// <AppData>/<subdir>/<slug>/. Replies use the MCP envelope ({ok:false, error, detail} on failure).
namespace drift::forge {

struct PackageKind
{
    QString name;     // "effect" | "transition" | "animation" | "audio_effect"
    QString subdir;   // also the addon kind
    QString manifest; // file name inside the package folder
};

std::optional<PackageKind> packageKind(const QString &name);
std::optional<PackageKind> packageKindForAddon(const QString &subdir);

QString userRoot(const PackageKind &kind);
bool isUserDir(const PackageKind &kind, const QString &packageDir);
// Directory of the catalog entry with this id, or empty when there is none.
QString packageDirForId(const PackageKind &kind, const QString &id);
QString listOp(const PackageKind &kind);
void reload(AppController *controller, const PackageKind &kind);

// Writes manifest + args.files / args.binary_files into a fresh `dir`. Empty object on success.
QJsonObject stage(const PackageKind &kind, const QString &dir, QJsonObject manifest, const QJsonObject &args);
// Parses a staged package and, for GPU kinds, test-compiles its shaders. shaderCheck is set to
// "ok", "skipped" or "n/a". Empty object on success.
QJsonObject validate(const PackageKind &kind, const QString &dir, QString *shaderCheck);
// Stage, validate, then swap into `target`; `id` is written into the manifest.
QJsonObject install(AppController *controller, const PackageKind &kind, const QString &target, const QString &id,
                    const QJsonObject &args);
// Packs a user package into a .driftfx (args: kind-less; id, path, author, description).
QJsonObject exportDriftfx(const PackageKind &kind, const QJsonObject &args);

} // namespace drift::forge
