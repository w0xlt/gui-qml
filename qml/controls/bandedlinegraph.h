// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_CONTROLS_BANDEDLINEGRAPH_H
#define BITCOIN_QML_CONTROLS_BANDEDLINEGRAPH_H

#include <QColor>
#include <QList>
#include <QQuickPaintedItem>
#include <QVariantList>

class QPainter;

//! A line through points, colored by value in bands, over dotted grid lines
//! and under a dashed reference line, like mempool.space's incoming
//! transactions chart. Points outside minX to maxX are left out.
class BandedLineGraph : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QList<qreal> xValues READ xValues WRITE setXValues NOTIFY pointsChanged)
    Q_PROPERTY(QList<qreal> yValues READ yValues WRITE setYValues NOTIFY pointsChanged)
    Q_PROPERTY(qreal minX READ minX WRITE setMinX NOTIFY rangeChanged)
    Q_PROPERTY(qreal maxX READ maxX WRITE setMaxX NOTIFY rangeChanged)
    //! The y axis runs from 0 at the bottom to maxY at the top.
    Q_PROPERTY(qreal maxY READ maxY WRITE setMaxY NOTIFY rangeChanged)
    //! The line is bandColors[i] between bandLimits[i - 1] and bandLimits[i],
    //! so there is one more color than limits.
    Q_PROPERTY(QList<qreal> bandLimits READ bandLimits WRITE setBandLimits NOTIFY styleChanged)
    Q_PROPERTY(QVariantList bandColors READ bandColors WRITE setBandColors NOTIFY styleChanged)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth NOTIFY styleChanged)
    Q_PROPERTY(QList<qreal> gridValues READ gridValues WRITE setGridValues NOTIFY styleChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY styleChanged)
    //! No reference line while 0.
    Q_PROPERTY(qreal referenceValue READ referenceValue WRITE setReferenceValue NOTIFY styleChanged)
    Q_PROPERTY(QColor referenceColor READ referenceColor WRITE setReferenceColor NOTIFY styleChanged)

public:
    explicit BandedLineGraph(QQuickItem* parent = nullptr);
    void paint(QPainter* painter) override;

    QList<qreal> xValues() const { return m_x_values; }
    QList<qreal> yValues() const { return m_y_values; }
    qreal minX() const { return m_min_x; }
    qreal maxX() const { return m_max_x; }
    qreal maxY() const { return m_max_y; }
    QList<qreal> bandLimits() const { return m_band_limits; }
    QVariantList bandColors() const { return m_band_colors; }
    qreal lineWidth() const { return m_line_width; }
    QList<qreal> gridValues() const { return m_grid_values; }
    QColor gridColor() const { return m_grid_color; }
    qreal referenceValue() const { return m_reference_value; }
    QColor referenceColor() const { return m_reference_color; }

    void setXValues(const QList<qreal>& values);
    void setYValues(const QList<qreal>& values);
    void setMinX(qreal value);
    void setMaxX(qreal value);
    void setMaxY(qreal value);
    void setBandLimits(const QList<qreal>& limits);
    void setBandColors(const QVariantList& colors);
    void setLineWidth(qreal width);
    void setGridValues(const QList<qreal>& values);
    void setGridColor(const QColor& color);
    void setReferenceValue(qreal value);
    void setReferenceColor(const QColor& color);

Q_SIGNALS:
    void pointsChanged();
    void rangeChanged();
    void styleChanged();

private:
    qreal yFor(qreal value) const;

    QList<qreal> m_x_values;
    QList<qreal> m_y_values;
    qreal m_min_x{0};
    qreal m_max_x{0};
    qreal m_max_y{0};
    QList<qreal> m_band_limits;
    QVariantList m_band_colors;
    qreal m_line_width{3};
    QList<qreal> m_grid_values;
    QColor m_grid_color;
    qreal m_reference_value{0};
    QColor m_reference_color;
};

#endif // BITCOIN_QML_CONTROLS_BANDEDLINEGRAPH_H
