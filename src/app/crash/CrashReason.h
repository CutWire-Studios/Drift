#pragma once

#include <QString>

namespace drift::crash {

struct Reason
{
    // One of the server's reasonKey values: crt-outdated, gpu-driver, addon, out-of-memory, unknown.
    QString key;
    QString text;
    QString linkUrl;
    QString linkLabel;
    // The directory name under the addons folder when key is "addon"; empty when the module is
    // onnxruntime from somewhere else.
    QString addonId;
};

// Maps what the crash dump says went wrong to advice a person can act on. `code` is "0xC0000005"
// style, `module` the faulting file name, `modulePath` its full path (may be empty).
Reason mapReason(const QString &code, const QString &module, const QString &modulePath,
                 const QString &addonsDir);

} // namespace drift::crash
