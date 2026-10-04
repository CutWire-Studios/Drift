#pragma once

#include <QJsonObject>
#include <QString>

class AppController;

// Agent-authored effect packages. Each one is an ordinary user package under
// <AppData>/<effects|transitions|audio-effects>/<slug>/, the same place an imported .driftfx
// lands, so it shows up in My Effects and plugs into add_effect / add_transition /
// add_audio_effect like any other catalog entry.
namespace drift::mcp::authoring {

QJsonObject guide(const QString &kind);
QJsonObject create(AppController *controller, const QJsonObject &args);
QJsonObject update(AppController *controller, const QJsonObject &args);
QJsonObject source(const QJsonObject &args);
QJsonObject remove(AppController *controller, const QJsonObject &args);
QJsonObject exportPackage(const QJsonObject &args);

} // namespace drift::mcp::authoring
