#pragma once

// All shared dialog and delegate classes used across view widgets.
// None of these classes use Q_OBJECT directly, so they can be safely
// included in multiple translation units.

#include "dbcutil.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

// ── ComboBoxDelegate ─────────────────────────────────────────────────────────
// Used by the attribute definitions table for the Object Type and Value Type
// columns so that editing those cells shows a QComboBox.
class ComboBoxDelegate : public QStyledItemDelegate
{
public:
    ComboBoxDelegate(QStringList items, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), items_(std::move(items)) {}

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
                          const QModelIndex&) const override
    {
        auto* cb = new QComboBox(parent);
        cb->addItems(items_);
        return cb;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        auto* cb = static_cast<QComboBox*>(editor);
        const QString val = index.data(Qt::EditRole).toString();
        const int idx = items_.indexOf(val);
        cb->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override
    {
        auto* cb = static_cast<QComboBox*>(editor);
        model->setData(index, cb->currentText(), Qt::EditRole);
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                               const QModelIndex&) const override
    {
        editor->setGeometry(option.rect);
    }

private:
    QStringList items_;
};

// ── FocusComboBox ─────────────────────────────────────────────────────────────
// QComboBox that opens/closes its popup with Enter and navigates with Up/Down
// when used as a cell widget inside QTableWidget (which normally eats those keys).
class FocusComboBox : public QComboBox
{
public:
    using QComboBox::QComboBox;
protected:
    void keyPressEvent(QKeyEvent* e) override
    {
        const int key = e->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            if (view()->isVisible()) {
                const int idx = view()->currentIndex().row();
                if (idx >= 0) { setCurrentIndex(idx); }
                hidePopup();
            } else {
                showPopup();
            }
            e->accept();
            return;
        }
        QComboBox::keyPressEvent(e);
    }
};

// ── MessagePickerDelegate ─────────────────────────────────────────────────────
// Delegate for the "Message" column of the signals-view table.
class MessagePickerDelegate : public QStyledItemDelegate
{
public:
    explicit MessagePickerDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void setMessageNames(const QStringList& names) { msgNames_ = names; }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
                          const QModelIndex&) const override
    {
        auto* combo = new FocusComboBox(parent);
        combo->addItems(msgNames_);
        return combo;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        static_cast<QComboBox*>(editor)->setCurrentText(
            index.data(Qt::DisplayRole).toString());
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override
    {
        model->setData(index, static_cast<QComboBox*>(editor)->currentText(),
                       Qt::EditRole);
    }

private:
    QStringList msgNames_;
};

// ── ChooseObjectsDialog ───────────────────────────────────────────────────────
class ChooseObjectsDialog : public QDialog
{
public:
    explicit ChooseObjectsDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Choose Objects");
        resize(760, 440);

        auto* rootLayout = new QVBoxLayout(this);
        auto* formLayout = new QFormLayout();

        filterByCombo_ = new QComboBox(this);
        filterByCombo_->addItems({"Name", "Length [Bit]", "Byte Order", "Value Type", "Message(s)"});
        formLayout->addRow("Filter by:", filterByCombo_);

        valueEdit_ = new QLineEdit(this);
        formLayout->addRow("Value:", valueEdit_);

        rootLayout->addLayout(formLayout);

        table_ = new QTableWidget(this);
        table_->setColumnCount(5);
        table_->setHorizontalHeaderLabels({"Name", "Length [Bit]", "Byte Order", "Value Type", "Message(s)"});
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        table_->horizontalHeader()->setStretchLastSection(true);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        rootLayout->addWidget(table_);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        rootLayout->addWidget(buttons);
    }

private:
    QComboBox*  filterByCombo_ = nullptr;
    QLineEdit*  valueEdit_      = nullptr;
    QTableWidget* table_        = nullptr;
};

// ── AddMessageDialog ──────────────────────────────────────────────────────────
class AddMessageDialog : public QDialog
{
public:
    explicit AddMessageDialog(const QStringList& nodeNames, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Add Message");
        resize(620, 420);

        auto* rootLayout = new QVBoxLayout(this);
        auto* tabs = new QTabWidget(this);

        // ── Definition tab ────────────────────────────────────────────────
        auto* definitionTab = new QWidget(tabs);
        auto* definitionForm = new QFormLayout(definitionTab);

        nameEdit_ = new QLineEdit(definitionTab);
        nameEdit_->setValidator(new QRegularExpressionValidator(
            QRegularExpression("^[A-Za-z0-9_]*$"), nameEdit_));
        definitionForm->addRow("Name:", nameEdit_);

        typeCombo_ = new QComboBox(definitionTab);
        typeCombo_->addItems({"CAN Standard", "CAN Extended"});
        definitionForm->addRow("Type:", typeCombo_);

        connect(typeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
            QString idText = idEdit_->text().trimmed();
            if (idText.isEmpty()) { idEdit_->setMaxLength(isExtended() ? 8 : 3); return; }
            bool ok = false;
            const qulonglong idValue = idText.toULongLong(&ok, 16);
            if (!ok) { idEdit_->setMaxLength(isExtended() ? 8 : 3); return; }
            if (!isExtended() && idValue > 0x7FFULL) { idEdit_->clear(); }
            if (!isExtended() && idText.length() > 3) { idEdit_->clear(); }
            idEdit_->setMaxLength(isExtended() ? 8 : 3);
        });

        auto* idDlcRow = new QWidget(definitionTab);
        auto* idDlcLayout = new QHBoxLayout(idDlcRow);
        idDlcLayout->setContentsMargins(0, 0, 0, 0);

        idDlcLayout->addWidget(new QLabel("ID:", idDlcRow));
        idEdit_ = new QLineEdit(idDlcRow);
        idEdit_->setPlaceholderText("Hex");
        idEdit_->setValidator(new QRegularExpressionValidator(
            QRegularExpression("^[0-9A-Fa-f]*$"), idEdit_));
        idEdit_->setMaxLength(3);
        idDlcLayout->addWidget(idEdit_, 1);

        idDlcLayout->addSpacing(12);
        idDlcLayout->addWidget(new QLabel("DLC:", idDlcRow));
        dlcEdit_ = new QLineEdit(idDlcRow);
        dlcEdit_->setPlaceholderText("DLC");
        allowedDlcValues_ = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
        dlcEdit_->setValidator(new QRegularExpressionValidator(
            QRegularExpression("^(0|1|2|3|4|5|6|7|8|12|16|20|24|32|48|64)$"), dlcEdit_));
        dlcEdit_->setText("8");
        idDlcLayout->addWidget(dlcEdit_);
        idDlcLayout->addStretch(1);
        definitionForm->addRow(idDlcRow);

        transmitterCombo_ = new QComboBox(definitionTab);
        transmitterCombo_->setEditable(false);
        transmitterCombo_->addItem("Vector__XXX");
        for (const QString& nodeName : nodeNames) {
            if (nodeName != "Vector__XXX" && isValidSymbolName(nodeName))
                transmitterCombo_->addItem(nodeName);
        }
        transmitterCombo_->setCurrentIndex(0);
        definitionForm->addRow("Transmitter:", transmitterCombo_);

        cycleTimeSpin_ = new QSpinBox(definitionTab);
        cycleTimeSpin_->setRange(0, 65535);
        cycleTimeSpin_->setValue(0);
        cycleTimeSpin_->setSuffix(" ms");
        definitionForm->addRow("Cycle Time:", cycleTimeSpin_);

        definitionForm->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
        tabs->addTab(definitionTab, "Definition");

        // ── Signals tab (placeholder) ─────────────────────────────────────
        auto* signalsTab = new QWidget(tabs);
        auto* signalsLayout = new QVBoxLayout(signalsTab);
        auto* signalsTable = new QTableWidget(signalsTab);
        signalsTable->setColumnCount(7);
        signalsTable->setHorizontalHeaderLabels(
            {"Signal", "Message", "Multiplexing/Group", "Startbit", "Length [Bit]", "Byte Order", "Value Type"});
        signalsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        signalsTable->horizontalHeader()->setStretchLastSection(true);
        signalsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        signalsTable->setSelectionMode(QAbstractItemView::SingleSelection);
        signalsLayout->addWidget(signalsTable);

        auto* signalsButtonRow = new QHBoxLayout();
        auto* addSignalButton = new QPushButton("Add", signalsTab);
        auto* removeSignalButton = new QPushButton("Remove", signalsTab);
        connect(addSignalButton, &QPushButton::clicked, this, [this]() {
            ChooseObjectsDialog dialog(this);
            dialog.exec();
        });
        signalsButtonRow->addWidget(addSignalButton);
        signalsButtonRow->addWidget(removeSignalButton);
        signalsButtonRow->addStretch(1);
        signalsLayout->addLayout(signalsButtonRow);
        tabs->addTab(signalsTab, "Signals");

        const QStringList tabNames = {"Transmitters", "Receivers", "Layout", "Attributes", "Comment"};
        for (const QString& tabName : tabNames) {
            auto* tab = new QWidget(tabs);
            auto* layout = new QVBoxLayout(tab);
            auto* label = new QLabel(QString("%1 settings will be added here.").arg(tabName), tab);
            label->setWordWrap(true);
            layout->addWidget(label);
            layout->addStretch(1);
            tabs->addTab(tab, tabName);
        }

        rootLayout->addWidget(tabs);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &AddMessageDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        rootLayout->addWidget(buttons);
    }

    QString messageName() const { return nameEdit_->text().trimmed(); }
    bool isExtended() const { return typeCombo_->currentIndex() == 1; }
    quint32 messageId() const { return messageId_; }
    int dlc() const { return dlc_; }
    int cycleTimeMs() const { return cycleTimeSpin_->value(); }
    QString transmitter() const { return transmitterCombo_->currentText().trimmed(); }

    void accept() override
    {
        const QString name = messageName();
        if (name.isEmpty()) {
            QMessageBox::warning(this, "Invalid Message", "Name cannot be empty.");
            return;
        }
        if (!isValidSymbolName(name)) {
            QMessageBox::warning(this, "Invalid Message",
                "Name must start with a letter or underscore and contain only A-Z, a-z, 0-9, or underscore.");
            return;
        }
        const QString idText = idEdit_->text().trimmed();
        const QString maxText = isExtended() ? "1FFFFFFF" : "7FF";
        if (idText.isEmpty()) {
            QMessageBox::warning(this, "Invalid Message",
                QString("Please enter a hex ID (0 to %1).").arg(maxText));
            return;
        }
        bool ok = false;
        const qulonglong idValue = idText.toULongLong(&ok, 16);
        if (!ok) {
            QMessageBox::warning(this, "Invalid Message",
                QString("Error: ID is outside of Range\nAllowable Range is 0 to %1").arg(maxText));
            return;
        }
        const qulonglong maxId = isExtended() ? 0x1FFFFFFFULL : 0x7FFULL;
        if (idValue > maxId) {
            QMessageBox::warning(this, "Invalid Message",
                QString("Error: ID is outside of Range\nAllowable Range is 0 to %1").arg(maxText));
            return;
        }
        const QString dlcText = dlcEdit_->text().trimmed();
        const int dlcValue = dlcText.toInt(&ok);
        if (!ok || !allowedDlcValues_.contains(dlcValue)) {
            QMessageBox::warning(this, "Invalid Message",
                "DLC must be one of: 0-8, 12, 16, 20, 24, 32, 48, 64.");
            return;
        }
        const QString tx = transmitter();
        if (tx.isEmpty()) {
            QMessageBox::warning(this, "Invalid Message", "Transmitter cannot be empty.");
            return;
        }
        messageId_ = static_cast<quint32>(idValue);
        dlc_ = dlcValue;
        QDialog::accept();
    }

private:
    QLineEdit*   nameEdit_          = nullptr;
    QComboBox*   typeCombo_         = nullptr;
    QLineEdit*   idEdit_            = nullptr;
    QLineEdit*   dlcEdit_           = nullptr;
    QComboBox*   transmitterCombo_  = nullptr;
    QSpinBox*    cycleTimeSpin_     = nullptr;
    QList<int>   allowedDlcValues_;
    quint32      messageId_         = 0;
    int          dlc_               = 8;
};

// ── SettingsDialog ────────────────────────────────────────────────────────────
class SettingsDialog : public QDialog
{
public:
    explicit SettingsDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Settings");
        resize(400, 200);

        auto* outerLayout = new QVBoxLayout(this);
        auto* groupBox = new QGroupBox("Default Values", this);
        auto* formLayout = new QFormLayout(groupBox);

        signalByteOrderCombo_ = new QComboBox(groupBox);
        signalByteOrderCombo_->addItems({"Last Value", "Intel", "Motorola"});
        formLayout->addRow("Signal Byte Order:", signalByteOrderCombo_);

        signalValueTypeCombo_ = new QComboBox(groupBox);
        signalValueTypeCombo_->addItems({"Last Value", "Unsigned", "Signed", "Float", "Double"});
        formLayout->addRow("Signal Value Type:", signalValueTypeCombo_);

        messageTypeCombo_ = new QComboBox(groupBox);
        messageTypeCombo_->addItems({"Last Value", "CAN Standard", "CAN Extended"});
        formLayout->addRow("Message Type:", messageTypeCombo_);

        outerLayout->addWidget(groupBox);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        outerLayout->addWidget(buttons);

        load();
    }

    void load()
    {
        QSettings settings;
        signalByteOrderCombo_->setCurrentText(settings.value("defaults/signals/byteOrder", "Last Value").toString());
        signalValueTypeCombo_->setCurrentText(settings.value("defaults/signals/valueType",  "Last Value").toString());
        messageTypeCombo_->setCurrentText(    settings.value("defaults/messages/type",       "Last Value").toString());
    }

    void save() const
    {
        QSettings settings;
        settings.setValue("defaults/signals/byteOrder", signalByteOrderCombo_->currentText());
        settings.setValue("defaults/signals/valueType",  signalValueTypeCombo_->currentText());
        settings.setValue("defaults/messages/type",       messageTypeCombo_->currentText());
    }

private:
    QComboBox* signalByteOrderCombo_ = nullptr;
    QComboBox* signalValueTypeCombo_ = nullptr;
    QComboBox* messageTypeCombo_     = nullptr;
};

// ── ObjectAttrDialog ──────────────────────────────────────────────────────────
// Dialog for editing per-object attribute values (BA_ lines).
class ObjectAttrDialog : public QDialog
{
public:
    explicit ObjectAttrDialog(const QString& title,
                              const QList<DbcAttributeDef>& defs,
                              const QMap<QString, QString>& current,
                              QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(title);
        setMinimumWidth(460);
        resize(460, 380);

        auto* outerLayout = new QVBoxLayout(this);
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);

        auto* container = new QWidget;
        auto* form = new QFormLayout(container);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setRowWrapPolicy(QFormLayout::DontWrapRows);
        form->setContentsMargins(8, 8, 8, 8);

        for (const DbcAttributeDef& def : defs) {
            const QString stored = current.value(def.name, def.defaultValue);
            QWidget* editor = nullptr;

            if (def.valueType == DbcAttributeDef::ValueType::Enumeration) {
                auto* combo = new QComboBox(container);
                combo->addItems(def.enumValues);
                bool ok = false;
                const int idx = stored.toInt(&ok);
                if (ok && idx >= 0 && idx < def.enumValues.size())
                    combo->setCurrentIndex(idx);
                editors_.append({def.name, def.valueType, {}, {}, nullptr, combo});
                editor = combo;
            } else {
                auto* le = new QLineEdit(stored, container);
                if (def.valueType == DbcAttributeDef::ValueType::Integer) {
                    le->setValidator(new QRegularExpressionValidator(
                        QRegularExpression(R"RE(-?\d*)RE"), le));
                } else if (def.valueType == DbcAttributeDef::ValueType::Hex) {
                    le->setValidator(new QRegularExpressionValidator(
                        QRegularExpression(R"RE(-?(?:0[xX])?[0-9A-Fa-f]*)RE"), le));
                } else if (def.valueType == DbcAttributeDef::ValueType::Float) {
                    le->setValidator(new QDoubleValidator(le));
                }
                if (def.valueType != DbcAttributeDef::ValueType::String) {
                    const QString capName = def.name;
                    const auto    capVt   = def.valueType;
                    const QString capMin  = def.minimum;
                    const QString capMax  = def.maximum;
                    connect(le, &QLineEdit::editingFinished, this,
                            [this, le, capName, capVt, capMin, capMax]() {
                        const QString valStr = le->text().trimmed();
                        if (valStr.isEmpty()) { return; }
                        QString errMsg;
                        if (capVt == DbcAttributeDef::ValueType::Float) {
                            bool ok = false;
                            const double v = valStr.toDouble(&ok);
                            if (!ok) {
                                errMsg = QString("'%1': \"%2\" is not a valid floating-point number.").arg(capName, valStr);
                            } else if (!capMin.isEmpty() && !capMax.isEmpty()) {
                                bool okMn = false, okMx = false;
                                const double mnV = capMin.toDouble(&okMn);
                                const double mxV = capMax.toDouble(&okMx);
                                if (okMn && okMx && (v < mnV || v > mxV))
                                    errMsg = QString("'%1': value %2 is outside the allowed range [%3, %4].")
                                        .arg(capName, valStr, capMin, capMax);
                            }
                        } else {
                            bool ok = false;
                            const qlonglong v = valStr.toLongLong(&ok, 0);
                            if (!ok) {
                                const QString typeName = (capVt == DbcAttributeDef::ValueType::Hex)
                                    ? QString("hexadecimal integer (e.g. 0x1F or 31)")
                                    : QString("integer");
                                errMsg = QString("'%1': \"%2\" is not a valid %3.").arg(capName, valStr, typeName);
                            } else if (!capMin.isEmpty() && !capMax.isEmpty()) {
                                bool okMn = false, okMx = false;
                                const qlonglong mnV = capMin.toLongLong(&okMn, 0);
                                const qlonglong mxV = capMax.toLongLong(&okMx, 0);
                                if (okMn && okMx && (v < mnV || v > mxV))
                                    errMsg = QString("'%1': value %2 is outside the allowed range [%3, %4].")
                                        .arg(capName, valStr, capMin, capMax);
                            }
                        }
                        if (!errMsg.isEmpty()) {
                            QTimer::singleShot(0, this, [this, le, errMsg]() {
                                QMessageBox::warning(this, "Invalid Value", errMsg);
                                le->setFocus();
                                le->selectAll();
                            });
                        }
                    });
                }
                editors_.append({def.name, def.valueType, def.minimum, def.maximum, le, nullptr});
                editor = le;
            }
            if (editor)
                form->addRow(def.name + ":", editor);
        }

        if (defs.isEmpty()) {
            form->addRow(new QLabel("No attribute definitions for this object type.", container));
        }

        scroll->setWidget(container);
        outerLayout->addWidget(scroll);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            for (const EditorEntry& e : editors_) {
                if (!e.lineEdit) { continue; }
                if (e.minimum.isEmpty() || e.maximum.isEmpty()) { continue; }
                const QString valStr = e.lineEdit->text().trimmed();
                QString errMsg;
                if (e.valueType == DbcAttributeDef::ValueType::Float) {
                    bool okV = false, okMn = false, okMx = false;
                    const double v  = valStr.toDouble(&okV);
                    const double mn = e.minimum.toDouble(&okMn);
                    const double mx = e.maximum.toDouble(&okMx);
                    if (okV && okMn && okMx && (v < mn || v > mx))
                        errMsg = QString("'%1': value %2 is outside the allowed range [%3, %4].")
                            .arg(e.name, valStr, e.minimum, e.maximum);
                } else {
                    bool okV = false, okMn = false, okMx = false;
                    const qlonglong v  = valStr.toLongLong(&okV, 0);
                    const qlonglong mn = e.minimum.toLongLong(&okMn, 0);
                    const qlonglong mx = e.maximum.toLongLong(&okMx, 0);
                    if (okV && okMn && okMx && (v < mn || v > mx))
                        errMsg = QString("'%1': value %2 is outside the allowed range [%3, %4].")
                            .arg(e.name, valStr, e.minimum, e.maximum);
                }
                if (!errMsg.isEmpty()) {
                    QMessageBox::warning(this, "Value Out of Range", errMsg);
                    e.lineEdit->setFocus();
                    e.lineEdit->selectAll();
                    return;
                }
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        outerLayout->addWidget(buttons);
    }

    QMap<QString, QString> result() const
    {
        QMap<QString, QString> out;
        for (const EditorEntry& e : editors_) {
            if (e.valueType == DbcAttributeDef::ValueType::Enumeration) {
                out[e.name] = QString::number(e.combo->currentIndex());
            } else {
                out[e.name] = e.lineEdit->text().trimmed();
            }
        }
        return out;
    }

private:
    struct EditorEntry {
        QString name;
        DbcAttributeDef::ValueType valueType;
        QString minimum;
        QString maximum;
        QLineEdit* lineEdit = nullptr;
        QComboBox* combo    = nullptr;
    };
    QVector<EditorEntry> editors_;
};

// ── ValueTableEditorDialog ────────────────────────────────────────────────────
class ValueTableEditorDialog : public QDialog
{
public:
    explicit ValueTableEditorDialog(const QString& title,
                                    const QString& currentName,
                                    const QList<DbcValueEntry>& entries,
                                    const QList<DbcValueTable>& globalTables,
                                    QWidget* parent = nullptr)
        : QDialog(parent), globalTables_(globalTables)
    {
        setWindowTitle(title);
        setMinimumSize(440, 420);
        resize(560, 500);

        auto* nameLabel = new QLabel("Table Name:", this);
        nameEdit_ = new QLineEdit(currentName, this);
        nameEdit_->setPlaceholderText("e.g. SPI_PinMode");

        globalCombo_ = new QComboBox(this);
        globalCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        globalCombo_->addItem("Load from global table\u2026");
        for (const DbcValueTable& vt : globalTables) {
            globalCombo_->addItem(vt.name);
        }
        connect(globalCombo_, &QComboBox::currentIndexChanged, this, [this](int idx) {
            if (idx <= 0 || idx - 1 >= globalTables_.size()) { return; }
            const DbcValueTable& vt = globalTables_[idx - 1];
            nameEdit_->setText(vt.name);
            const QSignalBlocker b(table_);
            table_->setRowCount(0);
            for (const DbcValueEntry& e : vt.entries) { addRow(e.rawValue, e.label); }
            const QSignalBlocker b2(globalCombo_);
            globalCombo_->setCurrentIndex(0);
        });

        auto* nameRow = new QHBoxLayout();
        nameRow->addWidget(nameLabel);
        nameRow->addWidget(nameEdit_, 1);
        nameRow->addWidget(globalCombo_);

        table_ = new QTableWidget(this);
        table_->setColumnCount(2);
        table_->setHorizontalHeaderLabels({"Raw Value", "Label"});
        table_->horizontalHeader()->setStretchLastSection(true);
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);

        for (const DbcValueEntry& e : entries) { addRow(e.rawValue, e.label); }

        auto* addBtn = new QPushButton("Add Entry", this);
        auto* delBtn = new QPushButton("Delete Entry", this);
        connect(addBtn, &QPushButton::clicked, this, [this] { addRow(0, QString{}); });
        connect(delBtn, &QPushButton::clicked, this, [this] {
            const auto sel = table_->selectionModel()->selectedRows();
            if (!sel.isEmpty()) { table_->removeRow(sel.first().row()); }
        });
        auto* btnRow = new QHBoxLayout();
        btnRow->addWidget(addBtn);
        btnRow->addWidget(delBtn);
        btnRow->addStretch();

        auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(btnBox, &QDialogButtonBox::accepted, this, [this] {
            if (nameEdit_->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, "Name Required",
                    "A table name is required. Please enter a name before clicking OK.");
                nameEdit_->setFocus();
                return;
            }
            accept();
        });
        connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto* layout = new QVBoxLayout(this);
        layout->addLayout(nameRow);
        layout->addWidget(table_, 1);
        layout->addLayout(btnRow);
        layout->addWidget(btnBox);
    }

    QString name() const { return nameEdit_->text().trimmed(); }

    QList<DbcValueEntry> entries() const
    {
        QList<DbcValueEntry> result;
        for (int row = 0; row < table_->rowCount(); ++row) {
            auto* valItem = table_->item(row, 0);
            auto* lblItem = table_->item(row, 1);
            if (!valItem || !lblItem) { continue; }
            bool ok = false;
            const qint64 val = valItem->text().trimmed().toLongLong(&ok);
            if (!ok) { continue; }
            result.append(DbcValueEntry{val, lblItem->text()});
        }
        return result;
    }

private:
    void addRow(qint64 rawValue, const QString& label)
    {
        const int row = table_->rowCount();
        table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(QString::number(rawValue)));
        table_->setItem(row, 1, new QTableWidgetItem(label));
        table_->scrollToItem(table_->item(row, 0));
    }

    QTableWidget*        table_       = nullptr;
    QLineEdit*           nameEdit_    = nullptr;
    QComboBox*           globalCombo_ = nullptr;
    QList<DbcValueTable> globalTables_;
};
