#pragma once

#include <QJsonObject>

#include "core/project/clip/Clip.h"

class AppController;

// Agent-authored procedural 3D packages: the MCP face of ProceduralAuthoringService. Each package is
// an ordinary user package under <AppData>/procedurals/<slug>/ and works with add_procedural.
namespace drift::mcp::procedural {

QJsonObject guide();
QJsonObject list();
QJsonObject create(const QJsonObject &args);
QJsonObject update(const QJsonObject &args);
QJsonObject source(AppController *controller, const QJsonObject &args);
QJsonObject remove(AppController *controller, const QJsonObject &args);
QJsonObject add(AppController *controller, const QJsonObject &args);
// `clip` is the clip the caller resolved from clip/track+index, or null to evaluate a package by id.
QJsonObject evaluate(AppController *controller, const QJsonObject &args, const drift::Clip *clip);

} // namespace drift::mcp::procedural
