#include "PageView.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSet>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
const int kMaxTiles = 160;    // finished tiles kept; about 120 MB
const int kMaxOldTiles = 120; // stretched placeholders from earlier zoom levels
} // namespace

PageView::PageView(QWidget *parent) : QAbstractScrollArea(parent)
{
    m_dpr = devicePixelRatioF();
    viewport()->setMouseTracking(true);
    viewport()->setAutoFillBackground(true);
    viewport()->setBackgroundRole(QPalette::Dark);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    verticalScrollBar()->setSingleStep(40);
    horizontalScrollBar()->setSingleStep(40);
    auto changed = [this] {
        updateCurrentPage();
        viewport()->update();
        emit viewChanged();
    };
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, changed);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, changed);
}

// ------------------------------------------------------------------ layout

QSize PageView::pagePixels(int page) const
{
    const QSizeF s = m_sizes.value(page) * m_zoom;
    return QSize(int(std::ceil(s.width() - 0.01)), int(std::ceil(s.height() - 0.01)));
}

void PageView::relayout()
{
    m_tops.clear();
    qint64 y = margin;
    int widest = 0;
    for (int i = 0; i < m_sizes.size(); ++i) {
        const QSize px = pagePixels(i);
        m_tops.append(y);
        y += px.height() + margin;
        widest = qMax(widest, px.width());
    }
    m_columnHeight = m_sizes.isEmpty() ? 0 : y;
    m_columnWidth = m_sizes.isEmpty() ? 0 : widest + 2 * margin;

    const QSize vp = viewport()->size();
    const qint64 maxV = qMax<qint64>(0, m_columnHeight - vp.height());
    verticalScrollBar()->setRange(0, int(qMin<qint64>(maxV, INT_MAX)));
    verticalScrollBar()->setPageStep(vp.height());
    horizontalScrollBar()->setRange(0, qMax(0, m_columnWidth - vp.width()));
    horizontalScrollBar()->setPageStep(vp.width());
}

QPoint PageView::origin() const
{
    const QSize vp = viewport()->size();
    const int x = m_columnWidth < vp.width() ? (vp.width() - m_columnWidth) / 2 : -horizontalScrollBar()->value();
    const int y = m_columnHeight < vp.height() ? int(vp.height() - m_columnHeight) / 2
                                               : -verticalScrollBar()->value();
    return QPoint(x, y);
}

QRect PageView::contentRect(int page) const
{
    const QSize px = pagePixels(page);
    return QRect((m_columnWidth - px.width()) / 2, int(m_tops.value(page)), px.width(), px.height());
}

QRect PageView::pageRect(int page) const
{
    return contentRect(page).translated(origin());
}

void PageView::setPages(const QList<QSizeF> &sizes, const QList<engine::PageBoxes> &boxes)
{
    m_sizes = sizes;
    m_boxes = boxes;
    m_tiles.clear();
    m_old.clear();
    m_hits.clear();
    m_currentHit.clear();
    m_currentHitPage = -1;
    m_selectionPage = -1;
    ++m_generation;
    relayout();
    m_currentPage = -1;
    updateCurrentPage();
    viewport()->update();
    emit viewChanged();
}

void PageView::setZoom(double zoom, QPoint anchor)
{
    if (anchor.x() < 0)
        anchor = viewport()->rect().center();
    int page = -1;
    QPointF point;
    const bool onPage = pageAt(anchor, &page, &point);
    if (!onPage && !m_sizes.isEmpty()) {
        page = qBound(0, m_currentPage, int(m_sizes.size()) - 1);
        point = QPointF(m_sizes[page].width() / 2, 0);
    }

    // The current tiles become stretched placeholders until the new ones arrive.
    if (zoom != m_zoom)
        retireTiles();
    m_zoom = zoom;
    relayout();

    if (page >= 0) {
        // Put the same point back under the anchor.
        const QPoint target = contentRect(page).topLeft() + (point * m_zoom).toPoint();
        horizontalScrollBar()->setValue(target.x() - anchor.x());
        verticalScrollBar()->setValue(target.y() - anchor.y());
    }
    updateCurrentPage();
    viewport()->update();
    emit viewChanged();
}

void PageView::retireTiles()
{
    for (auto it = m_tiles.constBegin(); it != m_tiles.constEnd(); ++it)
        m_old.append({ it.key().page, it->area, renderZoom(), it->image });
    if (m_old.size() > kMaxOldTiles)
        m_old.erase(m_old.begin(), m_old.begin() + (m_old.size() - kMaxOldTiles));
    m_tiles.clear();
    ++m_generation;
}

void PageView::checkDeviceScale()
{
    const double dpr = devicePixelRatioF();
    if (qFuzzyCompare(dpr, m_dpr))
        return;
    retireTiles();
    m_dpr = dpr;
    viewport()->update();
    emit viewChanged(); // ask for tiles at the new density
}

void PageView::changeEvent(QEvent *event)
{
    QAbstractScrollArea::changeEvent(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    if (event->type() == QEvent::DevicePixelRatioChange)
        checkDeviceScale();
#endif
}

void PageView::updateCurrentPage()
{
    if (m_sizes.isEmpty())
        return;
    const qint64 middle = qint64(viewport()->height() / 2) - origin().y();
    // The last page whose top is above the middle of the window.
    auto it = std::upper_bound(m_tops.begin(), m_tops.end(), middle);
    const int page = qBound(0, int(it - m_tops.begin()) - 1, int(m_sizes.size()) - 1);
    if (page != m_currentPage) {
        m_currentPage = page;
        emit currentPageChanged(page);
    }
}

void PageView::scrollToPage(int page)
{
    if (page < 0 || page >= m_sizes.size())
        return;
    verticalScrollBar()->setValue(int(m_tops[page]) - margin);
    updateCurrentPage();
}

void PageView::ensureVisible(int page, const QRectF &rect)
{
    if (page < 0 || page >= m_sizes.size())
        return;
    const QRect r = QRectF(pageRect(page).topLeft() + rect.topLeft() * m_zoom, rect.size() * m_zoom).toAlignedRect();
    if (viewport()->rect().contains(r))
        return;
    centerOn(page, rect.center());
}

void PageView::centerOn(int page, QPointF point)
{
    if (page < 0 || page >= m_sizes.size())
        return;
    const QPoint c = contentRect(page).topLeft() + (point * m_zoom).toPoint();
    horizontalScrollBar()->setValue(c.x() - viewport()->width() / 2);
    verticalScrollBar()->setValue(c.y() - viewport()->height() / 2);
    updateCurrentPage();
}

QPoint PageView::widgetPos(int page, QPointF point) const
{
    return pageRect(page).topLeft() + QPoint(int(std::floor(point.x() * m_zoom)), int(std::floor(point.y() * m_zoom)));
}

bool PageView::pageAt(QPointF pos, int *page, QPointF *point) const
{
    const QPointF c = pos - QPointF(origin());
    auto it = std::upper_bound(m_tops.begin(), m_tops.end(), qint64(std::floor(c.y())));
    const int i = int(it - m_tops.begin()) - 1;
    if (i < 0 || i >= m_sizes.size())
        return false;
    const QRectF r = contentRect(i);
    if (!r.contains(c))
        return false;
    if (page)
        *page = i;
    if (point)
        *point = (c - r.topLeft()) / m_zoom;
    return true;
}

// ------------------------------------------------------------------- tiles

QList<TileKey> PageView::tilesIn(int page, const QRect &columnArea) const
{
    QList<TileKey> keys;
    const QRect r = contentRect(page);
    const QRect local = columnArea.intersected(r).translated(-r.topLeft());
    if (local.isEmpty())
        return keys;
    // Tiles are counted in device pixels.
    const int size = TileKey::size;
    const int x0 = int(std::floor(local.left() * m_dpr)), y0 = int(std::floor(local.top() * m_dpr));
    const int x1 = int(std::ceil((local.right() + 1) * m_dpr)) - 1, y1 = int(std::ceil((local.bottom() + 1) * m_dpr)) - 1;
    for (int row = y0 / size; row <= y1 / size; ++row)
        for (int col = x0 / size; col <= x1 / size; ++col)
            keys.append({ page, col, row });
    return keys;
}

QList<TileKey> PageView::wantedTiles(bool ahead) const
{
    QList<TileKey> keys;
    if (m_sizes.isEmpty())
        return keys;
    QRect area(-origin(), viewport()->size());
    if (ahead)
        area.adjust(-TileKey::size / 2, -TileKey::size, TileKey::size / 2, TileKey::size);
    auto first = std::upper_bound(m_tops.begin(), m_tops.end(), qint64(area.top()));
    int page = qMax(0, int(first - m_tops.begin()) - 1);
    for (; page < m_sizes.size() && m_tops[page] <= area.bottom(); ++page)
        keys += tilesIn(page, area);

    // Nearest the middle of the window first.
    const QPoint middle = -origin() + viewport()->rect().center();
    auto distance = [&](const TileKey &k) {
        const QPoint c = contentRect(k.page).topLeft() + (QPointF(k.area().center()) / m_dpr).toPoint();
        return qint64(c.x() - middle.x()) * (c.x() - middle.x()) + qint64(c.y() - middle.y()) * (c.y() - middle.y());
    };
    std::stable_sort(keys.begin(), keys.end(),
                     [&](const TileKey &a, const TileKey &b) { return distance(a) < distance(b); });
    return keys;
}

bool PageView::isComplete() const
{
    const QList<TileKey> keys = wantedTiles(false);
    return std::all_of(keys.begin(), keys.end(), [this](const TileKey &k) { return m_tiles.contains(k); });
}

void PageView::setTile(const TileKey &key, int generation, const QImage &image, const QRect &area)
{
    if (generation != m_generation || key.page < 0 || key.page >= m_sizes.size())
        return;
    m_tiles.insert(key, { image, area });

    // Placeholders for a page can go once its visible part is sharp again.
    const QList<TileKey> onScreen = wantedTiles(false);
    bool pageDone = true;
    for (const TileKey &k : onScreen)
        if (k.page == key.page && !m_tiles.contains(k))
            pageDone = false;
    if (pageDone)
        m_old.erase(std::remove_if(m_old.begin(), m_old.end(), [&](const OldTile &t) { return t.page == key.page; }),
                    m_old.end());

    if (m_tiles.size() > kMaxTiles)
        evict();
    const QRectF logical(QPointF(area.topLeft()) / m_dpr, QSizeF(area.size()) / m_dpr);
    viewport()->update(logical.toAlignedRect().translated(pageRect(key.page).topLeft()).adjusted(-1, -1, 1, 1));
}

void PageView::evict()
{
    const QList<TileKey> keep = wantedTiles(true);
    const QSet<TileKey> wanted(keep.begin(), keep.end());
    for (auto it = m_tiles.begin(); it != m_tiles.end() && m_tiles.size() > kMaxTiles * 3 / 4;) {
        if (wanted.contains(it.key()))
            ++it;
        else
            it = m_tiles.erase(it);
    }
}

// --------------------------------------------------------------- overlays

void PageView::setVisibleBoxes(const BoxVisibility &boxes)
{
    m_show = boxes;
    viewport()->update();
}

void PageView::setSearchHits(const QHash<int, QList<QRectF>> &hits, int currentPage, const QList<QRectF> &current)
{
    m_hits = hits;
    m_currentHitPage = currentPage;
    m_currentHit = current;
    viewport()->update();
}

void PageView::setSelection(int page, const QRectF &bounds)
{
    m_selectionPage = bounds.isValid() ? page : -1;
    m_selection = bounds;
    viewport()->update();
}

// ---------------------------------------------------------------- painting

void PageView::paintEvent(QPaintEvent *event)
{
    checkDeviceScale(); // for Qt versions without a scale-change event
    if (m_sizes.isEmpty())
        return;
    QPainter p(viewport());
    const QRect dirty = event->rect();
    const QPoint o = origin();
    const QRect columnArea = dirty.translated(-o);
    auto first = std::upper_bound(m_tops.begin(), m_tops.end(), qint64(columnArea.top()));
    int page = qMax(0, int(first - m_tops.begin()) - 1);

    for (; page < m_sizes.size() && m_tops[page] <= columnArea.bottom(); ++page) {
        const QRect r = contentRect(page).translated(o);
        if (!r.intersects(dirty))
            continue;
        p.fillRect(r, Qt::white);

        // Stretched tiles from earlier zoom levels, then the sharp ones on top.
        // A placeholder's area divided by the render zoom it was made at is
        // in points; times the current zoom it is in logical pixels.
        for (const OldTile &t : m_old) {
            if (t.page != page)
                continue;
            const double s = m_zoom / t.renderZoom; // device pixels then -> logical pixels now
            p.drawImage(QRectF(QPointF(r.topLeft()) + QPointF(t.area.topLeft()) * s, QSizeF(t.area.size()) * s),
                        t.image, QRectF(QPointF(0, 0), QSizeF(t.image.size())));
        }
        for (const TileKey &k : tilesIn(page, columnArea)) {
            const auto it = m_tiles.constFind(k);
            if (it != m_tiles.constEnd())
                p.drawImage(QRectF(QPointF(r.topLeft()) + QPointF(it->area.topLeft()) / m_dpr,
                                   QSizeF(it->area.size()) / m_dpr),
                            it->image, QRectF(QPointF(0, 0), QSizeF(it->image.size())));
        }
        p.setPen(QColor(120, 120, 120));
        p.drawRect(r.adjusted(-1, -1, 0, 0));

        // Overlays, in view points.
        p.save();
        p.translate(r.topLeft());
        auto scaled = [this](const QRectF &q) { return QRectF(q.topLeft() * m_zoom, q.size() * m_zoom); };
        for (const QRectF &h : m_hits.value(page))
            p.fillRect(scaled(h), QColor(255, 220, 0, 90));
        if (page == m_currentHitPage) {
            for (const QRectF &h : m_currentHit) {
                p.fillRect(scaled(h), QColor(255, 140, 0, 130));
                p.setPen(QPen(QColor(200, 80, 0), 1));
                p.drawRect(scaled(h));
            }
        }
        if (page == m_selectionPage) {
            p.setPen(QPen(QColor(255, 0, 200), 2));
            p.drawRect(scaled(m_selection));
        }
        const engine::PageBoxes &b = m_boxes.value(page);
        auto box = [&](bool wanted, bool present, const QRectF &q, const QColor &c) {
            if (!wanted || !present)
                return;
            p.setPen(QPen(c, 1, Qt::DashLine));
            p.drawRect(scaled(q));
        };
        // A page without a CropBox is cropped to its MediaBox.
        box(m_show.media, true, b.media, QColor(90, 90, 90));
        box(m_show.crop, true, b.hasCrop ? b.crop : b.media, QColor(150, 0, 200));
        box(m_show.bleed, b.hasBleed, b.bleed, QColor(0, 120, 255));
        box(m_show.trim, b.hasTrim, b.trim, QColor(0, 170, 0));
        box(m_show.art, b.hasArt, b.art, QColor(220, 0, 0));
        p.restore();
    }
}

void PageView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    relayout();
    updateCurrentPage();
    emit viewChanged();
}

// ------------------------------------------------------------------- input

bool PageView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::Leave)
        emit hoverLeft();
    return QAbstractScrollArea::viewportEvent(event);
}

void PageView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_panning) {
        // Drag the page with the cursor: the scroll bars move the other way.
        const QPoint delta = e->globalPosition().toPoint() - m_panStart;
        horizontalScrollBar()->setValue(m_panScrollStart.x() - delta.x());
        verticalScrollBar()->setValue(m_panScrollStart.y() - delta.y());
        return;
    }
    int page;
    QPointF point;
    if (pageAt(e->position(), &page, &point))
        emit hovered(page, point);
    else
        emit hoverLeft();
}

void PageView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStart = e->globalPosition().toPoint();
        m_panScrollStart = QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
        viewport()->setCursor(Qt::ClosedHandCursor);
        return;
    }
    int page;
    QPointF point;
    if (e->button() == Qt::LeftButton && pageAt(e->position(), &page, &point))
        emit clicked(page, point);
}

void PageView::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        viewport()->unsetCursor();
    }
}

void PageView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() != 0)
            emit zoomRequested(event->angleDelta().y() > 0 ? 1 : -1, event->position().toPoint());
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

void PageView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void PageView::dropEvent(QDropEvent *event)
{
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty() && urls.first().isLocalFile())
        emit fileDropped(urls.first().toLocalFile());
}
