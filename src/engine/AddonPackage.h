#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

#include <functional>
#include <optional>

// The .driftpkg container: signed, solid-zstd-compressed addon packages.
//
//   0   "DRIFTPKG"          8   magic
//   8   formatVersion       4   uint32 LE, currently 1
//   12  metadataLength      4   uint32 LE
//   16  metadata            n   UTF-8 JSON, uncompressed (the addon manifest)
//       payloadCompressed   8   uint64 LE
//       payloadRaw          8   uint64 LE
//       payload             n   one zstd frame: every file's bytes concatenated
//       digest             32   SHA-256 over every byte above
//       signature          64   Ed25519 over digest, against AddonSigningKey.h
//
// The payload is deliberately not a tar: the file table lives in the metadata and indexes into
// the *uncompressed* stream, which keeps solid compression (much better across a font pack than
// per-file) without pulling in an archive library.

namespace drift::addon {

struct PackageFile
{
    QString path;     // package-relative, forward slashes, no "." or ".." components
    quint64 offset{}; // into the uncompressed payload; entries are contiguous and ascending
    quint64 size{};
    QByteArray sha256; // 32 raw bytes
};

// What the addon contributes, and where it lives inside the package. `kind` is the extension
// point: "fonts", "stickers", "whisper-model" today; "effects", "transitions", "audio-effects",
// "onnx-model" are routable without a format change. A pack may provide several kinds, and a
// one-item pack is the same shape as a twenty-item one.
struct PackageProvide
{
    QString kind;
    QString root; // package-relative directory handed to the matching catalog
    int items{};
};

struct PackageInfo
{
    QString id;
    QString version; // semver
    QString name;
    QString description;
    // Optional deeper notes for power users; empty when the pack has nothing technical to add.
    QString details;
    QString author;
    QString license;
    QString minAppVersion;
    // Empty for content — a font is a font everywhere. Set only by packages carrying native code
    // (the ONNX Runtime and execution provider addons), where installing the wrong build produces
    // a library that cannot be loaded rather than a visible mistake.
    QString platform; // e.g. "linux-x64", see currentPlatform()
    quint64 installedSize{}; // uncompressed total, for the UI
    QList<PackageProvide> provides;
    QList<PackageFile> files;
    QJsonObject raw;
};

// This build's platform tag: "<os>-<arch>", e.g. "linux-x64", "win-x64", "osx-arm64",
// "android-arm64". Matches the runtime identifiers Microsoft and NuGet use, so an addon recipe
// can name upstream's own archive and this string in the same breath.
QString currentPlatform();

// Official packs are signed .driftpkg files. User packages (.driftfx, exported by Drift Forge) are
// the same layout with magic "DRIFTFX\0" and no signature trailer; the distinct magic means one can
// never be passed off as the other. A user package must provide exactly one effect, transition or audio effect,
// with every file under <root>/<one folder>/.
enum class Container { Signed, User };

// Header + manifest only; cheap, and does NOT check the signature — nothing here is trustworthy
// until install() has run. Use it to show what a sideloaded file claims to be, never to decide
// whether to trust it.
std::optional<PackageInfo> readManifest(const QString &packagePath, QString *error,
                                        Container container = Container::Signed);

// Refuses a package built for another platform or needing a newer Drift. install() applies it too;
// it is separate so a side-loaded file or a hand-made addon folder can be refused before anything
// is extracted.
bool checkCompatible(const PackageInfo &info, QString *error);

// A hand-made addon (a dropped-in folder or a .zip) carries a manifest.json in the same schema as
// .driftpkg metadata, minus the file table: its files are simply whatever is in the folder.
std::optional<PackageInfo> parseFolderManifest(const QByteArray &json, QString *error);

// True when the package provides an ONNX Runtime or execution provider, i.e. a library Drift loads.
bool hasNativeCode(const PackageInfo &info);

// Official: the digest and the Ed25519 signature against AddonSigningKey.h both check out.
// Unverified: the digest is intact but the signature is not ours. The format names no signer, so a
// third party's pack and one whose signature was altered look the same.
// Corrupt: not a readable .driftpkg, or its bytes do not match its digest.
enum class SignatureCheck { Official, Unverified, Corrupt };

// Hashes the file without decompressing it, so a large model costs one read and no disk space.
SignatureCheck checkSignature(const QString &packagePath, QString *error);

// Return false to abort the install.
using ProgressFn = std::function<bool(qint64 done, qint64 total)>;

// Single streaming pass: decompress into `<destDir>.partial`, hash each file as it lands, hash
// the whole signed prefix as it is read, and only once the Ed25519 signature verifies is the
// staging directory promoted onto destDir. A failure, a cancel, or a crash therefore leaves no
// half-populated catalog directory behind — but note that unverified bytes do briefly exist on
// disk under .partial, which is why nothing outside this function ever looks there.
//
// destDir must not exist, or must be replaceable; any existing .partial sibling is discarded.
// requireSignature = false installs a Signed container whose signature is not ours; the digest and
// per-file hashes are still enforced. Only for a file the user chose to trust.
bool install(const QString &packagePath, const QString &destDir, const ProgressFn &progress,
             PackageInfo *installed, QString *error, Container container = Container::Signed,
             bool requireSignature = true);

// Pack <packageDir>'s files into a .driftfx at outPath, as <kind>/<folder name>/... where kind is
// "effects", "transitions" or "audio-effects". `meta` supplies id, name, version and the other
// descriptive fields; the layout fields are filled in here. Written via <outPath>.partial.
bool writeUserPackage(const QString &packageDir, const QString &kind, const QJsonObject &meta,
                      const QString &outPath, QString *error);

} // namespace drift::addon
