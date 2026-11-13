// Turns ink channels into a displayable image.
#pragma once

#include "Types.h"

#include <QColor>
#include <QImage>

namespace engine {

enum class DisplayIntent {
    RelativeColorimetric, // with black point compensation
    AbsoluteColorimetric, // simulates paper white
};

struct CompositeOptions {
    std::vector<bool> visible;      // per ink; empty = all visible
    QByteArray simulationProfile;   // CMYK ICC profile describing the press; empty = uncalibrated
    QByteArray monitorProfile;      // RGB ICC profile of the display; empty = sRGB
    DisplayIntent intent = DisplayIntent::RelativeColorimetric;
    bool showCoverage = false;      // highlight pixels above coverageLimit
    double coverageLimit = 300.0;   // total area coverage in percent
    QColor coverageColor = QColor(0, 255, 0);
};

// Returns an RGB image. When exactly one ink is visible it is shown as a
// greyscale plate. `calibrated` reports whether ICC profiles were used.
QImage composite(const SeparatedImage &image, const CompositeOptions &options,
                 bool *calibrated = nullptr);

// Ink percentages averaged over a (size x size) square centred on x, y.
std::vector<double> inkValues(const SeparatedImage &image, int x, int y, int size = 1);

} // namespace engine
