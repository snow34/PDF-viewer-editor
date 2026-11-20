// The window on a screen with scale factor 2 (like Windows at 200 % or a
// Retina display): the page must be rendered at the screen's full density.
#include "ui/MainWindow.h"
#include "ui/PageView.h"

#include <QApplication>
#include <QListWidget>
#include <QTreeWidget>
#include <QStandardPaths>
#include <QtTest>

class TestHiDpi : public QObject {
    Q_OBJECT

    MainWindow *window = nullptr;
    PageView *view = nullptr;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        window = new MainWindow;
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        view = window->findChild<PageView *>();
        QCOMPARE(view->deviceScale(), 2.0);
    }

    void pageIsRenderedAtScreenDensity()
    {
        window->openFile(QStringLiteral(TEST_DATA_DIR "/stripes.pdf"));
        QTRY_VERIFY(window->windowTitle().startsWith("stripes.pdf"));
        QTRY_VERIFY(view->isComplete());
        // Leave "fit page" (zooming does), then one logical pixel per point:
        // two device pixels per point, one per stripe.
        for (QAction *a : window->actions())
            if (a->text().remove('&') == QLatin1String("Zoom in"))
                a->trigger();
        QTest::qWait(300); // let any pending fit-to-window pass
        view->setZoom(1.0);
        QCOMPARE(view->renderZoom(), 2.0);
        QTRY_VERIFY(view->isComplete());

        const QImage shot = view->viewport()->grab().toImage(); // device pixels
        QCOMPARE(shot.width(), view->viewport()->width() * 2);
        const QPoint start = view->widgetPos(0, QPointF(100, 150)) * 2; // inside the stripes
        int dark = 0, light = 0, alternations = 0;
        bool previousDark = false;
        for (int x = 0; x < 40; ++x) {
            const int l = shot.pixelColor(start + QPoint(x, 0)).lightness();
            const bool isDark = l < 70;
            dark += isDark;
            light += l > 185;
            if (x > 0 && isDark != previousDark)
                ++alternations;
            previousDark = isDark;
        }
        // Drawn at 1x and stretched, every device pixel would be the same mid grey.
        QVERIFY2(dark >= 15 && light >= 15 && alternations >= 30,
                 qPrintable(QStringLiteral("dark %1, light %2, alternations %3").arg(dark).arg(light).arg(alternations)));
    }

    void inkReadoutUsesDevicePixels()
    {
        auto *inks = window->findChild<QTreeWidget *>(QStringLiteral("inks"));
        // The left half of the first stripe's point is black, the right half paper.
        // QTest::mouseMove does not arrive with a scale factor set in Qt 6.4, so
        // the move is sent straight to the view.
        auto moveTo = [this](QPointF point) {
            const QPointF at = QPointF(view->pageRect(0).topLeft()) + point * view->zoom();
            QMouseEvent move(QEvent::MouseMove, at, view->viewport()->mapToGlobal(at), Qt::NoButton, Qt::NoButton,
                             Qt::NoModifier);
            QCoreApplication::sendEvent(view->viewport(), &move);
        };
        // A sample size of one pixel, so neighbouring stripes do not average in.
        moveTo(QPointF(100.25, 150)); // in the left half of a point: black stripe
        QTRY_COMPARE(inks->topLevelItem(3)->text(1), QStringLiteral("100 %"));
        moveTo(QPointF(100.75, 150)); // right half: paper
        QTRY_COMPARE(inks->topLevelItem(3)->text(1), QStringLiteral("0 %"));
    }

    void thumbnailsAreSharpToo()
    {
        auto *list = window->findChild<QListWidget *>(QStringLiteral("thumbnails"));
        QTRY_VERIFY(list->count() == 1);
        QTRY_VERIFY(!list->item(0)->icon().isNull());
        const QPixmap icon = list->item(0)->icon().pixmap(QSize(120, 120), 2.0);
        QCOMPARE(icon.devicePixelRatio(), 2.0);
        QCOMPARE(icon.size(), QSize(240, 240));
    }

    void cleanupTestCase() { delete window; }
};

int main(int argc, char **argv)
{
    qputenv("QT_SCALE_FACTOR", "2");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PrepressPdfHiDpiTest"));
    QCoreApplication::setApplicationName(QStringLiteral("PrepressPdfHiDpiTest"));
    TestHiDpi test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_hidpi.moc"
