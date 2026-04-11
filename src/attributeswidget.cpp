#include "attributeswidget.h"
#include "dbcdialogs.h"

#include <QApplication>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTableWidgetItem>
#include <QVBoxLayout>

AttributesWidget::AttributesWidget(DbcDatabase* db, QWidget* parent)
    : QWidget(parent), db_(db)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Header
    auto* header = new QLabel("Attribute Definitions (BA_DEF_)", this);
    QFont hFont = header->font(); hFont.setBold(true); header->setFont(hFont);
    header->setContentsMargins(4, 4, 4, 4);
    mainLayout->addWidget(header);

    // Main table
    attrDefTable_ = new QTableWidget(0, 6, this);
    attrDefTable_->setHorizontalHeaderLabels(
        {"Type of Object", "Name", "Value Type", "Default", "Minimum", "Maximum"});
    attrDefTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    attrDefTable_->horizontalHeader()->setStretchLastSection(true);
    attrDefTable_->horizontalHeader()->setSectionsMovable(true);
    attrDefTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    attrDefTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    attrDefTable_->setEditTriggers(QAbstractItemView::DoubleClicked |
                                   QAbstractItemView::SelectedClicked |
                                   QAbstractItemView::EditKeyPressed);
    attrDefTable_->setAlternatingRowColors(true);
    attrDefTable_->setColumnWidth(0, 120);
    attrDefTable_->setColumnWidth(1, 160);
    attrDefTable_->setColumnWidth(2, 110);
    attrDefTable_->setColumnWidth(3, 100);
    attrDefTable_->setColumnWidth(4, 80);
    attrDefTable_->setItemDelegateForColumn(
        0, new ComboBoxDelegate({"Network", "Node", "Message", "Signal"}, attrDefTable_));
    attrDefTable_->setItemDelegateForColumn(
        2, new ComboBoxDelegate({"Integer", "Float", "String", "Enumeration", "Hex"}, attrDefTable_));
    mainLayout->addWidget(attrDefTable_, 1);

    // Validation label
    attrValidationLabel_ = new QLabel(this);
    attrValidationLabel_->setWordWrap(true);
    attrValidationLabel_->setStyleSheet("color: red;");
    attrValidationLabel_->setAlignment(Qt::AlignCenter);
    attrValidationLabel_->hide();
    mainLayout->addWidget(attrValidationLabel_);

    // Button row
    auto* btnRow = new QHBoxLayout();
    auto* addBtn = new QPushButton("Add Attribute", this);
    auto* delBtn = new QPushButton("Delete Attribute", this);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    connect(addBtn, &QPushButton::clicked, this, &AttributesWidget::onAddAttribute);
    connect(delBtn, &QPushButton::clicked, this, &AttributesWidget::onDeleteAttribute);

    connect(attrDefTable_, &QTableWidget::itemChanged,
            this, &AttributesWidget::onAttrDefTableItemChanged);
    connect(attrDefTable_, &QTableWidget::currentCellChanged,
            this, [this](int currentRow, int, int, int) {
                currentAttrIndex_ = currentRow;
                refreshAttrEnumPanel();
            });

    attrDefTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(attrDefTable_, &QTableWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        QMenu menu(this);
        auto* addAct = menu.addAction("Add Attribute");
        connect(addAct, &QAction::triggered, this, &AttributesWidget::onAddAttribute);
        if (attrDefTable_->itemAt(pos)) {
            menu.addSeparator();
            auto* delAct = menu.addAction("Delete Attribute");
            connect(delAct, &QAction::triggered, this, &AttributesWidget::onDeleteAttribute);
        }
        menu.exec(attrDefTable_->mapToGlobal(pos));
    });

    // Enumeration Values panel
    auto* enumGroupBox = new QGroupBox("Enumeration Values", this);
    attrEnumPanel_ = enumGroupBox;
    auto* enumPanelLayout = new QVBoxLayout(enumGroupBox);
    enumPanelLayout->setContentsMargins(6, 6, 6, 6);
    enumPanelLayout->setSpacing(4);

    auto* enumDefaultForm = new QFormLayout();
    enumDefaultForm->setLabelAlignment(Qt::AlignRight);
    attrEnumDefaultCombo_ = new QComboBox(enumGroupBox);
    enumDefaultForm->addRow("Default:", attrEnumDefaultCombo_);
    enumPanelLayout->addLayout(enumDefaultForm);

    attrEnumTable_ = new QTableWidget(0, 1, enumGroupBox);
    attrEnumTable_->setHorizontalHeaderLabels({"Value"});
    attrEnumTable_->horizontalHeader()->setStretchLastSection(true);
    attrEnumTable_->horizontalHeader()->setSectionsMovable(false);
    attrEnumTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    attrEnumTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    enumPanelLayout->addWidget(attrEnumTable_, 1);

    auto* enumBtnRow = new QHBoxLayout();
    auto* enumAddBtn = new QPushButton("Add Value", enumGroupBox);
    auto* enumDelBtn = new QPushButton("Remove Value", enumGroupBox);
    enumBtnRow->addWidget(enumAddBtn);
    enumBtnRow->addWidget(enumDelBtn);
    enumBtnRow->addStretch();
    enumPanelLayout->addLayout(enumBtnRow);

    mainLayout->addWidget(attrEnumPanel_);
    attrEnumPanel_->hide();

    connect(enumAddBtn, &QPushButton::clicked, this, &AttributesWidget::onAddEnumValue);
    connect(enumDelBtn, &QPushButton::clicked, this, &AttributesWidget::onDeleteEnumValue);
    connect(attrEnumTable_, &QTableWidget::itemChanged,
            this, &AttributesWidget::onAttrEnumItemChanged);

    attrEnumTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(attrEnumTable_, &QTableWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        if (attrFormUpdating_ || currentAttrIndex_ < 0) { return; }
        QMenu menu(this);
        auto* addAct = menu.addAction("Add Value");
        connect(addAct, &QAction::triggered, this, &AttributesWidget::onAddEnumValue);
        if (attrEnumTable_->indexAt(pos).isValid()) {
            auto* delAct = menu.addAction("Remove Value");
            connect(delAct, &QAction::triggered, this, &AttributesWidget::onDeleteEnumValue);
        }
        menu.exec(attrEnumTable_->mapToGlobal(pos));
    });

    connect(attrEnumDefaultCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (attrFormUpdating_ || currentAttrIndex_ < 0 ||
            currentAttrIndex_ >= db_->attributes.size()) { return; }
        DbcAttributeDef& attr = db_->attributes[currentAttrIndex_];
        if (idx >= 0 && idx < attr.enumValues.size()) {
            attr.defaultValue = attr.enumValues[idx];
        }
        attrFormUpdating_ = true;
        if (auto* cell = attrDefTable_->item(currentAttrIndex_, 3)) {
            cell->setText(attr.defaultValue);
        }
        attrFormUpdating_ = false;
        emit dataModified();
    });
}

void AttributesWidget::refresh()
{
    if (!attrDefTable_) { return; }
    const QSignalBlocker tb(attrDefTable_);
    attrDefTable_->setRowCount(0);
    for (int i = 0; i < db_->attributes.size(); ++i) {
        attrDefTable_->insertRow(i);
        refreshAttrTableRow(i);
    }
    if (!db_->attributes.isEmpty()) {
        const int row = qBound(0, currentAttrIndex_, db_->attributes.size() - 1);
        attrDefTable_->selectRow(row);
        currentAttrIndex_ = row;
    } else {
        currentAttrIndex_ = -1;
    }
    refreshAttrEnumPanel();
}

void AttributesWidget::deleteSelected()
{
    onDeleteAttribute();
}

void AttributesWidget::refreshAttrTableRow(int row)
{
    if (!attrDefTable_ || row < 0 || row >= db_->attributes.size()) { return; }
    const DbcAttributeDef& attr = db_->attributes[row];

    static const QStringList kObjTypeNames  = {"Network","Node","Message","Signal"};
    static const QStringList kValueTypeNames = {"Integer","Float","String","Enumeration","Hex"};

    const bool isStrOrEnum = (attr.valueType == DbcAttributeDef::ValueType::String ||
                              attr.valueType == DbcAttributeDef::ValueType::Enumeration);
    const bool isEnum      = (attr.valueType == DbcAttributeDef::ValueType::Enumeration);

    const Qt::ItemFlags editFlags     = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
    const Qt::ItemFlags readOnlyFlags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;

    auto* objTypeItem = new QTableWidgetItem(kObjTypeNames.value(static_cast<int>(attr.objectType)));
    auto* nameItem    = new QTableWidgetItem(attr.name);
    auto* vtItem      = new QTableWidgetItem(kValueTypeNames.value(static_cast<int>(attr.valueType)));
    auto* defaultItem = new QTableWidgetItem(attr.defaultValue);

    objTypeItem->setFlags(editFlags);
    nameItem->setFlags(editFlags);
    vtItem->setFlags(editFlags);

    QTableWidgetItem* minItem;
    QTableWidgetItem* maxItem;

    if (isStrOrEnum) {
        minItem = new QTableWidgetItem("-");
        maxItem = new QTableWidgetItem("-");
        minItem->setFlags(readOnlyFlags);
        maxItem->setFlags(readOnlyFlags);
        minItem->setForeground(QApplication::palette().color(QPalette::Disabled, QPalette::Text));
        maxItem->setForeground(QApplication::palette().color(QPalette::Disabled, QPalette::Text));
    } else {
        minItem = new QTableWidgetItem(attr.minimum);
        maxItem = new QTableWidgetItem(attr.maximum);
        minItem->setFlags(editFlags);
        maxItem->setFlags(editFlags);
    }

    if (isEnum) {
        defaultItem->setFlags(readOnlyFlags);
        defaultItem->setForeground(QApplication::palette().color(QPalette::Disabled, QPalette::Text));
        defaultItem->setToolTip("Use the Enumeration Values panel below to change the default.");
    } else {
        defaultItem->setFlags(editFlags);
    }

    attrDefTable_->setItem(row, 0, objTypeItem);
    attrDefTable_->setItem(row, 1, nameItem);
    attrDefTable_->setItem(row, 2, vtItem);
    attrDefTable_->setItem(row, 3, defaultItem);
    attrDefTable_->setItem(row, 4, minItem);
    attrDefTable_->setItem(row, 5, maxItem);
}

void AttributesWidget::refreshAttrEnumPanel()
{
    if (!attrEnumPanel_) { return; }
    if (currentAttrIndex_ < 0 || currentAttrIndex_ >= db_->attributes.size()) {
        attrEnumPanel_->hide();
        return;
    }
    const DbcAttributeDef& attr = db_->attributes[currentAttrIndex_];
    if (attr.valueType != DbcAttributeDef::ValueType::Enumeration) {
        attrEnumPanel_->hide();
        return;
    }
    attrEnumPanel_->show();

    const QSignalBlocker eb(attrEnumTable_);
    attrEnumTable_->setRowCount(0);
    for (const QString& v : attr.enumValues) {
        const int r = attrEnumTable_->rowCount();
        attrEnumTable_->insertRow(r);
        attrEnumTable_->setItem(r, 0, new QTableWidgetItem(v));
    }
    refreshAttrEnumCombo();
}

void AttributesWidget::refreshAttrEnumCombo()
{
    if (!attrEnumDefaultCombo_ || currentAttrIndex_ < 0 ||
        currentAttrIndex_ >= db_->attributes.size()) { return; }
    const QSignalBlocker eb(attrEnumDefaultCombo_);
    const DbcAttributeDef& attr = db_->attributes[currentAttrIndex_];
    attrEnumDefaultCombo_->clear();
    attrEnumDefaultCombo_->addItems(attr.enumValues);
    const int idx = attr.enumValues.indexOf(attr.defaultValue);
    attrEnumDefaultCombo_->setCurrentIndex(idx >= 0 ? idx : 0);

    attrFormUpdating_ = true;
    if (auto* cell = attrDefTable_ ? attrDefTable_->item(currentAttrIndex_, 3) : nullptr) {
        cell->setText(attr.defaultValue);
    }
    attrFormUpdating_ = false;
}

void AttributesWidget::onAttrDefTableItemChanged(QTableWidgetItem* item)
{
    if (!item || attrFormUpdating_) { return; }
    const int row = item->row();
    const int col = item->column();
    if (row < 0 || row >= db_->attributes.size()) { return; }

    DbcAttributeDef& attr = db_->attributes[row];
    attrFormUpdating_ = true;

    static const QStringList kObjTypeNames  = {"Network","Node","Message","Signal"};
    static const QStringList kValueTypeNames = {"Integer","Float","String","Enumeration","Hex"};

    switch (col) {
        case 0: {
            const int idx = kObjTypeNames.indexOf(item->text());
            if (idx >= 0) { attr.objectType = static_cast<DbcAttributeDef::ObjectType>(idx); }
            emit dataModified();
            break;
        }
        case 1: {
            const QString newName = item->text().trimmed();
            if (!newName.isEmpty()) { attr.name = newName; }
            emit dataModified();
            break;
        }
        case 2: {
            const int idx = kValueTypeNames.indexOf(item->text());
            if (idx >= 0) {
                const auto newType = static_cast<DbcAttributeDef::ValueType>(idx);
                if (newType != attr.valueType) {
                    attr.valueType = newType;
                    attr.defaultValue.clear();
                    attr.minimum.clear();
                    attr.maximum.clear();
                    refreshAttrTableRow(row);
                    emit dataModified();
                }
            }
            if (row == currentAttrIndex_) {
                attrFormUpdating_ = false;
                refreshAttrEnumPanel();
                attrFormUpdating_ = true;
            }
            break;
        }
        case 3: {
            attr.defaultValue = item->text().trimmed();
            emit dataModified();
            validateAttrNumericFields(row);
            break;
        }
        case 4: {
            attr.minimum = item->text().trimmed();
            if (attr.valueType == DbcAttributeDef::ValueType::Hex &&
                !attr.minimum.isEmpty() &&
                !attr.minimum.startsWith("0x", Qt::CaseInsensitive)) {
                attr.minimum = "0x" + attr.minimum;
                item->setText(attr.minimum);
            }
            emit dataModified();
            validateAttrNumericFields(row);
            break;
        }
        case 5: {
            attr.maximum = item->text().trimmed();
            if (attr.valueType == DbcAttributeDef::ValueType::Hex &&
                !attr.maximum.isEmpty() &&
                !attr.maximum.startsWith("0x", Qt::CaseInsensitive)) {
                attr.maximum = "0x" + attr.maximum;
                item->setText(attr.maximum);
            }
            emit dataModified();
            validateAttrNumericFields(row);
            break;
        }
    }

    attrFormUpdating_ = false;
}

void AttributesWidget::validateAttrNumericFields(int row)
{
    if (!attrValidationLabel_ || row < 0 || row >= db_->attributes.size()) { return; }
    const DbcAttributeDef& attr = db_->attributes[row];
    QString errMsg;

    if (attr.valueType == DbcAttributeDef::ValueType::Integer ||
        attr.valueType == DbcAttributeDef::ValueType::Hex) {
        bool okD = false, okMn = false, okMx = false;
        const qlonglong defV = attr.defaultValue.toLongLong(&okD, 0);
        const qlonglong mnV  = attr.minimum.toLongLong(&okMn, 0);
        const qlonglong mxV  = attr.maximum.toLongLong(&okMx, 0);
        if (okD && okMn && okMx) {
            if (mnV > mxV)
                errMsg = "Minimum must be \u2264 Maximum.";
            else if (defV < mnV || defV > mxV)
                errMsg = QString("Default (%1) is outside [%2, %3].")
                    .arg(attr.defaultValue, attr.minimum, attr.maximum);
        }
    } else if (attr.valueType == DbcAttributeDef::ValueType::Float) {
        bool okD = false, okMn = false, okMx = false;
        const double defV = attr.defaultValue.toDouble(&okD);
        const double mnV  = attr.minimum.toDouble(&okMn);
        const double mxV  = attr.maximum.toDouble(&okMx);
        if (okD && okMn && okMx) {
            if (mnV > mxV)
                errMsg = "Minimum must be \u2264 Maximum.";
            else if (defV < mnV || defV > mxV)
                errMsg = QString("Default (%1) is outside [%2, %3].")
                    .arg(attr.defaultValue, attr.minimum, attr.maximum);
        }
    }

    if (errMsg.isEmpty()) {
        attrValidationLabel_->hide();
    } else {
        attrValidationLabel_->setText(errMsg);
        attrValidationLabel_->show();
    }
}

void AttributesWidget::onAddAttribute()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "Add Attribute", "Attribute name:",
        QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) { return; }

    for (const DbcAttributeDef& a : db_->attributes) {
        if (a.name == name) {
            QMessageBox::warning(this, "Duplicate Name",
                QString("An attribute named \"%1\" already exists.").arg(name));
            return;
        }
    }

    DbcAttributeDef attr;
    attr.name = name;
    db_->attributes.append(attr);
    emit dataModified();
    refresh();
    const int newIdx = db_->attributes.size() - 1;
    attrDefTable_->selectRow(newIdx);
    currentAttrIndex_ = newIdx;
    refreshAttrEnumPanel();
}

void AttributesWidget::onDeleteAttribute()
{
    const int row = attrDefTable_ ? attrDefTable_->currentRow() : -1;
    if (row < 0 || row >= db_->attributes.size()) { return; }
    const QString name = db_->attributes[row].name;
    if (QMessageBox::question(this, "Delete Attribute",
            QString("Delete attribute definition \"%1\"?").arg(name),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) { return; }

    db_->attributes.removeAt(row);
    emit dataModified();
    refresh();
}

void AttributesWidget::onAddEnumValue()
{
    if (attrFormUpdating_ || currentAttrIndex_ < 0 ||
        currentAttrIndex_ >= db_->attributes.size()) { return; }
    DbcAttributeDef& attr = db_->attributes[currentAttrIndex_];
    attr.enumValues.append(QString("Value%1").arg(attr.enumValues.size()));
    emit dataModified();
    {
        const QSignalBlocker eb(attrEnumTable_);
        const int r = attrEnumTable_->rowCount();
        attrEnumTable_->insertRow(r);
        attrEnumTable_->setItem(r, 0, new QTableWidgetItem(attr.enumValues.last()));
    }
    refreshAttrEnumCombo();
    attrEnumTable_->scrollToBottom();
    attrEnumTable_->editItem(attrEnumTable_->item(attrEnumTable_->rowCount() - 1, 0));
}

void AttributesWidget::onDeleteEnumValue()
{
    if (attrFormUpdating_ || currentAttrIndex_ < 0 ||
        currentAttrIndex_ >= db_->attributes.size()) { return; }
    const auto sel = attrEnumTable_->selectionModel()
        ? attrEnumTable_->selectionModel()->selectedRows() : QModelIndexList{};
    if (sel.isEmpty()) { return; }
    const int row = sel.first().row();
    DbcAttributeDef& attr = db_->attributes[currentAttrIndex_];
    if (row < 0 || row >= attr.enumValues.size()) { return; }
    {
        const QSignalBlocker eb(attrEnumTable_);
        attrEnumTable_->removeRow(row);
        attr.enumValues.removeAt(row);
    }
    emit dataModified();
    refreshAttrEnumCombo();
}

void AttributesWidget::onAttrEnumItemChanged(QTableWidgetItem* item)
{
    if (!item || attrFormUpdating_) { return; }
    if (currentAttrIndex_ < 0 || currentAttrIndex_ >= db_->attributes.size()) { return; }
    DbcAttributeDef& attr = db_->attributes[currentAttrIndex_];
    const int row = item->row();
    if (row < 0 || row >= attr.enumValues.size()) { return; }
    attr.enumValues[row] = item->text();
    emit dataModified();
    refreshAttrEnumCombo();
}
