#pragma once

#include <QJsonObject>

class AppController;

namespace drift::mcp::projectfolder {

// project_folder_guide: docs/project-folder.md.
QJsonObject guide();
// project_lint: the folder's problems as {file, line, column, severity, message}.
QJsonObject lint(AppController *controller, const QJsonObject &args);
// template_render: a headless child renders the template once per selected row.
QJsonObject renderTemplate(const QJsonObject &args);

} // namespace drift::mcp::projectfolder
