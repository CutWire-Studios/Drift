#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "app/editor/SearchIndex.h"

namespace {

const char kSmallIndex[] = R"({
  "entries": [
    {"id": "op", "target": "panel:properties.transform#transform-opacity", "context": "TransformInspector",
     "source": "Opacity", "keywords": "transparency alpha fade", "needs": ["transformable"]},
    {"id": "fx", "target": "panel:assets.effects", "context": "AssetsPanel",
     "source": "Effects library", "keywords": "", "needs": []}
  ],
  "settingsPages": [
    {"id": "audio", "context": "SettingsPane", "source": "Audio", "keywords": "sound speakers"}
  ],
  "propertiesTabs": [
    {"id": "transform", "context": "PropertiesPanel", "source": "Transform", "needs": ["transformable"]}
  ]
})";

int rowOf(const SearchIndex &index, const QString &target)
{
    for (int i = 0; i < index.rowCount(); ++i) {
        if (index.data(index.index(i, 0), SearchIndex::TargetRole).toString() == target)
            return i;
    }
    return -1;
}

} // namespace

class SearchIndexTest : public QObject
{
    Q_OBJECT

private slots:
    void scoreOrdering();
    void multiWordMatchesAcrossLabelAndSub();
    void jsonRowsAndTargets();
    void enabledFollowsSelection();
    void activateNavigatesOnlyWhenEnabled();
    void realIndexParses();
};

void SearchIndexTest::scoreOrdering()
{
    const int exact = SearchIndex::score(QStringLiteral("zoom"), QStringLiteral("zoom"));
    const int prefix = SearchIndex::score(QStringLiteral("zoom in"), QStringLiteral("zoom"));
    const int wordStart = SearchIndex::score(QStringLiteral("fade zoom"), QStringLiteral("zoom"));
    const int substring = SearchIndex::score(QStringLiteral("xzoom"), QStringLiteral("zoom"));
    const int fuzzy = SearchIndex::score(QStringLiteral("zxbxo"), QStringLiteral("zo"));
    const int none = SearchIndex::score(QStringLiteral("abc"), QStringLiteral("zq"));

    QCOMPARE(exact, 1000);
    QCOMPARE(prefix, 800 - 7);
    QCOMPARE(wordStart, 600 - 5);
    QCOMPARE(substring, 400 - 1);
    QCOMPARE(fuzzy, 200 - 3 * 5);
    QCOMPARE(none, -1);
    QVERIFY(exact > prefix);
    QVERIFY(prefix > wordStart);
    QVERIFY(wordStart > substring);
    QVERIFY(substring > fuzzy);
    QVERIFY(fuzzy > none);
}

void SearchIndexTest::multiWordMatchesAcrossLabelAndSub()
{
    SearchIndex index;
    index.loadStaticJson(kSmallIndex);
    index.rebuild();
    index.setQuery(QStringLiteral("opacity transform"));
    QVERIFY(rowOf(index, QStringLiteral("panel:properties.transform#transform-opacity")) >= 0);
}

void SearchIndexTest::jsonRowsAndTargets()
{
    SearchIndex index;
    index.loadStaticJson(kSmallIndex);
    index.rebuild();

    index.setQuery(QStringLiteral("opacity"));
    const int opacity = rowOf(index, QStringLiteral("panel:properties.transform#transform-opacity"));
    QVERIFY(opacity >= 0);
    QCOMPARE(index.data(index.index(opacity, 0), SearchIndex::LabelRole).toString(), QStringLiteral("Opacity"));
    QCOMPARE(index.data(index.index(opacity, 0), SearchIndex::KindRole).toString(), QStringLiteral("control"));
    QCOMPARE(index.data(index.index(opacity, 0), SearchIndex::SubRole).toString(),
             QStringLiteral("properties.transform › Opacity"));

    index.setQuery(QStringLiteral("audio"));
    const int audio = rowOf(index, QStringLiteral("settings:audio"));
    QVERIFY(audio >= 0);
    QCOMPARE(index.data(index.index(audio, 0), SearchIndex::SubRole).toString(), QStringLiteral("Settings"));
    QCOMPARE(index.data(index.index(audio, 0), SearchIndex::KindRole).toString(), QStringLiteral("control"));

    index.setQuery(QString());
    QCOMPARE(index.rowCount(), 0);
}

void SearchIndexTest::enabledFollowsSelection()
{
    QObject editor;
    editor.setProperty("selectedTrack", -1);
    editor.setProperty("selectedClip", -1);
    editor.setProperty("selectedClipData", QVariantMap());

    SearchIndex index;
    index.setSources(&editor, nullptr, nullptr, nullptr);
    index.loadStaticJson(kSmallIndex);
    index.rebuild();
    index.setQuery(QStringLiteral("opacity"));

    const QString target = QStringLiteral("panel:properties.transform#transform-opacity");
    int row = rowOf(index, target);
    QVERIFY(row >= 0);
    QVERIFY(!index.data(index.index(row, 0), SearchIndex::EnabledRole).toBool());
    QCOMPARE(index.data(index.index(row, 0), SearchIndex::HintRole).toString(),
             QStringLiteral("Select a clip to edit its Transform"));

    editor.setProperty("selectedClip", 0);
    editor.setProperty("selectedClipData", QVariantMap{{QStringLiteral("caps"), QVariantList{QStringLiteral("transformable")}}});
    index.rebuild();
    index.setQuery(QStringLiteral("opacity"));
    row = rowOf(index, target);
    QVERIFY(row >= 0);
    QVERIFY(index.data(index.index(row, 0), SearchIndex::EnabledRole).toBool());
    QVERIFY(index.data(index.index(row, 0), SearchIndex::HintRole).toString().isEmpty());
}

void SearchIndexTest::activateNavigatesOnlyWhenEnabled()
{
    QObject editor;
    editor.setProperty("selectedTrack", -1);
    editor.setProperty("selectedClip", -1);
    editor.setProperty("selectedClipData", QVariantMap());

    SearchIndex index;
    index.setSources(&editor, nullptr, nullptr, nullptr);
    index.loadStaticJson(kSmallIndex);
    index.rebuild();
    QSignalSpy navigated(&index, &SearchIndex::navigateRequested);

    index.setQuery(QStringLiteral("opacity"));
    const int disabled = rowOf(index, QStringLiteral("panel:properties.transform#transform-opacity"));
    QVERIFY(disabled >= 0);
    QVERIFY(!index.activate(disabled));
    QCOMPARE(navigated.count(), 0);

    index.setQuery(QStringLiteral("effects library"));
    const int enabled = rowOf(index, QStringLiteral("panel:assets.effects"));
    QVERIFY(enabled >= 0);
    QVERIFY(index.activate(enabled));
    QCOMPARE(navigated.count(), 1);
    QCOMPARE(navigated.at(0).at(0).toString(), QStringLiteral("panel:assets.effects"));

    QVERIFY(!index.activate(-1));
    QVERIFY(!index.activate(index.rowCount()));
}

void SearchIndexTest::realIndexParses()
{
    QFile file(QStringLiteral(DRIFT_SOURCE_DIR "/resources/search-index.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    QVERIFY(root.value(QLatin1String("entries")).toArray().size() > 100);

    SearchIndex index;
    index.loadStaticJson(bytes);
    index.rebuild();
    index.setQuery(QStringLiteral("settings"));
    QVERIFY(index.rowCount() > 0);
}

QTEST_MAIN(SearchIndexTest)
#include "tst_searchindex.moc"
