#pragma once

#include <QString>
#include <QStringList>

// Which graphics API Qt Quick draws the interface with. OpenGL is the default because only there
// can the preview wrap the compositor's texture directly; any other API gets each preview frame
// as a CPU readback. The compositor itself stays on OpenGL either way.
//
// Ids: "opengl", "d3d11", "d3d12", "vulkan", "metal".
namespace drift::graphics {

// What Qt Quick can use on this platform, OpenGL first. Only OpenGL on Android and the browser.
QStringList availableApis();

QString storedApi();
// Takes effect on the next launch.
void storeApi(const QString &id);

// For main(), after the application and organization names are set and before QApplication.
// Picks the API — --graphics-api=<id> for this run, otherwise the stored choice — and applies it.
//
// A stored non-OpenGL choice is guarded: it is marked pending until markStarted(), and a launch
// that finds the mark still set (the previous one crashed or never drew) goes back to OpenGL.
// Returns whether the guard is armed for this run.
bool applyAtStartup(int argc, char *argv[]);

// The first frame reached the screen; clears the pending mark.
void markStarted();

// Qt Quick reported it could not start on the chosen API. Stores OpenGL and records the failed
// choice for revertedFrom() on the next launch.
void revertAfterFailure();

// The id this launch fell back from, or empty when nothing was reverted.
QString revertedFrom();

} // namespace drift::graphics
