// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/components/blockclockdial.h>

#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QQuickWindow>
#include <QtMath>

#include <atomic>
#include <cmath>
#include <utility>

namespace {
constexpr int DIAL_SIZE{120};
const QColor BACKGROUND_COLOR{QStringLiteral("#202020")};
const QList<QColor> CONFIRMATION_COLORS{
    QColor{QStringLiteral("#FF1C1C")},
    QColor{QStringLiteral("#ED6E46")},
    QColor{QStringLiteral("#EE8847")},
    QColor{QStringLiteral("#EFA148")},
    QColor{QStringLiteral("#F0BB49")},
    QColor{QStringLiteral("#F1D54A")},
};

class TestableBlockClockDial : public BlockClockDial
{
public:
    using BlockClockDial::BlockClockDial;
    void complete() { componentComplete(); }
    void paint(QPainter* painter) override
    {
        BlockClockDial::paint(painter);
        ++paint_count;
    }
    std::atomic<int> paint_count{0};
};

int ColorDistance(const QColor& a, const QColor& b)
{
    return std::abs(a.red() - b.red()) +
           std::abs(a.green() - b.green()) +
           std::abs(a.blue() - b.blue());
}

void ConfigureDial(BlockClockDial& dial)
{
    dial.setWidth(DIAL_SIZE);
    dial.setHeight(DIAL_SIZE);
    dial.setPenWidth(12);
    dial.setBackgroundColor(BACKGROUND_COLOR);
    dial.setConfirmationColors(CONFIRMATION_COLORS);
    dial.setTimeTickColor(Qt::black);
    dial.setShowTimeTicks(false);
}

void ConfigureSyncedDial(BlockClockDial& dial, qreal current_fraction,
                         const QList<qreal>& block_fractions)
{
    ConfigureDial(dial);
    dial.setAnimateDial(false);
    dial.setConnected(true);
    dial.setSynced(true);
    dial.setCurrentTimeFraction(current_fraction);
    dial.setBlockTimeFractions(block_fractions);
}

QImage RenderDial(BlockClockDial& dial)
{
    QImage image{DIAL_SIZE, DIAL_SIZE, QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::transparent);

    QPainter painter{&image};
    dial.paint(&painter);
    return image;
}

int CountConfirmationPixels(const QImage& image)
{
    int count{0};
    for (int y{0}; y < image.height(); ++y) {
        for (int x{0}; x < image.width(); ++x) {
            const QColor pixel{image.pixelColor(x, y)};
            if (pixel.alpha() == 0) continue;
            for (const QColor& confirmation_color : CONFIRMATION_COLORS) {
                if (ColorDistance(pixel, confirmation_color) < 80) {
                    ++count;
                    break;
                }
            }
        }
    }
    return count;
}

QPoint DialPoint(qreal fraction)
{
    constexpr qreal center{DIAL_SIZE / 2.0};
    constexpr qreal radius{DIAL_SIZE / 2.0 - 7.0};
    const qreal angle{fraction * 2.0 * M_PI};
    return {qRound(center + qSin(angle) * radius), qRound(center - qCos(angle) * radius)};
}

void VerifyDialColor(const QImage& image, qreal fraction, const QColor& expected)
{
    const QColor actual{image.pixelColor(DialPoint(fraction))};
    QVERIFY2(ColorDistance(actual, expected) < 10,
             qPrintable(QStringLiteral("expected %1 at %2, got %3")
                            .arg(expected.name(QColor::HexArgb))
                            .arg(fraction)
                            .arg(actual.name(QColor::HexArgb))));
}
} // namespace

class BlockClockDialTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void ibdProgressRendersImmediateHalfArc();
    void syncedGradientToggleChangesRenderedColors();
    void syncedGradientRunsFromOlderToNewerConfirmationColors();
    void syncedGradientSpansCurrentPeriodAndUpdatesWithTime();
    void syncedGradientUpdatesWhenConfirmationColorsChange();
    void connectingDelayControlsInitialAnimation();
    void inactiveDialStopsAnimationAndRetainsLatestState();
    void reactivatedStaticDialRequestsRepaint_data();
    void reactivatedStaticDialRequestsRepaint();
    void paintDoesNotAdvanceAnimationState();
    void equalBlockTimestampsPreserveConfirmationDepth();
    void distinctBlocksUseExactConfirmationColors();
    void futureBlocksKeepASegmentBeforeTheBlockBeingBuilt_data();
    void futureBlocksKeepASegmentBeforeTheBlockBeingBuilt();
    void blocksFoundTogetherKeepASegmentEach();
    void blocksFoundTogetherSaturateAtHighestConfirmationColor();
    void recentBlocksKeepASegmentBeforeTheBlockBeingBuilt();
    void blocksWithoutRoomAfterThePeriodStartAreLeftOut();
    void layersSplitSettledAndNewestSegments();
    void newestBlockGlowsIntoTheRingMargin();
    void blockAtFindsTheBlockOfASegment();
    void blockAtFindsRecentAndFutureBlocks();
    void highlightedBlockDimsTheOtherSegments();
    void confirmationWaveShowsEarlierColorsUntilItPasses();
    void newestBlockLayerPaintsTheFoundBlockLight();
};

void BlockClockDialTests::ibdProgressRendersImmediateHalfArc()
{
    BlockClockDial dial;
    ConfigureDial(dial);
    dial.setAnimateDial(false);
    dial.setConnected(true);
    dial.setSynced(false);
    dial.setSyncProgress(0.5);

    const QImage image{RenderDial(dial)};
    const QColor active_pixel{image.pixelColor(QPoint{DIAL_SIZE - 7, DIAL_SIZE / 2})};
    const QColor inactive_pixel{image.pixelColor(QPoint{7, DIAL_SIZE / 2})};

    QVERIFY2(ColorDistance(active_pixel, CONFIRMATION_COLORS[5]) < 35,
             qPrintable(QStringLiteral("expected active IBD arc pixel, got %1").arg(active_pixel.name(QColor::HexArgb))));
    QVERIFY2(ColorDistance(inactive_pixel, BACKGROUND_COLOR) < 15,
             qPrintable(QStringLiteral("expected background outside IBD arc, got %1").arg(inactive_pixel.name(QColor::HexArgb))));
}

void BlockClockDialTests::syncedGradientToggleChangesRenderedColors()
{
    BlockClockDial dial;
    ConfigureDial(dial);
    dial.setAnimateDial(false);
    dial.setConnected(true);
    dial.setSynced(true);
    dial.setShowBlockSegments(false);
    dial.setCurrentTimeFraction(1.0);

    dial.setUseGradientArcWhenSynced(false);
    const QImage uniform_image{RenderDial(dial)};
    const QColor uniform_right{uniform_image.pixelColor(QPoint{DIAL_SIZE - 7, DIAL_SIZE / 2})};
    const QColor uniform_bottom{uniform_image.pixelColor(QPoint{DIAL_SIZE / 2, DIAL_SIZE - 7})};
    QVERIFY2(ColorDistance(uniform_right, CONFIRMATION_COLORS[5]) < 35,
             qPrintable(QStringLiteral("expected uniform synced arc color, got %1").arg(uniform_right.name(QColor::HexArgb))));
    QVERIFY2(ColorDistance(uniform_bottom, CONFIRMATION_COLORS[5]) < 35,
             qPrintable(QStringLiteral("expected uniform synced arc color, got %1").arg(uniform_bottom.name(QColor::HexArgb))));

    dial.setUseGradientArcWhenSynced(true);
    const QImage gradient_image{RenderDial(dial)};
    const QColor gradient_right{gradient_image.pixelColor(QPoint{DIAL_SIZE - 7, DIAL_SIZE / 2})};
    const QColor gradient_bottom{gradient_image.pixelColor(QPoint{DIAL_SIZE / 2, DIAL_SIZE - 7})};
    QVERIFY2(ColorDistance(gradient_right, gradient_bottom) > 25,
             qPrintable(QStringLiteral("expected gradient samples to differ, got %1 and %2")
                            .arg(gradient_right.name(QColor::HexArgb), gradient_bottom.name(QColor::HexArgb))));
}

void BlockClockDialTests::syncedGradientRunsFromOlderToNewerConfirmationColors()
{
    BlockClockDial dial;
    ConfigureDial(dial);
    dial.setAnimateDial(false);
    dial.setConnected(true);
    dial.setSynced(true);
    dial.setShowBlockSegments(false);
    dial.setUseGradientArcWhenSynced(true);
    dial.setCurrentTimeFraction(0.75);

    const QImage image{RenderDial(dial)};
    const QColor older_color{image.pixelColor(DialPoint(0.05))};
    const QColor middle_color{image.pixelColor(DialPoint(0.375))};
    const QColor newer_color{image.pixelColor(DialPoint(0.70))};

    QVERIFY(ColorDistance(older_color, CONFIRMATION_COLORS[5]) <
            ColorDistance(older_color, CONFIRMATION_COLORS[0]));
    QVERIFY(ColorDistance(newer_color, CONFIRMATION_COLORS[0]) <
            ColorDistance(older_color, CONFIRMATION_COLORS[0]));
    QVERIFY(ColorDistance(older_color, CONFIRMATION_COLORS[5]) <
            ColorDistance(newer_color, CONFIRMATION_COLORS[5]));
    QVERIFY(older_color.green() > middle_color.green());
    QVERIFY(middle_color.green() > newer_color.green());
}

void BlockClockDialTests::syncedGradientUpdatesWhenConfirmationColorsChange()
{
    BlockClockDial dial;
    ConfigureDial(dial);
    dial.setAnimateDial(false);
    dial.setConnected(true);
    dial.setSynced(true);
    dial.setShowBlockSegments(false);
    dial.setUseGradientArcWhenSynced(true);
    dial.setCurrentTimeFraction(1.0);

    const QColor initial_color{RenderDial(dial).pixelColor(QPoint{DIAL_SIZE - 7, DIAL_SIZE / 2})};

    const QList<QColor> updated_colors{
        QColor{QStringLiteral("#3399FF")},
        QColor{QStringLiteral("#33CCFF")},
        QColor{QStringLiteral("#33FFCC")},
        QColor{QStringLiteral("#99FF33")},
        QColor{QStringLiteral("#FFFF33")},
        QColor{QStringLiteral("#FFFFFF")},
    };
    dial.setConfirmationColors(updated_colors);

    const QColor updated_color{RenderDial(dial).pixelColor(QPoint{DIAL_SIZE - 7, DIAL_SIZE / 2})};
    QVERIFY2(ColorDistance(initial_color, updated_color) > 50,
             qPrintable(QStringLiteral("expected gradient color to update, got %1 and %2")
                            .arg(initial_color.name(QColor::HexArgb), updated_color.name(QColor::HexArgb))));
}

void BlockClockDialTests::syncedGradientSpansCurrentPeriodAndUpdatesWithTime()
{
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 1.0, {});
    dial.setShowBlockSegments(false);
    dial.setUseGradientArcWhenSynced(true);

    // Reuse the dial across time changes, including a period rollover, so stale
    // cached gradients or stops from a previous span cannot pass this test.
    for (const qreal current : {0.25, 0.50, 0.75, 1.0, 0.25}) {
        dial.setCurrentTimeFraction(current);
        const QImage image{RenderDial(dial)};
        VerifyDialColor(image, current * 0.08, CONFIRMATION_COLORS[5]);
        VerifyDialColor(image, current * 0.32, CONFIRMATION_COLORS[4]);
        VerifyDialColor(image, current * 0.48, CONFIRMATION_COLORS[3]);
        VerifyDialColor(image, current * 0.64, CONFIRMATION_COLORS[2]);
        VerifyDialColor(image, current * 0.80, CONFIRMATION_COLORS[1]);
        VerifyDialColor(image, current * 0.995, CONFIRMATION_COLORS[0]);
        if (current < 1.0) VerifyDialColor(image, current + 0.10, BACKGROUND_COLOR);
    }
}

void BlockClockDialTests::connectingDelayControlsInitialAnimation()
{
    TestableBlockClockDial delayed_dial;
    ConfigureDial(delayed_dial);
    delayed_dial.setConnected(false);
    delayed_dial.setAnimateDial(true);
    delayed_dial.setConnectingAnimationDelayMs(5000);
    delayed_dial.complete();
    QCOMPARE(CountConfirmationPixels(RenderDial(delayed_dial)), 0);

    TestableBlockClockDial immediate_dial;
    ConfigureDial(immediate_dial);
    immediate_dial.setConnected(false);
    immediate_dial.setAnimateDial(true);
    immediate_dial.setConnectingAnimationDelayMs(0);
    immediate_dial.complete();

    QTRY_VERIFY_WITH_TIMEOUT(CountConfirmationPixels(RenderDial(immediate_dial)) > 0, 250);
}

void BlockClockDialTests::inactiveDialStopsAnimationAndRetainsLatestState()
{
    TestableBlockClockDial dial;
    ConfigureDial(dial);
    dial.setConnectingAnimationDelayMs(0);
    dial.complete();
    QTest::qWait(20);

    QTimer* animation_timer{nullptr};
    for (QTimer* timer : dial.findChildren<QTimer*>()) {
        if (timer->interval() == 16) animation_timer = timer;
    }
    QVERIFY(animation_timer);
    QVERIFY(animation_timer->isActive());

    dial.setRenderingActive(false);
    QVERIFY(!animation_timer->isActive());

    dial.setCurrentTimeFraction(0.75);
    dial.setConnected(true);
    dial.setSynced(true);
    QCOMPARE(dial.currentTimeFraction(), 0.75);

    dial.setRenderingActive(true);
    QVERIFY(!animation_timer->isActive());
    const QImage image{RenderDial(dial)};
    QVERIFY(CountConfirmationPixels(image) > 0);
}

void BlockClockDialTests::reactivatedStaticDialRequestsRepaint_data()
{
    QTest::addColumn<bool>("paused");
    QTest::newRow("non-animated") << false;
    QTest::newRow("paused") << true;
}

void BlockClockDialTests::reactivatedStaticDialRequestsRepaint()
{
    QFETCH(bool, paused);
    QQuickWindow window;
    window.resize(DIAL_SIZE, DIAL_SIZE);
    TestableBlockClockDial dial{window.contentItem()};
    ConfigureDial(dial);
    dial.setAnimateDial(paused);
    dial.setPaused(paused);
    dial.setConnected(true);
    dial.setSynced(true);
    dial.setShowBlockSegments(false);
    dial.setCurrentTimeFraction(0.25);
    dial.complete();
    window.show();
    QTRY_VERIFY(dial.paint_count.load() > 0);
    QTest::qWait(50);

    dial.setRenderingActive(false);
    const int paints_before{dial.paint_count.load()};
    dial.setCurrentTimeFraction(0.75);
    QTest::qWait(50);
    QCOMPARE(dial.paint_count.load(), paints_before);

    dial.setRenderingActive(true);
    QTRY_VERIFY(dial.paint_count.load() > paints_before);
    for (const QTimer* timer : dial.findChildren<QTimer*>()) {
        QVERIFY(!timer->isActive());
    }
}

void BlockClockDialTests::equalBlockTimestampsPreserveConfirmationDepth()
{
    BlockClockDial dial;
    ConfigureDial(dial);
    dial.setAnimateDial(false);
    dial.setConnected(true);
    dial.setSynced(true);
    dial.setCurrentTimeFraction(0.75);
    dial.setBlockTimeFractions({0.25, 0.50, 0.50});
    QCOMPARE(dial.blockTimeFractions().size(), 3);
    const QImage image{RenderDial(dial)};
    QVERIFY(ColorDistance(image.pixelColor(DialPoint(0.125)), CONFIRMATION_COLORS[3]) < 10);
    // The older of the two blocks keeps a segment before the newer one's end.
    QVERIFY(ColorDistance(image.pixelColor(DialPoint(0.35)), CONFIRMATION_COLORS[2]) < 10);
    QVERIFY(ColorDistance(image.pixelColor(DialPoint(0.4715)), CONFIRMATION_COLORS[1]) < 10);
    QVERIFY(ColorDistance(image.pixelColor(DialPoint(0.625)), CONFIRMATION_COLORS[0]) < 10);
}

void BlockClockDialTests::paintDoesNotAdvanceAnimationState()
{
    TestableBlockClockDial dial;
    ConfigureDial(dial);
    dial.setRenderingActive(false);
    dial.complete();

    const QImage first{RenderDial(dial)};
    const QImage second{RenderDial(dial)};
    QCOMPARE(first, second);
}

void BlockClockDialTests::distinctBlocksUseExactConfirmationColors()
{
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.90, {0.15, 0.30, 0.45, 0.60, 0.75});

    const QImage image{RenderDial(dial)};
    for (int i{0}; i < 6; ++i) {
        VerifyDialColor(image, 0.075 + 0.15 * i, CONFIRMATION_COLORS[5 - i]);
    }
}

// In the test dial, the smallest segment spans 19 pixels of a 53 pixel radius.
constexpr qreal SMALLEST_SEGMENT{19.0 / 53.0 / (2.0 * M_PI)};

void BlockClockDialTests::futureBlocksKeepASegmentBeforeTheBlockBeingBuilt_data()
{
    QTest::addColumn<QList<qreal>>("blocks");
    QTest::addColumn<int>("future_blocks");

    QTest::newRow("ahead-of-now") << QList<qreal>{0.25, 0.50, 0.76} << 1;
    QTest::newRow("equal-future-timestamps") << QList<qreal>{0.25, 0.50, 0.76, 0.76} << 2;
    QTest::newRow("exactly-now") << QList<qreal>{0.25, 0.50, 0.75} << 1;
    QTest::newRow("period-end") << QList<qreal>{0.25, 0.50, 1.0} << 1;
    QTest::newRow("several-future-blocks") << QList<qreal>{0.25, 0.50, 0.76, 0.80} << 2;
    QTest::newRow("saturated") << QList<qreal>{0.25, 0.50, 0.76, 0.80, 0.85, 0.90, 0.95, 1.0} << 6;
}

void BlockClockDialTests::futureBlocksKeepASegmentBeforeTheBlockBeingBuilt()
{
    QFETCH(QList<qreal>, blocks);
    QFETCH(int, future_blocks);
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.75, blocks);

    const QImage image{RenderDial(dial)};
    VerifyDialColor(image, 0.125, CONFIRMATION_COLORS[qMin(5, future_blocks + 2)]);
    VerifyDialColor(image, 0.30, CONFIRMATION_COLORS[qMin(5, future_blocks + 1)]);
    // The newest one ends a smallest segment before the current time.
    VerifyDialColor(image, 0.75 - 1.5 * SMALLEST_SEGMENT, CONFIRMATION_COLORS[1]);
    VerifyDialColor(image, 0.75 - 0.5 * SMALLEST_SEGMENT, CONFIRMATION_COLORS[0]);
    VerifyDialColor(image, 0.875, BACKGROUND_COLOR);
}

void BlockClockDialTests::blocksFoundTogetherKeepASegmentEach()
{
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.75, {0.25, 0.2501, 0.2502});

    // Each ends a smallest segment before the next one does.
    const QImage image{RenderDial(dial)};
    VerifyDialColor(image, 0.05, CONFIRMATION_COLORS[3]);
    VerifyDialColor(image, 0.2502 - 1.5 * SMALLEST_SEGMENT, CONFIRMATION_COLORS[2]);
    VerifyDialColor(image, 0.2502 - 0.5 * SMALLEST_SEGMENT, CONFIRMATION_COLORS[1]);
    VerifyDialColor(image, 0.2502 - SMALLEST_SEGMENT, BACKGROUND_COLOR);
    VerifyDialColor(image, 0.50, CONFIRMATION_COLORS[0]);
}

void BlockClockDialTests::blocksFoundTogetherSaturateAtHighestConfirmationColor()
{
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.95, {0.50, 0.5001, 0.5002, 0.5003, 0.5004, 0.5005});

    const QImage image{RenderDial(dial)};
    VerifyDialColor(image, 0.10, CONFIRMATION_COLORS[5]);
    for (int confirmations{1}; confirmations <= 5; ++confirmations) {
        VerifyDialColor(image, 0.5005 - (confirmations - 0.5) * SMALLEST_SEGMENT, CONFIRMATION_COLORS[confirmations]);
    }
    VerifyDialColor(image, 0.70, CONFIRMATION_COLORS[0]);
}

void BlockClockDialTests::recentBlocksKeepASegmentBeforeTheBlockBeingBuilt()
{
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.75, {0.7498, 0.7499});

    const QImage image{RenderDial(dial)};
    VerifyDialColor(image, 0.10, CONFIRMATION_COLORS[2]);
    VerifyDialColor(image, 0.75 - 1.5 * SMALLEST_SEGMENT, CONFIRMATION_COLORS[1]);
    VerifyDialColor(image, 0.75 - 0.5 * SMALLEST_SEGMENT, CONFIRMATION_COLORS[0]);
}

void BlockClockDialTests::blocksWithoutRoomAfterThePeriodStartAreLeftOut()
{
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.75, {0.0001, 0.25});

    const QImage image{RenderDial(dial)};
    VerifyDialColor(image, 0.125, CONFIRMATION_COLORS[1]);
    VerifyDialColor(image, 0.50, CONFIRMATION_COLORS[0]);
}

void BlockClockDialTests::layersSplitSettledAndNewestSegments()
{
    const QList<qreal> blocks{0.15, 0.30, 0.45, 0.60, 0.75};

    BlockClockDial settled;
    ConfigureSyncedDial(settled, 0.90, blocks);
    settled.setBlockLayer(BlockClockDial::SettledBlocks);
    const QImage settled_image{RenderDial(settled)};
    VerifyDialColor(settled_image, 0.675, CONFIRMATION_COLORS[1]);
    // The track shows where the newest segment is left out.
    VerifyDialColor(settled_image, 0.825, BACKGROUND_COLOR);

    BlockClockDial newest;
    ConfigureSyncedDial(newest, 0.90, blocks);
    newest.setBlockLayer(BlockClockDial::NewestBlock);
    const QImage newest_image{RenderDial(newest)};
    VerifyDialColor(newest_image, 0.825, CONFIRMATION_COLORS[0]);
    // Without the track or the older segments.
    QCOMPARE(newest_image.pixelColor(DialPoint(0.675)).alpha(), 0);
    QCOMPARE(newest_image.pixelColor(DialPoint(0.40)).alpha(), 0);
}

void BlockClockDialTests::newestBlockGlowsIntoTheRingMargin()
{
    constexpr int margin{30};
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.90, {0.75});
    dial.setBlockLayer(BlockClockDial::NewestBlock);
    dial.setHeadColor(Qt::white);
    dial.setRingMargin(margin);
    dial.setWidth(DIAL_SIZE + 2 * margin);
    dial.setHeight(DIAL_SIZE + 2 * margin);

    QImage image{DIAL_SIZE + 2 * margin, DIAL_SIZE + 2 * margin, QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::transparent);
    {
        QPainter painter{&image};
        dial.paint(&painter);
    }

    // The ring is inset by the margin.
    const QPoint offset{margin, margin};
    QVERIFY(ColorDistance(image.pixelColor(DialPoint(0.825) + offset), CONFIRMATION_COLORS[0]) < 10);

    // The head marks the end of the segment, half a gap before the current
    // time, and its glow reaches past the ring.
    constexpr qreal pen_width{12};
    constexpr qreal ring_radius{DIAL_SIZE / 2.0 - pen_width / 2.0 - 1.0};
    const qreal head{0.90 - qRadiansToDegrees((pen_width * 1.5) / ring_radius) / 2.0 / 360.0};
    QVERIFY(ColorDistance(image.pixelColor(DialPoint(head) + offset), Qt::white) < 10);
    const qreal angle{head * 2.0 * M_PI};
    const qreal outside_radius{DIAL_SIZE / 2.0 + 6.0};
    const QPoint outside{qRound(DIAL_SIZE / 2.0 + qSin(angle) * outside_radius),
                         qRound(DIAL_SIZE / 2.0 - qCos(angle) * outside_radius)};
    QVERIFY(image.pixelColor(outside + offset).alpha() > 0);
}


namespace {
int BlockAt(const BlockClockDial& dial, qreal fraction)
{
    const QPoint point{DialPoint(fraction)};
    return dial.blockAt(point.x(), point.y());
}
} // namespace

void BlockClockDialTests::blockAtFindsTheBlockOfASegment()
{
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.75, {0.15, 0.30, 0.3001, 0.45});

    QCOMPARE(BlockAt(dial, 0.10), 0);
    // Blocks found together each have a segment.
    QCOMPARE(BlockAt(dial, 0.20), 1);
    QCOMPARE(BlockAt(dial, 0.28), 2);
    QCOMPARE(BlockAt(dial, 0.40), 3);
    // The block being built.
    QCOMPARE(BlockAt(dial, 0.60), -1);
    // Past the current time, and away from the ring.
    QCOMPARE(BlockAt(dial, 0.80), -1);
    QCOMPARE(dial.blockAt(DIAL_SIZE / 2.0, DIAL_SIZE / 2.0), -1);
    // Near enough the ring.
    QCOMPARE(dial.blockAt(DIAL_SIZE / 2.0, 0), 0);

    // Only while segments are painted.
    dial.setSynced(false);
    QCOMPARE(BlockAt(dial, 0.10), -1);
}

void BlockClockDialTests::blockAtFindsRecentAndFutureBlocks()
{
    BlockClockDial dial;
    // A block timestamped ahead of the clock ends before the block being built.
    ConfigureSyncedDial(dial, 0.75, {0.15, 0.90});
    QCOMPARE(BlockAt(dial, 0.10), 0);
    QCOMPARE(BlockAt(dial, 0.50), 1);
    QCOMPARE(BlockAt(dial, 0.74), -1);

    dial.setBlockTimeFractions({0.15, 0.7499});
    QCOMPARE(BlockAt(dial, 0.50), 1);
    QCOMPARE(BlockAt(dial, 0.74), -1);
}

void BlockClockDialTests::highlightedBlockDimsTheOtherSegments()
{
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.75, {0.25, 0.50});
    dial.setHighlightedBlock(1);

    const QImage image{RenderDial(dial)};
    VerifyDialColor(image, 0.375, CONFIRMATION_COLORS[1]);
    for (const auto& [fraction, color] : {std::pair{0.125, CONFIRMATION_COLORS[2]}, std::pair{0.625, CONFIRMATION_COLORS[0]}}) {
        const QColor dimmed{image.pixelColor(DialPoint(fraction))};
        QVERIFY2(ColorDistance(dimmed, color) > 150, qPrintable(dimmed.name()));
        QVERIFY2(ColorDistance(dimmed, BACKGROUND_COLOR) > 50, qPrintable(dimmed.name()));
    }

    dial.setHighlightedBlock(-1);
    const QImage plain{RenderDial(dial)};
    VerifyDialColor(plain, 0.125, CONFIRMATION_COLORS[2]);
    VerifyDialColor(plain, 0.625, CONFIRMATION_COLORS[0]);
}

void BlockClockDialTests::confirmationWaveShowsEarlierColorsUntilItPasses()
{
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.90, {0.15, 0.30, 0.45, 0.60, 0.75});

    // Before it leaves the newest block, each block shows the color it had
    // before that block was found.
    dial.setConfirmationWave(0.0);
    QImage image{RenderDial(dial)};
    for (int i{0}; i < 5; ++i) {
        VerifyDialColor(image, 0.075 + 0.15 * i, CONFIRMATION_COLORS[4 - i]);
    }
    VerifyDialColor(image, 0.825, CONFIRMATION_COLORS[0]);

    // It runs back from the newest block to the start of the fifth newest.
    dial.setConfirmationWave(0.5);
    image = RenderDial(dial);
    VerifyDialColor(image, 0.225, CONFIRMATION_COLORS[3]);
    VerifyDialColor(image, 0.525, CONFIRMATION_COLORS[2]);

    for (const qreal wave : {1.0, -1.0}) {
        dial.setConfirmationWave(wave);
        image = RenderDial(dial);
        for (int i{0}; i < 6; ++i) {
            VerifyDialColor(image, 0.075 + 0.15 * i, CONFIRMATION_COLORS[5 - i]);
        }
    }
}

void BlockClockDialTests::newestBlockLayerPaintsTheFoundBlockLight()
{
    constexpr int margin{30};
    const auto render{[](BlockClockDial& dial) {
        QImage image{DIAL_SIZE + 2 * margin, DIAL_SIZE + 2 * margin, QImage::Format_ARGB32_Premultiplied};
        image.fill(Qt::transparent);
        QPainter painter{&image};
        dial.paint(&painter);
        return image;
    }};
    const QPoint offset{margin, margin};
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.90, {0.25, 0.50, 0.75});
    dial.setBlockLayer(BlockClockDial::NewestBlock);
    dial.setHeadColor(Qt::white);
    dial.setRingMargin(margin);
    dial.setWidth(DIAL_SIZE + 2 * margin);
    dial.setHeight(DIAL_SIZE + 2 * margin);
    QCOMPARE(render(dial).pixelColor(DialPoint(0.75) + offset).alpha(), 0);

    // A flash where the newest block ends and the block being built begins.
    dial.setFoundFlash(1.0);
    QVERIFY(ColorDistance(render(dial).pixelColor(DialPoint(0.75) + offset), Qt::white) < 10);
    dial.setFoundFlash(0.0);

    // A light at the front of the confirmation wave, halfway back to the
    // start of the oldest block.
    dial.setConfirmationWave(0.5);
    const QImage image{render(dial)};
    QVERIFY(image.pixelColor(DialPoint(0.375) + offset).alpha() > 100);
    QCOMPARE(image.pixelColor(DialPoint(0.125) + offset).alpha(), 0);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(BlockClockDialTests)
#else
QTEST_MAIN(BlockClockDialTests)
#endif
#include "test_blockclockdial.moc"
