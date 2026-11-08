#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace drift::mcp {

QStringList toolboxNames();
// Default: {toolboxes:[{name, when, ops:["name — when", …]}], limitations}. Flags: guide:true adds
// the agent guide prose, brief:true lists op names only, endpoints:true adds the HTTP endpoints.
QJsonObject catalogPayload(const QJsonObject &args = {});
// name selects one toolbox; only (op names, any toolbox) narrows or replaces it.
QJsonObject toolboxPayload(const QString &name, const QStringList &only = {});
QJsonArray homepageTools();
// The inputSchema of one homepage tool (inspect, capture, frames, …); empty if there is none.
QJsonObject homepageToolSchema(const QString &name);
// Validates a homepage tool's args against its schema (coercing them in place, like toolbox ops)
// and refuses unknown keys with bad_args naming the valid ones: capture({time: 3}) used to capture
// at the playhead without a word. Toolbox ops keep reporting unknown keys as ignored:[...].
// Returns the error object, or {} when the args are fine (or the name has no schema).
QJsonObject checkHomepageArgs(const QString &name, QJsonObject &args);
QJsonArray toolboxDirectTools(const QString &name);
bool isHomepageTool(const QString &name);
bool isKnownOp(const QString &name);
bool isReadOnlyOp(const QString &name);
QString toolboxForOp(const QString &name);
QJsonObject opInputSchema(const QString &name);
QStringList opNames();
// Levenshtein ≤ 3 on lowercased, '-'→'_' forms, or one a substring of the other (≥ 4 chars).
QStringList nearestStrings(const QString &needle, const QStringList &pool, int max = 3);
QStringList nearestOps(const QString &name, int max = 3);
// Always error code "unknown_op"; the detail names the nearest ops or explains that a homepage
// tool cannot run inside apply.
QJsonObject unknownOpError(const QString &name);
// Keyword search over names, toolboxes, when hints, descriptions and schema properties.
QJsonObject searchOps(const QString &q, int limit = 8, bool schema = false);
QString homepageHtml();
QString agentGuideText();
// Mutation ops that isUndoable skips, in addition to every read-only op. The catalog
// limitations, undo/apply descriptions and docs/MCP.md are generated from this list.
QStringList undoExemptOps();
// Ops that take no clip argument and act on the current selection. freeze_frame and
// paste_at_playhead are playhead-based and are deliberately not in this list.
QStringList selectionBasedOps();

} // namespace drift::mcp
