#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QtEndian>

// The UI chrome ships *static* Inter instances rather than the variable font: on Windows Qt
// registers application fonts through GDI before converting them to a DirectWrite face, and GDI
// cannot express a variable instance, so the face Qt rasterised with stopped matching the glyph
// indices it had looked up and labels came out with neighbouring glyphs (issues #115, #162).
//
// Static faces bring their own trap. Only Regular/Bold/Italic/Bold Italic may appear in name ID 2,
// so Medium and SemiBold carry their own ID1 family and are reunited under "Inter UI" by name IDs
// 16/17. Get that wrong and the ~90 Font.Medium / Font.DemiBold call sites silently collapse to
// Regular — the font still loads, so nothing else would notice.
class UiFontTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void resolvesEveryWeightUnderOneFamily_data();
    void resolvesEveryWeightUnderOneFamily();
    void facesAreStatic_data();
    void facesAreStatic();

private:
    static QStringList faceFiles();
};

QStringList UiFontTest::faceFiles()
{
    const QString dir = QStringLiteral(DRIFT_TEST_UI_FONT_DIR);
    return {dir + QStringLiteral("/InterUI-Regular.ttf"),  dir + QStringLiteral("/InterUI-Medium.ttf"),
            dir + QStringLiteral("/InterUI-SemiBold.ttf"), dir + QStringLiteral("/InterUI-Bold.ttf"),
            dir + QStringLiteral("/InterUI-Italic.ttf")};
}

void UiFontTest::initTestCase()
{
    for (const QString &file : faceFiles())
        QVERIFY2(QFontDatabase::addApplicationFont(file) >= 0, qPrintable(file));
    QVERIFY(QFontDatabase::families().contains(QStringLiteral("Inter UI")));
}

void UiFontTest::resolvesEveryWeightUnderOneFamily_data()
{
    QTest::addColumn<int>("weight");
    QTest::addColumn<bool>("italic");

    QTest::newRow("regular") << int(QFont::Normal) << false;
    QTest::newRow("medium") << int(QFont::Medium) << false;
    QTest::newRow("semibold") << int(QFont::DemiBold) << false;
    QTest::newRow("bold") << int(QFont::Bold) << false;
    QTest::newRow("italic") << int(QFont::Normal) << true;
}

void UiFontTest::resolvesEveryWeightUnderOneFamily()
{
    QFETCH(int, weight);
    QFETCH(bool, italic);

    QFont font(QStringLiteral("Inter UI"));
    font.setWeight(QFont::Weight(weight));
    font.setItalic(italic);
    font.setPixelSize(13);

    const QFontInfo resolved(font);
    QCOMPARE(resolved.family(), QStringLiteral("Inter UI"));
    QCOMPARE(resolved.weight(), weight);
    QCOMPARE(resolved.italic(), italic);
}

void UiFontTest::facesAreStatic_data()
{
    QTest::addColumn<QString>("file");
    for (const QString &file : faceFiles())
        QTest::newRow(qPrintable(QFileInfo(file).fileName())) << file;
}

// A variable face here is the whole of #115/#162, and it would look perfectly fine on Linux.
void UiFontTest::facesAreStatic()
{
    QFETCH(QString, file);

    QFile ttf(file);
    QVERIFY(ttf.open(QIODevice::ReadOnly));
    const QByteArray header = ttf.read(12);
    QCOMPARE(header.size(), 12);
    const int tables = qFromBigEndian<quint16>(header.constData() + 4);
    const QByteArray directory = ttf.read(tables * 16);
    QCOMPARE(directory.size(), tables * 16);

    for (int i = 0; i < tables; ++i)
        QVERIFY2(directory.mid(i * 16, 4) != QByteArrayLiteral("fvar"), qPrintable(file));
}

QTEST_MAIN(UiFontTest)
#include "tst_uifont.moc"
