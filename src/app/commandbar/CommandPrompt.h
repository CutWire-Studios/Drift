#pragma once

#include "app/commandbar/CommandCatalog.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace drift::commandbar {

// What the model is told about the editor. Only these four values: the model was trained on
// exactly this context line.
struct CommandContext
{
    bool hasSelection = false;
    QString clip; // uuid
    int track = 0;
    int index = 0;
    double playhead = 0; // seconds
};

struct ToolCall
{
    QString name;
    QJsonObject arguments;
};

struct ParsedCalls
{
    QList<ToolCall> calls;
    bool refusal = false; // the model said "No tool fits this request."
};

extern const char kCommandSystemPrompt[];
extern const char kCommandRefusal[];

// "Selected clip: <uuid>, track 1, index 3. Playhead: 18.0 seconds." or
// "No clip is selected. Playhead: 0.0 seconds." The playhead is rounded to 2 decimals and written
// like a Python float (always a decimal point).
QString commandContextLine(const CommandContext &context);

// One tool exactly as the training prompt carries it: Python json.dumps defaults (", " and ": "),
// non-ASCII kept, {"type": "function", "function": {name, description, parameters}}, with the
// description cut to 200 characters and the schema compacted (cactus-research compact_schema.py).
QString commandToolJson(const CommandOp &op);

// The whole Qwen3 chat-template prompt: system (instruction + context line + tools block), user,
// assistant prefix with the empty think block. The caller sends it to /completion; the model
// answers with <tool_call> blocks or the refusal sentence and ends with <|im_end|>.
// The query is trimmed and loses any chat-template control tokens.
QString commandPrompt(const QString &query, const CommandContext &context, const QList<CommandOp> &tools);

// Reads <tool_call>{json}</tool_call> blocks (an unterminated last block is fine), or a bare JSON
// object/list when there are none. Entries without a name, or with unparsable arguments, are
// skipped. Port of eval_llamacpp.fallback_calls without its Python-literal fallback.
ParsedCalls parseCommandCalls(const QString &completion);

} // namespace drift::commandbar
