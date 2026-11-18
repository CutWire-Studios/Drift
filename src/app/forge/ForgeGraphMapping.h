#pragma once

#include "app/forge/compiler/Compiler.h"
#include "app/forge/doc/ForgeDoc.h"
#include "ui/nodes/NodeGraphController.h"

#include <QMap>
#include <QUndoCommand>
#include <QVariant>

#include <functional>

namespace drift::forge {

QString accentForCategory(const QString &category);
QString socketTypeName(SocketType t);

QList<NodeItem> nodeItems(const ForgeDoc &doc, const QMap<QString, QString> &nodeErrors);
QList<EdgeItem> edgeItems(const ForgeDoc &doc);
QList<NodeGraphController::CatalogEntry> catalogEntries(Kind kind);

// Select, toggle, number, code, labels and curve-like options of a node, as the Node tab edits them:
// {id, label, kind, value, choices[{value, label}], min, max, step, exposed}.
QVariantList nodeOptionList(const ForgeDoc &doc, const QString &nodeId);

// A literal in the shape GlRuntime binds for its socket type: double, QVector2D or QVector4D.
QVariant literalUniform(const QJsonValue &literal, SocketType type);
// The uniforms of a preview: every exposed slider (`values` overrides its default) and every
// unconnected input the preview compile turned into a uniform.
QMap<QString, QVariant> previewParams(const ForgeDoc &doc, const CompileResult &result,
                                      const QMap<QString, QJsonValue> &values);

// One undoable document state. Commands with the same non-empty key merge into one step.
class DocSnapshotCommand : public QUndoCommand
{
public:
    DocSnapshotCommand(ForgeDoc before, ForgeDoc after, const QString &text, const QString &key,
                       std::function<void(const ForgeDoc &)> apply);

    int id() const override { return m_key.isEmpty() ? -1 : 1; }
    bool mergeWith(const QUndoCommand *other) override;
    void undo() override { m_apply(m_before); }
    void redo() override { m_apply(m_after); }

private:
    ForgeDoc m_before;
    ForgeDoc m_after;
    QString m_key;
    std::function<void(const ForgeDoc &)> m_apply;
};

}
