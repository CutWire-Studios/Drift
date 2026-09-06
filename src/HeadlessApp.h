#pragma once

namespace drift {

// Runs the editor with no window, serving MCP over stdio and/or HTTP so automation can
// drive it on a server. Returns a process exit code; never falls through to the GUI.
int runHeadless(int argc, char *argv[]);

} // namespace drift
