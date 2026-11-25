#pragma once

#include <QJsonObject>

#include <functional>

class AppController;

namespace drift::mcp::projectfolder {

// project_folder_guide: docs/project-folder.md.
QJsonObject guide();
// project_lint: the folder's problems as {file, line, column, severity, message}.
QJsonObject lint(AppController *controller, const QJsonObject &args);
// How template_render's queue and farm targets reach the editor's render queue, which this file cannot
// link against. Takes (folder, data file, output pattern, row, all, target "local" or "farm") and
// returns {error?, jobs:[{row,id,output}], failures:[...]}. Unset in a process without a queue.
using TemplateQueueHook = std::function<QJsonObject(const QString &, const QString &, const QString &, int, bool, const QString &)>;
void setTemplateQueueHook(TemplateQueueHook hook);
// template_render: a headless child renders the template once per selected row.
QJsonObject renderTemplate(const QJsonObject &args);

} // namespace drift::mcp::projectfolder
