#include "PageView.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

class PageCanvas : public QWidget {
public:
    PageCanvas(PageView *view) : QWidget(view), m_view(view) { setMouseTracking(true); }

    QImage image;
    engine::PageBoxes boxes;
    double zoom = 1.0;
    bool showBoxes = true;

    static constexpr int margin = 12;

    void refresh()
    {
        setFixedSize(image.isNull() ? QSize(0, 0)
                                    : image.size() + QSize(2 * margin, 2 * margin));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (image.isNull())
            return;
        QPainter p(this);
        p.drawImage(margin, margin, image);
        p.setPen(QColor(120, 120, 120));
        p.drawRect(QRect(margin - 1, margin - 1, image.width() + 1, image.height() + 1));
        if (!showBoxes)
            return;
        p.translate(margin, margin);
        auto draw = [&](bool present, const QRectF &r, const QColor &c) {
            if (!present)
                return;
            p.setPen(QPen(c, 1, Qt::DashLine));
            p.drawRect(QRectF(r.topLeft() * zoom, r.size() * zoom));
        };
        draw(boxes.hasBleed, boxes.bleed, QColor(0, 120, 255));
        draw(boxes.hasTrim, boxes.trim, QColor(0, 170, 0));
        draw(boxes.hasArt, boxes.art, QColor(220, 0, 0));
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const QPoint pos = e->position().toPoint() - QPoint(margin, margin);
        if (!image.isNull() && image.rect().contains(pos))
            emit m_view->hovered(pos.x(), pos.y());
        else
            emit m_view->hoverLeft();
    }

    void leaveEvent(QEvent *) override { emit m_view->hoverLeft(); }

private:
    PageView *m_view;
};

PageView::PageView(QWidget *parent) : QScrollArea(parent), m_canvas(new PageCanvas(this))
{
    setWidget(m_canvas);
    setAlignment(Qt::AlignCenter);
    setBackgroundRole(QPalette::Dark);
    setAcceptDrops(true);
}

void PageView::setPage(const QImage &image, const engine::PageBoxes &boxes, double zoom)
{
    m_canvas->image = image;
    m_canvas->boxes = boxes;
    m_canvas->zoom = zoom;
    m_canvas->refresh();
}

void PageView::setImage(const QImage &image)
{
    m_canvas->image = image;
    m_canvas->refresh();
}

void PageView::setShowBoxes(bool on)
{
    m_canvas->showBoxes = on;
    m_canvas->update();
}

void PageView::clear()
{
    m_canvas->image = QImage();
    m_canvas->refresh();
}

void PageView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() != 0)
            emit zoomRequested(event->angleDelta().y() > 0 ? 1 : -1);
        event->accept();
        return;
    }
    QScrollArea::wheelEvent(event);
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
