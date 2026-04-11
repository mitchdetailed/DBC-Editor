#include "signalswidget.h"
#include "dbcdialogs.h"
#include "dbcutil.h"

#include <QApplication>
#include <QDrag>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QTableWidget>
#include <QVBoxLayout>

static const char* kSignalsWidgetMimeSignal = "application/x-dbc-signal";

SignalsWidget::SignalsWidget(DbcDatabase* db, QWidget* parent)
    : QWidget(parent), db_(db)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    table_ = new QTableWidget(this);
    table_->setColumnCount(13);
    table_->setHorizontalHeaderLabels({"Name", "Length", "Byte Order", "Value Type", "Factor",
                                       "Offset", "Minimum", "Maximum", "Unit", "ValueTable",
                                       "Comment", "Message(s)", "Attributes"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionsClickable(true);
    table_->horizontalHeader()->setSortIndicatorShown(true);
    table_->horizontalHeader()->setSectionsMovable(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSortingEnabled(true);
    table_->setDragEnabled(true);
    table_->setDragDropMode(QAbstractItemView::DragOnly);
    table_->viewport()->installEventFilter(this);

    layout->addWidget(table_);

    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu contextMenu(this);
        const QModelIndexList selected = table_->selectionModel()
            ? table_->selectionModel()->selectedRows() : QModelIndexList{};

        if (!db_->messages.isEmpty()) {
            auto* addAction = new QAction("Add Signal", &contextMenu);
            connect(addAction, &QAction::triggered, this, [this, selected]() {
                int targetMsgIdx = -1;
                if (!selected.isEmpty()) {
                    auto* nameItem = table_->item(selected.first().row(), 0);
                    if (nameItem) {
                        const QString owningMsg = nameItem->data(Qt::UserRole).toString();
                        for (int i = 0; i < db_->messages.size(); ++i) {
                            if (db_->messages.at(i).name == owningMsg) {
                                targetMsgIdx = i;
                                break;
                            }
                        }
                    }
                }
                if (targetMsgIdx < 0) {
                    QStringList msgNames;
                    for (const DbcMessage& m : db_->messages) { msgNames << m.name; }
                    bool ok = false;
                    const QString chosen = QInputDialog::getItem(
                        this, "Add Signal", "Select target message:", msgNames, 0, false, &ok);
                    if (!ok) { return; }
                    for (int i = 0; i < db_->messages.size(); ++i) {
                        if (db_->messages.at(i).name == chosen) {
                            targetMsgIdx = i;
                            break;
                        }
                    }
                }
                if (targetMsgIdx < 0) { return; }
                emit requestAddSignal(targetMsgIdx);
            });
            contextMenu.addAction(addAction);
        }

        if (!selected.isEmpty()) {
            auto* delAct = contextMenu.addAction("Delete Signal");
            connect(delAct, &QAction::triggered, this, &SignalsWidget::onDeleteSignalFromView);
        }
        contextMenu.exec(table_->viewport()->mapToGlobal(pos));
    });

    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        auto* nameItem = table_->item(row, 0);
        if (!nameItem) { return; }
        const QString sigName = nameItem->text();
        const QString msgName = nameItem->data(Qt::UserRole).toString();
        if (col == 10) {
            QString current;
            for (const DbcMessage& m : db_->messages) {
                if (m.name != msgName) { continue; }
                for (const DbcSignal& sig : m.signalList) {
                    if (sig.name == sigName) { current = sig.comment; break; }
                }
                break;
            }
            openCommentEditor(this, current, [this, row, sigName, msgName](const QString& text) {
                for (DbcMessage& m : db_->messages) {
                    if (m.name != msgName) { continue; }
                    for (DbcSignal& sig : m.signalList) {
                        if (sig.name != sigName) { continue; }
                        sig.comment = text;
                        auto* cell = table_->item(row, 10);
                        if (cell) { cell->setText(text); }
                        emit dataModified();
                        break;
                    }
                    break;
                }
            });
        } else if (col == 12) {
            for (DbcMessage& m : db_->messages) {
                if (m.name != msgName) { continue; }
                for (DbcSignal& sig : m.signalList) {
                    if (sig.name != sigName) { continue; }
                    QList<DbcAttributeDef> defs;
                    for (const DbcAttributeDef& d : db_->attributes)
                        if (d.objectType == DbcAttributeDef::ObjectType::Signal) defs.append(d);
                    ObjectAttrDialog dlg(
                        QString("Attributes \u2013 Signal: %1 (in %2)").arg(sigName, msgName),
                        defs, sig.attrValues, this);
                    if (dlg.exec() == QDialog::Accepted) {
                        sig.attrValues = dlg.result();
                        emit dataModified();
                    }
                    return;
                }
                break;
            }
        }
    });

    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (col != 9) { return; }
        auto* nameItem = table_->item(row, 0);
        if (!nameItem) { return; }
        const QString sigName = nameItem->text();
        const QString msgName = nameItem->data(Qt::UserRole).toString();
        for (DbcMessage& msg : db_->messages) {
            if (msg.name != msgName) { continue; }
            for (DbcSignal& sig : msg.signalList) {
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
                    auto* vtItem = table_->item(row, 9);
                    if (vtItem) { vtItem->setText(sig.valueTableName); }
                    emit dataModified();
                }
                return;
            }
            break;
        }
    });

    auto* msgPickerDelegate = new MessagePickerDelegate(table_);
    table_->setItemDelegateForColumn(11, msgPickerDelegate);

    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item->column() != 11) { return; }
        const int row = item->row();
        const QTableWidgetItem* nameItem = table_->item(row, 0);
        if (!nameItem) { return; }
        const QString sigName    = nameItem->text();
        const QString oldMsgName = nameItem->data(Qt::UserRole).toString();
        const QString newMsgName = item->text();
        if (newMsgName == oldMsgName || newMsgName.isEmpty()) { return; }

        DbcSignal movedSig;
        bool found = false;
        for (DbcMessage& m : db_->messages) {
            if (m.name != oldMsgName) { continue; }
            for (int i = 0; i < m.signalList.size(); ++i) {
                if (m.signalList.at(i).name == sigName) {
                    movedSig = m.signalList.takeAt(i);
                    found = true;
                    break;
                }
            }
            break;
        }
        if (!found) { return; }

        for (DbcMessage& m : db_->messages) {
            if (m.name == newMsgName) {
                m.signalList.append(movedSig);
                break;
            }
        }
        emit dataModified();
        refresh();
    });
}

void SignalsWidget::refresh()
{
    auto* msgPickerDelegate =
        static_cast<MessagePickerDelegate*>(table_->itemDelegateForColumn(11));

    const bool wasSorting = table_->isSortingEnabled();
    table_->setSortingEnabled(false);

    int totalSignals = 0;
    for (const DbcMessage& msg : db_->messages) { totalSignals += msg.signalList.size(); }

    QStringList msgNames;
    for (const DbcMessage& m : db_->messages) { msgNames.append(m.name); }
    if (msgPickerDelegate) { msgPickerDelegate->setMessageNames(msgNames); }

    table_->blockSignals(true);
    table_->clearContents();
    table_->setRowCount(totalSignals);
    table_->setUpdatesEnabled(false);

    int row = 0;
    for (const DbcMessage& msg : db_->messages) {
        for (const DbcSignal& sig : msg.signalList) {
            auto ro = [](const QString& text) -> QTableWidgetItem* {
                auto* it = new QTableWidgetItem(text);
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                return it;
            };
            auto* nameItem = new QTableWidgetItem(sig.name);
            nameItem->setData(Qt::UserRole, msg.name);
            nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            table_->setItem(row, 0,  nameItem);
            table_->setItem(row, 1,  ro(QString::number(sig.bitLength)));
            table_->setItem(row, 2,  ro(sig.byteOrder));
            table_->setItem(row, 3,  ro(sig.valueType));
            table_->setItem(row, 4,  ro(QString::number(sig.factor, 'g', 6)));
            table_->setItem(row, 5,  ro(QString::number(sig.offset, 'g', 6)));
            table_->setItem(row, 6,  ro(QString::number(sig.minimum, 'g', 6)));
            table_->setItem(row, 7,  ro(QString::number(sig.maximum, 'g', 6)));
            table_->setItem(row, 8,  ro(sig.unit));
            table_->setItem(row, 9,  ro(sig.valueTableName));
            {
                auto* ci = new QTableWidgetItem(sig.comment);
                ci->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                table_->setItem(row, 10, ci);
            }
            table_->setItem(row, 11, new QTableWidgetItem(msg.name));
            {
                auto* ai = new QTableWidgetItem("Edit...");
                ai->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                ai->setForeground(QBrush(QColor("#0057b8")));
                table_->setItem(row, 12, ai);
            }
            ++row;
        }
    }
    table_->blockSignals(false);
    table_->setSortingEnabled(wasSorting);
    table_->setUpdatesEnabled(true);
}

void SignalsWidget::deleteSelected()
{
    onDeleteSignalFromView();
}

void SignalsWidget::selectSignal(const QString& sigName)
{
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto* item = table_->item(row, 0);
        if (item && item->text() == sigName) {
            table_->selectRow(row);
            table_->scrollToItem(item);
            break;
        }
    }
}

void SignalsWidget::onDeleteSignalFromView()
{
    const QModelIndexList selected = table_->selectionModel()
        ? table_->selectionModel()->selectedRows() : QModelIndexList{};
    if (selected.isEmpty()) { return; }

    const int row = selected.first().row();
    auto* nameItem = table_->item(row, 0);
    const QString sigName = nameItem ? nameItem->text() : QString{};
    const QString msgName = nameItem ? nameItem->data(Qt::UserRole).toString() : QString{};
    if (sigName.isEmpty() || msgName.isEmpty()) { return; }

    if (QMessageBox::question(this, "Delete Signal",
            QString("Delete signal '%1' from message '%2'?").arg(sigName, msgName),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) { return; }

    for (DbcMessage& message : db_->messages) {
        if (message.name == msgName) {
            for (int i = 0; i < message.signalList.size(); ++i) {
                if (message.signalList.at(i).name == sigName) {
                    message.signalList.remove(i);
                    break;
                }
            }
            break;
        }
    }
    emit dataModified();
    refresh();
}

bool SignalsWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj != table_->viewport()) { return QWidget::eventFilter(obj, event); }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            dragStartPos_ = me->pos();
            dragging_ = false;
        }
    } else if (event->type() == QEvent::MouseMove) {
        auto* me = static_cast<QMouseEvent*>(event);
        if ((me->buttons() & Qt::LeftButton) && !dragging_ &&
            (me->pos() - dragStartPos_).manhattanLength() >= QApplication::startDragDistance()) {
            const QModelIndex idx = table_->indexAt(dragStartPos_);
            if (idx.isValid()) {
                QTableWidgetItem* nameItem = table_->item(idx.row(), 0);
                QTableWidgetItem* msgItem  = table_->item(idx.row(), 11);
                if (nameItem && msgItem) {
                    dragging_ = true;
                    const QString payload = nameItem->text() + QChar('\x1E') + msgItem->text();
                    auto* mimeData = new QMimeData;
                    mimeData->setData(kSignalsWidgetMimeSignal, payload.toUtf8());
                    auto* drag = new QDrag(table_);
                    drag->setMimeData(mimeData);
                    drag->exec(Qt::MoveAction);
                    dragging_ = false;
                    return true;
                }
            }
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        dragging_ = false;
    }

    return QWidget::eventFilter(obj, event);
}
