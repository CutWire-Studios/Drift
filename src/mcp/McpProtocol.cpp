#include "mcp/McpProtocol.h"
#include "mcp/McpCatalog.h"
#include "mcp/McpJson.h"

#ifndef DRIFT_VERSION
#define DRIFT_VERSION "0.0.0"
#endif

namespace drift::mcp {
namespace {

QJsonObject jsonRpcError(const QJsonValue &id, int code, const QString &message)
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id},
            {QStringLiteral("error"),
             QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}};
}

QJsonObject jsonRpcOk(const QJsonValue &id, const QJsonValue &result)
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id},
            {QStringLiteral("result"), result}};
}

QJsonObject initializeResult()
{
    return {
        {QStringLiteral("protocolVersion"), QStringLiteral("2025-03-26")},
        {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject{}}}},
        {QStringLiteral("serverInfo"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("drift")},
                     {QStringLiteral("version"), QStringLiteral(DRIFT_VERSION)}}},
        {QStringLiteral("instructions"),
         QStringLiteral(
             "Drift video editor. Workflow: catalog (or search({q}) by keyword) → toolbox({name}) or "
             "toolbox({ops:[…]}) for schemas → apply({ops:[{tool,args}…]}) to edit; one batch is one undo "
             "step. To see the footage: activity() finds where content/motion/audio change, frames() "
             "renders a labelled contact sheet of distinct moments, capture({at}) gives one full still. "
             "Clip refs: pass clip (uuid from inspect({clips:true})) or track+index; ops never fall back "
             "to the selection. Times are seconds; track 0 is the top lane; overlap is off by default. "
             "apply is not atomic: on failure done lists only the ops that ran. Args are validated "
             "against the schema (bad_args, type_mismatch) and unknown keys come back as ignored. "
             "Effect ids come from list_effects (compact by default; id/cat/q for params). "
             "inspect is a summary; clips:true adds clip rows, detail:true expands them (defaults "
             "and empties are omitted; verbose:true keeps them), clip:<uuid>/track:<n> filter. Async "
             "jobs report under inspect({detail:true}).jobs and inspect().export. Selection-based ops "
             "(separate_audio, merge_clips, copy_selection…) need select_clip first. Full guide: "
             "catalog({guide:true}).")},
    };
}

QJsonValue handleOne(const QJsonObject &req, const QString &toolbox, const ToolHandler &handler)
{
    const QJsonValue id = req.value(QStringLiteral("id"));
    const QString method = req.value(QStringLiteral("method")).toString();
    const QJsonObject params = req.value(QStringLiteral("params")).toObject();
    const bool notification = !req.contains(QStringLiteral("id"));

    if (method == QLatin1String("initialize"))
        return jsonRpcOk(id, initializeResult());
    if (method == QLatin1String("notifications/initialized") || method == QLatin1String("initialized"))
        return notification ? QJsonValue(QJsonValue::Undefined) : jsonRpcOk(id, QJsonObject{});
    if (method == QLatin1String("ping"))
        return jsonRpcOk(id, QJsonObject{});
    if (method == QLatin1String("tools/list"))
        return jsonRpcOk(id, QJsonObject{{QStringLiteral("tools"), toolsForEndpoint(toolbox)}});
    if (method == QLatin1String("resources/list"))
        return jsonRpcOk(id, QJsonObject{{QStringLiteral("resources"), QJsonArray{}}});
    if (method == QLatin1String("prompts/list"))
        return jsonRpcOk(id, QJsonObject{{QStringLiteral("prompts"), QJsonArray{}}});

    if (method == QLatin1String("tools/call")) {
        const QString name = params.value(QStringLiteral("name")).toString();
        const QJsonObject args = params.value(QStringLiteral("arguments")).toObject();
        if (name.isEmpty())
            return jsonRpcError(id, -32602, QStringLiteral("Missing tool name"));
        if (!handler)
            return jsonRpcError(id, -32603, QStringLiteral("No tool handler"));

        const bool homepage = toolbox.isEmpty();
        if (homepage && !isHomepageTool(name) && !isKnownOp(name))
            return jsonRpcOk(id, textResult(unknownOpError(name), true));
        if (!homepage) {
            static const QStringList readTools = {QStringLiteral("inspect"), QStringLiteral("capture"),
                                                  QStringLiteral("frames"), QStringLiteral("activity")};
            if (isHomepageTool(name) && !readTools.contains(name))
                return jsonRpcOk(id, textResult(err("wrong_endpoint", QStringLiteral("Use /mcp for %1").arg(name)), true));
            if (isKnownOp(name) && toolboxForOp(name) != toolbox)
                return jsonRpcOk(id, textResult(err("wrong_toolbox", QStringLiteral("%1 belongs to %2").arg(name, toolboxForOp(name))), true));
        }

        const QJsonObject result = handler(name, args);
        return jsonRpcOk(id, result);
    }

    if (notification)
        return QJsonValue(QJsonValue::Undefined);
    return jsonRpcError(id, -32601, QStringLiteral("Unknown method: %1").arg(method));
}

} // namespace

QJsonArray toolsForEndpoint(const QString &toolbox)
{
    if (toolbox.isEmpty())
        return homepageTools();
    return toolboxDirectTools(toolbox);
}

QJsonValue handleJsonRpc(const QJsonValue &body, const QString &toolbox, const ToolHandler &handler)
{
    if (body.isArray()) {
        QJsonArray out;
        for (const QJsonValue &item : body.toArray()) {
            if (!item.isObject())
                continue;
            const QJsonValue one = handleOne(item.toObject(), toolbox, handler);
            if (!one.isUndefined())
                out.append(one);
        }
        if (out.isEmpty())
            return QJsonValue(QJsonValue::Undefined);
        return out;
    }
    if (body.isObject())
        return handleOne(body.toObject(), toolbox, handler);
    return jsonRpcError(QJsonValue::Null, -32700, QStringLiteral("Parse error"));
}

} // namespace drift::mcp
