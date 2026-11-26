#pragma once

#include <QString>

class EditorState;

namespace drift::projdir {

// `drift lint <folder> [--json] [--all]` and `drift --lsp`. Returns true and sets `exitCode` when argv asked
// for one of them; nothing else is started in that case, so this runs before any application object.
bool handleProjectDirCli(int argc, char *argv[], int *exitCode);

struct RenderRequest
{
    QString folder;
    QString dataFile; // --data
    QString output;   // -o pattern
    int row = 0;      // --row N, 1-based; 0 = not given
    bool all = false; // --all
};

// `--render <folder>` where the folder is a template. False for anything else (a plain project, left to
// the single-project render).
bool parseTemplateRender(int argc, char *argv[], RenderRequest *request);

// Renders the template once per selected row in this process. Returns the exit code.
int runTemplateRender(EditorState &state, const RenderRequest &request);

} // namespace drift::projdir
