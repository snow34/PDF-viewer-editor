#include "engine/Compositor.h"
#include "engine/Document.h"
#include "engine/ProfileLibrary.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace engine;

class TestEngine : public QObject {
    Q_OBJECT

    std::unique_ptr<Document> doc;
    PageRender render;
    static constexpr double zoom = 2.0;

    // Ink values at a position given in PDF points (origin bottom-left).
    std::vector<double> at(double x, double y) const
    {
        return inkValues(*render.image, int(x * zoom), int((300 - y) * zoom), 3);
    }
    static void compare(const std::vector<double> &got, std::vector<double> want)
    {
        QCOMPARE(got.size(), want.size());
        for (size_t i = 0; i < want.size(); ++i)
            QVERIFY2(qAbs(got[i] - want[i]) <= 1.0,
                     qPrintable(QStringLiteral("ink %1: got %2, want %3").arg(i).arg(got[i]).arg(want[i])));
    }

private slots:
    void initTestCase()
    {
        QString error;
        doc = Document::open(QStringLiteral(TEST_DATA_DIR "/ink_patches.pdf"), &error);
        QVERIFY2(doc, qPrintable(error));
        render = doc->render(0, zoom);
        QVERIFY2(render.image, qPrintable(render.error));
    }

    void documentInfo()
    {
        QCOMPARE(doc->info().pageCount, 1);
        QVERIFY(doc->info().isPdf);
        QVERIFY(doc->info().pdfxVersion.isEmpty());
        QCOMPARE(doc->pageSize(0), QSizeF(300, 300));
    }

    void pdfxDetection()
    {
        QString error;
        auto x1a = Document::open(QStringLiteral(TEST_DATA_DIR "/pdfx1a_marked.pdf"), &error);
        QVERIFY2(x1a, qPrintable(error));
        QCOMPARE(x1a->info().pdfxVersion, QStringLiteral("PDF/X-1a:2001"));
        QCOMPARE(x1a->info().title, QStringLiteral("Marked X-1a"));
        QCOMPARE(x1a->info().outputIntents.size(), 1);
        QCOMPARE(x1a->info().outputIntents.first().subtype, QStringLiteral("GTS_PDFX"));
        QCOMPARE(x1a->info().outputIntents.first().conditionIdentifier, QStringLiteral("FOGRA39"));
        QVERIFY(!x1a->info().outputIntents.first().hasProfile);

        auto x4 = Document::open(QStringLiteral(TEST_DATA_DIR "/pdfx4_marked.pdf"), &error);
        QVERIFY2(x4, qPrintable(error));
        QCOMPARE(x4->info().pdfxVersion, QStringLiteral("PDF/X-4"));
    }

    void openFailure()
    {
        QString error;
        QVERIFY(!Document::open(QStringLiteral(TEST_DATA_DIR "/missing.pdf"), &error));
        QVERIFY(!error.isEmpty());
    }

    void separationsListed()
    {
        const QStringList want{ "Cyan", "Magenta", "Yellow", "Black", "PANTONE 185 C" };
        QCOMPARE(render.image->inks, want);
        QCOMPARE(render.image->width, 600);
        QCOMPARE(render.image->height, 600);
    }

    void inkReadout()
    {
        compare(at(5, 5), { 0, 0, 0, 0, 0 });          // paper
        compare(at(50, 250), { 40, 30, 20, 10, 0 });   // A: process patch
        compare(at(130, 250), { 0, 0, 0, 0, 50 });     // B: spot at 50 %
        compare(at(210, 250), { 100, 100, 100, 20, 0 }); // C: 320 % coverage
    }

    void overprint()
    {
        compare(at(40, 160), { 100, 0, 0, 0, 0 });   // cyan only
        compare(at(120, 80), { 0, 0, 100, 0, 0 });   // yellow only
        compare(at(80, 120), { 100, 0, 100, 0, 0 }); // overprinting overlap keeps cyan
        compare(at(240, 120), { 0, 0, 100, 0, 0 });  // knockout overlap removes cyan
    }

    void pageBoxes()
    {
        QCOMPARE(render.boxes.media, QRectF(0, 0, 300, 300));
        QVERIFY(render.boxes.hasTrim);
        QCOMPARE(render.boxes.trim, QRectF(10, 10, 280, 280));
        QVERIFY(render.boxes.hasBleed);
        QCOMPARE(render.boxes.bleed, QRectF(2, 2, 296, 296));
        QVERIFY(!render.boxes.hasArt);
    }

    void compositeAllInks()
    {
        const QImage img = composite(*render.image, {});
        QCOMPARE(img.size(), QSize(600, 600));
        QCOMPARE(img.pixelColor(10, 590), QColor(255, 255, 255));
        const QColor spot = img.pixelColor(260, 100); // patch B, a light red
        QVERIFY(spot.red() > spot.green() && spot.red() > spot.blue());
    }

    void compositeHidesInks()
    {
        CompositeOptions o;
        o.visible = { true, true, true, true, false }; // spot hidden
        QCOMPARE(composite(*render.image, o).pixelColor(260, 100), QColor(255, 255, 255));

        o.visible = { false, false, false, false, true }; // spot plate alone, as grey
        const QColor plate = composite(*render.image, o).pixelColor(260, 100);
        QCOMPARE(plate.red(), plate.green());
        QVERIFY(qAbs(plate.red() - 128) <= 2);
    }

    void coverageHighlight()
    {
        CompositeOptions o;
        o.showCoverage = true;
        o.coverageLimit = 300;
        const QImage img = composite(*render.image, o);
        QCOMPARE(img.pixelColor(420, 100), o.coverageColor);  // patch C, 320 %
        QVERIFY(img.pixelColor(100, 100) != o.coverageColor); // patch A, 100 %
    }

    void calibratedDisplay()
    {
        const QString cmyk = QStringLiteral("/usr/share/color/icc/ghostscript/default_cmyk.icc");
        if (!QFile::exists(cmyk))
            QSKIP("No CMYK profile available on this machine");
        CompositeOptions o;
        o.simulationProfile = ProfileLibrary::load(cmyk);
        bool calibrated = false;
        const QImage img = composite(*render.image, o, &calibrated);
        QVERIFY(calibrated);
        const QColor cyan = img.pixelColor(80, 280);
        QVERIFY(cyan.blue() > cyan.red());
    }

    void rotation()
    {
        // Patch A (40/30/20/10) is top-left; turned 90 degrees clockwise it is top-right.
        const PageRender turned = doc->render(0, zoom, 90);
        QVERIFY(turned.image);
        QCOMPARE(turned.rotation, 90);
        compare(inkValues(*turned.image, int(250 * zoom), int(50 * zoom), 3), { 40, 30, 20, 10, 0 });
        QCOMPARE(turned.boxes.trim, QRectF(10, 10, 280, 280));
        // Upside down it is bottom-right.
        const PageRender flipped = doc->render(0, zoom, 180);
        compare(inkValues(*flipped.image, int((300 - 50) * zoom), int(250 * zoom), 3), { 40, 30, 20, 10, 0 });
    }

    void search()
    {
        QString error;
        auto objects = Document::open(QStringLiteral(TEST_DATA_DIR "/objects.pdf"), &error);
        QVERIFY2(objects, qPrintable(error));
        const QList<SearchHit> hits = objects->search(0, QStringLiteral("prepress"));
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().page, 0);
        const QRectF r = hits.first().rects.first();
        // "Prepress" starts after "Hello " on the baseline y = 300 - 250 = 50.
        QVERIFY2(r.left() > 55 && r.right() < 120 && r.top() < 50 && r.bottom() > 44,
                 qPrintable(QStringLiteral("%1 %2 %3 %4").arg(r.left()).arg(r.top()).arg(r.right()).arg(r.bottom())));
        QVERIFY(objects->search(0, QStringLiteral("absent")).isEmpty());
        // Turned 90 degrees the hit becomes tall and narrow.
        const QRectF turned = objects->search(0, QStringLiteral("prepress"), 90).first().rects.first();
        QVERIFY(turned.height() > turned.width());
    }

    void objectInspector()
    {
        QString error;
        auto objects = Document::open(QStringLiteral(TEST_DATA_DIR "/objects.pdf"), &error);
        QVERIFY2(objects, qPrintable(error));
        auto at = [&](double x, double y) { return objects->objectsAt(0, QPointF(x, 300 - y)); };

        QVERIFY(at(290, 290).isEmpty()); // empty paper

        const auto text = at(40, 253);
        QCOMPARE(text.size(), 1);
        QCOMPARE(text.first().kind, QStringLiteral("Text"));
        QVERIFY(text.first().font.contains(QStringLiteral("Helvetica"), Qt::CaseInsensitive)
                || text.first().font.contains(QStringLiteral("Nimbus"), Qt::CaseInsensitive));
        QCOMPARE(qRound(text.first().fontSize), 12);
        QCOMPARE(text.first().values, QStringList({ "Cyan 0 %", "Magenta 0 %", "Yellow 0 %", "Black 100 %" }));

        const auto image = at(60, 150);
        QCOMPARE(image.size(), 1);
        QCOMPARE(image.first().kind, QStringLiteral("Image"));
        QCOMPARE(image.first().imageWidth, 2);
        QCOMPARE(image.first().imageHeight, 2);
        QCOMPARE(qRound(image.first().ppiX), 2);
        QCOMPARE(image.first().bitsPerComponent, 8);
        QCOMPARE(image.first().bounds, QRectF(30, 300 - 192, 72, 72));

        const auto spot = at(190, 160);
        QCOMPARE(spot.size(), 1);
        QCOMPARE(spot.first().colorSpace, QStringLiteral("Separation (PANTONE 185 C)"));
        QCOMPARE(spot.first().values, QStringList({ "PANTONE 185 C 100 %" }));
        QVERIFY(spot.first().overprint);
        QCOMPARE(spot.first().overprintMode, 1);

        const auto stacked = at(180, 50); // RGB square on top of a CMYK square
        QCOMPARE(stacked.size(), 2);
        QCOMPARE(stacked.at(0).values, QStringList({ "Red 255", "Green 0", "Blue 0" }));
        QCOMPARE(stacked.at(1).values, QStringList({ "Cyan 10 %", "Magenta 20 %", "Yellow 30 %", "Black 0 %" }));
        QVERIFY(!stacked.at(0).overprint);
    }

    void profileLibrary()
    {
        const QString src = QStringLiteral("/usr/share/color/icc/ghostscript/default_cmyk.icc");
        if (!QFile::exists(src))
            QSKIP("No CMYK profile available on this machine");
        QTemporaryDir tmp;
        ProfileLibrary lib(tmp.path() + "/profiles");
        QString error;
        const QString imported = lib.import(src, &error);
        QVERIFY2(!imported.isEmpty(), qPrintable(error));
        ProfileInfo info;
        QVERIFY(ProfileLibrary::inspect(imported, &info, &error));
        QVERIFY(info.isCmyk());
        bool found = false;
        for (const ProfileInfo &p : lib.list())
            found = found || (p.inAppLibrary && p.path == imported);
        QVERIFY(found);
        QVERIFY(!lib.remove(src, &error)); // system profiles are never deleted
        QVERIFY(lib.remove(imported, &error));
        QVERIFY(lib.import(QStringLiteral(TEST_DATA_DIR "/ink_patches.pdf"), &error).isEmpty());
    }
};

QTEST_MAIN(TestEngine)
#include "test_engine.moc"
