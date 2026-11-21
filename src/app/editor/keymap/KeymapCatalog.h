#pragma once

#include <QList>
#include <QString>

namespace drift::keymap {

inline constexpr char kGlobalScope[] = "global";

struct CategoryDef {
    const char *id;
    const char *label;
    const char *scope;
};

struct ActionDef {
    const char *id;
    const char *label;
    const char *category;
};

// Declaration order is display order, for categories and for the actions inside them.
const QList<CategoryDef> &categories();
const QList<ActionDef> &actions();

const ActionDef *findAction(const QString &id);
const CategoryDef *findCategory(const QString &id);

QString scopeOf(const ActionDef &action);
QString translatedLabel(const ActionDef &action);
QString translatedCategoryLabel(const CategoryDef &category);

} // namespace drift::keymap
