// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/controls/bandedlinegraph.h>

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>

BandedLineGraph::BandedLineGraph(QQuickItem* parent)
    : QQuickPaintedItem{parent}
{
    setAntialiasing(true);
    connect(this, &BandedLineGraph::pointsChanged, this, [this] { update(); });
    connect(this, &BandedLineGraph::rangeChanged, this, [this] { update(); });
    connect(this, &BandedLineGraph::styleChanged, this, [this] { update(); });
}

void BandedLineGraph::setXValues(const QList<qreal>& values)
{
    if (m_x_values == values) return;
    m_x_values = values;
    Q_EMIT pointsChanged();
}

void BandedLineGraph::setYValues(const QList<qreal>& values)
{
    if (m_y_values == values) return;
    m_y_values = values;
    Q_EMIT pointsChanged();
}

void BandedLineGraph::setMinX(qreal value)
{
    if (m_min_x == value) return;
    m_min_x = value;
    Q_EMIT rangeChanged();
}

void BandedLineGraph::setMaxX(qreal value)
{
    if (m_max_x == value) return;
    m_max_x = value;
    Q_EMIT rangeChanged();
}

void BandedLineGraph::setMaxY(qreal value)
{
    if (m_max_y == value) return;
    m_max_y = value;
    Q_EMIT rangeChanged();
}

void BandedLineGraph::setBandLimits(const QList<qreal>& limits)
{
    if (m_band_limits == limits) return;
    m_band_limits = limits;
    Q_EMIT styleChanged();
}

void BandedLineGraph::setBandColors(const QVariantList& colors)
{
    if (m_band_colors == colors) return;
    m_band_colors = colors;
    Q_EMIT styleChanged();
}

void BandedLineGraph::setLineWidth(qreal width)
{
    if (m_line_width == width) return;
    m_line_width = width;
    Q_EMIT styleChanged();
}

void BandedLineGraph::setGridValues(const QList<qreal>& values)
{
    if (m_grid_values == values) return;
    m_grid_values = values;
    Q_EMIT styleChanged();
}

void BandedLineGraph::setGridColor(const QColor& color)
{
    if (m_grid_color == color) return;
    m_grid_color = color;
    Q_EMIT styleChanged();
}

void BandedLineGraph::setReferenceValue(qreal value)
{
    if (m_reference_value == value) return;
    m_reference_value = value;
    Q_EMIT styleChanged();
}

void BandedLineGraph::setReferenceColor(const QColor& color)
{
    if (m_reference_color == color) return;
    m_reference_color = color;
    Q_EMIT styleChanged();
}

qreal BandedLineGraph::yFor(qreal value) const
{
    return height() - height() * value / m_max_y;
}

void BandedLineGraph::paint(QPainter* painter)
{
    if (m_max_y <= 0 || m_max_x <= m_min_x) return;
    painter->setRenderHint(QPainter::Antialiasing);

    painter->setPen(QPen{m_grid_color, 1, Qt::DotLine});
    for (const qreal value : m_grid_values) {
        const qreal y{yFor(value)};
        painter->drawLine(QPointF{0, y}, QPointF{width(), y});
    }

    QPainterPath path;
    const qsizetype count{std::min(m_x_values.size(), m_y_values.size())};
    for (qsizetype i = 0; i < count; ++i) {
        if (m_x_values[i] < m_min_x || m_x_values[i] > m_max_x) continue;
        const QPointF point{(m_x_values[i] - m_min_x) / (m_max_x - m_min_x) * width(), yFor(m_y_values[i])};
        if (path.elementCount() == 0) {
            path.moveTo(point);
        } else {
            path.lineTo(point);
        }
    }
    if (path.elementCount() > 0 && !m_band_colors.isEmpty()) {
        // A gradient from the bottom up with a sharp step at each limit, so
        // each part of the line takes the color of its value's band.
        QLinearGradient gradient{QPointF{0, height()}, QPointF{0, 0}};
        qreal from{0};
        for (qsizetype band = 0; band < m_band_colors.size(); ++band) {
            const QColor color{m_band_colors[band].value<QColor>()};
            const qreal to{band < m_band_limits.size() ? std::clamp(m_band_limits[band] / m_max_y, from, 1.0) : 1.0};
            gradient.setColorAt(from, color);
            gradient.setColorAt(to, color);
            from = std::nextafter(to, 2.0);
            if (from > 1) break;
        }
        QPen line{QBrush{gradient}, m_line_width};
        line.setJoinStyle(Qt::RoundJoin);
        line.setCapStyle(Qt::RoundCap);
        painter->setPen(line);
        painter->drawPath(path);
    }

    if (m_reference_value > 0 && m_reference_value <= m_max_y) {
        QPen reference{m_reference_color, 2, Qt::DashLine};
        painter->setPen(reference);
        const qreal y{yFor(m_reference_value)};
        painter->drawLine(QPointF{0, y}, QPointF{width(), y});
    }
}
