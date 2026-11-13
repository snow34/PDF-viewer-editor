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
