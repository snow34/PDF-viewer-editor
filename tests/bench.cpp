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
    return 0;
}
