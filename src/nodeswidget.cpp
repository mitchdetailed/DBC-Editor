#include "nodeswidget.h"
#include "dbcdialogs.h"
#include "dbcutil.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QVBoxLayout>

static const char* kNodesWidgetMimeMessage = "application/x-dbc-message";

NodesWidget::NodesWidget(DbcDatabase* db, QWidget* parent)
    : QWidget(parent), db_(db)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({"Name", "Address", "Comment", "Attributes"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionsClickable(true);
    table_->horizontalHeader()->setSortIndicatorShown(true);
    table_->horizontalHeader()->setSectionsMovable(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSortingEnabled(true);
    table_->setAcceptDrops(true);
    table_->setDragDropMode(QAbstractItemView::DropOnly);
    table_->setDropIndicatorShown(true);
    table_->viewport()->installEventFilter(this);

    layout->addWidget(table_);

    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        auto* addAct = menu.addAction("Add Node");
        connect(addAct, &QAction::triggered, this, &NodesWidget::onAddNode);
        const QModelIndexList selected = table_->selectionModel()
            ? table_->selectionModel()->selectedRows() : QModelIndexList{};
        if (!selected.isEmpty()) {
            menu.addSeparator();
            auto* delAct = menu.addAction("Delete Node");
            connect(delAct, &QAction::triggered, this, &NodesWidget::onDeleteNode);
            auto* delWithAttrAct = menu.addAction("Delete Node with Attributes");
            connect(delWithAttrAct, &QAction::triggered, this, &NodesWidget::onDeleteNodeWithAttributes);
        }
        menu.exec(table_->viewport()->mapToGlobal(pos));
    });

    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        auto* nameItem = table_->item(row, 0);
        if (!nameItem) { return; }
        const QString nodeName = nameItem->text();
        if (col == 2) {
            QString current;
            for (const DbcNode& n : db_->nodes) {
                if (n.name == nodeName) { current = n.comment; break; }
            }
            openCommentEditor(this, current, [this, row, nodeName](const QString& text) {
                for (DbcNode& n : db_->nodes) {
                    if (n.name != nodeName) { continue; }
                    n.comment = text;
                    auto* cell = table_->item(row, 2);
                    if (cell) { cell->setText(text); }
                    emit dataModified();
                    break;
                }
            });
        } else if (col == 3) {
            for (DbcNode& n : db_->nodes) {
                if (n.name != nodeName) { continue; }
                QList<DbcAttributeDef> defs;
                for (const DbcAttributeDef& d : db_->attributes)
                    if (d.objectType == DbcAttributeDef::ObjectType::Node) defs.append(d);
                ObjectAttrDialog dlg(
                    QString("Attributes \u2013 Node: %1").arg(nodeName),
                    defs, n.attrValues, this);
                if (dlg.exec() == QDialog::Accepted) {
                    n.attrValues = dlg.result();
                    emit dataModified();
                }
                break;
            }
        }
    });
}

void NodesWidget::refresh()
{
    const bool wasSorting = table_->isSortingEnabled();
    table_->setSortingEnabled(false);
    table_->clearContents();
    table_->setRowCount(db_->nodes.size());
    for (int row = 0; row < db_->nodes.size(); ++row) {
        const DbcNode& node = db_->nodes.at(row);
        auto ro = [](const QString& text) -> QTableWidgetItem* {
            auto* it = new QTableWidgetItem(text);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            return it;
        };
        table_->setItem(row, 0, ro(node.name));
        table_->setItem(row, 1, ro(node.address));
        table_->setItem(row, 2, ro(node.comment));
        auto* ai = new QTableWidgetItem("Edit...");
        ai->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        ai->setForeground(QBrush(QColor("#0057b8")));
        table_->setItem(row, 3, ai);
    }
    table_->setSortingEnabled(wasSorting);
}

void NodesWidget::deleteSelected()
{
    onDeleteNode();
}

void NodesWidget::onAddNode()
{
    bool ok = false;
    const QString nodeName = QInputDialog::getText(
        this, "Add Node", "Node name:", QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || nodeName.isEmpty()) { return; }

    for (const DbcNode& existing : db_->nodes) {
        if (existing.name == nodeName) {
            QMessageBox::warning(this, "Invalid Node",
                QString("Node name already exists: %1").arg(nodeName));
            return;
        }
    }

    if (!isValidSymbolName(nodeName)) {
        QMessageBox::warning(this, "Invalid Node Name",
            QString("'%1' is not a valid node name.\n\nNames must start with a letter or underscore\nand contain only A-Z, a-z, 0-9, or underscore.").arg(nodeName));
        return;
    }

    db_->nodes.append(DbcNode{nodeName, QString{}, QString{}});
    emit dataModified();
    refresh();
}

void NodesWidget::onDeleteNode()
{
    const QModelIndexList selected = table_->selectionModel()
        ? table_->selectionModel()->selectedRows() : QModelIndexList{};
    if (selected.isEmpty()) { return; }

    const QString nodeName = table_->item(selected.first().row(), 0)
        ? table_->item(selected.first().row(), 0)->text() : QString{};
    if (nodeName.isEmpty()) { return; }

    if (QMessageBox::question(this, "Delete Node",
            QString("Delete node '%1'?").arg(nodeName),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) { return; }

    for (int i = 0; i < db_->nodes.size(); ++i) {
        if (db_->nodes.at(i).name == nodeName) {
            db_->nodes.removeAt(i);
            break;
        }
    }

    for (DbcMessage& message : db_->messages) {
        if (message.transmitter == nodeName) {
            message.transmitter = "Vector__XXX";
        }
    }

    emit dataModified();
    refresh();
}

void NodesWidget::onDeleteNodeWithAttributes()
{
    onDeleteNode();
}

bool NodesWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj != table_->viewport()) { return QWidget::eventFilter(obj, event); }

    // Clear selection when clicking empty area
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (!table_->indexAt(me->pos()).isValid()) {
            table_->clearSelection();
        }
    }

    // Accept message drops to set transmitter
    if (event->type() == QEvent::DragEnter) {
        auto* de = static_cast<QDragEnterEvent*>(event);
        if (de->mimeData()->hasFormat(kNodesWidgetMimeMessage)) {
            de->acceptProposedAction();
            return true;
        }
    }
    if (event->type() == QEvent::DragMove) {
        auto* dm = static_cast<QDragMoveEvent*>(event);
        if (dm->mimeData()->hasFormat(kNodesWidgetMimeMessage)) {
            const QModelIndex idx = table_->indexAt(dm->position().toPoint());
            if (idx.isValid()) { dm->acceptProposedAction(); }
            else               { dm->ignore(); }
            return true;
        }
    }
    if (event->type() == QEvent::Drop) {
        auto* de = static_cast<QDropEvent*>(event);
        if (de->mimeData()->hasFormat(kNodesWidgetMimeMessage)) {
            const QModelIndex idx = table_->indexAt(de->position().toPoint());
            if (!idx.isValid()) { de->ignore(); return true; }

            QTableWidgetItem* nodeItem = table_->item(idx.row(), 0);
            if (!nodeItem) { de->ignore(); return true; }
            const QString nodeName = nodeItem->text();
            const QString msgName  = QString::fromUtf8(de->mimeData()->data(kNodesWidgetMimeMessage));

            for (DbcMessage& msg : db_->messages) {
                if (msg.name == msgName) {
                    msg.transmitter = nodeName;
                    break;
                }
            }

            emit dataModified();
            de->acceptProposedAction();
            return true;
        }
    }

    return QWidget::eventFilter(obj, event);
}
