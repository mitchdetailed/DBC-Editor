#include "messageswidget.h"
#include "dbcdialogs.h"
#include "dbcutil.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QVBoxLayout>

static const char* kMessagesWidgetMimeSignal  = "application/x-dbc-signal";
static const char* kMessagesWidgetMimeMessage = "application/x-dbc-message";

MessagesWidget::MessagesWidget(DbcDatabase* db, QWidget* parent)
    : QWidget(parent), db_(db)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    table_ = new QTableWidget(this);
    table_->setColumnCount(8);
    table_->setHorizontalHeaderLabels({"Name", "ID", "ID-Format", "DLC", "Cycle Time", "Transmitter", "Comment", "Attributes"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionsClickable(true);
    table_->horizontalHeader()->setSortIndicatorShown(true);
    table_->horizontalHeader()->setSectionsMovable(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSortingEnabled(true);
    table_->setDragEnabled(true);
    table_->setAcceptDrops(true);
    table_->setDragDropMode(QAbstractItemView::DragDrop);
    table_->setDropIndicatorShown(true);
    table_->viewport()->installEventFilter(this);

    layout->addWidget(table_);

    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        auto* addAct = menu.addAction("Add Message");
        connect(addAct, &QAction::triggered, this, &MessagesWidget::requestAddMessage);
        const QModelIndexList selected = table_->selectionModel()
            ? table_->selectionModel()->selectedRows() : QModelIndexList{};
        if (!selected.isEmpty()) {
            menu.addSeparator();
            auto* delAct = menu.addAction("Delete Message");
            connect(delAct, &QAction::triggered, this, &MessagesWidget::onDeleteMessage);
            auto* delWithAttrAct = menu.addAction("Delete Message with Attributes");
            connect(delWithAttrAct, &QAction::triggered, this, &MessagesWidget::onDeleteMessageWithAttributes);
        }
        menu.exec(table_->viewport()->mapToGlobal(pos));
    });

    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        auto* nameItem = table_->item(row, 0);
        if (!nameItem) { return; }
        const QString msgName = nameItem->text();
        if (col == 6) {
            QString current;
            for (const DbcMessage& m : db_->messages) {
                if (m.name == msgName) { current = m.comment; break; }
            }
            openCommentEditor(this, current, [this, row, msgName](const QString& text) {
                for (DbcMessage& m : db_->messages) {
                    if (m.name != msgName) { continue; }
                    m.comment = text;
                    auto* cell = table_->item(row, 6);
                    if (cell) { cell->setText(text); }
                    emit dataModified();
                    break;
                }
            });
        } else if (col == 7) {
            for (DbcMessage& m : db_->messages) {
                if (m.name != msgName) { continue; }
                QList<DbcAttributeDef> defs;
                for (const DbcAttributeDef& d : db_->attributes)
                    if (d.objectType == DbcAttributeDef::ObjectType::Message) defs.append(d);
                ObjectAttrDialog dlg(
                    QString("Attributes \u2013 Message: %1").arg(msgName),
                    defs, m.attrValues, this);
                if (dlg.exec() == QDialog::Accepted) {
                    m.attrValues = dlg.result();
                    emit dataModified();
                }
                break;
            }
        }
    });
}

void MessagesWidget::refresh()
{
    const bool wasSorting = table_->isSortingEnabled();
    table_->setSortingEnabled(false);
    table_->setUpdatesEnabled(false);
    table_->clearContents();
    table_->setRowCount(db_->messages.size());
    for (int row = 0; row < db_->messages.size(); ++row) {
        const DbcMessage& msg = db_->messages.at(row);
        auto ro = [](const QString& text) -> QTableWidgetItem* {
            auto* it = new QTableWidgetItem(text);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            return it;
        };
        table_->setItem(row, 0, ro(msg.name));
        table_->setItem(row, 1, ro(QString("0x%1").arg(QString::number(msg.id, 16).toUpper())));
        table_->setItem(row, 2, ro(msg.isExtended ? "CAN Extended" : "CAN Standard"));
        table_->setItem(row, 3, ro(QString::number(msg.dlc)));
        table_->setItem(row, 4, ro(QString::number(msg.cycleTimeMs)));
        table_->setItem(row, 5, ro(msg.transmitter));
        {
            auto* ci = new QTableWidgetItem(msg.comment);
            ci->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            table_->setItem(row, 6, ci);
        }
        {
            auto* ai = new QTableWidgetItem("Edit...");
            ai->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            ai->setForeground(QBrush(QColor("#0057b8")));
            table_->setItem(row, 7, ai);
        }
    }
    table_->setSortingEnabled(wasSorting);
    table_->setUpdatesEnabled(true);
}

void MessagesWidget::deleteSelected()
{
    onDeleteMessage();
}

void MessagesWidget::onDeleteMessage()
{
    const QModelIndexList selected = table_->selectionModel()
        ? table_->selectionModel()->selectedRows() : QModelIndexList{};
    if (selected.isEmpty()) { return; }

    const QString msgName = table_->item(selected.first().row(), 0)
        ? table_->item(selected.first().row(), 0)->text() : QString{};
    if (msgName.isEmpty()) { return; }

    if (QMessageBox::question(this, "Delete Message",
            QString("Delete message '%1'?").arg(msgName),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) { return; }

    for (int i = 0; i < db_->messages.size(); ++i) {
        if (db_->messages.at(i).name == msgName) {
            db_->messages.remove(i);
            break;
        }
    }
    emit dataModified();
    refresh();
}

void MessagesWidget::onDeleteMessageWithAttributes()
{
    onDeleteMessage();
}

bool MessagesWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj != table_->viewport()) { return QWidget::eventFilter(obj, event); }

    // Drag initiation: emit message name as MIME data
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
                if (nameItem) {
                    dragging_ = true;
                    auto* mimeData = new QMimeData;
                    mimeData->setData(kMessagesWidgetMimeMessage, nameItem->text().toUtf8());
                    auto* drag = new QDrag(table_);
                    drag->setMimeData(mimeData);
                    drag->exec(Qt::CopyAction);
                    dragging_ = false;
                    return true;
                }
            }
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        dragging_ = false;
    }

    // Drop acceptance: signal → message (move signal to this message)
    if (event->type() == QEvent::DragEnter) {
        auto* de = static_cast<QDragEnterEvent*>(event);
        if (de->mimeData()->hasFormat(kMessagesWidgetMimeSignal)) {
            de->acceptProposedAction();
            return true;
        }
    }
    if (event->type() == QEvent::DragMove) {
        auto* dm = static_cast<QDragMoveEvent*>(event);
        if (dm->mimeData()->hasFormat(kMessagesWidgetMimeSignal)) {
            const QModelIndex idx = table_->indexAt(dm->position().toPoint());
            if (idx.isValid()) { dm->acceptProposedAction(); }
            else               { dm->ignore(); }
            return true;
        }
    }
    if (event->type() == QEvent::Drop) {
        auto* de = static_cast<QDropEvent*>(event);
        if (de->mimeData()->hasFormat(kMessagesWidgetMimeSignal)) {
            const QModelIndex idx = table_->indexAt(de->position().toPoint());
            if (!idx.isValid()) { de->ignore(); return true; }

            QTableWidgetItem* targetMsgItem = table_->item(idx.row(), 0);
            if (!targetMsgItem) { de->ignore(); return true; }
            const QString targetMsgName = targetMsgItem->text();

            const QString payload = QString::fromUtf8(de->mimeData()->data(kMessagesWidgetMimeSignal));
            const int sep = payload.indexOf(QChar('\x1E'));
            if (sep < 0) { de->ignore(); return true; }
            const QString sigName       = payload.left(sep);
            const QString sourceMsgName = payload.mid(sep + 1);

            if (sourceMsgName == targetMsgName) { de->ignore(); return true; }

            DbcSignal movedSig;
            bool found = false;
            for (DbcMessage& srcMsg : db_->messages) {
                if (srcMsg.name != sourceMsgName) { continue; }
                for (int i = 0; i < srcMsg.signalList.size(); ++i) {
                    if (srcMsg.signalList.at(i).name == sigName) {
                        movedSig = srcMsg.signalList.at(i);
                        srcMsg.signalList.remove(i);
                        found = true;
                        break;
                    }
                }
                break;
            }
            if (!found) { de->ignore(); return true; }

            for (DbcMessage& dstMsg : db_->messages) {
                if (dstMsg.name != targetMsgName) { continue; }
                dstMsg.signalList.append(movedSig);
                break;
            }

            emit dataModified();
            de->acceptProposedAction();
            return true;
        }
    }

    return QWidget::eventFilter(obj, event);
}
