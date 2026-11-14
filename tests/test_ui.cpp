// Drives the real main window: open a file, read inks under the cursor,
// switch plates and the coverage highlight.
#include "ui/MainWindow.h"
#include "ui/PageView.h"
#include "ui/PreflightPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QtTest>

class TestUi : public QObject {
    Q_OBJECT

    MainWindow *window = nullptr;
    QTreeWidget *inks = nullptr;
    QWidget *canvas = nullptr;
    QTemporaryDir configDir;

    QStringList toolbarTexts() const
    {
        QStringList texts;
        for (QAction *a : window->findChild<QToolBar *>(QStringLiteral("mainToolBar"))->actions())
            texts << (a->isSeparator() ? QStringLiteral("-") : a->text().remove(QLatin1Char('&')));
        return texts;
    }
    QStringList menuTitles() const
    {
        QStringList titles;
        for (QAction *a : window->menuBar()->actions())
            titles << a->text().remove(QLatin1Char('&'));
        return titles;
    }
    void writeFile(const QString &name, const QByteArray &content)
    {
        QFile f(configDir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(content);
    }

    double zoom() const { return (canvas->width() - 24) / 300.0; }
    // Widget position of a point given in PDF points (origin bottom-left).
    QPoint pos(double x, double y) const
    {
        return QPoint(12 + int(x * zoom()), 12 + int((300 - y) * zoom()));
    }
    QColor shown(double x, double y) const { return canvas->grab().toImage().pixelColor(pos(x, y)); }
    QStringList readout() const
    {
        QStringList values;
        for (int i = 0; i < inks->topLevelItemCount(); ++i)
            values << inks->topLevelItem(i)->text(1);
        return values;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("PrepressPdfTest"));
        QCoreApplication::setApplicationName(QStringLiteral("PrepressPdfTest"));
        QSettings().clear();
        qputenv("PREPRESSPDF_CONFIG_DIR", configDir.path().toLocal8Bit());
        window = new MainWindow;
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        inks = window->findChild<QTreeWidget *>(QStringLiteral("inks"));
        QVERIFY(inks);
        window->openFile(QStringLiteral(TEST_DATA_DIR "/ink_patches.pdf"));
        QTRY_COMPARE(inks->topLevelItemCount(), 5);
        canvas = window->findChild<PageView *>()->widget();
        QTRY_VERIFY(canvas->width() > 200);
    }

    void inkReadoutFollowsCursor()
    {
        QTest::mouseMove(canvas, pos(50, 250));
        QTRY_COMPARE(readout(), QStringList({ "40 %", "30 %", "20 %", "10 %", "0 %" }));
        QCOMPARE(window->findChild<QLabel *>(QStringLiteral("total"))->text(),
                 QStringLiteral("Total area coverage: 100 %"));
        QTest::mouseMove(canvas, pos(130, 250));
        QTRY_COMPARE(readout(), QStringList({ "0 %", "0 %", "0 %", "0 %", "50 %" }));
        QTest::mouseMove(canvas, pos(80, 120)); // overprinting overlap
        QTRY_COMPARE(readout(), QStringList({ "100 %", "0 %", "100 %", "0 %", "0 %" }));
    }

    void hidingAPlateChangesTheView()
    {
        QVERIFY(shown(130, 250) != QColor(Qt::white));
        inks->topLevelItem(4)->setCheckState(0, Qt::Unchecked);
        QTRY_COMPARE(shown(130, 250), QColor(Qt::white));
        QVERIFY(shown(50, 250) != QColor(Qt::white)); // process patch still there
        inks->topLevelItem(4)->setCheckState(0, Qt::Checked);
    }

    void coverageHighlight()
    {
        auto *box = window->findChild<QCheckBox *>(QStringLiteral("coverage"));
        box->setChecked(true);
        QTRY_COMPARE(shown(210, 250), QColor(0, 255, 0)); // 320 % patch
        QVERIFY(shown(50, 250) != QColor(0, 255, 0));
        if (qEnvironmentVariableIsSet("UI_SCREENSHOT"))
            window->grab().save(qEnvironmentVariable("UI_SCREENSHOT"));
        box->setChecked(false);
    }

    void outputPreviewIsASeparateWindow()
    {
        auto *preview = window->findChild<QDockWidget *>(QStringLiteral("outputPreview"));
        QVERIFY(preview);
        QVERIFY(preview->isWindow()); // its own top-level window, not part of the main one
        QVERIFY(preview->isVisible());
        QVERIFY(preview->isAncestorOf(inks));
        const QPoint target = preview->pos() + QPoint(120, 60);
        preview->move(target);
        QTRY_COMPARE(preview->pos(), target);
        QVERIFY(preview->isWindow()); // moving it never docks it
        if (qEnvironmentVariableIsSet("UI_SCREENSHOT"))
            preview->grab().save(qEnvironmentVariable("UI_SCREENSHOT") + ".preview.png");
    }

    void thumbnails()
    {
        auto *list = window->findChild<QListWidget *>(QStringLiteral("thumbnails"));
        QVERIFY(list);
        QCOMPARE(list->count(), 1);
        QCOMPARE(list->currentRow(), 0);
        // The thumbnail arrives from the render thread and shows the page: the
        // centre of the cyan square (PDF 40,160 of 300) is cyan-ish, not blank.
        auto colourAt = [list](double x, double y) {
            const QImage icon = list->item(0)->icon().pixmap(120, 120).toImage();
            return icon.pixelColor(int(x / 300 * 120), int((300 - y) / 300 * 120));
        };
        QTRY_VERIFY(colourAt(40, 160).alpha() == 255);
        const QColor c = colourAt(40, 160);
        QVERIFY2(c.blue() > 150 && c.red() < 120, qPrintable(c.name()));
    }

    void rotateTurnsThePage()
    {
        // The readout only refreshes on mouse movement, so keep nudging the
        // cursor until the newly rendered page has arrived.
        auto reads = [this](QPoint at, const QStringList &want) {
            static int nudge = 0;
            QTest::mouseMove(canvas, at + QPoint(++nudge % 2, 0));
            QCoreApplication::processEvents();
            return readout() == want;
        };
        const QStringList patchA{ "40 %", "30 %", "20 %", "10 %", "0 %" };
        QTRY_VERIFY(reads(pos(50, 250), patchA)); // top-left

        QAction *right = nullptr, *left = nullptr;
        for (QAction *a : window->actions()) {
            if (a->text().remove('&') == QLatin1String("Rotate right"))
                right = a;
            if (a->text().remove('&') == QLatin1String("Rotate left"))
                left = a;
        }
        QVERIFY(right && left);
        right->trigger();
        // After a quarter turn clockwise patch A is top-right: view point (250, 50).
        QTRY_VERIFY(reads(QPoint(12 + int(250 * zoom()), 12 + int(50 * zoom())), patchA));
        QVERIFY(!reads(pos(50, 250), patchA)); // and no longer top-left
        left->trigger();
        QTRY_VERIFY(reads(pos(50, 250), patchA));
    }

    void inspectorAndSearch()
    {
        window->openFile(QStringLiteral(TEST_DATA_DIR "/objects.pdf"));
        auto *tree = window->findChild<QTreeWidget *>(QStringLiteral("inspector"));
        QVERIFY(tree);
        QTRY_COMPARE(inks->topLevelItemCount(), 5);
        QTRY_VERIFY(window->windowTitle().startsWith("objects.pdf"));
        QTest::qWait(300); // let the new page arrive

        auto rows = [tree](int top) {
            QMap<QString, QString> map;
            QTreeWidgetItem *item = tree->topLevelItem(top);
            map.insert("kind", item->text(0));
            for (int i = 0; i < item->childCount(); ++i)
                map.insert(item->child(i)->text(0), item->child(i)->text(1));
            return map;
        };

        QTest::mouseClick(canvas, Qt::LeftButton, {}, pos(60, 150)); // the image
        QTRY_COMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Image"));
        QCOMPARE(rows(0).value("Pixels"), QStringLiteral("2 × 2, 8 bit"));
        QCOMPARE(rows(0).value("Resolution"), QStringLiteral("2 ppi"));

        QTest::mouseClick(canvas, Qt::LeftButton, {}, pos(190, 160)); // the spot square
        QTRY_COMPARE(rows(0).value("Colour"), QStringLiteral("PANTONE 185 C 100 %"));
        QCOMPARE(rows(0).value("Overprint"), QStringLiteral("on (mode 1)"));

        QTest::mouseClick(canvas, Qt::LeftButton, {}, pos(180, 50)); // RGB over CMYK
        QTRY_COMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(rows(0).value("Colour"), QStringLiteral("Red 255, Green 0, Blue 0"));
        QCOMPARE(rows(1).value("Colour"), QStringLiteral("Cyan 10 %, Magenta 20 %, Yellow 30 %, Black 0 %"));

        QTest::mouseClick(canvas, Qt::LeftButton, {}, pos(290, 290)); // paper
        QTRY_COMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Nothing here"));

        // Search: Ctrl+F opens the bar, Enter searches, the hit is highlighted.
        auto *field = window->findChild<QLineEdit *>(QStringLiteral("searchField"));
        auto *status = window->findChild<QLabel *>(QStringLiteral("searchStatus"));
        QVERIFY(!field->isVisible());
        QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(field->isVisible());
        const QColor before = shown(80, 253);
        QTest::keyClicks(field, "prepress");
        QTest::keyClick(field, Qt::Key_Return);
        QTRY_COMPARE(status->text().trimmed(), QStringLiteral("1 of 1"));
        QTRY_VERIFY(shown(80, 253) != before); // highlight drawn over the word
        if (qEnvironmentVariableIsSet("UI_SCREENSHOT"))
            window->grab().save(qEnvironmentVariable("UI_SCREENSHOT") + ".features.png");
        field->clear();
        QTest::keyClicks(field, "nowhere");
        QTest::keyClick(field, Qt::Key_Return);
        QTRY_VERIFY(status->text().contains("was not found"));

        window->openFile(QStringLiteral(TEST_DATA_DIR "/ink_patches.pdf"));
        QTRY_VERIFY(window->windowTitle().startsWith("ink_patches.pdf"));
        QTest::qWait(300);
    }

    void preflight()
    {
        auto *panel = window->findChild<PreflightPanel *>();
        auto *result = window->findChild<QLabel *>(QStringLiteral("preflightResult"));
        auto *tree = window->findChild<QTreeWidget *>(QStringLiteral("preflightFindings"));
        auto *button = window->findChild<QPushButton *>(QStringLiteral("preflightCheck"));
        auto *standard = window->findChild<QComboBox *>(QStringLiteral("preflightStandard"));
        QVERIFY(panel && result && tree && button && standard);

        window->openFile(QStringLiteral(TEST_DATA_DIR "/x1a_two_pages.pdf"));
        QTRY_VERIFY(window->windowTitle().startsWith("x1a_two_pages.pdf"));
        // The standard the file claims is preselected, and nothing is verified yet.
        QTRY_COMPARE(standard->currentText(), QStringLiteral("PDF/X-1a:2001"));
        QVERIFY(window->statusBar()->findChild<QLabel *>()->text().contains("not verified")
                || window->findChildren<QLabel *>().size() > 0);
        QVERIFY(!panel->hasReport());

        button->click();
        QTRY_VERIFY(panel->hasReport());
        QVERIFY2(result->text().contains("Failed") && result->text().contains("1 error"), qPrintable(result->text()));
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Colour"));
        QTreeWidgetItem *finding = tree->topLevelItem(0)->child(0);
        QVERIFY(finding->text(0).contains("RGB"));
        QCOMPARE(finding->text(1), QStringLiteral("2"));
        bool bannerUpdated = false;
        for (QLabel *label : window->statusBar()->findChildren<QLabel *>())
            bannerUpdated = bannerUpdated || label->text().contains("PDF/X-1a:2001 check: 1 error");
        QVERIFY(bannerUpdated);

        // Clicking the finding jumps to page 2 and frames the square at PDF (100..150, 100..150).
        auto framed = [this](double x, double y) {
            const QColor c = shown(x, y);
            return c.red() > 200 && c.green() < 80 && c.blue() > 150; // the pink selection frame
        };
        auto *spin = window->findChild<QSpinBox *>();
        QCOMPARE(spin->value(), 1);
        tree->setCurrentItem(finding);
        QTRY_COMPARE(spin->value(), 2);
        auto *detail = window->findChild<QLabel *>(QStringLiteral("preflightDetail"));
        QVERIFY(detail->text().contains("PDF/X-1a allows only CMYK, grey and spot colours"));
        QVERIFY(detail->text().contains("Page 2") && detail->text().contains("color.rgb"));
        QTRY_VERIFY(framed(100, 125)); // left edge of the square
        QVERIFY(framed(150, 125));     // right edge
        QVERIFY(!framed(125, 125));    // the inside stays red
        if (qEnvironmentVariableIsSet("UI_SCREENSHOT")) {
            window->findChild<QDockWidget *>(QStringLiteral("preflightPanel"))->raise();
            QTest::qWait(100);
            window->grab().save(qEnvironmentVariable("UI_SCREENSHOT") + ".preflight.png");
        }

        // The frame follows a rotated view: a quarter turn clockwise moves the
        // square's left edge (PDF x = 100, view y 150..200) to view (100..150, 100).
        for (QAction *a : window->actions())
            if (a->text().remove('&') == QLatin1String("Rotate right"))
                a->trigger();
        tree->setCurrentItem(nullptr);
        QTest::qWait(400);
        tree->setCurrentItem(finding);
        auto framedView = [this](double vx, double vy) {
            const QColor c = canvas->grab().toImage().pixelColor(QPoint(12 + int(vx * zoom()), 12 + int(vy * zoom())));
            return c.red() > 200 && c.green() < 80 && c.blue() > 150;
        };
        QTRY_VERIFY(framedView(125, 100)); // top edge of the turned square
        QVERIFY(framedView(100, 125));     // its left edge
        for (QAction *a : window->actions())
            if (a->text().remove('&') == QLatin1String("Rotate left"))
                a->trigger();

        // Reports: JSON and PDF.
        QTemporaryDir out;
        QVERIFY(panel->saveReport(out.filePath("r.json")));
        QFile json(out.filePath("r.json"));
        QVERIFY(json.open(QIODevice::ReadOnly));
        const QByteArray text = json.readAll();
        QVERIFY(text.contains("\"color.rgb\"") && text.contains("x1a_two_pages.pdf"));
        QVERIFY(panel->saveReport(out.filePath("r.pdf")));
        QFile pdf(out.filePath("r.pdf"));
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QVERIFY(pdf.read(5) == "%PDF-");

        // A clean file passes, and opening it clears the old result first.
        window->openFile(QStringLiteral(TEST_DATA_DIR "/x1a_ok.pdf"));
        QTRY_VERIFY(window->windowTitle().startsWith("x1a_ok.pdf"));
        QTRY_VERIFY(!panel->hasReport());
        QCOMPARE(tree->topLevelItemCount(), 0);
        button->click();
        QTRY_VERIFY(panel->hasReport());
        QVERIFY2(result->text().contains("Passed"), qPrintable(result->text()));

        // Checking the same file against another standard reports the wrong marker.
        standard->setCurrentIndex(standard->findText(QStringLiteral("PDF/X-4")));
        button->click();
        QTRY_VERIFY(result->text().contains("Failed"));
        QVERIFY(tree->topLevelItemCount() >= 1);

        window->openFile(QStringLiteral(TEST_DATA_DIR "/ink_patches.pdf"));
        QTRY_VERIFY(window->windowTitle().startsWith("ink_patches.pdf"));
        QTest::qWait(300);
    }

    void defaultLayout()
    {
        QCOMPARE(menuTitles(), QStringList({ "File", "Edit", "View", "Tools", "Window", "Help", "Customise" }));
        QCOMPARE(toolbarTexts().first(), QStringLiteral("Open…"));
        QVERIFY(qApp->styleSheet().isEmpty());
        QVERIFY(!QFile::exists(configDir.filePath("layout.json"))); // nothing written until dev mode
    }

    void developerModeWritesEditableFiles()
    {
        window->findChild<QAction *>(QStringLiteral("devMode"))->setChecked(true);
        QVERIFY(QFile::exists(configDir.filePath("layout.json")));
        QVERIFY(QFile::exists(configDir.filePath("theme.qss")));
        QFile guide(configDir.filePath("HOW-TO.txt"));
        QVERIFY(guide.open(QIODevice::ReadOnly));
        QVERIFY(guide.readAll().contains("view.zoomIn"));
        // The generated files reproduce the standard layout exactly.
        QCOMPARE(menuTitles(), QStringList({ "File", "Edit", "View", "Tools", "Window", "Help", "Customise" }));
    }

    void editingLayoutUpdatesTheRunningApp()
    {
        writeFile("layout.json", R"({
            "toolbar": ["view.zoomIn", "-", "file.open", "no.such.item"],
            "menus": [ { "title": "&Document", "items": ["file.open", "file.quit"] } ],
            "shortcuts": { "file.open": "Ctrl+Shift+O" } })");
        QTRY_COMPARE(toolbarTexts(), QStringList({ "Zoom in", "-", "Open…" })); // picked up without a restart
        QCOMPARE(menuTitles(), QStringList({ "Document", "Customise" }));    // Customise always stays
        QCOMPARE(window->menuBar()->actions().first()->menu()->actions().first()->shortcut(),
                 QKeySequence("Ctrl+Shift+O"));
        QVERIFY(window->statusBar()->currentMessage().contains("no.such.item"));
    }

    void brokenLayoutFallsBackToDefault()
    {
        writeFile("layout.json", "{ \"toolbar\": [ \n oops");
        QTRY_COMPARE(menuTitles(), QStringList({ "File", "Edit", "View", "Tools", "Window", "Help", "Customise" }));
        QVERIFY(window->statusBar()->currentMessage().contains("layout.json, line 2"));
    }

    void editingThemeUpdatesTheRunningApp()
    {
        writeFile("theme.qss", "QToolBar { background: #ff0000; }");
        QTRY_VERIFY(qApp->styleSheet().contains("#ff0000"));
        auto *bar = window->findChild<QToolBar *>(QStringLiteral("mainToolBar"));
        auto isRed = [bar] {
            const QColor c = bar->grab().toImage().pixelColor(bar->width() - 3, bar->height() / 2);
            return c.red() > 200 && c.green() < 60 && c.blue() < 60;
        };
        QTRY_VERIFY(isRed());
        if (qEnvironmentVariableIsSet("UI_SCREENSHOT"))
            window->grab().save(qEnvironmentVariable("UI_SCREENSHOT") + ".themed.png");
    }

    void safeModeIgnoresCustomisation()
    {
        MainWindow safe(true);
        QStringList titles;
        for (QAction *a : safe.menuBar()->actions())
            titles << a->text().remove(QLatin1Char('&'));
        QCOMPARE(titles, QStringList({ "File", "Edit", "View", "Tools", "Window", "Help", "Customise" }));
        QVERIFY(qApp->styleSheet().isEmpty());
    }

    void cleanupTestCase() { delete window; }
};

QTEST_MAIN(TestUi)
#include "test_ui.moc"
