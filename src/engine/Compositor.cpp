#include "Compositor.h"

#include <lcms2.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

namespace engine {

namespace {

// A colour transform takes a noticeable time to build, so the last one is
// kept. It is shared between threads, which lcms allows for transforms
// created without its one-pixel cache.
struct CachedTransform {
    QByteArray simulation, monitor;
    DisplayIntent intent = DisplayIntent::RelativeColorimetric;
    cmsHTRANSFORM handle = nullptr;
    ~CachedTransform()
    {
        if (handle)
            cmsDeleteTransform(handle);
    }
};

std::shared_ptr<CachedTransform> transformFor(const CompositeOptions &o)
{
    static std::mutex mutex;
    static std::shared_ptr<CachedTransform> cached;
    if (o.simulationProfile.isEmpty())
        return nullptr;
    std::lock_guard<std::mutex> lock(mutex);
    if (cached && cached->intent == o.intent && cached->simulation == o.simulationProfile
        && cached->monitor == o.monitorProfile)
        return cached->handle ? cached : nullptr;

    auto entry = std::make_shared<CachedTransform>();
    entry->simulation = o.simulationProfile;
    entry->monitor = o.monitorProfile;
    entry->intent = o.intent;
    cached = entry; // also remembers a profile that cannot be used, so it is not retried every time

    cmsHPROFILE in = cmsOpenProfileFromMem(o.simulationProfile.constData(),
                                           cmsUInt32Number(o.simulationProfile.size()));
    if (!in || cmsGetColorSpace(in) != cmsSigCmykData) {
        if (in)
            cmsCloseProfile(in);
        return nullptr;
    }
    cmsHPROFILE out = nullptr;
    if (!o.monitorProfile.isEmpty()) {
        out = cmsOpenProfileFromMem(o.monitorProfile.constData(),
                                    cmsUInt32Number(o.monitorProfile.size()));
        if (out && cmsGetColorSpace(out) != cmsSigRgbData) {
            cmsCloseProfile(out);
            out = nullptr;
        }
    }
    if (!out)
        out = cmsCreate_sRGBProfile();
    const bool absolute = o.intent == DisplayIntent::AbsoluteColorimetric;
    entry->handle = cmsCreateTransform(
        in, TYPE_CMYK_8, out, TYPE_RGB_8,
        absolute ? INTENT_ABSOLUTE_COLORIMETRIC : INTENT_RELATIVE_COLORIMETRIC,
        cmsFLAGS_NOCACHE | (absolute ? 0 : cmsFLAGS_BLACKPOINTCOMPENSATION));
    cmsCloseProfile(in);
    cmsCloseProfile(out);
    return entry->handle ? entry : nullptr;
}

} // namespace

QImage composite(const SeparatedImage &image, const CompositeOptions &o, bool *calibrated)
{
    if (calibrated)
        *calibrated = false;
    if (image.isNull())
        return {};

    const int n = image.channels();
    const int w = image.width;
    const int h = image.height;
    std::vector<bool> visible = o.visible;
    if (visible.empty() && !o.hiddenInks.isEmpty())
        for (const QString &ink : image.inks)
            visible.push_back(!o.hiddenInks.contains(ink));
    visible.resize(size_t(n), true);
    const int visibleCount = int(std::count(visible.begin(), visible.end(), true));
    int single = -1;
    if (visibleCount == 1)
        single = int(std::find(visible.begin(), visible.end(), true) - visible.begin());

    QImage result(w, h, QImage::Format_RGB888);
    const std::shared_ptr<CachedTransform> cachedTransform = single < 0 ? transformFor(o) : nullptr;
    const cmsHTRANSFORM transform = cachedTransform ? cachedTransform->handle : nullptr;
    if (calibrated)
        *calibrated = transform != nullptr;

    const int limit = int(o.coverageLimit * 255.0 / 100.0 + 0.5);
    const uchar hr = uchar(o.coverageColor.red()), hg = uchar(o.coverageColor.green()),
                hb = uchar(o.coverageColor.blue());
    uchar *const bits = result.bits(); // detaches once, before the threads start
    const qsizetype stride = result.bytesPerLine();

    // With only the four process inks, all shown, the page data is already CMYK.
    const bool plainCmyk = n == 4 && visibleCount == 4;
    std::vector<uint8_t> spot8; // CMYK equivalent of each spot ink, 0..255
    for (const auto &eq : image.spotCmyk)
        for (float v : eq)
            spot8.push_back(uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f));

    auto rows = [&](int from, int to) {
        std::vector<uint8_t> cmyk(plainCmyk ? 0 : size_t(w) * 4);
        std::vector<uint8_t> runIn(transform ? size_t(w) * 4 : 0), runOut(transform ? size_t(w) * 3 : 0);
        std::vector<int> runOf(transform ? size_t(w) : 0);
        for (int y = from; y < to; ++y) {
            const uint8_t *src = image.pixel(0, y);
            uchar *dst = bits + y * stride;

            if (single >= 0) {
                for (int x = 0; x < w; ++x) {
                    const uchar g = uchar(255 - src[x * n + single]);
                    dst[x * 3] = dst[x * 3 + 1] = dst[x * 3 + 2] = g;
                }
            } else {
                // Fold visible spot inks into CMYK using their equivalents
                // (whole-number arithmetic: this loop runs for every pixel).
                const uint8_t *in = src;
                if (!plainCmyk) {
                    for (int x = 0; x < w; ++x) {
                        const uint8_t *p = src + x * n;
                        uint8_t *c = cmyk.data() + size_t(x) * 4;
                        for (int i = 0; i < 4; ++i)
                            c[i] = visible[size_t(i)] ? p[i] : 0;
                        for (int s = 4; s < n; ++s) {
                            if (p[s] == 0 || !visible[size_t(s)])
                                continue;
                            const uint8_t *eq = spot8.data() + size_t(s - 4) * 4;
                            for (int i = 0; i < 4; ++i) {
                                const int ink = eq[i] * p[s] / 255;
                                c[i] = uint8_t(255 - (255 - c[i]) * (255 - ink) / 255);
                            }
                        }
                    }
                    in = cmyk.data();
                }
                if (transform) {
                    // Converting a colour is the expensive step, and neighbouring
                    // pixels often share one. Convert each run of equal pixels once.
                    int runs = 0;
                    for (int x = 0; x < w; ++x) {
                        if (x == 0 || std::memcmp(in + size_t(x) * 4, in + size_t(x - 1) * 4, 4) != 0) {
                            std::memcpy(runIn.data() + size_t(runs) * 4, in + size_t(x) * 4, 4);
                            ++runs;
                        }
                        runOf[size_t(x)] = runs - 1;
                    }
                    if (runs > w / 2) {
                        cmsDoTransform(transform, in, dst, cmsUInt32Number(w)); // photo-like row
                    } else {
                        cmsDoTransform(transform, runIn.data(), runOut.data(), cmsUInt32Number(runs));
                        for (int x = 0; x < w; ++x)
                            std::memcpy(dst + x * 3, runOut.data() + size_t(runOf[size_t(x)]) * 3, 3);
                    }
                } else {
                    for (int x = 0; x < w; ++x) {
                        const uint8_t *q = in + size_t(x) * 4;
                        const int k = 255 - q[3];
                        dst[x * 3] = uchar((255 - q[0]) * k / 255);
                        dst[x * 3 + 1] = uchar((255 - q[1]) * k / 255);
                        dst[x * 3 + 2] = uchar((255 - q[2]) * k / 255);
                    }
                }
            }

            if (o.showCoverage) {
                for (int x = 0; x < w; ++x) {
                    const uint8_t *p = src + x * n;
                    int total = 0;
                    for (int i = 0; i < n; ++i)
                        total += p[i];
                    if (total > limit) {
                        dst[x * 3] = hr;
                        dst[x * 3 + 1] = hg;
                        dst[x * 3 + 2] = hb;
                    }
                }
            }
        }
    };

    // Rows are independent, so large images are split across the processor cores.
    const int threads = qint64(w) * h < 500000
                            ? 1
                            : int(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
    if (threads <= 1) {
        rows(0, h);
    } else {
        std::vector<std::thread> pool;
        for (int i = 0; i < threads; ++i)
            pool.emplace_back(rows, h * i / threads, h * (i + 1) / threads);
        for (std::thread &t : pool)
            t.join();
    }
    return result;
}

std::vector<double> inkValues(const SeparatedImage &image, int x, int y, int size)
{
    const int n = image.channels();
    std::vector<double> sums(size_t(n), 0.0);
    if (image.isNull() || x < 0 || y < 0 || x >= image.width || y >= image.height)
        return {};
    const int r = std::max(0, size / 2);
    int count = 0;
    for (int yy = std::max(0, y - r); yy <= std::min(image.height - 1, y + r); ++yy) {
        for (int xx = std::max(0, x - r); xx <= std::min(image.width - 1, x + r); ++xx) {
            const uint8_t *p = image.pixel(xx, yy);
            for (int i = 0; i < n; ++i)
                sums[size_t(i)] += p[i];
            ++count;
        }
    }
    for (double &v : sums)
        v = v / count * 100.0 / 255.0;
    return sums;
}

} // namespace engine
