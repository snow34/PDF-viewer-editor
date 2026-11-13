#include "Compositor.h"

#include <lcms2.h>

#include <algorithm>

namespace engine {

namespace {

cmsHTRANSFORM makeTransform(const CompositeOptions &o)
{
    if (o.simulationProfile.isEmpty())
        return nullptr;
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
    cmsHTRANSFORM t = cmsCreateTransform(
        in, TYPE_CMYK_8, out, TYPE_RGB_8,
        absolute ? INTENT_ABSOLUTE_COLORIMETRIC : INTENT_RELATIVE_COLORIMETRIC,
        absolute ? 0 : cmsFLAGS_BLACKPOINTCOMPENSATION);
    cmsCloseProfile(in);
    cmsCloseProfile(out);
    return t;
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
    visible.resize(size_t(n), true);
    const int visibleCount = int(std::count(visible.begin(), visible.end(), true));
    int single = -1;
    if (visibleCount == 1)
        single = int(std::find(visible.begin(), visible.end(), true) - visible.begin());

    QImage result(w, h, QImage::Format_RGB888);
    cmsHTRANSFORM transform = single < 0 ? makeTransform(o) : nullptr;
    if (calibrated)
        *calibrated = transform != nullptr;

    const int limit = int(o.coverageLimit * 255.0 / 100.0 + 0.5);
    const uchar hr = uchar(o.coverageColor.red()), hg = uchar(o.coverageColor.green()),
                hb = uchar(o.coverageColor.blue());
    std::vector<uint8_t> cmyk(size_t(w) * 4);

    for (int y = 0; y < h; ++y) {
        const uint8_t *src = image.pixel(0, y);
        uchar *dst = result.scanLine(y);

        if (single >= 0) {
            for (int x = 0; x < w; ++x) {
                const uchar g = uchar(255 - src[x * n + single]);
                dst[x * 3] = dst[x * 3 + 1] = dst[x * 3 + 2] = g;
            }
        } else {
            // Fold visible spot inks into CMYK using their equivalents.
            for (int x = 0; x < w; ++x) {
                const uint8_t *p = src + x * n;
                float c[4];
                for (int i = 0; i < 4; ++i)
                    c[i] = visible[size_t(i)] ? p[i] / 255.0f : 0.0f;
                for (int s = 4; s < n; ++s) {
                    if (!visible[size_t(s)] || p[s] == 0)
                        continue;
                    const float tint = p[s] / 255.0f;
                    const auto &eq = image.spotCmyk[size_t(s - 4)];
                    for (int i = 0; i < 4; ++i)
                        c[i] = 1.0f - (1.0f - c[i]) * (1.0f - eq[size_t(i)] * tint);
                }
                for (int i = 0; i < 4; ++i)
                    cmyk[size_t(x) * 4 + size_t(i)] = uint8_t(c[i] * 255.0f + 0.5f);
            }
            if (transform) {
                cmsDoTransform(transform, cmyk.data(), dst, cmsUInt32Number(w));
            } else {
                for (int x = 0; x < w; ++x) {
                    const uint8_t *q = cmyk.data() + size_t(x) * 4;
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

    if (transform)
        cmsDeleteTransform(transform);
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
