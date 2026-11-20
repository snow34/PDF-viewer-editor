// All pages of the document in one scrolling column, drawn from tiles.
//
// The view does no rendering itself. It works out which tiles are on screen
// (wantedTiles), the window has them rendered and composited, and hands the
// finished pictures back with setTile. Until a tile arrives, a stretched tile
// from the previous zoom level, or plain paper, is shown in its place.
//
// Positions on a page are given in view points: 1/72 inch, origin at the
// page's top-left corner, y down, with the view rotation already applied.
#pragma once

#include "TileKey.h"
#include "engine/Types.h"

#include <QAbstractScrollArea>
#include <QImage>
#include <QList>

class PageView : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit PageView(QWidget *parent = nullptr);

    // Page sizes and boxes in view points, view rotation applied. Clears all tiles.
    void setPages(const QList<QSizeF> &sizes, const QList<engine::PageBoxes> &boxes);
    int pageCount() const { return int(m_sizes.size()); }

    // Pixels per point. The point under `anchor` (viewport position; the
    // centre when omitted) stays where it is.
    void setZoom(double zoom, QPoint anchor = QPoint(-1, -1));
    double zoom() const { return m_zoom; }
    // Changes whenever existing tiles stop being valid (new zoom, new pages).
    int generation() const { return m_generation; }

    // The page in the middle of the window.
    int currentPage() const { return m_currentPage; }
    void scrollToPage(int page);
    void ensureVisible(int page, const QRectF &rect); // scrolls only if needed
    void centerOn(int page, QPointF point);
    // Viewport position of a point on a page, and the reverse.
    QPoint widgetPos(int page, QPointF point) const;
    bool pageAt(QPoint pos, int *page, QPointF *point) const;
    QRect pageRect(int page) const; // where the page is drawn, viewport coordinates

    // Tiles needed for what is on screen, nearest the middle first.
    // `ahead` adds a band of tiles around the window so scrolling finds them ready.
    QList<TileKey> wantedTiles(bool ahead) const;
    bool hasTile(const TileKey &key) const { return m_tiles.contains(key); }
    // `area` is the part of the page the image covers, in pixels.
    void setTile(const TileKey &key, int generation, const QImage &image, const QRect &area);
    // True when every tile on screen has arrived.
    bool isComplete() const;
    int tileCount() const { return int(m_tiles.size()); }

    struct BoxVisibility {
        bool trim = true, bleed = true, crop = false, art = true, media = false;
    };
    void setVisibleBoxes(const BoxVisibility &boxes);
    // Search results by page, and the current one.
    void setSearchHits(const QHash<int, QList<QRectF>> &hits, int currentPage, const QList<QRectF> &current);
    // A frame around one object (Object Inspector, preflight). page < 0 removes it.
    void setSelection(int page, const QRectF &bounds);

    static constexpr int margin = 12; // around the column, and between pages

signals:
    void hovered(int page, QPointF point);
    void hoverLeft();
    void clicked(int page, QPointF point);
    void zoomRequested(int steps, QPoint anchor);
    void fileDropped(const QString &path);
    void currentPageChanged(int page);
    // Scrolled, resized or zoomed: the wanted tiles may have changed.
    void viewChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    struct Tile {
        QImage image;
        QRect area;
    };
    struct OldTile {
        int page;
        QRect area;
        double zoom;
        QImage image;
    };

    void relayout();
    QSize pagePixels(int page) const;
    QPoint origin() const; // viewport position of the column's top-left corner
    QRect contentRect(int page) const; // in column coordinates
    QList<TileKey> tilesIn(int page, const QRect &columnArea) const;
    void updateCurrentPage();
    void evict();

    QList<QSizeF> m_sizes;
    QList<engine::PageBoxes> m_boxes;
    QList<qint64> m_tops; // top of each page in column coordinates
    qint64 m_columnHeight = 0;
    int m_columnWidth = 0;
    double m_zoom = 1.0;
    int m_generation = 0;
    int m_currentPage = 0;

    QHash<TileKey, Tile> m_tiles;
    QList<OldTile> m_old;

    BoxVisibility m_show;
    QHash<int, QList<QRectF>> m_hits;
    int m_currentHitPage = -1;
    QList<QRectF> m_currentHit;
    int m_selectionPage = -1;
    QRectF m_selection;

    bool m_panning = false;
    QPoint m_panStart, m_panScrollStart;
};
