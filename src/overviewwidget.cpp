#include "overviewwidget.h"
#include "dbcdialogs.h"
#include "dbcutil.h"

#include <QApplication>
#include <QComboBox>
#include <QDrag>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <limits>

OverviewWidget::OverviewWidget(DbcDatabase* db, QWidget* parent)
    : QWidget(parent), db_(db)
{
    // ── Message table ─────────────────────────────────────────────────────────
    messageTable_ = new QTableWidget(this);
    messageTable_->setColumnCount(8);
    messageTable_->setHorizontalHeaderLabels({"Name", "Type", "ID", "DLC", "Cycle Time",
                                               "Transmitter", "Comment", "Attributes"});
    messageTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    messageTable_->horizontalHeader()->setStretchLastSection(true);
    messageTable_->horizontalHeader()->setSectionsClickable(true);
    messageTable_->horizontalHeader()->setSortIndicatorShown(true);
    messageTable_->horizontalHeader()->setSectionsMovable(true);
    messageTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    messageTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    messageTable_->setSortingEnabled(true);
    messageTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(messageTable_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu contextMenu(this);
        auto* addAct = contextMenu.addAction("Add Message");
        connect(addAct, &QAction::triggered, this, &OverviewWidget::requestAddMessage);
        const QModelIndexList selectedRows = messageTable_->selectionModel()
            ? messageTable_->selectionModel()->selectedRows() : QModelIndexList{};
        if (!selectedRows.isEmpty()) {
            contextMenu.addSeparator();
            auto* delAct = contextMenu.addAction("Delete Message");
            connect(delAct, &QAction::triggered, this, [this]() {
                const QModelIndexList sel = messageTable_->selectionModel()
                    ? messageTable_->selectionModel()->selectedRows() : QModelIndexList{};
                if (sel.isEmpty()) { return; }
                auto* item = messageTable_->item(sel.first().row(), 0);
                if (!item) { return; }
                const int msgIndex = item->data(Qt::UserRole).toInt();
                if (msgIndex < 0 || msgIndex >= db_->messages.size()) { return; }
                db_->messages.remove(msgIndex);
                if (db_->messages.isEmpty()) {
                    currentMessageIndex_ = -1;
                } else if (currentMessageIndex_ >= db_->messages.size()) {
                    currentMessageIndex_ = db_->messages.size() - 1;
                }
                emit dataModified();
            });
        }
        contextMenu.exec(messageTable_->viewport()->mapToGlobal(pos));
    });
    connect(messageTable_, &QTableWidget::itemSelectionChanged,
            this, &OverviewWidget::onMessageSelectionChanged);
    connect(messageTable_, &QTableWidget::itemChanged,
            this, &OverviewWidget::onMessageTableItemChanged);
    connect(messageTable_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        auto* nameItem = messageTable_->item(row, 0);
        if (!nameItem) { return; }
        const int msgIndex = nameItem->data(Qt::UserRole).toInt();
        if (msgIndex < 0 || msgIndex >= db_->messages.size()) { return; }
        if (col == 6) {
            const QString current = db_->messages.at(msgIndex).comment;
            openCommentEditor(this, current, [this, row, msgIndex](const QString& text) {
                if (msgIndex < 0 || msgIndex >= db_->messages.size()) { return; }
                db_->messages[msgIndex].comment = text;
                auto* cell = messageTable_->item(row, 6);
                if (cell) { const QSignalBlocker b(messageTable_); cell->setText(text); }
                emit dataModified();
            });
        } else if (col == 7) {
            DbcMessage& msg = db_->messages[msgIndex];
            QList<DbcAttributeDef> defs;
            for (const DbcAttributeDef& d : db_->attributes)
                if (d.objectType == DbcAttributeDef::ObjectType::Message) defs.append(d);
            ObjectAttrDialog dlg(
                QString("Attributes \u2013 Message: %1").arg(msg.name),
                defs, msg.attrValues, this);
            if (dlg.exec() == QDialog::Accepted) {
                msg.attrValues = dlg.result();
                emit dataModified();
            }
        }
    });

    // ── Signal table ──────────────────────────────────────────────────────────
    signalTable_ = new QTableWidget(this);
    signalTable_->setColumnCount(14);
    signalTable_->setHorizontalHeaderLabels({"Name", "Mode", "Startbit", "Length [Bit]",
                                              "Byte Order", "Type", "Factor", "Offset",
                                              "Minimum", "Maximum", "Unit", "Comment",
                                              "Value Table", "Attributes"});
    signalTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    signalTable_->horizontalHeader()->setStretchLastSection(true);
    signalTable_->horizontalHeader()->setSectionsClickable(true);
    signalTable_->horizontalHeader()->setSortIndicatorShown(true);
    signalTable_->horizontalHeader()->setSectionsMovable(true);
    signalTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    signalTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    signalTable_->setSortingEnabled(true);
    signalTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(signalTable_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu contextMenu(this);
        auto* addAct = contextMenu.addAction("Add Signal");
        connect(addAct, &QAction::triggered, this, &OverviewWidget::requestAddSignal);
        const QModelIndexList selected = signalTable_->selectionModel()
            ? signalTable_->selectionModel()->selectedRows() : QModelIndexList{};
        if (!selected.isEmpty()) {
            contextMenu.addSeparator();
            auto* calcAction = new QAction("Calculate Min / Max", &contextMenu);
            connect(calcAction, &QAction::triggered, this, [this, selected]() {
                if (currentMessageIndex_ < 0 || currentMessageIndex_ >= db_->messages.size()) { return; }
                const int row = selected.first().row();
                QTableWidgetItem* ni = signalTable_->item(row, 0);
                if (!ni) { return; }
                const int sigIndex = ni->data(Qt::UserRole).toInt();
                DbcMessage& msg = db_->messages[currentMessageIndex_];
                if (sigIndex < 0 || sigIndex >= msg.signalList.size()) { return; }
                DbcSignal& sig = msg.signalList[sigIndex];

                double rawMin = 0.0, rawMax = 0.0;
                const int bits = sig.bitLength;
                if (sig.valueType == "Unsigned") {
                    rawMin = 0.0;
                    rawMax = (bits >= 64) ? 1.8446744073709552e19
                                         : static_cast<double>((quint64(1) << bits) - 1);
                } else if (sig.valueType == "Signed") {
                    const int clampedBits = qMin(bits, 63);
                    rawMin = -static_cast<double>(qint64(1) << (clampedBits - 1));
                    rawMax =  static_cast<double>((qint64(1) << (clampedBits - 1)) - 1);
                } else if (sig.valueType == "Float") {
                    rawMin = -static_cast<double>(std::numeric_limits<float>::max());
                    rawMax =  static_cast<double>(std::numeric_limits<float>::max());
                } else {
                    rawMin = -std::numeric_limits<double>::max();
                    rawMax =  std::numeric_limits<double>::max();
                }

                if (sig.factor >= 0.0) {
                    sig.minimum = rawMin * sig.factor + sig.offset;
                    sig.maximum = rawMax * sig.factor + sig.offset;
                } else {
                    sig.minimum = rawMax * sig.factor + sig.offset;
                    sig.maximum = rawMin * sig.factor + sig.offset;
                }
                emit dataModified();

                const QSignalBlocker b(signalTable_);
                if (auto* minItem = signalTable_->item(row, 8)) { minItem->setText(QString::number(sig.minimum, 'g', 10)); }
                if (auto* maxItem = signalTable_->item(row, 9)) { maxItem->setText(QString::number(sig.maximum, 'g', 10)); }
            });
            contextMenu.addAction(calcAction);
            contextMenu.addSeparator();
            auto* removeAction = new QAction("Remove Signal", &contextMenu);
            connect(removeAction, &QAction::triggered, this, [this]() {
                const int sigIdx = currentSelectedSignalIndex();
                if (sigIdx < 0 || currentMessageIndex_ < 0 || currentMessageIndex_ >= db_->messages.size()) { return; }
                auto& sl = db_->messages[currentMessageIndex_].signalList;
                if (sigIdx < sl.size()) {
                    sl.remove(sigIdx);
                    emit dataModified();
                }
            });
            contextMenu.addAction(removeAction);
        }
        contextMenu.exec(signalTable_->viewport()->mapToGlobal(pos));
    });
    connect(signalTable_, &QTableWidget::itemChanged,
            this, &OverviewWidget::onSignalTableItemChanged);
    connect(signalTable_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (col == 11) {
            if (currentMessageIndex_ < 0 || currentMessageIndex_ >= db_->messages.size()) { return; }
            auto* nameItem = signalTable_->item(row, 0);
            if (!nameItem) { return; }
            const QString sigName = nameItem->text();
            auto& signalList = db_->messages[currentMessageIndex_].signalList;
            int si = -1;
            for (int i = 0; i < signalList.size(); ++i) {
                if (signalList.at(i).name == sigName) { si = i; break; }
            }
            if (si < 0) { return; }
            const QString current = signalList.at(si).comment;
            openCommentEditor(this, current, [this, row, si](const QString& text) {
                if (currentMessageIndex_ < 0 || currentMessageIndex_ >= db_->messages.size()) { return; }
                auto& sl = db_->messages[currentMessageIndex_].signalList;
                if (si < 0 || si >= sl.size()) { return; }
                sl[si].comment = text;
                auto* cell = signalTable_->item(row, 11);
                if (cell) { const QSignalBlocker b(signalTable_); cell->setText(text); }
                emit dataModified();
            });
        } else if (col == 13) {
            if (currentMessageIndex_ < 0 || currentMessageIndex_ >= db_->messages.size()) { return; }
            auto* nameItem = signalTable_->item(row, 0);
            if (!nameItem) { return; }
            const QString sigName = nameItem->text();
            DbcMessage& msg = db_->messages[currentMessageIndex_];
            for (DbcSignal& sig : msg.signalList) {
                if (sig.name != sigName) { continue; }
                QList<DbcAttributeDef> defs;
                for (const DbcAttributeDef& d : db_->attributes)
                    if (d.objectType == DbcAttributeDef::ObjectType::Signal) defs.append(d);
                ObjectAttrDialog dlg(
                    QString("Attributes \u2013 Signal: %1 (in %2)").arg(sigName, msg.name),
                    defs, sig.attrValues, this);
                if (dlg.exec() == QDialog::Accepted) {
                    sig.attrValues = dlg.result();
                    emit dataModified();
                }
                break;
            }
        }
    });

    signalTable_->viewport()->installEventFilter(this);
    signalTable_->setStyleSheet(
        "QTableWidget::item:selected {"
        "    background-color: #2979CC;"
        "    color: white;"
        "}"
        "QTableWidget::item:focus {"
        "    background-color: #1565C0;"
        "    color: white;"
        "    border: 2px solid #FFC107;"
        "}"
        "QComboBox:focus {"
        "    border: 2px solid #FFC107;"
        "    outline: none;"
        "}"
    );

    // ── Panels + splitter ─────────────────────────────────────────────────────
    auto* messageTablePanel = new QWidget(this);
    auto* messageTableLayout = new QVBoxLayout(messageTablePanel);
    messageTableLayout->setContentsMargins(0, 0, 0, 0);
    messageTableLayout->setSpacing(0);
    auto* messageTableHeader = new QLabel("CAN Messages", messageTablePanel);
    QFont messageHeaderFont = messageTableHeader->font();
    messageHeaderFont.setBold(true);
    messageTableHeader->setFont(messageHeaderFont);
    messageTableHeader->setContentsMargins(4, 4, 4, 4);
    messageTableLayout->addWidget(messageTableHeader);
    messageTableLayout->addWidget(messageTable_);

    auto* signalTablePanel = new QWidget(this);
    auto* signalTableLayout = new QVBoxLayout(signalTablePanel);
    signalTableLayout->setContentsMargins(0, 0, 0, 0);
    signalTableLayout->setSpacing(0);
    auto* signalTableHeader = new QLabel("Signals of Selected CAN Message", signalTablePanel);
    QFont signalHeaderFont = signalTableHeader->font();
    signalHeaderFont.setBold(true);
    signalTableHeader->setFont(signalHeaderFont);
    signalTableHeader->setContentsMargins(4, 4, 4, 4);
    signalTableLayout->addWidget(signalTableHeader);
    signalTableLayout->addWidget(signalTable_);

    auto* tableSplitter = new QSplitter(Qt::Vertical, this);
    tableSplitter->addWidget(messageTablePanel);
    tableSplitter->addWidget(signalTablePanel);
    tableSplitter->setStretchFactor(0, 1);
    tableSplitter->setStretchFactor(1, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tableSplitter);

    // ── BitLayoutWidget + dock contents ───────────────────────────────────────
    bitLayoutWidget_ = new BitLayoutWidget();

    connect(bitLayoutWidget_, &BitLayoutWidget::signalMoved,
            this, [this](const QString& signalName, int newStartBit) {
        if (currentMessageIndex_ < 0 || currentMessageIndex_ >= db_->messages.size()) { return; }
        auto& msg = db_->messages[currentMessageIndex_];
        for (DbcSignal& sig : msg.signalList) {
            if (sig.name == signalName) {
                sig.startBit = newStartBit;
                break;
            }
        }
        emit dataModified();
    });

    connect(bitLayoutWidget_, &BitLayoutWidget::signalClicked,
            this, [this](const QString& signalName) {
        if (signalName.isEmpty()) {
            signalTable_->clearSelection();
            return;
        }
        const QSignalBlocker blocker(signalTable_);
        for (int row = 0; row < signalTable_->rowCount(); ++row) {
            QTableWidgetItem* nameItem = signalTable_->item(row, 0);
            if (nameItem && nameItem->text() == signalName) {
                signalTable_->selectRow(row);
                break;
            }
        }
    });

    connect(signalTable_, &QTableWidget::itemSelectionChanged,
            this, [this]() {
        const QModelIndexList indexes = signalTable_->selectionModel()
            ? signalTable_->selectionModel()->selectedRows() : QModelIndexList{};
        if (indexes.isEmpty()) {
            bitLayoutWidget_->setSelectedSignal({});
            return;
        }
        QTableWidgetItem* nameItem = signalTable_->item(indexes.first().row(), 0);
        bitLayoutWidget_->setSelectedSignal(nameItem ? nameItem->text() : QString{});
    });

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidget(bitLayoutWidget_);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    dockContentsWidget_ = new QWidget();
    auto* dockLayout = new QVBoxLayout(dockContentsWidget_);
    dockLayout->setContentsMargins(8, 8, 8, 8);
    dockLayout->setSpacing(8);

    auto* muxRow = new QHBoxLayout();
    auto* muxLabel = new QLabel("Multiplexor Signal", dockContentsWidget_);
    multiplexorSignalCombo_ = new QComboBox(dockContentsWidget_);
    multiplexorSignalCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(multiplexorSignalCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        refreshBitLayout();
    });
    muxRow->addWidget(muxLabel);
    muxRow->addWidget(multiplexorSignalCombo_, 1);

    dockLayout->addLayout(muxRow);
    dockLayout->addWidget(scrollArea, 1);
}

QWidget* OverviewWidget::dockContentsWidget() const
{
    return dockContentsWidget_;
}

void OverviewWidget::refresh()
{
    refreshMessageTable();
    refreshSignalTable();
    refreshMultiplexorSignalDropdown();
    refreshBitLayout();
}

void OverviewWidget::refreshBitLayout()
{
    if (currentMessageIndex_ < 0 || currentMessageIndex_ >= db_->messages.size()) {
        bitLayoutWidget_->setMessage(nullptr);
        return;
    }

    const DbcMessage& sourceMessage = db_->messages.at(currentMessageIndex_);

    if (!multiplexorSignalCombo_ || multiplexorSignalCombo_->currentIndex() <= 0) {
        DbcMessage filteredMessage = sourceMessage;
        filteredMessage.signalList.clear();
        for (const DbcSignal& sig : sourceMessage.signalList) {
            if (sig.mode.compare("M=", Qt::CaseInsensitive) != 0) {
                filteredMessage.signalList.append(sig);
            }
        }
        bitLayoutWidget_->setMessage(&filteredMessage);
        return;
    }

    bool selectedOk = false;
    const quint32 selectedMuxValue = multiplexorSignalCombo_->currentData().toUInt(&selectedOk);
    if (!selectedOk) {
        bitLayoutWidget_->setMessage(&sourceMessage);
        return;
    }

    DbcMessage filteredMessage = sourceMessage;
    filteredMessage.signalList.clear();
    for (const DbcSignal& sig : sourceMessage.signalList) {
        if (sig.mode.compare("Signal", Qt::CaseInsensitive) == 0 ||
            sig.mode.compare("Multiplexor", Qt::CaseInsensitive) == 0) {
            filteredMessage.signalList.append(sig);
            continue;
        }
        if (sig.mode.compare("M=", Qt::CaseInsensitive) == 0) {
            QString raw = sig.modeValueHex.trimmed();
            if (raw.startsWith("0x", Qt::CaseInsensitive)) { raw = raw.mid(2); }
            bool valueOk = false;
            const quint32 value = raw.toUInt(&valueOk, 16);
            if (valueOk && value == selectedMuxValue) {
                filteredMessage.signalList.append(sig);
            }
            continue;
        }
        filteredMessage.signalList.append(sig);
    }
    bitLayoutWidget_->setMessage(&filteredMessage);
}

void OverviewWidget::deleteSelected()
{
    const int sigIdx = currentSelectedSignalIndex();
    if (sigIdx >= 0 && currentMessageIndex_ >= 0 && currentMessageIndex_ < db_->messages.size()) {
        auto& msg = db_->messages[currentMessageIndex_];
        if (sigIdx < msg.signalList.size()) {
            msg.signalList.remove(sigIdx);
            emit dataModified();
            return;
        }
    }

    const QModelIndexList selectedMessageRows = messageTable_->selectionModel()
        ? messageTable_->selectionModel()->selectedRows() : QModelIndexList{};
    if (!selectedMessageRows.isEmpty()) {
        auto* item = messageTable_->item(selectedMessageRows.first().row(), 0);
        if (!item) { return; }
        const int msgIndex = item->data(Qt::UserRole).toInt();
        if (msgIndex >= 0 && msgIndex < db_->messages.size()) {
            db_->messages.remove(msgIndex);
            if (db_->messages.isEmpty()) {
                currentMessageIndex_ = -1;
            } else if (currentMessageIndex_ >= db_->messages.size()) {
                currentMessageIndex_ = db_->messages.size() - 1;
            }
            emit dataModified();
        }
    }
}

void OverviewWidget::setCurrentMessage(int index)
{
    if (index < 0 || index >= db_->messages.size()) {
        currentMessageIndex_ = -1;
    } else {
        currentMessageIndex_ = index;
    }

    refreshSignalTable();
    refreshMultiplexorSignalDropdown();
    refreshBitLayout();

    // Sync message table row selection.
    if (currentMessageIndex_ >= 0) {
        const QSignalBlocker blocker(messageTable_);
        for (int row = 0; row < messageTable_->rowCount(); ++row) {
            QTableWidgetItem* item = messageTable_->item(row, 0);
            if (item && item->data(Qt::UserRole).toInt() == currentMessageIndex_) {
                messageTable_->selectRow(row);
                break;
            }
        }
    }
}

int OverviewWidget::currentMessageIndex() const
{
    return currentMessageIndex_;
}

int OverviewWidget::currentSelectedSignalIndex() const
{
    const QModelIndexList indexes = signalTable_->selectionModel()
        ? signalTable_->selectionModel()->selectedRows() : QModelIndexList{};
    if (indexes.isEmpty()) { return -1; }
    QTableWidgetItem* item = signalTable_->item(indexes.first().row(), 0);
    if (!item) { return -1; }
    return item->data(Qt::UserRole).toInt();
}

void OverviewWidget::onMessageSelectionChanged()
{
    const QModelIndexList selectedRows = messageTable_->selectionModel()
        ? messageTable_->selectionModel()->selectedRows() : QModelIndexList{};
    if (selectedRows.isEmpty()) {
        currentMessageIndex_ = -1;
        refreshSignalTable();
        refreshMultiplexorSignalDropdown();
        refreshBitLayout();
        emit messageSelected(-1);
        return;
    }

    const int row = selectedRows.first().row();
    QTableWidgetItem* item = messageTable_->item(row, 0);
    if (!item) {
        currentMessageIndex_ = -1;
        refreshSignalTable();
        refreshMultiplexorSignalDropdown();
        refreshBitLayout();
        emit messageSelected(-1);
        return;
    }

    currentMessageIndex_ = item->data(Qt::UserRole).toInt();
    refreshSignalTable();
    refreshMultiplexorSignalDropdown();
    refreshBitLayout();
    emit messageSelected(currentMessageIndex_);
}

void OverviewWidget::onMessageTableItemChanged(QTableWidgetItem* item)
{
    if (!item || item->column() != 0) { return; }

    QTableWidgetItem* nameItem = messageTable_->item(item->row(), 0);
    if (!nameItem) { return; }
    const int msgIndex = nameItem->data(Qt::UserRole).toInt();
    if (msgIndex < 0 || msgIndex >= db_->messages.size()) { return; }

    DbcMessage& msg = db_->messages[msgIndex];
    const QString text = item->text().trimmed();
    if (text == msg.name) { return; }

    if (!isValidSymbolName(text)) {
        const QSignalBlocker b(messageTable_);
        item->setText(msg.name);
        QMessageBox::warning(this, "Invalid Message Name",
            QString("'%1' is not a valid message name.\n\n"
                    "Names must start with a letter and contain only\n"
                    "letters, digits, and underscores (A-Z, 0-9, _).").arg(text));
        return;
    }

    for (int mi = 0; mi < db_->messages.size(); ++mi) {
        if (mi == msgIndex) { continue; }
        if (db_->messages.at(mi).name == text) {
            const QSignalBlocker b(messageTable_);
            item->setText(msg.name);
            QMessageBox::warning(this, "Duplicate Message Name",
                QString("A message named '%1' already exists.").arg(text));
            return;
        }
    }

    msg.name = text;
    emit dataModified();
}

void OverviewWidget::onSignalTableItemChanged(QTableWidgetItem* item)
{
    if (!item) { return; }

    const int col = item->column();
    if (col != 0 && col != 2 && col != 3 && col != 8 && col != 9) { return; }
    if (currentMessageIndex_ < 0 || currentMessageIndex_ >= db_->messages.size()) { return; }

    QTableWidgetItem* nameItem = signalTable_->item(item->row(), 0);
    if (!nameItem) { return; }
    const int sigIndex = nameItem->data(Qt::UserRole).toInt();

    DbcMessage& msg = db_->messages[currentMessageIndex_];
    if (sigIndex < 0 || sigIndex >= msg.signalList.size()) { return; }

    DbcSignal& sig = msg.signalList[sigIndex];
    const QString text = item->text().trimmed();
    bool needsLayoutRefresh = false;

    if (col == 0) {
        if (text == sig.name) { return; }
        if (!isValidSymbolName(text)) {
            const QSignalBlocker b(signalTable_);
            item->setText(sig.name);
            QMessageBox::warning(this, "Invalid Signal Name",
                QString("'%1' is not a valid signal name.\n\n"
                        "Names must start with a letter and contain only\n"
                        "letters, digits, and underscores (A-Z, 0-9, _).").arg(text));
            return;
        }
        sig.name = text;
        needsLayoutRefresh = true;

    } else if (col == 2) {
        bool ok = false;
        const int lsb = text.toInt(&ok);
        const int currentLsb = dbcSignalLsb(sig);
        if (!ok || lsb < 0 || lsb == currentLsb) {
            const QSignalBlocker b(signalTable_);
            item->setText(QString::number(currentLsb));
            return;
        }
        sig.startBit = dbcStartBitFromLsb(sig, lsb);
        needsLayoutRefresh = true;

    } else if (col == 3) {
        bool ok = false;
        const int len = text.toInt(&ok);
        if (!ok || len < 1 || len == sig.bitLength) {
            const QSignalBlocker b(signalTable_);
            item->setText(QString::number(sig.bitLength));
            return;
        }
        const int savedLsb = dbcSignalLsb(sig);
        sig.bitLength = len;
        sig.startBit  = dbcStartBitFromLsb(sig, savedLsb);
        needsLayoutRefresh = true;

    } else if (col == 8) {
        bool ok = false;
        const double val = text.toDouble(&ok);
        if (!ok) {
            const QSignalBlocker b(signalTable_);
            item->setText(QString::number(sig.minimum, 'g', 10));
            return;
        }
        sig.minimum = val;
        emit dataModified();

    } else if (col == 9) {
        bool ok = false;
        const double val = text.toDouble(&ok);
        if (!ok) {
            const QSignalBlocker b(signalTable_);
            item->setText(QString::number(sig.maximum, 'g', 10));
            return;
        }
        sig.maximum = val;
        emit dataModified();
    }

    if (needsLayoutRefresh) {
        emit dataModified();
        refreshBitLayout();
    }
}

bool OverviewWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == signalTable_->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (!signalTable_->indexAt(me->pos()).isValid()) {
            signalTable_->clearSelection();
            signalTable_->setCurrentItem(nullptr);
        }
    }
    return QWidget::eventFilter(obj, event);
}

void OverviewWidget::refreshMessageTable()
{
    const bool wasSorting = messageTable_->isSortingEnabled();
    messageTable_->setSortingEnabled(false);

    const int savedMessageIndex = currentMessageIndex_;

    messageTable_->clearContents();
    messageTable_->setRowCount(db_->messages.size());

    for (int row = 0; row < db_->messages.size(); ++row) {
        const DbcMessage& msg = db_->messages.at(row);
        auto* nameItem = new QTableWidgetItem(msg.name);
        nameItem->setData(Qt::UserRole, row);
        messageTable_->setItem(row, 0, nameItem);
        messageTable_->setItem(row, 1, new QTableWidgetItem(msg.isExtended ? "CAN Extended" : "CAN Standard"));
        messageTable_->setItem(row, 2, new QTableWidgetItem(QString("0x%1").arg(QString::number(msg.id, 16).toUpper())));
        messageTable_->setItem(row, 3, new NumericTableWidgetItem(msg.dlc));
        messageTable_->setItem(row, 4, new NumericTableWidgetItem(msg.cycleTimeMs));
        messageTable_->setItem(row, 5, new QTableWidgetItem(msg.transmitter));
        {
            auto* ci = new QTableWidgetItem(msg.comment);
            ci->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            messageTable_->setItem(row, 6, ci);
        }
        {
            auto* ai = new QTableWidgetItem("Edit...");
            ai->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            ai->setForeground(QBrush(QColor("#0057b8")));
            messageTable_->setItem(row, 7, ai);
        }
    }

    messageTable_->setSortingEnabled(wasSorting);

    if (savedMessageIndex >= 0) {
        for (int row = 0; row < messageTable_->rowCount(); ++row) {
            QTableWidgetItem* item = messageTable_->item(row, 0);
            if (item && item->data(Qt::UserRole).toInt() == savedMessageIndex) {
                messageTable_->selectRow(row);
                break;
            }
        }
    }
}

void OverviewWidget::refreshSignalTable()
{
    const bool wasSorting = signalTable_->isSortingEnabled();
    signalTable_->setSortingEnabled(false);

    const QSignalBlocker sigTableBlocker(signalTable_);

    signalTable_->clearContents();

    if (currentMessageIndex_ < 0 || currentMessageIndex_ >= db_->messages.size()) {
        signalTable_->setRowCount(0);
        signalTable_->setSortingEnabled(wasSorting);
        return;
    }

    const DbcMessage& message = db_->messages.at(currentMessageIndex_);
    signalTable_->setRowCount(message.signalList.size());

    for (int row = 0; row < message.signalList.size(); ++row) {
        const DbcSignal& dbcSig = message.signalList.at(row);
        auto* nameItem = new QTableWidgetItem(dbcSig.name);
        nameItem->setData(Qt::UserRole, row);
        signalTable_->setItem(row, 0, nameItem);

        auto* modeContainer = new QWidget(signalTable_);
        auto* modeLayout = new QHBoxLayout(modeContainer);
        modeLayout->setContentsMargins(0, 0, 0, 0);
        modeLayout->setSpacing(4);

        auto* modeCombo = new FocusComboBox(modeContainer);
        modeCombo->addItems({"Signal", "Multiplexor", "M="});
        const QString initialMode = dbcSig.mode.isEmpty() ? "Signal" : dbcSig.mode;
        modeCombo->setCurrentText(initialMode);
        modeLayout->addWidget(modeCombo);

        auto* modeValueEdit = new QLineEdit(modeContainer);
        modeValueEdit->setPlaceholderText("Hex");
        modeValueEdit->setValidator(new QRegularExpressionValidator(
            QRegularExpression("^[0-9A-Fa-f]*$"), modeValueEdit));
        modeValueEdit->setMaxLength(8);
        modeValueEdit->setText(dbcSig.modeValueHex);
        modeValueEdit->setVisible(initialMode == "M=");
        modeLayout->addWidget(modeValueEdit);

        const QString initialModeDisplay = initialMode == "M="
            ? QString("M=%1").arg(dbcSig.modeValueHex) : initialMode;
        signalTable_->setItem(row, 1, new QTableWidgetItem(initialModeDisplay));
        signalTable_->setCellWidget(row, 1, modeContainer);

        connect(modeCombo, &QComboBox::currentTextChanged,
                this, [this, modeValueEdit, name = dbcSig.name](const QString& text) {
            if (currentMessageIndex_ < 0 || currentMessageIndex_ >= db_->messages.size()) { return; }
            auto& signalList = db_->messages[currentMessageIndex_].signalList;
            for (int i = 0; i < signalList.size(); ++i) {
                if (signalList[i].name != name) { continue; }
                signalList[i].mode = text;
                if (text != "M=") { signalList[i].modeValueHex.clear(); }
                modeValueEdit->setVisible(text == "M=");
                break;
            }
            QModelIndex modelIndex = signalTable_->indexAt(modeValueEdit->parentWidget()->pos());
            if (modelIndex.isValid()) {
                QTableWidgetItem* modeItem = signalTable_->item(modelIndex.row(), 1);
                if (modeItem) {
                    modeItem->setText(text == "M=" ? QString("M=%1").arg(modeValueEdit->text().toUpper()) : text);
                }
            }
            emit dataModified();
            refreshMultiplexorSignalDropdown();
            refreshBitLayout();
        });

        connect(modeValueEdit, &QLineEdit::textChanged,
                this, [this, name = dbcSig.name, modeValueEdit](const QString& text) {
            if (currentMessageIndex_ < 0 || currentMessageIndex_ >= db_->messages.size()) { return; }
            auto& signalList = db_->messages[currentMessageIndex_].signalList;
            for (int i = 0; i < signalList.size(); ++i) {
                if (signalList[i].name != name) { continue; }
                if (signalList[i].mode == "M=") {
                    signalList[i].modeValueHex = text.toUpper();
                    QModelIndex modelIndex = signalTable_->indexAt(modeValueEdit->parentWidget()->pos());
                    if (modelIndex.isValid()) {
                        QTableWidgetItem* modeItem = signalTable_->item(modelIndex.row(), 1);
                        if (modeItem) { modeItem->setText(QString("M=%1").arg(text.toUpper())); }
                    }
                    emit dataModified();
                    refreshMultiplexorSignalDropdown();
                    refreshBitLayout();
                }
                break;
            }
        });

        signalTable_->setItem(row, 2, new NumericTableWidgetItem(dbcSignalLsb(dbcSig)));
        signalTable_->setItem(row, 3, new NumericTableWidgetItem(dbcSig.bitLength));

        // ── Byte Order ────────────────────────────────────────────────────────
        {
            auto* byteOrderCombo = new FocusComboBox(signalTable_);
            byteOrderCombo->addItems({"Motorola", "Intel"});
            byteOrderCombo->setCurrentText(dbcSig.byteOrder);
            signalTable_->setItem(row, 4, new QTableWidgetItem(dbcSig.byteOrder));
            signalTable_->setCellWidget(row, 4, byteOrderCombo);
            connect(byteOrderCombo, &QComboBox::currentTextChanged,
                    this, [this, name = dbcSig.name](const QString& text) {
                if (currentMessageIndex_ < 0 || currentMessageIndex_ >= db_->messages.size()) { return; }
                auto& signalList = db_->messages[currentMessageIndex_].signalList;
                for (DbcSignal& sig : signalList) {
                    if (sig.name != name) { continue; }
                    if (sig.byteOrder == text) { return; }
                    const int lsb = dbcSignalLsb(sig);
                    sig.byteOrder = text;
                    sig.startBit  = dbcStartBitFromLsb(sig, lsb);
                    {
                        QSettings s;
                        if (s.value("defaults/signals/byteOrder", "Last Value").toString() == "Last Value") {
                            s.setValue("defaults/signals/byteOrder/lastValue", text);
                        }
                    }
                    for (int r = 0; r < signalTable_->rowCount(); ++r) {
                        QTableWidgetItem* ni = signalTable_->item(r, 0);
                        if (ni && ni->text() == name) {
                            const QSignalBlocker b(signalTable_);
                            QTableWidgetItem* sbItem = signalTable_->item(r, 2);
                            if (sbItem) { sbItem->setText(QString::number(dbcSignalLsb(sig))); }
                            break;
                        }
                    }
                    emit dataModified();
                    refreshBitLayout();
                    break;
                }
            });
        }

        // ── Value Type ────────────────────────────────────────────────────────
        {
            auto* typeCombo = new FocusComboBox(signalTable_);
            typeCombo->addItems({"Unsigned", "Signed", "Float", "Double"});
            typeCombo->setCurrentText(dbcSig.valueType);
            signalTable_->setItem(row, 5, new QTableWidgetItem(dbcSig.valueType));
            signalTable_->setCellWidget(row, 5, typeCombo);
            connect(typeCombo, &QComboBox::currentTextChanged,
                    this, [this, name = dbcSig.name](const QString& text) {
                if (currentMessageIndex_ < 0 || currentMessageIndex_ >= db_->messages.size()) { return; }
                auto& signalList = db_->messages[currentMessageIndex_].signalList;
                for (DbcSignal& sig : signalList) {
                    if (sig.name != name) { continue; }
                    sig.valueType = text;
                    emit dataModified();
                    break;
                }
            });
        }

        signalTable_->setItem(row, 6, new NumericTableWidgetItem(dbcSig.factor));
        signalTable_->setItem(row, 7, new NumericTableWidgetItem(dbcSig.offset));
        signalTable_->setItem(row, 8, new NumericTableWidgetItem(dbcSig.minimum));
        signalTable_->setItem(row, 9, new NumericTableWidgetItem(dbcSig.maximum));
        signalTable_->setItem(row, 10, new QTableWidgetItem(dbcSig.unit));
        {
            auto* ci = new QTableWidgetItem(dbcSig.comment);
            ci->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            signalTable_->setItem(row, 11, ci);
        }

        // ── Value Table button (col 12) ───────────────────────────────────────
        {
            auto* vtBtn = new QPushButton(dbcSig.valueTableName, signalTable_);
            const QString sigName = dbcSig.name;
            connect(vtBtn, &QPushButton::clicked, this, [this, sigName]() {
                if (currentMessageIndex_ < 0 || currentMessageIndex_ >= db_->messages.size()) { return; }
                auto& msgSigs = db_->messages[currentMessageIndex_].signalList;
                for (DbcSignal& sig : msgSigs) {
                    if (sig.name != sigName) { continue; }
                    ValueTableEditorDialog dlg(
                        QString("Value Table \u2013 %1").arg(sigName),
                        sig.valueTableName,
                        sig.valueEntries,
                        db_->valueTables,
                        this);
                    if (dlg.exec() == QDialog::Accepted) {
                        sig.valueTableName = dlg.name();
                        sig.valueEntries   = dlg.entries();
                        emit dataModified();
                        for (int r = 0; r < signalTable_->rowCount(); ++r) {
                            auto* ni = signalTable_->item(r, 0);
                            if (ni && ni->text() == sigName) {
                                auto* btn = qobject_cast<QPushButton*>(signalTable_->cellWidget(r, 12));
                                if (btn) { btn->setText(sig.valueTableName); }
                                break;
                            }
                        }
                    }
                    break;
                }
            });
            signalTable_->setCellWidget(row, 12, vtBtn);
        }

        // ── Attributes (col 13) ───────────────────────────────────────────────
        {
            auto* ai = new QTableWidgetItem("Edit...");
            ai->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            ai->setForeground(QBrush(QColor("#0057b8")));
            signalTable_->setItem(row, 13, ai);
        }
    }

    signalTable_->setSortingEnabled(wasSorting);
}

void OverviewWidget::refreshMultiplexorSignalDropdown()
{
    if (!multiplexorSignalCombo_) { return; }

    const QSignalBlocker blocker(multiplexorSignalCombo_);
    multiplexorSignalCombo_->clear();

    if (currentMessageIndex_ < 0 || currentMessageIndex_ >= db_->messages.size()) {
        multiplexorSignalCombo_->addItem("(No message selected)");
        multiplexorSignalCombo_->setEnabled(false);
        return;
    }

    const DbcMessage& message = db_->messages.at(currentMessageIndex_);
    QString multiplexorName;
    QSet<quint32> multiplexedValues;

    for (const DbcSignal& sig : message.signalList) {
        if (sig.mode.compare("Multiplexor", Qt::CaseInsensitive) == 0) {
            if (multiplexorName.isEmpty()) { multiplexorName = sig.name; }
        }
        if (sig.mode.compare("M=", Qt::CaseInsensitive) == 0) {
            QString raw = sig.modeValueHex.trimmed();
            if (raw.startsWith("0x", Qt::CaseInsensitive)) { raw = raw.mid(2); }
            bool ok = false;
            const quint32 value = raw.toUInt(&ok, 16);
            if (ok) { multiplexedValues.insert(value); }
        }
    }

    if (multiplexorName.isEmpty()) { multiplexorName = "Multiplexor"; }

    multiplexorSignalCombo_->addItem("-- No Multiplexor --", QVariant());

    QList<quint32> sortedValues = multiplexedValues.values();
    std::sort(sortedValues.begin(), sortedValues.end());
    for (quint32 value : sortedValues) {
        multiplexorSignalCombo_->addItem(
            QString("%1 = 0x%2").arg(multiplexorName, QString::number(value, 16).toUpper()),
            value);
    }

    multiplexorSignalCombo_->setEnabled(true);
    multiplexorSignalCombo_->setCurrentIndex(0);
}
