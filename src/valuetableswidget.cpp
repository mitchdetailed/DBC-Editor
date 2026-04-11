#include "valuetableswidget.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

ValueTablesWidget::ValueTablesWidget(DbcDatabase* db, QWidget* parent)
    : QWidget(parent), db_(db)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // ── Left panel: named table list ─────────────────────────────────────────
    auto* leftWidget = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(4);

    auto* listHeader = new QLabel("Named Value Tables (VAL_TABLE_)", leftWidget);
    QFont headerFont = listHeader->font();
    headerFont.setBold(true);
    listHeader->setFont(headerFont);
    listHeader->setContentsMargins(4, 4, 4, 4);

    listWidget_ = new QListWidget(leftWidget);

    auto* listBtnRow = new QHBoxLayout();
    auto* addTableBtn = new QPushButton("Add Table", leftWidget);
    auto* delTableBtn = new QPushButton("Delete Table", leftWidget);
    listBtnRow->addWidget(addTableBtn);
    listBtnRow->addWidget(delTableBtn);

    leftLayout->addWidget(listHeader);
    leftLayout->addWidget(listWidget_, 1);
    leftLayout->addLayout(listBtnRow);

    connect(addTableBtn, &QPushButton::clicked, this, &ValueTablesWidget::onAddValueTable);
    connect(delTableBtn, &QPushButton::clicked, this, &ValueTablesWidget::onDeleteValueTable);
    connect(listWidget_, &QListWidget::currentRowChanged,
            this, &ValueTablesWidget::onValueTableSelected);

    listWidget_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(listWidget_, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        QMenu menu(this);
        auto* addAct = menu.addAction("Add Table");
        connect(addAct, &QAction::triggered, this, &ValueTablesWidget::onAddValueTable);
        const QListWidgetItem* clickedItem = listWidget_->itemAt(pos);
        if (clickedItem) {
            menu.addSeparator();
            auto* renameAct = menu.addAction("Rename Table");
            auto* deleteAct = menu.addAction("Delete Table");
            connect(renameAct, &QAction::triggered, this, [this]() {
                if (currentIndex_ < 0 || currentIndex_ >= db_->valueTables.size()) { return; }
                const QString oldName = db_->valueTables[currentIndex_].name;
                bool ok = false;
                const QString newName = QInputDialog::getText(
                    this, "Rename Value Table", "New name:",
                    QLineEdit::Normal, oldName, &ok).trimmed();
                if (!ok || newName.isEmpty() || newName == oldName) { return; }
                for (const DbcValueTable& vt : db_->valueTables) {
                    if (vt.name == newName) {
                        QMessageBox::warning(this, "Duplicate Name",
                            QString("A value table named \"%1\" already exists.").arg(newName));
                        return;
                    }
                }
                // Update all signal references pointing to this table
                for (DbcMessage& msg : db_->messages) {
                    for (DbcSignal& sig : msg.signalList) {
                        if (sig.valueTableName == oldName) { sig.valueTableName = newName; }
                    }
                }
                db_->valueTables[currentIndex_].name = newName;
                emit dataModified();
                refresh();
            });
            connect(deleteAct, &QAction::triggered, this, &ValueTablesWidget::onDeleteValueTable);
        }
        menu.exec(listWidget_->mapToGlobal(pos));
    });

    // ── Right panel: entry editor ────────────────────────────────────────────
    auto* rightWidget = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(4);

    auto* entriesHeader = new QLabel("Entries (Raw Value \u2192 Label)", rightWidget);
    QFont entriesFont = entriesHeader->font();
    entriesFont.setBold(true);
    entriesHeader->setFont(entriesFont);
    entriesHeader->setContentsMargins(4, 4, 4, 4);

    entriesTable_ = new QTableWidget(rightWidget);
    entriesTable_->setColumnCount(2);
    entriesTable_->setHorizontalHeaderLabels({"Raw Value", "Label"});
    entriesTable_->horizontalHeader()->setStretchLastSection(true);
    entriesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    entriesTable_->horizontalHeader()->setSectionsMovable(true);
    entriesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    entriesTable_->setSelectionMode(QAbstractItemView::SingleSelection);

    auto* entryBtnRow = new QHBoxLayout();
    auto* addEntryBtn = new QPushButton("Add Entry", rightWidget);
    auto* delEntryBtn = new QPushButton("Delete Entry", rightWidget);
    entryBtnRow->addWidget(addEntryBtn);
    entryBtnRow->addWidget(delEntryBtn);
    entryBtnRow->addStretch();

    rightLayout->addWidget(entriesHeader);
    rightLayout->addWidget(entriesTable_, 1);
    rightLayout->addLayout(entryBtnRow);

    connect(addEntryBtn, &QPushButton::clicked, this, &ValueTablesWidget::onAddValueTableEntry);
    connect(delEntryBtn, &QPushButton::clicked, this, &ValueTablesWidget::onDeleteValueTableEntry);

    // Delete key shortcuts scoped to each widget
    {
        auto* sc = new QShortcut(QKeySequence::Delete, listWidget_);
        sc->setContext(Qt::WidgetShortcut);
        connect(sc, &QShortcut::activated, this, &ValueTablesWidget::onDeleteValueTable);
    }
    {
        auto* sc = new QShortcut(QKeySequence::Delete, entriesTable_);
        sc->setContext(Qt::WidgetShortcut);
        connect(sc, &QShortcut::activated, this, &ValueTablesWidget::onDeleteValueTableEntry);
    }

    connect(entriesTable_, &QTableWidget::itemChanged,
            this, &ValueTablesWidget::onValueTableEntryItemChanged);

    entriesTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(entriesTable_, &QTableWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        if (currentIndex_ < 0) { return; }
        QMenu menu(this);
        auto* addAct = menu.addAction("Add Entry");
        connect(addAct, &QAction::triggered, this, &ValueTablesWidget::onAddValueTableEntry);
        const QModelIndex idx = entriesTable_->indexAt(pos);
        if (idx.isValid()) {
            menu.addSeparator();
            auto* delAct = menu.addAction("Delete Entry");
            connect(delAct, &QAction::triggered, this, &ValueTablesWidget::onDeleteValueTableEntry);
        }
        menu.exec(entriesTable_->mapToGlobal(pos));
    });

    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 500});

    mainLayout->addWidget(splitter, 1);
}

void ValueTablesWidget::refresh()
{
    refreshValueTablesView();
}

void ValueTablesWidget::deleteSelected()
{
    // If an entry row is focused, delete the entry; otherwise delete the table.
    if (entriesTable_->hasFocus()) {
        onDeleteValueTableEntry();
    } else {
        onDeleteValueTable();
    }
}

void ValueTablesWidget::refreshValueTablesView()
{
    if (!listWidget_ || !entriesTable_) { return; }

    std::sort(db_->valueTables.begin(), db_->valueTables.end(),
              [](const DbcValueTable& a, const DbcValueTable& b) {
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });

    const QSignalBlocker listBlocker(listWidget_);
    const QSignalBlocker entryBlocker(entriesTable_);

    const int prevRow = listWidget_->currentRow();
    listWidget_->clear();
    for (const DbcValueTable& vt : db_->valueTables) {
        listWidget_->addItem(vt.name);
    }

    const int newRow = qBound(-1, prevRow, db_->valueTables.size() - 1);
    if (newRow >= 0) {
        listWidget_->setCurrentRow(newRow);
        currentIndex_ = newRow;
        sortAndRefreshEntries(newRow);
    } else {
        currentIndex_ = -1;
        entriesTable_->setRowCount(0);
    }
}

void ValueTablesWidget::sortAndRefreshEntries(int tableIndex)
{
    if (!entriesTable_) { return; }
    if (tableIndex < 0 || tableIndex >= db_->valueTables.size()) { return; }

    auto& entries = db_->valueTables[tableIndex].entries;
    std::sort(entries.begin(), entries.end(), [](const DbcValueEntry& a, const DbcValueEntry& b) {
        return a.rawValue < b.rawValue;
    });

    const QSignalBlocker b(entriesTable_);
    entriesTable_->setRowCount(0);
    for (const DbcValueEntry& e : entries) {
        const int r = entriesTable_->rowCount();
        entriesTable_->insertRow(r);
        entriesTable_->setItem(r, 0, new QTableWidgetItem(QString::number(e.rawValue)));
        entriesTable_->setItem(r, 1, new QTableWidgetItem(e.label));
    }
}

void ValueTablesWidget::onAddValueTable()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "Add Value Table", "Table name:", QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) { return; }

    for (const DbcValueTable& vt : db_->valueTables) {
        if (vt.name == name) {
            QMessageBox::warning(this, "Duplicate Name",
                QString("A value table named \"%1\" already exists.").arg(name));
            return;
        }
    }

    db_->valueTables.append(DbcValueTable{name, {}});
    emit dataModified();
    std::sort(db_->valueTables.begin(), db_->valueTables.end(),
              [](const DbcValueTable& a, const DbcValueTable& b) {
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });
    const int newIdx = [&]() -> int {
        for (int i = 0; i < db_->valueTables.size(); ++i) {
            if (db_->valueTables[i].name == name) { return i; }
        }
        return db_->valueTables.size() - 1;
    }();
    refreshValueTablesView();
    listWidget_->setCurrentRow(newIdx);
}

void ValueTablesWidget::onDeleteValueTable()
{
    if (currentIndex_ < 0 || currentIndex_ >= db_->valueTables.size()) { return; }

    const QString name = db_->valueTables[currentIndex_].name;
    if (QMessageBox::question(this, "Delete Value Table",
            QString("Delete value table \"%1\"?").arg(name),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) { return; }

    db_->valueTables.removeAt(currentIndex_);
    emit dataModified();
    refreshValueTablesView();
}

void ValueTablesWidget::onValueTableSelected()
{
    if (!listWidget_ || !entriesTable_) { return; }
    currentIndex_ = listWidget_->currentRow();
    if (currentIndex_ < 0 || currentIndex_ >= db_->valueTables.size()) {
        const QSignalBlocker b(entriesTable_);
        entriesTable_->setRowCount(0);
        return;
    }
    sortAndRefreshEntries(currentIndex_);
}

void ValueTablesWidget::onAddValueTableEntry()
{
    if (currentIndex_ < 0 || currentIndex_ >= db_->valueTables.size()) { return; }

    const auto& existing = db_->valueTables[currentIndex_].entries;
    qint64 newRaw = 0;
    if (!existing.isEmpty()) {
        qint64 maxVal = existing.first().rawValue;
        for (const DbcValueEntry& e : existing) { maxVal = qMax(maxVal, e.rawValue); }
        newRaw = maxVal + 1;
    }
    const QString newLabel = QString("Description for the value '%1'").arg(newRaw);

    db_->valueTables[currentIndex_].entries.append(DbcValueEntry{newRaw, newLabel});
    emit dataModified();
    sortAndRefreshEntries(currentIndex_);
    for (int r = 0; r < entriesTable_->rowCount(); ++r) {
        auto* it = entriesTable_->item(r, 0);
        if (it && it->text().toLongLong() == newRaw) {
            entriesTable_->scrollToItem(it);
            entriesTable_->setCurrentItem(it);
            entriesTable_->editItem(entriesTable_->item(r, 1));
            break;
        }
    }
}

void ValueTablesWidget::onDeleteValueTableEntry()
{
    if (currentIndex_ < 0 || currentIndex_ >= db_->valueTables.size()) { return; }
    const auto sel = entriesTable_->selectionModel()
        ? entriesTable_->selectionModel()->selectedRows() : QModelIndexList{};
    if (sel.isEmpty()) { return; }

    const int row = sel.first().row();
    auto& entries = db_->valueTables[currentIndex_].entries;
    if (row < 0 || row >= entries.size()) { return; }

    const qint64 rawVal = entries.at(row).rawValue;
    const QString label = entries.at(row).label;
    if (QMessageBox::question(this, "Confirm Delete Entry",
            QString("Delete entry %1 : %2?").arg(rawVal).arg(label),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) { return; }

    const QSignalBlocker b(entriesTable_);
    entriesTable_->removeRow(row);
    entries.removeAt(row);
    emit dataModified();
}

void ValueTablesWidget::onValueTableEntryItemChanged(QTableWidgetItem* item)
{
    if (!item) { return; }
    if (currentIndex_ < 0 || currentIndex_ >= db_->valueTables.size()) { return; }

    auto& entries = db_->valueTables[currentIndex_].entries;
    const int row = item->row();
    if (row < 0 || row >= entries.size()) { return; }

    if (item->column() == 0) {
        bool ok = false;
        const QString text = item->text().trimmed();
        const qint64 val = (text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
            ? text.mid(2).toLongLong(&ok, 16)
            : text.toLongLong(&ok);
        if (ok) {
            entries[row].rawValue = val;
            emit dataModified();
            {
                const QSignalBlocker b(entriesTable_);
                item->setText(QString::number(val));
            }
            sortAndRefreshEntries(currentIndex_);
        } else {
            const QSignalBlocker b(entriesTable_);
            item->setText(QString::number(entries[row].rawValue));
        }
    } else if (item->column() == 1) {
        entries[row].label = item->text();
        emit dataModified();
    }
}
