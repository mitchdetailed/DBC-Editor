#pragma once

#include "dbcmodel.h"

#include <QSet>
#include <QVector>
#include <QColor>
#include <QWidget>

// Custom-painted bit-layout grid for a CAN message.
// Columns: bit positions 7..0 (MSB to LSB, matching DBC convention).
// Rows   : byte numbers 0..DLC-1.
// Each cell shows the DBC bit index (bottom-right, small) and is filled
// with a distinct colour for every signal that occupies it.
class BitLayoutWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BitLayoutWidget(QWidget* parent = nullptr);

    // Pass nullptr to clear the display.
    void setMessage(const DbcMessage* message);

    QSize sizeHint()        const override;
    QSize minimumSizeHint() const override;

    // Highlight a signal by name; pass empty string to clear the selection.
    void setSelectedSignal(const QString& signalName);

signals:
    // Emitted when the user drags a signal to a new position.
    void signalMoved(const QString& signalName, int newStartBit);
    // Emitted when the user clicks on a signal cell (empty string = clicked empty cell).
    void signalClicked(const QString& signalName);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // ── layout constants ───────────────────────────────────────────────────
    static constexpr int kRowHeaderW  = 50;
    static constexpr int kMinCellW    = 40;   // minimum column width (pixels)
    static constexpr int kHeaderRowH  = 28;
    static constexpr int kRowH        = 52;

    // ── data ───────────────────────────────────────────────────────────────
    DbcMessage message_;
    bool       hasMessage_ = false;
    int        dlc_        = 0;

    // grid_[byte_row][bit_col 0..7] = signalIndex, -1 = empty
    QVector<QVector<int>> grid_;
    QVector<QColor>       signalColors_;

    // ── selection state ────────────────────────────────────────────────────
    int selectedSignalIndex_ = -1;  // -1 = none

    // ── drag state ─────────────────────────────────────────────────────────
    bool dragging_            = false;
    int  dragSignalIndex_     = -1;
    int  dragOrigStartBit_    = 0;   // original DBC startBit (MSB for Motorola, LSB for Intel)
    int  dragOrigLsb_         = 0;   // original LSB position (used for delta calculation)
    int  dragPreviewStartBit_ = -1;  // DBC startBit for preview (-1 = invalid)
    int  dragStartBitCol_     = 0;   // bitCol (0-7) of cell where drag began
    int  dragStartByteRow_    = 0;   // byte row where drag began

    // ── helpers ────────────────────────────────────────────────────────────
    void rebuild();
    QString hoveredSignalNameAt(const QPoint& pos) const;

    // Returns the set of DBC bit indices a signal occupies.
    // Index formula: byte_row * 8 + bit_col  (bit_col: 7=MSB/left, 0=LSB/right)
    static QSet<int> occupiedBits(const DbcSignal& sig);

    static const QColor kPalette[];
    static const int    kPaletteSize;
};
