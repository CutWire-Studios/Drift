#pragma once

#include <QStringList>

namespace drift::forge {

// Helper sources in dependency order, each once.
QStringList resolveHelpers(const QStringList &names);

}
