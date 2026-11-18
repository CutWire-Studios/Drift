#pragma once

#include "core/base/Time.h"

#include <QMap>
#include <QString>
#include <QVariant>

#include <functional>

namespace drift {
class Project;
}

// Project patches for CompositorService::setPreviewPatch: each points one clip (or cut) at the
// Forge's draft package on the detached snapshot, leaving the live project untouched.
namespace drift::forge {

using PreviewPatch = std::function<void(drift::Project &)>;

PreviewPatch patchAddEffect(const QString &clipId, const QMap<QString, QVariant> &params);
// The cut is addressed the way Transition does: outgoing and incoming clip ids. Reuses the
// transition already there, otherwise adds a temporary one.
PreviewPatch patchTransition(const QString &fromClipId, const QString &toClipId, const QMap<QString, QVariant> &params,
                             drift::TimeUs durationUs);
PreviewPatch patchAnimation(const QString &clipId, bool in, const QMap<QString, QVariant> &params,
                            drift::TimeUs durationUs);
PreviewPatch patchAudioEffect(const QString &clipId, const QMap<QString, QVariant> &params);

} // namespace drift::forge
