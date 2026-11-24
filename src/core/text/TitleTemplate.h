#pragma once

#include "core/base/Time.h"
#include "core/project/clip/Clip.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>

#include <optional>

// Multi-element title and lower-third templates: JSON recipes (presets/title-templates/*.json,
// user files under AppData) that expand into a handful of text and shape clips laid out on the
// canvas. Boxes are fractions of the canvas, text sizes fractions of the reference height, so a
// template follows the project's size; the clips it makes are ordinary ones and stay editable.

namespace drift {

struct TitleTemplateField
{
    QString key;
    QString label;
    QString text; // the placeholder the clip is made with
};

struct TitleTemplate
{
    QString id;
    QString label;
    QString category; // lower-third, title, chapter, call-to-action, quote, credits
    int order = 100;
    double durationSec = 5.0;
    QJsonArray elements;
    bool builtIn = true;

    // The editable text elements, in stacking order.
    QList<TitleTemplateField> fields() const;
    QJsonObject toJson() const;
    static std::optional<TitleTemplate> fromJson(const QJsonObject &o, QString *error = nullptr);
};

class TitleTemplateCatalog
{
public:
    static TitleTemplateCatalog &instance();

    QList<TitleTemplate> templates() const;
    std::optional<TitleTemplate> templateForId(const QString &id) const;
    // Built-in resources, then $DRIFT_TITLE_TEMPLATES (':'-separated dirs), then the user dir; later
    // roots override earlier ids.
    void reload();
    static QString userDir();

private:
    TitleTemplateCatalog() = default;
    void ensureLoaded() const;
    void loadDir(const QString &dir, bool builtIn) const;

    mutable bool m_loaded = false;
    mutable QList<TitleTemplate> m_templates;
};

// The clips of one insertion, back to front (shapes behind the text drawn over them), all
// starting at startUs and loosely linked so they move together. `texts` replaces the placeholder
// of the fields it names. Ids are fresh; the caller puts each clip on a track of its type.
QList<Clip> instantiateTitleTemplate(const TitleTemplate &tpl, int canvasW, int canvasH, TimeUs startUs,
                                     const QMap<QString, QString> &texts = {});

} // namespace drift
