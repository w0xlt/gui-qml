// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_COMPONENTS_BLOCKCLOCKDIAL_H
#define BITCOIN_QML_COMPONENTS_BLOCKCLOCKDIAL_H

#include <QQuickPaintedItem>
#include <QConicalGradient>
#include <QMetaObject>
#include <QPainter>
#include <QTimer>
#include <QtGlobal>

#include <optional>

/**
 * Paints the current twelve-hour block timeline clockwise from midnight or noon.
 *
 * Each block is a segment from the block before it to its timestamp, the time
 * it took to find, in the color of its confirmations. The newest segment, from
 * the latest block to the current time, is the block being built, with zero
 * confirmations. A block found too soon after the one before it to be drawn
 * apart still gets a segment of the smallest size, which moves the end of the
 * older segments back, so every block remains visible and can be pointed at
 * with blockAt(), e.g. to show its details while its segment is highlighted.
 *
 * The block being built can be painted by a second dial on top, so it can be
 * animated without repainting the others. That dial also paints the light of
 * a block being found: a flash where it ends, and a wave that runs back over
 * the blocks whose color its confirmation changes.
 */
class BlockClockDial : public QQuickPaintedItem
{
    Q_OBJECT
    /** Current position through the local twelve-hour period, in [0, 1]. */
    Q_PROPERTY(qreal currentTimeFraction READ currentTimeFraction WRITE setCurrentTimeFraction)
    /** Sorted block positions through the same period, each in [0, 1]. */
    Q_PROPERTY(QList<qreal> blockTimeFractions READ blockTimeFractions WRITE setBlockTimeFractions)
    /** Progress of the synchronization phase currently presented by the dial. */
    Q_PROPERTY(double syncProgress READ syncProgress WRITE setSyncProgress)
    Q_PROPERTY(bool connected READ connected WRITE setConnected)
    Q_PROPERTY(bool synced READ synced WRITE setSynced)
    Q_PROPERTY(bool paused READ paused WRITE setPaused)
    Q_PROPERTY(bool animateDial READ animateDial WRITE setAnimateDial)
    Q_PROPERTY(bool renderingActive READ renderingActive WRITE setRenderingActive NOTIFY renderingActiveChanged)
    Q_PROPERTY(int connectingAnimationDelayMs READ connectingAnimationDelayMs WRITE setConnectingAnimationDelayMs)
    Q_PROPERTY(bool showTimeTicks READ showTimeTicks WRITE setShowTimeTicks)
    Q_PROPERTY(bool showBlockSegments READ showBlockSegments WRITE setShowBlockSegments)
    Q_PROPERTY(bool useGradientArcWhenSynced READ useGradientArcWhenSynced WRITE setUseGradientArcWhenSynced)
    Q_PROPERTY(qreal penWidth READ penWidth WRITE setPenWidth)
    Q_PROPERTY(qreal scale READ scale WRITE setScale NOTIFY scaleChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)
    Q_PROPERTY(QList<QColor> confirmationColors READ confirmationColors WRITE setConfirmationColors )
    Q_PROPERTY(QColor timeTickColor READ timeTickColor WRITE setTimeTickColor)
    /** Which block segments a synced dial paints. */
    Q_PROPERTY(BlockLayer blockLayer READ blockLayer WRITE setBlockLayer)
    /** Space around the ring, e.g. for the newest segment's glow. */
    Q_PROPERTY(qreal ringMargin READ ringMargin WRITE setRingMargin)
    /** Marks the current time at the end of the newest segment, if valid. */
    Q_PROPERTY(QColor headColor READ headColor WRITE setHeadColor)
    /**
     * Index in blockTimeFractions of a block whose segment is highlighted by
     * dimming the others, or -1.
     */
    Q_PROPERTY(int highlightedBlock READ highlightedBlock WRITE setHighlightedBlock)
    /** Strength in [0, 1] of the flash at the newest block. */
    Q_PROPERTY(qreal foundFlash READ foundFlash WRITE setFoundFlash)
    /**
     * Progress in [0, 1] of the confirmation wave, or -1. The blocks it
     * hasn't reached show one confirmation fewer, as before the newest block
     * was found.
     */
    Q_PROPERTY(qreal confirmationWave READ confirmationWave WRITE setConfirmationWave)

public:
    enum BlockLayer {
        AllBlocks,
        /** Every segment except the newest. */
        SettledBlocks,
        /** Only the newest segment, with a glow and the head color at its end. */
        NewestBlock,
    };
    Q_ENUM(BlockLayer)

    explicit BlockClockDial(QQuickItem * parent = nullptr);
    ~BlockClockDial() override;
    void paint(QPainter * painter) override;

    qreal currentTimeFraction() const { return m_current_time_fraction; }
    QList<qreal> blockTimeFractions() const { return m_block_time_fractions; }
    double syncProgress() const { return m_sync_progress; };
    bool connected() const { return m_is_connected; };
    bool synced() const { return m_is_synced; };
    bool paused() const { return m_is_paused; };
    bool animateDial() const { return m_animate_dial; };
    bool renderingActive() const { return m_rendering_active; }
    int connectingAnimationDelayMs() const { return m_connecting_animation_delay_ms; };
    bool showTimeTicks() const { return m_show_time_ticks; };
    bool showBlockSegments() const { return m_show_block_segments; };
    bool useGradientArcWhenSynced() const { return m_use_gradient_arc_when_synced; };
    qreal penWidth() const { return m_pen_width; };
    qreal scale() const { return m_scale; };
    QColor backgroundColor() const { return m_background_color; };
    QList<QColor> confirmationColors() const { return m_confirmation_colors; };
    QColor timeTickColor() const { return m_time_tick_color; };
    BlockLayer blockLayer() const { return m_block_layer; }
    qreal ringMargin() const { return m_ring_margin; }
    QColor headColor() const { return m_head_color; }
    int highlightedBlock() const { return m_highlighted_block; }
    qreal foundFlash() const { return m_found_flash; }
    qreal confirmationWave() const { return m_confirmation_wave; }

    /**
     * Index in blockTimeFractions of the block whose segment is painted at the
     * point, or -1 if none is, e.g. at the block being built.
     */
    Q_INVOKABLE int blockAt(qreal x, qreal y) const;

public Q_SLOTS:
    void setCurrentTimeFraction(qreal fraction);
    void setBlockTimeFractions(QList<qreal> fractions);
    void setSyncProgress(double progress);
    void setConnected(bool connected);
    void setSynced(bool synced);
    void setPaused(bool paused);
    void setAnimateDial(bool animate_dial);
    void setRenderingActive(bool active);
    void setConnectingAnimationDelayMs(int connecting_animation_delay_ms);
    void setShowTimeTicks(bool show_time_ticks);
    void setShowBlockSegments(bool show_block_segments);
    void setUseGradientArcWhenSynced(bool use_gradient_arc_when_synced);
    void setPenWidth(qreal width);
    void setScale(qreal scale);
    void setBackgroundColor(QColor color);
    void setConfirmationColors(QList<QColor> colorList);
    void setTimeTickColor(QColor color);
    void setBlockLayer(BlockLayer layer);
    void setRingMargin(qreal margin);
    void setHeadColor(QColor color);
    void setHighlightedBlock(int block);
    void setFoundFlash(qreal flash);
    void setConfirmationWave(qreal progress);

Q_SIGNALS:
    void scaleChanged();
    void renderingActiveChanged();

protected:
    void componentComplete() override;

private:
    /** The drawn interval of a block. */
    struct BlockSegment
    {
        qreal start_fraction;
        qreal end_fraction;
        qsizetype confirmations;
        /** Index in blockTimeFractions, or -1 for the block being built. */
        qsizetype block;
    };

    void paintConnectingAnimation(QPainter * painter);
    void paintProgress(QPainter * painter);
    void paintCurrentTimeArc(QPainter* painter);
    void paintSyncedGradientArc(QPainter* painter);
    void paintBlocks(QPainter * painter);
    void paintNewestBlockHead(QPainter* painter, const QRectF& bounds, qreal angle, const QColor& color);
    void paintBlockFoundLight(QPainter* painter, const QRectF& bounds, const QList<BlockSegment>& segments,
                              std::optional<qreal> wave_front);
    void paintGlow(QPainter* painter, const QPointF& center, qreal radius, const QColor& color);
    void paintBackground(QPainter * painter);
    void paintTimeTicks(QPainter * painter);
    QRectF getBoundsForPen(const QPen & pen) const;
    double degreesPerPixel();
    qreal degreesForArcPixels(qreal pixels, const QRectF& bounds) const;
    qreal blockSegmentGapPixels() const;
    QList<BlockSegment> blockSegments(const QRectF& bounds) const;
    std::optional<qreal> confirmationWaveFront(const QList<BlockSegment>& segments) const;
    void setupConnectingGradient(const QPen & pen);
    void setupSyncedGradient(const QRectF& bounds);
    void invalidateSyncedGradient();
    qreal decrementGradientAngle(qreal angle) const;
    qreal getTargetAnimationAngle() const;
    bool presentationActive() const;
    void requestRepaint();
    void advanceAnimation();
    void syncAnimationState();
    void connectWindowVisibility();

    qreal m_current_time_fraction{0.0};
    QList<qreal> m_block_time_fractions;
    double m_sync_progress{0.0};
    bool m_is_connected{false};
    bool m_is_synced{false};
    bool m_is_paused{false};
    bool m_animate_dial{true};
    bool m_rendering_active{true};
    bool m_component_complete{false};
    bool m_presentation_was_active{false};
    int m_connecting_animation_delay_ms{5000};
    bool m_show_time_ticks{true};
    bool m_show_block_segments{true};
    bool m_use_gradient_arc_when_synced{false};
    qreal m_pen_width{4};
    qreal m_scale{5/12};
    QColor m_background_color{"#2D2D2D"};
    QConicalGradient m_connecting_gradient;
    QConicalGradient m_synced_gradient;
    QRectF m_synced_gradient_bounds;
    bool m_synced_gradient_needs_update{true};
    qreal m_connecting_start_angle = 90;
    const qreal m_connecting_end_angle = -180;
    QList<QColor> m_confirmation_colors{};
    QColor m_time_tick_color{"#000000"};
    BlockLayer m_block_layer{AllBlocks};
    qreal m_ring_margin{0};
    QColor m_head_color;
    int m_highlighted_block{-1};
    qreal m_found_flash{0};
    qreal m_confirmation_wave{-1};
    QTimer m_animation_timer;
    QTimer m_delay_timer;
    QMetaObject::Connection m_window_visibility_connection;
    qreal m_animating_max_angle = 0;
};

#endif // BITCOIN_QML_COMPONENTS_BLOCKCLOCKDIAL_H
