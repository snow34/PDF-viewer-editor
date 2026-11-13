// Plain data types shared between the engine and the UI.
#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace engine {

// One page rendered as ink channels: Cyan, Magenta, Yellow, Black, then one
// channel per spot colour. 0 = no ink, 255 = 100 % ink.
struct SeparatedImage {
    int width = 0;
    int height = 0;
    QStringList inks;
    std::vector<uint8_t> data; // interleaved, inks.size() bytes per pixel
    // CMYK equivalent (0..1) of each spot ink at 100 %, used only for display.
    std::vector<std::array<float, 4>> spotCmyk;

    int channels() const { return int(inks.size()); }
    bool isNull() const { return width <= 0 || height <= 0; }
    const uint8_t *pixel(int x, int y) const
    {
        return data.data() + (size_t(y) * size_t(width) + size_t(x)) * size_t(channels());
    }
};

// Page boxes in points, in display orientation (origin top-left, y down),
// relative to the rendered area (the CropBox).
struct PageBoxes {
    QRectF media, crop, bleed, trim, art;
    bool hasCrop = false, hasBleed = false, hasTrim = false, hasArt = false;
};

struct PageRender {
    int page = -1;
    double zoom = 1.0;
    std::shared_ptr<SeparatedImage> image;
    PageBoxes boxes;
    QString error;
};

struct OutputIntentInfo {
    QString subtype;             // e.g. GTS_PDFX
    QString conditionIdentifier; // e.g. FOGRA39
    QString info;
    QString registry;
    bool hasProfile = false;
    int profileComponents = 0;
};

struct DocumentInfo {
    QString path;
    QString title;
    int pageCount = 0;
    bool isPdf = false;
    QString pdfVersion;
    QString pdfxVersion; // empty when the file does not claim PDF/X
    QList<OutputIntentInfo> outputIntents;
    // ICC profile of the first output intent, when it is a CMYK profile.
    QByteArray outputIntentProfile;
};

} // namespace engine

Q_DECLARE_METATYPE(engine::PageRender)
Q_DECLARE_METATYPE(engine::DocumentInfo)
