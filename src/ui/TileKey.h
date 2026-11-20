// Identifies one square piece of a page as drawn on screen.
#pragma once

#include <QHash>
#include <QMetaType>
#include <QRect>

struct TileKey {
    int page = -1;
    int col = 0;
    int row = 0;

    static constexpr int size = 512; // pixels

    // The pixels of the page this tile covers (before clipping to the page).
    QRect area() const { return QRect(col * size, row * size, size, size); }
    bool operator==(const TileKey &o) const { return page == o.page && col == o.col && row == o.row; }
};

inline size_t qHash(const TileKey &k, size_t seed = 0)
{
    return qHashMulti(seed, k.page, k.col, k.row);
}

Q_DECLARE_METATYPE(TileKey)
