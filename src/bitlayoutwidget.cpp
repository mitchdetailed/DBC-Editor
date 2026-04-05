#include "bitlayoutwidget.h"

#include <QHash>
#include <QMouseEvent>
#include <QPainter>
#include <QSizePolicy>
#include <QToolTip>

// ── signal colour palette ──────────────────────────────────────────────────
const QColor BitLayoutWidget::kPalette[] = {
    QColor(100, 149, 237, 210),   // cornflower blue
    QColor(205,  92,  92, 210),   // indian red
    QColor( 60, 179, 113, 210),   // medium sea green
    QColor(218, 165,  32, 210),   // goldenrod
    QColor(147, 112, 219, 210),   // medium purple
    QColor( 32, 178, 170, 210),   // light sea green
    QColor(210, 105,  30, 210),   // chocolate
    QColor( 70, 130, 180, 210),   // steel blue
    QColor(188, 143, 143, 210),   // rosy brown
    QColor(102, 205, 170, 210),   // medium aquamarine
};
const int BitLayoutWidget::kPaletteSize =
    static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

// ── ctor ──────────────────────────────────────────────────────────────────
BitLayoutWidget::BitLayoutWidget(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kHeaderRowH + 2 * kRowH);
    setMinimumWidth(kRowHeaderW + 8 * kMinCellW);
    setMouseTracking(true);
}

// ── public interface ──────────────────────────────────────────────────────
void BitLayoutWidget::setMessage(const DbcMessage* message)
{
    // Cancel any in-progress drag when the message changes.
    dragging_            = false;
    dragSignalIndex_     = -1;
    dragOrigStartBit_    = 0;
    dragOrigLsb_         = 0;
    dragPreviewStartBit_ = -1;
    selectedSignalIndex_ = -1;
    unsetCursor();

    hasMessage_ = (message != nullptr);
    if (hasMessage_) {
        message_ = *message;
        dlc_     = qBound(0, message_.dlc, 64);
    } else {
        message_ = DbcMessage{};
        dlc_     = 0;
    }
    rebuild();
    setFixedHeight(kHeaderRowH + qMax(dlc_, 1) * kRowH);
    updateGeometry();
    update();
}

void BitLayoutWidget::setSelectedSignal(const QString& signalName)
{
    int found = -1;
    if (!signalName.isEmpty()) {
        for (int i = 0; i < message_.signalList.size(); ++i) {
            if (message_.signalList.at(i).name == signalName) {
                found = i;
                break;
            }
        }
    }
    if (found == selectedSignalIndex_) {
        return;
    }
    selectedSignalIndex_ = found;
    update();
}

QSize BitLayoutWidget::sizeHint() const
{
    return {kRowHeaderW + 8 * kMinCellW, height()};
}

QSize BitLayoutWidget::minimumSizeHint() const
{
    return {kRowHeaderW + 8 * kMinCellW, kHeaderRowH + 2 * kRowH};
}

// ── private ───────────────────────────────────────────────────────────────
void BitLayoutWidget::rebuild()
{
    grid_.clear();
    signalColors_.clear();

    if (!hasMessage_ || dlc_ <= 0) {
        return;
    }

    grid_.resize(dlc_, QVector<int>(8, -1));
    signalColors_.resize(message_.signalList.size());

    // Two-pass fill: background signals first, dragged signal last.
    // This guarantees the dragged signal always wins any cell conflict,
    // visually appearing "on top" when it overlaps other signals.
    for (int pass = 0; pass < 2; ++pass) {
        for (int si = 0; si < message_.signalList.size(); ++si) {
            signalColors_[si] = kPalette[si % kPaletteSize];

            const bool isDragged = dragging_ && si == dragSignalIndex_;

            // Pass 0: background signals only. Pass 1: dragged signal only.
            if (pass == 0 && isDragged) { continue; }
            if (pass == 1 && !isDragged) { continue; }

            DbcSignal sig = message_.signalList.at(si);
            // During drag, render the dragged signal at its preview position.
            if (isDragged && dragPreviewStartBit_ >= 0) {
                sig.startBit = dragPreviewStartBit_;
            }
            const QSet<int> bits = occupiedBits(sig);
            for (int b : bits) {
                const int byteRow = b / 8;
                const int bitCol  = b % 8;
                if (byteRow >= 0 && byteRow < dlc_) {
                    grid_[byteRow][bitCol] = si;
                }
            }
        }
    }
}

QSet<int> BitLayoutWidget::occupiedBits(const DbcSignal& sig)
{
    QSet<int> result;
    const bool motorola =
        sig.byteOrder.compare("Motorola", Qt::CaseInsensitive) == 0;

    if (!motorola) {
        // Intel / little-endian: startBit = LSB index, ascend continuously.
        for (int i = 0; i < sig.bitLength; ++i) {
            result.insert(sig.startBit + i);
        }
    } else {
        // Motorola / big-endian: startBit = MSB.
        // Traverse right within a byte then jump to MSB of the next byte.
        int cur = sig.startBit;
        for (int i = 0; i < sig.bitLength; ++i) {
            result.insert(cur);
            if (cur % 8 == 0) {        // reached LSB of this byte
                cur = cur + 15;        // → MSB of next byte  (cur+8+7)
            } else {
                cur -= 1;
            }
        }
    }
    return result;
}

// Returns the LSB bit position for a signal.
static int lsbFromSignal(const DbcSignal& sig) { return dbcSignalLsb(sig); }

// For Motorola: given the LSB position, reverse-traverse to find the MSB (DBC startBit).
static int startBitFromLsb(const DbcSignal& sig, int lsb) { return dbcStartBitFromLsb(sig, lsb); }

// ── paintEvent ────────────────────────────────────────────────────────────
void BitLayoutWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Compute actual column width from available width; respect minimum.
    const int colW  = qMax((width() - kRowHeaderW) / 8, kMinCellW);
    const int gridW = kRowHeaderW + 8 * colW;

    const QColor kHeaderBg(210, 210, 215);
    const QColor kCellBg  (255, 255, 255);
    const QColor kGridLine(160, 160, 160);
    const QColor kIdxColor(130, 130, 130);

    QFont baseFont = font();
    QFont smallFont = font();
    smallFont.setPointSize(qMax(baseFont.pointSize() - 1, 6));

    // ── column header row ─────────────────────────────────────────────────
    p.fillRect(0, 0, gridW, kHeaderRowH, kHeaderBg);
    p.setPen(Qt::black);
    p.setFont(baseFont);
    p.drawText(QRect(0, 0, kRowHeaderW, kHeaderRowH), Qt::AlignCenter, "Byte");
    for (int col = 7; col >= 0; --col) {
        const int x = kRowHeaderW + (7 - col) * colW;
        p.drawText(QRect(x, 0, colW, kHeaderRowH),
                   Qt::AlignCenter, QString::number(col));
    }

    // ── placeholder when no message is loaded ─────────────────────────────
    if (!hasMessage_ || dlc_ <= 0) {
        p.setPen(kIdxColor);
        p.drawText(QRect(0, kHeaderRowH, width(), height() - kHeaderRowH),
                   Qt::AlignCenter, "No message selected");
        return;
    }

    // ── byte rows ─────────────────────────────────────────────────────────
    for (int row = 0; row < dlc_; ++row) {
        const int y = kHeaderRowH + row * kRowH;

        // Row header (byte number)
        p.fillRect(0, y, kRowHeaderW, kRowH, kHeaderBg);
        p.setPen(Qt::black);
        p.setFont(baseFont);
        p.drawText(QRect(0, y, kRowHeaderW, kRowH),
                   Qt::AlignCenter, QString::number(row));

        // ── bit cells ──────────────────────────────────────────────────────
        for (int col = 7; col >= 0; --col) {
            const int x = kRowHeaderW + (7 - col) * colW;
            const QRect cellRect(x, y, colW, kRowH);

            // Background: signal colour or plain white
            const int si = grid_[row][col];
            if (si >= 0 && si < signalColors_.size()) {
                p.fillRect(cellRect, signalColors_[si]);
            } else {
                p.fillRect(cellRect, kCellBg);
            }

            // DBC bit index (bottom-right, small)
            const int bitIndex = row * 8 + col;
            p.setPen(kIdxColor);
            p.setFont(smallFont);
            p.drawText(cellRect.adjusted(0, 0, -3, -2),
                       Qt::AlignBottom | Qt::AlignRight,
                       QString::number(bitIndex));

            // Cell border
            p.setPen(kGridLine);
            p.drawRect(cellRect.adjusted(0, 0, -1, -1));
        }

        // ── signal name labels (drawn as spans per row) ────────────────────
        // Find each signal's leftmost + rightmost occupied col in this row.
        // "leftmost" = highest bit column number (MSB side, visual left).
        struct Span { int leftCol; int rightCol; };
        QHash<int, Span> spans;

        for (int col = 7; col >= 0; --col) {
            const int si = grid_[row][col];
            if (si < 0) {
                continue;
            }
            if (!spans.contains(si)) {
                spans[si] = {col, col};
            } else {
                if (col > spans[si].leftCol) {
                    spans[si].leftCol = col;
                }
                if (col < spans[si].rightCol) {
                    spans[si].rightCol = col;
                }
            }
        }

        p.setFont(baseFont);
        for (auto it = spans.cbegin(); it != spans.cend(); ++it) {
            const int si       = it.key();
            const int leftCol  = it.value().leftCol;
            const int rightCol = it.value().rightCol;

            const int xLeft  = kRowHeaderW + (7 - leftCol)  * colW;
            const int xRight = kRowHeaderW + (7 - rightCol) * colW + colW;
            // Upper half of the cell for the label
            const QRect labelRect(xLeft + 2, y + 2, xRight - xLeft - 4, kRowH / 2 - 2);

            const QString name = (si < message_.signalList.size())
                                     ? message_.signalList.at(si).name
                                     : QString{};
            p.setPen(Qt::black);
            const QString elided =
                p.fontMetrics().elidedText(name, Qt::ElideRight, labelRect.width());
            p.drawText(labelRect,
                       Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                       elided);
        }
    }

    // Outer border
    p.setPen(kGridLine);
    p.drawRect(0, 0, gridW - 1, kHeaderRowH + dlc_ * kRowH - 1);

    // ── selected signal outline ───────────────────────────────────────────
    // Draw a thick dark border around the outer edges of the selected signal's
    // cells so it stands out clearly from the regular gray grid lines.
    if (selectedSignalIndex_ >= 0) {
        const QColor kSelBorder(40, 40, 40);
        p.setPen(QPen(kSelBorder, 2));

        for (int row = 0; row < dlc_; ++row) {
            const int y = kHeaderRowH + row * kRowH;
            for (int col = 7; col >= 0; --col) {
                if (grid_[row][col] != selectedSignalIndex_) {
                    continue;
                }
                const int x = kRowHeaderW + (7 - col) * colW;

                // top edge
                if (row == 0 || grid_[row - 1][col] != selectedSignalIndex_)
                    p.drawLine(x, y, x + colW, y);
                // bottom edge
                if (row == dlc_ - 1 || grid_[row + 1][col] != selectedSignalIndex_)
                    p.drawLine(x, y + kRowH, x + colW, y + kRowH);
                // visual-left edge (col + 1 is left in screen space)
                if (col == 7 || grid_[row][col + 1] != selectedSignalIndex_)
                    p.drawLine(x, y, x, y + kRowH);
                // visual-right edge (col - 1 is right in screen space)
                if (col == 0 || grid_[row][col - 1] != selectedSignalIndex_)
                    p.drawLine(x + colW, y, x + colW, y + kRowH);
            }
        }
    }
}

void BitLayoutWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !hasMessage_ || dlc_ <= 0) {
        QWidget::mousePressEvent(event);
        return;
    }

    const int colW = qMax((width() - kRowHeaderW) / 8, kMinCellW);
    const QPoint pos = event->pos();
    if (pos.x() < kRowHeaderW || pos.y() < kHeaderRowH) {
        QWidget::mousePressEvent(event);
        return;
    }

    const int visualCol = (pos.x() - kRowHeaderW) / colW;
    const int row       = (pos.y() - kHeaderRowH) / kRowH;
    if (visualCol < 0 || visualCol >= 8 || row < 0 || row >= dlc_) {
        QWidget::mousePressEvent(event);
        return;
    }

    const int bitCol = 7 - visualCol;
    const int si     = grid_[row][bitCol];

    // Update selection and notify MainWindow regardless of whether it becomes a drag.
    const QString clickedName = (si >= 0 && si < message_.signalList.size())
                                    ? message_.signalList.at(si).name : QString{};
    if (selectedSignalIndex_ != si) {
        selectedSignalIndex_ = si;
        update();
    }
    emit signalClicked(clickedName);

    if (si < 0) {
        QWidget::mousePressEvent(event);
        return;
    }

    dragSignalIndex_     = si;
    dragOrigStartBit_    = message_.signalList.at(si).startBit;
    dragOrigLsb_         = lsbFromSignal(message_.signalList.at(si));
    dragPreviewStartBit_ = dragOrigStartBit_;
    dragStartBitCol_     = bitCol;
    dragStartByteRow_    = row;
    dragging_            = true;
    setCursor(Qt::ClosedHandCursor);
}

void BitLayoutWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging_) {
        const int colW = qMax((width() - kRowHeaderW) / 8, kMinCellW);
        const QPoint pos = event->pos();

        // Clamp to valid grid area.
        const int visualCol = qBound(0, (pos.x() - kRowHeaderW) / colW, 7);
        const int row       = qBound(0, (pos.y() - kHeaderRowH) / kRowH, dlc_ - 1);
        const int bitCol    = 7 - visualCol;

        const int deltaBitCol = bitCol - dragStartBitCol_;
        const int deltaRow    = row   - dragStartByteRow_;
        const int newLsb      = dragOrigLsb_ + deltaRow * 8 + deltaBitCol;

        // Validate: convert new LSB to DBC startBit, check all occupied bits fit.
        DbcSignal testSig = message_.signalList.at(dragSignalIndex_);
        const int newStartBit = startBitFromLsb(testSig, newLsb);
        testSig.startBit  = newStartBit;
        const QSet<int> bits = occupiedBits(testSig);
        bool valid = !bits.isEmpty();
        for (int b : bits) {
            if (b < 0 || b >= dlc_ * 8) {
                valid = false;
                break;
            }
        }

        dragPreviewStartBit_ = valid ? newStartBit : -1;
        rebuild();
        update();
        return;
    }

    const QString signalName = hoveredSignalNameAt(event->pos());
    if (signalName.isEmpty()) {
        QToolTip::hideText();
    } else {
        QToolTip::showText(mapToGlobal(event->pos() + QPoint(14, 18)), signalName, this);
    }

    QWidget::mouseMoveEvent(event);
}

void BitLayoutWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (!dragging_ || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    dragging_ = false;
    unsetCursor();

    if (dragPreviewStartBit_ >= 0 && dragPreviewStartBit_ != dragOrigStartBit_) {
        const QString name = message_.signalList.at(dragSignalIndex_).name;
        emit signalMoved(name, dragPreviewStartBit_);
    } else {
        // Snap back to original position.
        dragPreviewStartBit_ = -1;
        rebuild();
        update();
    }

    dragSignalIndex_ = -1;
}

void BitLayoutWidget::leaveEvent(QEvent* event)
{
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}

QString BitLayoutWidget::hoveredSignalNameAt(const QPoint& pos) const
{
    if (!hasMessage_ || dlc_ <= 0) {
        return {};
    }

    if (pos.y() < kHeaderRowH || pos.x() < kRowHeaderW) {
        return {};
    }

    const int colW = qMax((width() - kRowHeaderW) / 8, kMinCellW);
    const int relX = pos.x() - kRowHeaderW;
    const int relY = pos.y() - kHeaderRowH;

    const int visualCol = relX / colW;
    const int row = relY / kRowH;

    if (visualCol < 0 || visualCol >= 8 || row < 0 || row >= grid_.size()) {
        return {};
    }

    const int bitCol = 7 - visualCol;
    const int signalIndex = grid_[row][bitCol];
    if (signalIndex < 0 || signalIndex >= message_.signalList.size()) {
        return {};
    }

    return message_.signalList.at(signalIndex).name;
}
