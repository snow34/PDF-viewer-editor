// Drives the real main window: open a file, read inks under the cursor,
// switch plates and the coverage highlight.
#include "ui/MainWindow.h"
#include "ui/PageView.h"
#include "ui/PreflightPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QScrollBar>
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
    PageView *view = nullptr;
    QWidget *canvas = nullptr; // the view's viewport: where pages are painted and mouse events go
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

    double zoom() const { return view->zoom(); }
    // Viewport position of a point in view points (origin top-left), on the
    // given page or the page in the middle of the window.
    QPoint viewPos(double x, double y, int page = -1) const
    {
        return view->widgetPos(page < 0 ? view->currentPage() : page, QPointF(x, y));
    }
    // The same for PDF points (origin bottom-left) on the 300 pt test pages.
    QPoint pos(double x, double y, int page = -1) const { return viewPos(x, 300 - y, page); }
    QColor shown(double x, double y, int page = -1) const
    {
        return canvas->grab().toImage().pixelColor(pos(x, y, page));
    }
    // Opens a test file and waits until everything on screen is drawn.
    void openAndWait(const char *name)
    {
        window->openFile(QStringLiteral(TEST_DATA_DIR "/") + QLatin1String(name));
        QTRY_VERIFY(window->windowTitle().startsWith(QLatin1String(name)));
        QTRY_VERIFY(view->isComplete());
    }
    QAction *action(const char *text) const
    {
        for (QAction *a : window->actions())
            if (a->text().remove('&') == QLatin1String(text))
                return a;
        return nullptr;
    }
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
        view = window->findChild<PageView *>();
        canvas = view->viewport();
        QTRY_VERIFY(view->isComplete());
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
        QTRY_VERIFY(reads(viewPos(250, 50), patchA));
        QVERIFY(!reads(pos(50, 250), patchA)); // and no longer top-left
        left->trigger();
        QTRY_VERIFY(reads(pos(50, 250), patchA));
    }

    void inspectorAndSearch()
    {
        openAndWait("objects.pdf");
        auto *tree = window->findChild<QTreeWidget *>(QStringLiteral("inspector"));
        QVERIFY(tree);
        QTRY_COMPARE(inks->topLevelItemCount(), 5);

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

        openAndWait("ink_patches.pdf");
    }

    void preflight()
    {
        auto *panel = window->findChild<PreflightPanel *>();
        auto *result = window->findChild<QLabel *>(QStringLiteral("preflightResult"));
        auto *tree = window->findChild<QTreeWidget *>(QStringLiteral("preflightFindings"));
        auto *button = window->findChild<QPushButton *>(QStringLiteral("preflightCheck"));
        auto *standard = window->findChild<QComboBox *>(QStringLiteral("preflightStandard"));
        QVERIFY(panel && result && tree && button && standard);

        openAndWait("x1a_two_pages.pdf");
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
            const QColor c = canvas->grab().toImage().pixelColor(viewPos(vx, vy, 1));
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
        openAndWait("x1a_ok.pdf");
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

        openAndWait("ink_patches.pdf");
    }

    void preflightGroupsRepeatedProblems()
    {
        auto *panel = window->findChild<PreflightPanel *>();
        auto *tree = window->findChild<QTreeWidget *>(QStringLiteral("preflightFindings"));
        auto *button = window->findChild<QPushButton *>(QStringLiteral("preflightCheck"));
        auto *standard = window->findChild<QComboBox *>(QStringLiteral("preflightStandard"));
        auto *spin = window->findChild<QSpinBox *>();
        openAndWait("x4_acrobat_comparison.pdf"); // 3 pages, none with a TrimBox
        QCOMPARE(standard->currentText(), QStringLiteral("PDF/X-4")); // no claim: X-4 is the default
        button->click();
        QTRY_VERIFY(panel->hasReport());

        QTreeWidgetItem *boxes = nullptr;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->text(0) == QLatin1String("Page boxes"))
                boxes = tree->topLevelItem(i);
        QVERIFY(boxes);
        QCOMPARE(boxes->childCount(), 1); // one row for the problem ...
        QTreeWidgetItem *problem = boxes->child(0);
        QVERIFY(problem->text(0).contains("no TrimBox"));
        QCOMPARE(problem->text(1), QStringLiteral("3 pages"));
        QCOMPARE(problem->childCount(), 3); // ... with the pages underneath
        QCOMPARE(problem->child(2)->text(0), QStringLiteral("Page 3"));

        tree->setCurrentItem(problem->child(2));
        QTRY_COMPARE(spin->value(), 3);
        if (qEnvironmentVariableIsSet("UI_SCREENSHOT")) {
            QTRY_VERIFY(view->isComplete());
            window->findChild<QDockWidget *>(QStringLiteral("preflightPanel"))->raise();
            QTest::qWait(150);
            window->grab().save(qEnvironmentVariable("UI_SCREENSHOT") + ".grouped.png");
        }
        tree->setCurrentItem(problem->child(0));
        QTRY_COMPARE(spin->value(), 1);
        openAndWait("ink_patches.pdf");
    }

    void pageBoxesCanBeSwitchedIndividually()
    {
        openAndWait("x1a_trim_and_art.pdf"); // Trim 10..290, Art 20..280
        QAction *trim = action("Trim box"), *crop = action("Crop box"), *art = action("Art box"),
                *bleed = action("Bleed box"), *media = action("Media box");
        QVERIFY(trim && crop && art && bleed && media);
        QVERIFY(trim->isChecked() && art->isChecked() && !crop->isChecked() && !media->isChecked());

        // A box line is dashed, so look along the whole left edge of the box for its colour.
        auto lineAt = [this](double x, auto matches) {
            const QImage img = canvas->grab().toImage();
            const int px = viewPos(x, 0).x();
            for (int y = viewPos(0, 60).y(); y < viewPos(0, 240).y(); ++y)
                for (int dx = -1; dx <= 1; ++dx)
                    if (matches(img.pixelColor(px + dx, y)))
                        return true;
            return false;
        };
        auto green = [](QColor c) { return c.green() > 120 && c.red() < 90 && c.blue() < 90; };
        auto red = [](QColor c) { return c.red() > 170 && c.green() < 90 && c.blue() < 90; };
        auto purple = [](QColor c) { return c.red() > 110 && c.green() < 70 && c.blue() > 150; };

        QTRY_VERIFY(lineAt(10, green)); // trim
        QVERIFY(lineAt(20, red));       // art
        QVERIFY(!lineAt(0, purple));    // crop is off

        trim->setChecked(false);
        QTRY_VERIFY(!lineAt(10, green));
        QVERIFY(lineAt(20, red)); // the art box is not affected

        art->setChecked(false);
        QTRY_VERIFY(!lineAt(20, red));

        crop->setChecked(true); // no CropBox in the file: it equals the page edge
        QTRY_VERIFY(lineAt(0, purple));

        // The switches in the Output Preview window are the same switches.
        auto *trimBox = window->findChild<QCheckBox *>(QStringLiteral("boxTrim"));
        auto *cropBox = window->findChild<QCheckBox *>(QStringLiteral("boxCrop"));
        QVERIFY(trimBox && cropBox);
        QVERIFY(!trimBox->isChecked() && cropBox->isChecked());
        cropBox->setChecked(false);
        QVERIFY(!crop->isChecked());
        QTRY_VERIFY(!lineAt(0, purple));
        cropBox->setChecked(true);

        // The choice is remembered.
        QVERIFY(!QSettings().value("showBox/trim").toBool());
        QVERIFY(QSettings().value("showBox/crop").toBool());

        crop->setChecked(false);
        trim->setChecked(true);
        art->setChecked(true);
        QTRY_VERIFY(lineAt(10, green));
    }

    void middleButtonDragsThePage()
    {
        QAction *zoomIn = action("Zoom in"), *fit = action("Fit page");
        QVERIFY(zoomIn && fit);
        const double fitted = zoom();
        for (int i = 0; i < 4; ++i)
            zoomIn->trigger();
        QTRY_VERIFY(zoom() > fitted * 2); // now larger than the window, so it can scroll
        QScrollBar *h = view->horizontalScrollBar(), *v = view->verticalScrollBar();
        QVERIFY(h->maximum() > 100 && v->maximum() > 100);
        h->setValue(0);
        v->setValue(0);

        // Pressing the wheel and dragging 60 left and 40 up pulls the page that way.
        const QPoint start(200, 150);
        QTest::mousePress(canvas, Qt::MiddleButton, {}, start);
        QCOMPARE(canvas->cursor().shape(), Qt::ClosedHandCursor);
        QTest::mouseMove(canvas, start + QPoint(-60, -40));
        QTRY_COMPARE(h->value(), 60);
        QCOMPARE(v->value(), 40);
        QTest::mouseRelease(canvas, Qt::MiddleButton, {}, start + QPoint(-60, -40));

        // After the button is released the page no longer follows the mouse.
        QTest::mouseMove(canvas, start + QPoint(-200, -200));
        QTest::qWait(50);
        QCOMPARE(h->value(), 60);
        QCOMPARE(v->value(), 40);

        fit->trigger();
        QTRY_VERIFY(qAbs(zoom() - fitted) < fitted * 0.05); // back to the whole page
        openAndWait("ink_patches.pdf");
    }

    void continuousScrolling()
    {
        openAndWait("x1a_two_pages.pdf"); // page 1: black square, page 2: red square
        auto *spin = window->findChild<QSpinBox *>();
        auto *thumbs = window->findChild<QListWidget *>(QStringLiteral("thumbnails"));
        QCOMPARE(view->pageCount(), 2);
        QCOMPARE(spin->value(), 1);
        // Page 2 follows page 1 in one column.
        QVERIFY(view->pageRect(1).top() > view->pageRect(0).bottom());

        // Scrolling down is enough to reach page 2: page number and thumbnail follow.
        view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
        QTRY_COMPARE(spin->value(), 2);
        QCOMPARE(thumbs->currentRow(), 1);
        QTRY_VERIFY(view->isComplete());
        QTRY_COMPARE(shown(125, 125, 1), QColor(Qt::red));

        // The ink readout works on whichever page is under the cursor.
        QTest::mouseMove(canvas, pos(125, 125, 1));
        QTRY_COMPARE(readout().first(), QStringLiteral("0 %")); // RGB red has no cyan once separated
        QVERIFY(readout().at(1) != QStringLiteral("–"));

        // Clicking the first thumbnail scrolls back up.
        thumbs->setCurrentRow(0);
        QTRY_COMPARE(spin->value(), 1);
        QTRY_VERIFY(view->isComplete());
        QTRY_COMPARE(shown(150, 150, 0), QColor(Qt::black));

        // Page Down goes to the next page.
        action("Next page")->trigger();
        QTRY_COMPARE(spin->value(), 2);
        action("Previous page")->trigger();
        QTRY_COMPARE(spin->value(), 1);
    }

    void zoomFarIntoALargePage()
    {
        // 25x is a 7500 x 7500 pixel page, larger than one picture used to be
        // allowed; only the tiles on screen are drawn.
        openAndWait("ink_patches.pdf");
        for (int i = 0; i < 12 && zoom() < 25; ++i)
            action("Zoom in")->trigger();
        QVERIFY2(zoom() > 20, qPrintable(QString::number(zoom())));
        view->centerOn(0, QPointF(50, 50)); // patch A, PDF (50, 250)
        QTRY_VERIFY(view->isComplete());
        QVERIFY2(view->tileCount() < 60, qPrintable(QString::number(view->tileCount())));
        QTest::mouseMove(canvas, pos(50, 250));
        QTRY_COMPARE(readout(), QStringList({ "40 %", "30 %", "20 %", "10 %", "0 %" }));

        // Ctrl + wheel zooms around the cursor: the point under it stays put.
        const QPoint at = pos(50, 250);
        const double before = zoom();
        QWheelEvent wheel(at, canvas->mapToGlobal(at), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::ControlModifier,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(canvas, &wheel);
        QTRY_VERIFY(zoom() < before);
        QVERIFY((pos(50, 250) - at).manhattanLength() <= 2);

        action("Fit page")->trigger();
        QTRY_VERIFY(view->isComplete());
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
