// Timing of the steps behind every screen update. Not a test: run it by hand.
//   ./build/bench [cmyk-profile.icc]
#include "engine/Compositor.h"
#include "engine/Document.h"
#include "engine/ProfileLibrary.h"

#include <QElapsedTimer>
#include <QTextStream>

using namespace engine;

int main(int argc, char **argv)
{
    QTextStream out(stdout);
    QString error;
    auto doc = Document::open(QStringLiteral(TEST_DATA_DIR "/ink_patches.pdf"), &error);
    if (!doc) {
        out << "cannot open test file: " << error << "\n";
        return 1;
    }
    CompositeOptions options;
    if (argc > 1)
        options.simulationProfile = ProfileLibrary::load(QString::fromLocal8Bit(argv[1]));
    out << "profile: " << (options.simulationProfile.isEmpty() ? "none" : argv[1]) << "\n";

    for (double megapixels : { 2.0, 8.0, 30.0, 60.0 }) {
        const double zoom = std::sqrt(megapixels * 1e6 / (300.0 * 300.0));
        QElapsedTimer t;
        t.start();
        const PageRender render = doc->render(0, zoom);
        const qint64 renderMs = t.restart();
        composite(*render.image, options);
        const qint64 firstMs = t.restart();
        composite(*render.image, options);
        const qint64 secondMs = t.restart();
        out << QStringLiteral("%1 MP: render %2 ms, composite %3 ms, composite again %4 ms\n")
                   .arg(megapixels, 4)
                   .arg(renderMs, 5)
                   .arg(firstMs, 5)
                   .arg(secondMs, 5);
        out.flush();
    }

    // What the viewer does now: only the tiles that fill one 1920 x 1080 window.
    out << "\ntiles for one 1920 x 1080 window (512 px tiles):\n";
    for (double zoom : { 2.0, 10.0, 50.0 }) {
        QElapsedTimer t;
        t.start();
        int tiles = 0;
        for (int y = 0; y < 1080; y += 512)
            for (int x = 0; x < 1920; x += 512) {
                const PageRender tile = doc->renderTile(0, zoom, 0, QRect(x, y, 512, 512));
                if (tile.image)
                    composite(*tile.image, options);
                ++tiles;
            }
        out << QStringLiteral("  zoom %1x (page %2 px wide): %3 tiles, render + composite %4 ms\n")
                   .arg(zoom)
                   .arg(int(300 * zoom))
                   .arg(tiles)
                   .arg(t.elapsed());
        out.flush();
    }
    return 0;
}
