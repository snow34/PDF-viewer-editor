// Drives the real main window: open a file, read inks under the cursor,
// switch plates and the coverage highlight.
#include "ui/MainWindow.h"
#include "ui/PageView.h"

#include <QCheckBox>
#include <QDockWidget>
#include <QLabel>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QtTest>

class TestUi : public QObject {
    Q_OBJECT

    MainWindow *window = nullptr;
    QTreeWidget *inks = nullptr;
    QWidget *canvas = nullptr;

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

    void cleanupTestCase() { delete window; }
};

QTEST_MAIN(TestUi)
#include "test_ui.moc"
