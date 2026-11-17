#pragma once

#include "core/base/Time.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace drift {

// Where a source's externally made proxy is expected to sit:
// <srcDir>/<subdir>/<prefix><stem><suffix>.<ext>. An empty ext means the source's own suffix and
// an empty subdir means the source's folder. The stem is QFileInfo::completeBaseName.
struct ProxyNameRule
{
    QString subdir;
    QString prefix;
    QString suffix;
    QString ext;

    bool operator==(const ProxyNameRule &) const = default;
};

// Display and storage form: "*_proxy", "Proxy/*_proxy.mov", "PRX_*". `*` stands for the stem.
QString proxyRuleToString(const ProxyNameRule &rule);
// Rejects a string without exactly one `*` in its filename part, or with a ".." subdir segment.
std::optional<ProxyNameRule> proxyRuleFromString(const QString &text);

// The rule that maps sourcePath onto proxyPath. Fails when the proxy's stem doesn't contain the
// source's stem, when the proxy sits outside the source's folder tree, or when the rule would
// map the source onto itself.
std::optional<ProxyNameRule> learnProxyRule(const QString &sourcePath, const QString &proxyPath);

QString proxyCandidatePath(const ProxyNameRule &rule, const QString &sourcePath);

// Where Drift writes its own proxy: <srcDir>/<stem>_proxy_<N>.mp4.
QString generatedProxyPath(const QString &sourcePath, int shortSide);

QStringList defaultProxyNamePatterns();

// A sibling file can only be looked for beside a real filesystem path; content:// has none.
bool isLocalSourcePath(const QString &path);

// Whether `target` can be created in its folder, tried by really creating and removing
// `<target>.part`: QFileInfo::isWritable lies on network shares.
bool canWriteBeside(const QString &target);

// Where Drift writes a reversed render: <srcDir>/<stem>_reversed_<inMs>-<outMs>.mp4. The range is
// rounded outward to whole milliseconds, so callers that render exactly that range get a name
// that is exact and a file that covers at least what was asked for.
QString reversedSidecarPath(const QString &sourcePath, TimeUs inUs, TimeUs outUs);

struct ReversedRange
{
    TimeUs inUs = 0;
    TimeUs outUs = 0;
};

// The range a file named by reversedSidecarPath covers, or nullopt when `fileName` isn't one for
// this source. The stem is matched as a literal prefix: stems can hold glob characters.
std::optional<ReversedRange> parseReversedSidecarName(const QString &sourcePath,
                                                      const QString &fileName);

// 8 lowercase hex chars from SHA-1 of the cleaned absolute source path. Case-folded where the
// file system ignores case, so one file opened under two spellings maps to one proxy.
QString proxySourceKey(const QString &sourcePath);

// Where Drift writes its own proxy inside the chosen folder: <folder>/<stem>_<key>_proxy_<N>.mp4.
QString folderProxyPath(const QString &folder, const QString &sourcePath, int shortSide);
// <folder>/<stem>_<key>_reversed_<inMs>-<outMs>.mp4, rounded outward as reversedSidecarPath.
QString folderReversedPath(const QString &folder, const QString &sourcePath, TimeUs inUs, TimeUs outUs);
// The range a folderReversedPath name covers, or nullopt when it isn't one for this source.
std::optional<ReversedRange> parseFolderReversedName(const QString &sourcePath, const QString &fileName);
// The same, split so a caller scanning many files for one source hashes the path once.
QString folderReversedHead(const QString &sourcePath);
std::optional<ReversedRange> parseFolderReversedNameWithHead(const QString &head, const QString &fileName);

} // namespace drift
