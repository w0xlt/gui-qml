// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/components/blockclockdial.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include <QBrush>
#include <QColor>
#include <QPainterPath>
#include <QConicalGradient>
#include <QPen>
#include <QPointF>
#include <QRadialGradient>
#include <QQuickWindow>
#include <QtMath>
#include <QtGlobal>

BlockClockDial::BlockClockDial(QQuickItem *parent)
    : QQuickPaintedItem(parent),
      m_animation_timer{this},
      m_delay_timer{this}
{
    m_animation_timer.setTimerType(Qt::PreciseTimer);
    m_animation_timer.setInterval(16);
    m_delay_timer.setSingleShot(true);
    m_delay_timer.setInterval(m_connecting_animation_delay_ms);
    connect(&m_delay_timer, &QTimer::timeout,
            this, [this] {
                if (presentationActive() && m_animate_dial && !m_is_paused) {
                    m_animation_timer.start();
                }
            });
    connect(&m_animation_timer, &QTimer::timeout,
            this, &BlockClockDial::advanceAnimation);
    connect(this, &QQuickItem::visibleChanged, this, &BlockClockDial::syncAnimationState);
    connect(this, &QQuickItem::windowChanged, this, [this] {
        connectWindowVisibility();
        syncAnimationState();
    });
}

BlockClockDial::~BlockClockDial()
{
    // QQuickItem detaches from its window after derived members are destroyed.
    // Its visibility/window signals must no longer call back into this object.
    disconnect(this, nullptr, this, nullptr);
    disconnect(m_window_visibility_connection);
}

void BlockClockDial::componentComplete()
{
    QQuickPaintedItem::componentComplete();
    m_component_complete = true;
    connectWindowVisibility();
    syncAnimationState();
}

void BlockClockDial::setupConnectingGradient(const QPen & pen)
{
    m_connecting_gradient.setCenter(getBoundsForPen(pen).center());
    m_connecting_gradient.setAngle(m_connecting_start_angle);
    m_connecting_gradient.setColorAt(0, m_confirmation_colors[5]);
    m_connecting_gradient.setColorAt(0.5, m_confirmation_colors[5]);
    m_connecting_gradient.setColorAt(0.6, m_confirmation_colors[4]);
    m_connecting_gradient.setColorAt(0.7, m_confirmation_colors[3]);
    m_connecting_gradient.setColorAt(1, "transparent");
}

void BlockClockDial::setupSyncedGradient(const QRectF& bounds)
{
    if (!m_synced_gradient_needs_update && m_synced_gradient_bounds == bounds) {
        return;
    }

    m_synced_gradient_bounds = bounds;
    m_synced_gradient.setCenter(bounds.center());
    m_synced_gradient.setAngle(90);
    // The dial advances clockwise while conical gradients advance
    // counter-clockwise. Mirror the stops so older time is more confirmed and
    // the color moves toward zero confirmations as the dial advances. Scale
    // the stops to the elapsed arc so its newest end is always red, and clear
    // the old stops when the time span changes.
    const qreal span{m_current_time_fraction};
    m_synced_gradient.setStops({});
    m_synced_gradient.setColorAt(0, m_confirmation_colors[0]);
    m_synced_gradient.setColorAt(1.0 - span, m_confirmation_colors[0]);
    m_synced_gradient.setColorAt(1.0 - 0.80 * span, m_confirmation_colors[1]);
    m_synced_gradient.setColorAt(1.0 - 0.64 * span, m_confirmation_colors[2]);
    m_synced_gradient.setColorAt(1.0 - 0.48 * span, m_confirmation_colors[3]);
    m_synced_gradient.setColorAt(1.0 - 0.32 * span, m_confirmation_colors[4]);
    m_synced_gradient.setColorAt(1.0 - 0.16 * span, m_confirmation_colors[5]);
    m_synced_gradient.setColorAt(1, m_confirmation_colors[5]);
    m_synced_gradient_needs_update = false;
}

void BlockClockDial::invalidateSyncedGradient()
{
    m_synced_gradient_needs_update = true;
}

qreal BlockClockDial::decrementGradientAngle(qreal angle) const
{
    if (angle == -360) {
        return 0;
    } else {
        return angle -= 4;
    }
}

qreal BlockClockDial::getTargetAnimationAngle() const
{
    if (connected() && synced()) {
        return m_current_time_fraction * 360;
    } else if (connected()) {
        return syncProgress() * 360;
    } else {
        return 360;
    }
}

bool BlockClockDial::presentationActive() const
{
    if (!m_component_complete || !m_rendering_active || !isVisible()) return false;
    if (!window()) return true;

    const QWindow::Visibility visibility{window()->visibility()};
    return window()->isVisible() &&
           visibility != QWindow::Hidden &&
           visibility != QWindow::Minimized;
}

void BlockClockDial::requestRepaint()
{
    if (presentationActive()) update();
}

void BlockClockDial::advanceAnimation()
{
    if (!presentationActive() || !m_animate_dial || m_is_paused) {
        m_animation_timer.stop();
        return;
    }

    if (connected()) {
        const qreal target{getTargetAnimationAngle()};
        m_animating_max_angle += (target - m_animating_max_angle) * 0.05;
        if (qAbs(target - m_animating_max_angle) < 1.0) {
            m_animating_max_angle = target;
            m_animation_timer.stop();
        }
    } else {
        m_animating_max_angle = qMin<qreal>(360.0, m_animating_max_angle + 4.0);
        if (m_animating_max_angle >= m_connecting_end_angle * -1) {
            m_connecting_start_angle = decrementGradientAngle(m_connecting_start_angle);
        }
    }

    requestRepaint();
}

void BlockClockDial::syncAnimationState()
{
    const bool active{presentationActive()};
    const bool becoming_active{active && !m_presentation_was_active};
    m_presentation_was_active = active;

    if (!active || !m_animate_dial || m_is_paused) {
        m_animation_timer.stop();
        m_delay_timer.stop();
        if (!m_animate_dial) m_animating_max_angle = getTargetAnimationAngle();
        // Static and paused dials also need their cached image refreshed after
        // backing data changed while presentation was inactive.
        if (becoming_active) requestRepaint();
        return;
    }

    if (becoming_active) {
        // Backing models continued to advance while hidden. Connected clocks
        // resume at the latest value rather than replaying stale animation.
        m_animating_max_angle = connected() ? getTargetAnimationAngle() : 0.0;
        if (!connected()) m_connecting_start_angle = 90.0;
        requestRepaint();
    }

    if (!connected()) {
        if (m_animation_timer.isActive() || m_delay_timer.isActive()) return;
        if (m_connecting_animation_delay_ms == 0) {
            m_animation_timer.start();
        } else {
            m_delay_timer.start(m_connecting_animation_delay_ms);
        }
        return;
    }

    m_delay_timer.stop();
    const qreal target{getTargetAnimationAngle()};
    if (qAbs(target - m_animating_max_angle) < 1.0) {
        m_animating_max_angle = target;
        m_animation_timer.stop();
    } else if (!m_animation_timer.isActive()) {
        m_animation_timer.start();
    }
}

void BlockClockDial::connectWindowVisibility()
{
    disconnect(m_window_visibility_connection);
    if (!window()) return;
    m_window_visibility_connection = connect(window(), &QWindow::visibilityChanged, this, [this] {
        syncAnimationState();
    });
}

void BlockClockDial::setCurrentTimeFraction(qreal fraction)
{
    fraction = qBound<qreal>(0.0, fraction, 1.0);
    if (qFuzzyCompare(m_current_time_fraction + 1.0, fraction + 1.0)) return;
    m_current_time_fraction = fraction;
    invalidateSyncedGradient();
    syncAnimationState();
    requestRepaint();
}

void BlockClockDial::setBlockTimeFractions(QList<qreal> fractions)
{
    for (qreal& fraction : fractions) fraction = qBound<qreal>(0.0, fraction, 1.0);
    std::sort(fractions.begin(), fractions.end());
    if (m_block_time_fractions == fractions) return;
    m_block_time_fractions = std::move(fractions);
    requestRepaint();
}

void BlockClockDial::setSyncProgress(double progress)
{
    progress = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_sync_progress + 1.0, progress + 1.0)) return;
    m_sync_progress = progress;
    syncAnimationState();
    requestRepaint();
}

void BlockClockDial::setConnected(bool connected)
{
    if (m_is_connected != connected) {
        m_is_connected = connected;
        m_animating_max_angle = 0;
        if (!m_is_connected) m_connecting_start_angle = 90.0;
        syncAnimationState();
        requestRepaint();
    }
}

void BlockClockDial::setSynced(bool is_synced)
{
    if (m_is_synced != is_synced) {
        m_is_synced = is_synced;
        m_animating_max_angle = 0;
        syncAnimationState();
        requestRepaint();
    }
}

void BlockClockDial::setPaused(bool paused)
{
    if (m_is_paused != paused) {
        m_is_paused = paused;
        syncAnimationState();
        requestRepaint();
    }
}

void BlockClockDial::setAnimateDial(bool animate_dial)
{
    if (m_animate_dial != animate_dial) {
        m_animate_dial = animate_dial;
        if (m_animate_dial) m_animating_max_angle = 0;
        syncAnimationState();
        requestRepaint();
    }
}

void BlockClockDial::setRenderingActive(bool active)
{
    if (m_rendering_active == active) return;
    m_rendering_active = active;
    Q_EMIT renderingActiveChanged();
    syncAnimationState();
}

void BlockClockDial::setConnectingAnimationDelayMs(int connecting_animation_delay_ms)
{
    connecting_animation_delay_ms = qMax(connecting_animation_delay_ms, 0);
    if (m_connecting_animation_delay_ms != connecting_animation_delay_ms) {
        m_connecting_animation_delay_ms = connecting_animation_delay_ms;
        m_delay_timer.setInterval(m_connecting_animation_delay_ms);
        if (!m_is_connected) {
            m_animation_timer.stop();
            m_delay_timer.stop();
        }
        syncAnimationState();
    }
}

void BlockClockDial::setShowTimeTicks(bool show_time_ticks)
{
    if (m_show_time_ticks != show_time_ticks) {
        m_show_time_ticks = show_time_ticks;
        requestRepaint();
    }
}

void BlockClockDial::setShowBlockSegments(bool show_block_segments)
{
    if (m_show_block_segments != show_block_segments) {
        m_show_block_segments = show_block_segments;
        requestRepaint();
    }
}

void BlockClockDial::setUseGradientArcWhenSynced(bool use_gradient_arc_when_synced)
{
    if (m_use_gradient_arc_when_synced != use_gradient_arc_when_synced) {
        m_use_gradient_arc_when_synced = use_gradient_arc_when_synced;
        requestRepaint();
    }
}

void BlockClockDial::setPenWidth(qreal width)
{
    if (m_pen_width != width) {
        m_pen_width = width;
        invalidateSyncedGradient();
        requestRepaint();
    }
}

void BlockClockDial::setScale(qreal scale)
{
    if (m_scale != scale) {
        m_scale = scale;
        invalidateSyncedGradient();
        requestRepaint();

        Q_EMIT scaleChanged();
    }
}

void BlockClockDial::setBackgroundColor(QColor color)
{
    if (m_background_color == color) return;
    m_background_color = color;
    requestRepaint();
}

void BlockClockDial::setConfirmationColors(QList<QColor> colorList)
{
    if (m_confirmation_colors != colorList) {
        m_confirmation_colors = colorList;
        invalidateSyncedGradient();
        requestRepaint();
    }
}

void BlockClockDial::setTimeTickColor(QColor color)
{
    if (m_time_tick_color == color) return;
    m_time_tick_color = color;
    requestRepaint();
}

void BlockClockDial::setBlockLayer(BlockLayer layer)
{
    if (m_block_layer == layer) return;
    m_block_layer = layer;
    requestRepaint();
}

void BlockClockDial::setRingMargin(qreal margin)
{
    margin = qMax<qreal>(0.0, margin);
    if (qFuzzyCompare(m_ring_margin + 1.0, margin + 1.0)) return;
    m_ring_margin = margin;
    invalidateSyncedGradient();
    requestRepaint();
}

void BlockClockDial::setHeadColor(QColor color)
{
    if (m_head_color == color) return;
    m_head_color = color;
    requestRepaint();
}

void BlockClockDial::setHighlightedBlock(int block)
{
    block = qMax(-1, block);
    if (m_highlighted_block == block) return;
    m_highlighted_block = block;
    requestRepaint();
}

int BlockClockDial::blockAt(qreal x, qreal y) const
{
    if (paused() || !connected() || !synced() || !showBlockSegments()) return -1;

    QPen pen;
    pen.setWidthF(m_pen_width);
    const QRectF bounds{getBoundsForPen(pen)};
    const QPointF offset{QPointF{x, y} - bounds.center()};
    // Wider than the ring, so it is easy to point at with a finger.
    const qreal reach{qMax<qreal>(m_pen_width * 1.5, 10.0)};
    if (qAbs(std::hypot(offset.x(), offset.y()) - bounds.width() / 2.0) > reach) return -1;

    // Clockwise from twelve o'clock.
    qreal fraction{std::atan2(offset.x(), -offset.y()) / (2.0 * M_PI)};
    if (fraction < 0.0) fraction += 1.0;
    if (fraction > m_animating_max_angle / 360.0) return -1;
    for (const BlockSegment& segment : blockSegments(bounds)) {
        if (fraction <= segment.end_fraction) return static_cast<int>(segment.block);
    }
    return -1;
}

QRectF BlockClockDial::getBoundsForPen(const QPen & pen) const
{
    const QRectF bounds = boundingRect().adjusted(m_ring_margin, m_ring_margin, -m_ring_margin, -m_ring_margin);
    const qreal smallest = qMin(bounds.width(), bounds.height());
    QRectF rect = QRectF(
        pen.widthF() / 2.0 + 1,
        pen.widthF() / 2.0 + 1,
        smallest - pen.widthF() - 2,
        smallest - pen.widthF() - 2
    );
    rect.moveCenter(bounds.center());

    // Make sure the arc is aligned to whole pixels.
    if (rect.x() - int(rect.x()) > 0)
        rect.setX(qCeil(rect.x()));
    if (rect.y() - int(rect.y()) > 0)
        rect.setY(qCeil(rect.y()));
    if (rect.width() - int(rect.width()) > 0)
        rect.setWidth(qFloor(rect.width()));
    if (rect.height() - int(rect.height()) > 0)
        rect.setHeight(qFloor(rect.height()));

    return rect;
}

void BlockClockDial::paintBlocks(QPainter * painter)
{
    if (m_confirmation_colors.size() < 6) return;

    QPen pen(m_confirmation_colors.constLast());
    pen.setWidthF(m_pen_width);
    pen.setCapStyle(Qt::RoundCap);
    const QRectF bounds = getBoundsForPen(pen);
    painter->setPen(pen);

    // Rounded caps extend half a stroke beyond each path endpoint. Separate
    // their center lines by one stroke plus a small visible gap so neighboring
    // confirmation segments retain the pill-shaped spacing from the design.
    const QList<BlockSegment> segments{blockSegments(bounds)};
    const qreal boundary_gap_degrees{
        degreesForArcPixels(m_pen_width + blockSegmentGapPixels(), bounds)};
    const qreal half_boundary_gap{boundary_gap_degrees / 2.0};
    const qreal animated_fraction{qBound<qreal>(0.0, m_animating_max_angle / 360.0, 1.0)};
    for (qsizetype i{0}; i < segments.size(); ++i) {
        const BlockSegment& segment{segments.at(i)};
        const bool newest{i == segments.size() - 1};
        if ((m_block_layer == SettledBlocks && newest) || (m_block_layer == NewestBlock && !newest)) {
            if (segment.end_fraction > animated_fraction) break;
            continue;
        }
        painter->setOpacity(m_highlighted_block < 0 || segment.block == m_highlighted_block ? 1.0 : 0.3);

        const qreal painted_end{qMin(segment.end_fraction, animated_fraction)};
        const qreal available_degrees{(painted_end - segment.start_fraction) * 360.0};
        if (available_degrees <= boundary_gap_degrees) {
            if (segment.end_fraction > animated_fraction) break;
            continue;
        }

        const QColor& color{m_confirmation_colors.at(qMin<qsizetype>(5, segment.confirmations))};
        pen.setColor(color);
        painter->setPen(pen);

        const qreal start_angle{90 - 360 * segment.start_fraction - half_boundary_gap};
        const qreal end_angle{90 - 360 * painted_end + half_boundary_gap};

        QPainterPath path;
        path.arcMoveTo(bounds, start_angle);
        path.arcTo(bounds, start_angle, end_angle - start_angle);
        painter->drawPath(path);

        if (m_block_layer == NewestBlock) {
            paintNewestBlockHead(painter, bounds, end_angle, color);
        }
        if (segment.end_fraction > animated_fraction) break;
    }
    painter->setOpacity(1.0);
}

void BlockClockDial::paintNewestBlockHead(QPainter* painter, const QRectF& bounds, qreal angle, const QColor& color)
{
    const qreal radians{qDegreesToRadians(angle)};
    const QPointF head{bounds.center() +
                       QPointF{std::cos(radians) * bounds.width(), -std::sin(radians) * bounds.height()} / 2.0};

    // The glow reaches past the ring, into ringMargin.
    const qreal glow_radius{m_pen_width * 2.6};
    QColor glow_color{color};
    glow_color.setAlphaF(0.55);
    QRadialGradient glow{head, glow_radius};
    glow.setColorAt(0.0, glow_color);
    glow_color.setAlphaF(0.0);
    glow.setColorAt(1.0, glow_color);
    painter->setPen(Qt::NoPen);
    painter->setBrush(glow);
    painter->drawEllipse(head, glow_radius, glow_radius);

    if (m_head_color.isValid()) {
        painter->setBrush(m_head_color);
        painter->drawEllipse(head, m_pen_width * 0.24, m_pen_width * 0.24);
    }
}

void BlockClockDial::paintProgress(QPainter * painter)
{
    if (m_confirmation_colors.size() < 6) return;
    QPen pen(m_confirmation_colors[5]);
    pen.setWidthF(m_pen_width);
    pen.setCapStyle(Qt::RoundCap);
    const QRectF bounds = getBoundsForPen(pen);
    painter->setPen(pen);

    // QPainter::drawArc uses positive values for counter clockwise - the opposite of our API -
    // so we must reverse the angles with * -1. Also, our angle origin is at 12 o'clock, whereas
    // QPainter's is 3 o'clock, hence - 90.
    const qreal startAngle = 90;
    qreal spanAngle;
    if (syncProgress() * 360 > m_animating_max_angle) {
        spanAngle = m_animating_max_angle * -1;
    } else {
        spanAngle = syncProgress() * -360;
    }

    // QPainter::drawArc parameters are 1/16 of a degree
    painter->drawArc(bounds, startAngle * 16, spanAngle * 16);
}

void BlockClockDial::paintCurrentTimeArc(QPainter* painter)
{
    if (m_confirmation_colors.size() < 6) return;

    QPen pen(m_confirmation_colors[5]);
    pen.setWidthF(m_pen_width);
    pen.setCapStyle(Qt::RoundCap);
    const QRectF bounds = getBoundsForPen(pen);
    painter->setPen(pen);

    const qreal start_angle = 90;
    const qreal time_angle = m_current_time_fraction * 360;
    const qreal span_angle = -1 * qMin(time_angle, m_animating_max_angle);
    painter->drawArc(bounds, start_angle * 16, span_angle * 16);
}

void BlockClockDial::paintSyncedGradientArc(QPainter* painter)
{
    if (m_confirmation_colors.size() < 6) {
        paintCurrentTimeArc(painter);
        return;
    }

    QPen pen;
    pen.setWidthF(m_pen_width);
    pen.setCapStyle(Qt::RoundCap);
    const QRectF bounds = getBoundsForPen(pen);
    setupSyncedGradient(bounds);
    pen.setBrush(QBrush(m_synced_gradient));
    painter->setPen(pen);

    const qreal start_angle = 90;
    const qreal time_angle = m_current_time_fraction * 360;
    const qreal span_angle = -1 * qMin(time_angle, m_animating_max_angle);
    painter->drawArc(bounds, start_angle * 16, span_angle * 16);
}

void BlockClockDial::paintConnectingAnimation(QPainter * painter)
{
    if (m_confirmation_colors.size() < 6) return;

    QPen pen;
    pen.setWidthF(m_pen_width);
    setupConnectingGradient(pen);
    pen.setBrush(QBrush(m_connecting_gradient));
    pen.setCapStyle(Qt::RoundCap);
    const QRectF bounds = getBoundsForPen(pen);
    painter->setPen(pen);
    if (m_animating_max_angle < m_connecting_end_angle * -1) {
        painter->drawArc(bounds, m_connecting_start_angle * 16, m_animating_max_angle * -16);
    } else {
        painter->drawArc(bounds, m_connecting_start_angle * 16, m_connecting_end_angle * 16);
    }
}

void BlockClockDial::paintBackground(QPainter * painter)
{
    QPen pen(m_background_color);
    pen.setWidthF(m_pen_width);
    const QRectF bounds = getBoundsForPen(pen);
    painter->setPen(pen);

    painter->drawEllipse(bounds);
}

double BlockClockDial::degreesPerPixel()
{
    double circumference = width() * 3.1415926;
    return 360 / circumference;
}

qreal BlockClockDial::degreesForArcPixels(qreal pixels, const QRectF& bounds) const
{
    const qreal radius{qMin(bounds.width(), bounds.height()) / 2.0};
    if (radius <= 0.0) return 360.0;
    return qRadiansToDegrees(pixels / radius);
}

qreal BlockClockDial::blockSegmentGapPixels() const
{
    // The Figma dial uses a gap around half the stroke width, with one device
    // pixel as the lower bound for small miniatures.
    return qMax<qreal>(1.0, m_pen_width / 2.0);
}

QList<BlockClockDial::BlockSegment> BlockClockDial::blockSegments(const QRectF& bounds) const
{
    const qreal current_fraction{qBound<qreal>(0.0, m_current_time_fraction, 1.0)};
    if (current_fraction <= 0.0) return {};

    // A rounded segment needs room for both half-caps, the visual gap, and at
    // least one pixel of center line.
    const qreal minimum_fraction{
        degreesForArcPixels(m_pen_width + blockSegmentGapPixels() + 1.0, bounds) / 360.0};

    // Walk back from the block being built. Each block's segment ends at its
    // timestamp, or earlier to leave the newer segment room, so blocks found in
    // quick succession, or timestamped ahead of the clock, keep a segment each.
    const qsizetype block_count{m_block_time_fractions.size()};
    QList<BlockSegment> segments{{0.0, current_fraction, 0, -1}};
    for (qsizetype i{block_count - 1}; i >= 0; --i) {
        const qreal end{qMin(m_block_time_fractions.at(i), segments.last().end_fraction - minimum_fraction)};
        // Older blocks have no room after the start of the period.
        if (end < minimum_fraction) break;
        segments.last().start_fraction = end;
        // A block is confirmed by itself and each newer block.
        segments.push_back({0.0, end, block_count - i, i});
    }
    std::reverse(segments.begin(), segments.end());
    return segments;
}

void BlockClockDial::paintTimeTicks(QPainter * painter)
{
    QPen pen(m_time_tick_color);
    pen.setWidthF(m_pen_width);
    // Calculate bound based on width of default pen
    const QRectF bounds = getBoundsForPen(pen);

    QPen time_tick_pen = QPen(m_time_tick_color);
    time_tick_pen.setWidthF(m_pen_width / 2);
    time_tick_pen.setCapStyle(Qt::RoundCap);
    painter->setPen(time_tick_pen);
    for (double angle = 0; angle < 360; angle += 30) {
        QPainterPath path;
        path.arcMoveTo(bounds, angle);
        path.arcTo(bounds, angle, degreesPerPixel());
        painter->drawPath(path);
    }
}

void BlockClockDial::paint(QPainter * painter)
{
    if (width() <= 0 || height() <= 0) {
        return;
    }
    painter->setRenderHint(QPainter::Antialiasing);

    if (m_block_layer == NewestBlock) {
        // Painted over a dial that paints everything else.
        if (!paused() && connected() && synced() && showBlockSegments()) paintBlocks(painter);
        return;
    }

    paintBackground(painter);
    if (showTimeTicks()) {
        paintTimeTicks(painter);
    }

    if (paused()) return;

    if (connected() && synced()) {
        if (showBlockSegments()) {
            paintBlocks(painter);
        } else if (useGradientArcWhenSynced()) {
            paintSyncedGradientArc(painter);
        } else {
            paintCurrentTimeArc(painter);
        }
    } else if (connected()) {
        paintProgress(painter);
    } else if (m_animation_timer.isActive()) {
        paintConnectingAnimation(painter);
    }
}
