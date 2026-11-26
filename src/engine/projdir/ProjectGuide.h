#pragma once

#include <QString>

namespace drift::projdir {

// docs/project-folder.md as compiled into the build; what AGENTS.md and the MCP tool
// project_folder_guide hand out. Empty when the resource is not linked in.
QString projectFolderGuide();

} // namespace drift::projdir
