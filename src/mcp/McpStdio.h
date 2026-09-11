#pragma once

#include <QByteArray>

#include <cstdio>

namespace drift::mcp {

// MCP stdio framing: one compact JSON message per line. Content-Length headers are
// LSP's convention, not MCP's — the spec (2025-03-26, Transports) says messages are
// newline-delimited, must not contain embedded newlines, and that nothing which is not
// a message may go to stdout.
//
// Reading accepts Content-Length framing as well: Drift emitted it before #135, and a
// client configured against that still has to work.
QByteArray readStdioMessage(std::FILE *in);
void writeStdioMessage(std::FILE *out, const QByteArray &json);

// Attach to a running Drift MCP server over stdio. Returns a process exit code.
// Does not start the GUI. See runHeadless() for the no-editor case.
int runStdioAttach();

} // namespace drift::mcp
