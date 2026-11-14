#include "engine/Document.h"
#include "preflight/Preflight.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace preflight;

class TestPreflight : public QObject {
    Q_OBJECT

    static Report check(const char *file, Standard standard, Options options = {})
    {
        QString error;
        auto doc = engine::Document::open(QStringLiteral(TEST_DATA_DIR "/") + QLatin1String(file), &error);
        if (!doc)
            qFatal("cannot open %s: %s", file, qPrintable(error));
        return run(*doc, standard, options);
    }
    // The distinct rule ids of a report, sorted.
    static QStringList rules(const Report &r)
    {
        QStringList ids;
        for (const Finding &f : r.findings)
            if (!ids.contains(f.ruleId))
                ids << f.ruleId;
        ids.sort();
        return ids;
    }
    static Finding first(const Report &r, const char *rule)
    {
        for (const Finding &f : r.findings)
            if (f.ruleId == QLatin1String(rule))
                return f;
        return {};
    }
    static QString dump(const Report &r)
    {
        QStringList lines;
        for (const Finding &f : r.findings)
            lines << f.ruleId + ": " + f.message;
        return lines.join('\n');
    }

private slots:
    void cleanFilePasses()
    {
        const Report r = check("x1a_ok.pdf", Standard::X1a_2001);
        QVERIFY2(r.findings.isEmpty(), qPrintable(dump(r)));
        QVERIFY(r.passed());
        QCOMPARE(r.pagesChecked, 1);
    }

    void oneProblemEach_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("rgb") << "x1a_rgb.pdf" << QStringList{ "color.rgb" };
        QTest::newRow("no trim") << "x1a_no_trim.pdf" << QStringList{ "box.trimMissing" };
        QTest::newRow("trim and art") << "x1a_trim_and_art.pdf" << QStringList{ "box.trimAndArt" };
        QTest::newRow("trim outside bleed") << "x1a_trim_outside.pdf" << QStringList{ "box.nesting" };
        QTest::newRow("opacity") << "x1a_opacity.pdf" << QStringList{ "transparency.opacity" };
        QTest::newRow("font") << "x1a_font.pdf" << QStringList{ "font.notEmbedded" };
        QTest::newRow("annotation") << "x1a_annot.pdf" << QStringList{ "annot.inside" };
        QTest::newRow("annotation outside") << "x1a_annot_outside.pdf" << QStringList{};
        QTest::newRow("metadata") << "x1a_no_meta.pdf" << QStringList{ "meta.title", "meta.trapped" };
        QTest::newRow("no intent") << "x1a_no_intent.pdf" << QStringList{ "intent.missing" };
        QTest::newRow("javascript") << "x1a_javascript.pdf" << QStringList{ "forbidden.javascript" };
        QTest::newRow("low resolution") << "x1a_lowres.pdf" << QStringList{ "image.lowResolution" };
    }
    void oneProblemEach()
    {
        QFETCH(QString, file);
        QFETCH(QStringList, expected);
        const Report r = check(qPrintable(file), Standard::X1a_2001);
        QVERIFY2(rules(r) == expected, qPrintable(dump(r)));
    }

    void findingsPointAtTheObject()
    {
        // Red square at PDF (100,100) 50 x 50 on a 300 pt page -> view y = 300 - 150.
        Finding f = first(check("x1a_rgb.pdf", Standard::X1a_2001), "color.rgb");
        QCOMPARE(f.page, 0);
        QCOMPARE(f.bounds, QRectF(100, 150, 50, 50));
        QCOMPARE(f.severity, Severity::Error);
        QVERIFY(f.message.contains("RGB"));

        QCOMPARE(first(check("x1a_opacity.pdf", Standard::X1a_2001), "transparency.opacity").bounds,
                 QRectF(100, 150, 50, 50));
        QCOMPARE(first(check("x1a_annot.pdf", Standard::X1a_2001), "annot.inside").bounds,
                 QRectF(100, 180, 20, 20));
        QCOMPARE(first(check("x1a_lowres.pdf", Standard::X1a_2001), "image.lowResolution").bounds,
                 QRectF(30, 300 - 264, 144, 144));

        f = first(check("x1a_font.pdf", Standard::X1a_2001), "font.notEmbedded");
        QVERIFY(f.bounds.isValid()); // located on the text, not just listed
        QVERIFY(f.bounds.left() >= 28 && f.bounds.left() <= 32 && f.bounds.bottom() > 44 && f.bounds.top() < 50);
        QVERIFY(f.message.contains("Helvetica"));
    }

    void severities()
    {
        const Report low = check("x1a_lowres.pdf", Standard::X1a_2001);
        QCOMPARE(low.errors(), 0);
        QCOMPARE(low.warnings(), 1);
        QVERIFY(low.passed()); // warnings do not fail a file

        Options off;
        off.minImagePpi = 0;
        QVERIFY(check("x1a_lowres.pdf", Standard::X1a_2001, off).findings.isEmpty());
    }

    void wrongStandard()
    {
        // An X-1a:2001 file checked as something else is reported as mis-marked.
        QCOMPARE(rules(check("x1a_ok.pdf", Standard::X3_2002)), QStringList{ "id.mismatch" });
        QCOMPARE(rules(check("x1a_ok.pdf", Standard::X1a_2003)), QStringList{ "id.mismatch" });
        // PDF/X-4 is identified in XMP, which this file does not have.
        QCOMPARE(rules(check("x1a_ok.pdf", Standard::X4)),
                 (QStringList{ "id.missing", "intent.profile", "meta.xmp" }));
        // A file with no PDF/X marker at all.
        QVERIFY(rules(check("ink_patches.pdf", Standard::X1a_2001)).contains("id.missing"));
    }

    void pdfx3ColourRules()
    {
        // Device RGB with a CMYK printing condition is an error in X-3 too, under a different rule.
        const Report r = check("x1a_rgb.pdf", Standard::X3_2002);
        QCOMPARE(rules(r), (QStringList{ "color.deviceRgb", "id.mismatch" }));
        QCOMPARE(first(r, "color.deviceRgb").bounds, QRectF(100, 150, 50, 50));
    }

    void pdfx4()
    {
        // Transparency is allowed in X-4; the only problem is the missing profile.
        const Report r = check("x4_no_profile.pdf", Standard::X4);
        QVERIFY2(rules(r) == QStringList{ "intent.profile" }, qPrintable(dump(r)));
        // The same file as X-3: wrong marker, and now the transparency counts.
        const QStringList asX3 = rules(check("x4_no_profile.pdf", Standard::X3_2003));
        QVERIFY(asX3.contains("transparency.opacity"));
        QVERIFY(asX3.contains("id.missing"));
    }

    void manyIdenticalFindingsAreSummarised()
    {
        // ink_patches.pdf is not PDF/X at all; what matters here is that it still yields a tidy report.
        const Report r = check("ink_patches.pdf", Standard::X1a_2001);
        QVERIFY(!r.passed());
        QVERIFY(rules(r).contains("intent.missing"));
        QVERIFY(rules(r).contains("meta.title"));
    }

    void cancel()
    {
        QString error;
        auto doc = engine::Document::open(QStringLiteral(TEST_DATA_DIR "/x1a_rgb.pdf"), &error);
        QVERIFY(doc);
        const Report r = run(*doc, Standard::X1a_2001, {}, [](int, int) { return false; });
        QVERIFY(r.cancelled);
        QVERIFY(!r.passed());
        QCOMPARE(r.pagesChecked, 0);
    }

    void claimedStandard()
    {
        Standard s;
        QVERIFY(standardFromClaim("PDF/X-1a:2001", &s));
        QCOMPARE(s, Standard::X1a_2001);
        QVERIFY(standardFromClaim("PDF/X-1:2001", &s));
        QCOMPARE(s, Standard::X1a_2001);
        QVERIFY(standardFromClaim("PDF/X-3:2003", &s));
        QCOMPARE(s, Standard::X3_2003);
        QVERIFY(standardFromClaim("PDF/X-4", &s));
        QCOMPARE(s, Standard::X4);
        QVERIFY(!standardFromClaim("", &s));
    }

    void exports()
    {
        const Report r = check("x1a_rgb.pdf", Standard::X1a_2001);
        const QJsonObject json = QJsonDocument::fromJson(toJson(r, "x1a_rgb.pdf")).object();
        QCOMPARE(json.value("standard").toString(), QStringLiteral("PDF/X-1a:2001"));
        QCOMPARE(json.value("passed").toBool(), false);
        QCOMPARE(json.value("errors").toInt(), 1);
        QCOMPARE(json.value("findings").toArray().first().toObject().value("rule").toString(),
                 QStringLiteral("color.rgb"));
        QCOMPARE(json.value("findings").toArray().first().toObject().value("page").toInt(), 1);
        const QString html = toHtml(r, "x1a_rgb.pdf");
        QVERIFY(html.contains("Failed: 1 error"));
        QVERIFY(html.contains("color.rgb"));
        QVERIFY(toHtml(check("x1a_ok.pdf", Standard::X1a_2001), "x").contains("Passed: no problems found"));
    }
};

QTEST_MAIN(TestPreflight)
#include "test_preflight.moc"
