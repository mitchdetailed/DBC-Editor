#include "mainwindow.h"

#include "dbcdocument.h"

#include <QScrollArea>

#include <QAction>
#include <QShortcut>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDrag>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <functional>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSet>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QWidget>
#include <QGroupBox>
#include <limits>

// QTableWidgetItem subclass that sorts by numeric value rather than string.
class NumericTableWidgetItem : public QTableWidgetItem
{
public:
    explicit NumericTableWidgetItem(double value)
        : QTableWidgetItem(QString::number(value, 'g', 12)), numericValue_(value)
    {}
    explicit NumericTableWidgetItem(int value)
        : QTableWidgetItem(QString::number(value)), numericValue_(static_cast<double>(value))
    {}
    bool operator<(const QTableWidgetItem& other) const override
    {
        const auto* o = dynamic_cast<const NumericTableWidgetItem*>(&other);
        if (o) { return numericValue_ < o->numericValue_; }
        return QTableWidgetItem::operator<(other);
    }
private:
    double numericValue_;
};

#include <QLineEdit>
#include <algorithm>
#include <numeric>

// â”€â”€ ComboBoxDelegate â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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

namespace {
const QRegularExpression kSymbolNameRegex("^[A-Za-z_][A-Za-z0-9_]*$");

bool isValidSymbolName(const QString& name)
{
    return kSymbolNameRegex.match(name).hasMatch();
}

bool validateDatabaseRules(const DbcDatabase& database, QString& error)
{
    QSet<QString> nodeNames;
    for (const DbcNode& node : database.nodes) {
        if (nodeNames.contains(node.name)) {
            error = QString("Duplicate node name found: %1").arg(node.name);
            return false;
        }
        nodeNames.insert(node.name);
    }

    QSet<quint32> messageIds;
    QSet<QString> messageNames;

    for (const DbcMessage& message : database.messages) {
        if (!isValidSymbolName(message.name)) {
            error = QString("Invalid message name: %1\nAllowed pattern: [A-Za-z_][A-Za-z0-9_]*").arg(message.name);
            return false;
        }

        if (messageNames.contains(message.name)) {
            error = QString("Duplicate message name found: %1").arg(message.name);
            return false;
        }
        messageNames.insert(message.name);

        if (messageIds.contains(message.id)) {
            error = QString("Duplicate message ID found: 0x%1").arg(QString::number(message.id, 16).toUpper());
            return false;
        }
        messageIds.insert(message.id);

        for (const DbcSignal& dbcSig : message.signalList) {
            if (!isValidSymbolName(dbcSig.name)) {
                error = QString("Invalid signal name: %1\nAllowed pattern: [A-Za-z_][A-Za-z0-9_]*").arg(dbcSig.name);
                return false;
            }
        }
    }

    return true;
}

// Opens a modal multi-line comment editor. Calls onAccepted with the new text if OK is clicked.
static void openCommentEditor(QWidget* parent, const QString& currentText,
                              const std::function<void(const QString&)>& onAccepted)
{
    auto* dlg = new QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dlg->setWindowTitle("Edit Comment");
    dlg->resize(520, 300);
    auto* layout = new QVBoxLayout(dlg);
    auto* edit = new QPlainTextEdit(dlg);
    edit->setPlainText(currentText);
    layout->addWidget(edit, 1);
    auto* btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, dlg);
    layout->addWidget(btnBox);
    QObject::connect(btnBox, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(btnBox, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    if (dlg->exec() == QDialog::Accepted) {
        onAccepted(edit->toPlainText());
    }
    delete dlg;
}

class ChooseObjectsDialog : public QDialog {
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
    QComboBox* filterByCombo_ = nullptr;
    QLineEdit* valueEdit_ = nullptr;
    QTableWidget* table_ = nullptr;
};

class AddMessageDialog : public QDialog {
public:
    explicit AddMessageDialog(const QStringList& nodeNames, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Add Message");
        resize(620, 420);

        auto* rootLayout = new QVBoxLayout(this);
        auto* tabs = new QTabWidget(this);

        auto* definitionTab = new QWidget(tabs);
        auto* definitionForm = new QFormLayout(definitionTab);

        nameEdit_ = new QLineEdit(definitionTab);
        nameEdit_->setValidator(new QRegularExpressionValidator(QRegularExpression("^[A-Za-z0-9_]*$"), nameEdit_));
        definitionForm->addRow("Name:", nameEdit_);

        typeCombo_ = new QComboBox(definitionTab);
        typeCombo_->addItems({"CAN Standard", "CAN Extended"});
        definitionForm->addRow("Type:", typeCombo_);

        connect(typeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
            QString idText = idEdit_->text().trimmed();
            if (idText.isEmpty()) {
                idEdit_->setMaxLength(isExtended() ? 8 : 3);
                return;
            }

            bool ok = false;
            const qulonglong idValue = idText.toULongLong(&ok, 16);
            if (!ok) {
                idEdit_->setMaxLength(isExtended() ? 8 : 3);
                return;
            }

            // If switching to Standard and the current value no longer fits, clear it.
            if (!isExtended() && idValue > 0x7FFULL) {
                idEdit_->clear();
            }

            if (!isExtended() && idText.length() > 3) {
                idEdit_->clear();
            }

            idEdit_->setMaxLength(isExtended() ? 8 : 3);
        });

        auto* idDlcRow = new QWidget(definitionTab);
        auto* idDlcLayout = new QHBoxLayout(idDlcRow);
        idDlcLayout->setContentsMargins(0, 0, 0, 0);

        idDlcLayout->addWidget(new QLabel("ID:", idDlcRow));
        idEdit_ = new QLineEdit(idDlcRow);
        idEdit_->setPlaceholderText("Hex");
        idEdit_->setValidator(new QRegularExpressionValidator(QRegularExpression("^[0-9A-Fa-f]*$"), idEdit_));
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
            if (nodeName != "Vector__XXX" && isValidSymbolName(nodeName)) {
                transmitterCombo_->addItem(nodeName);
            }
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
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
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
            QMessageBox::warning(this,
                                 "Invalid Message",
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
            QMessageBox::warning(this,
                                 "Invalid Message",
                                 QString("Error: ID is outside of Range\nAllowable Range is 0 to %1").arg(maxText));
            return;
        }

        const qulonglong maxId = isExtended() ? 0x1FFFFFFFULL : 0x7FFULL;
        if (idValue > maxId) {
            QMessageBox::warning(this,
                                 "Invalid Message",
                                 QString("Error: ID is outside of Range\nAllowable Range is 0 to %1").arg(maxText));
            return;
        }

        const QString dlcText = dlcEdit_->text().trimmed();
        const int dlcValue = dlcText.toInt(&ok);
        if (!ok || !allowedDlcValues_.contains(dlcValue)) {
            QMessageBox::warning(this,
                                 "Invalid Message",
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
    QLineEdit* nameEdit_ = nullptr;
    QComboBox* typeCombo_ = nullptr;
    QLineEdit* idEdit_ = nullptr;
    QLineEdit* dlcEdit_ = nullptr;
    QComboBox* transmitterCombo_ = nullptr;
    QSpinBox* cycleTimeSpin_ = nullptr;
    QList<int> allowedDlcValues_;
    quint32 messageId_ = 0;
    int dlc_ = 8;
};

class SettingsDialog : public QDialog {
public:
    explicit SettingsDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Settings");
        resize(560, 320);

        auto* rootLayout = new QVBoxLayout(this);
        auto* tabs = new QTabWidget(this);
        auto* defaultsTab = new QWidget(tabs);
        auto* defaultsLayout = new QVBoxLayout(defaultsTab);

        auto* signalsGroup = new QGroupBox("Signals", defaultsTab);
        auto* signalsForm = new QFormLayout(signalsGroup);

        signalByteOrderCombo_ = new QComboBox(signalsGroup);
        signalByteOrderCombo_->addItems({"Big Endian", "Little Endian", "Last Value"});
        signalByteOrderCombo_->setCurrentText("Last Value");
        signalsForm->addRow("Byte Order:", signalByteOrderCombo_);

        signalValueTypeCombo_ = new QComboBox(signalsGroup);
        signalValueTypeCombo_->addItems({"Unsigned", "Signed", "IEEE Float", "IEEE Double", "Last Value"});
        signalValueTypeCombo_->setCurrentText("Last Value");
        signalsForm->addRow("Value Type:", signalValueTypeCombo_);

        auto* messagesGroup = new QGroupBox("Messages", defaultsTab);
        auto* messagesForm = new QFormLayout(messagesGroup);

        messageTypeCombo_ = new QComboBox(messagesGroup);
        messageTypeCombo_->addItems({"11 Bit Standard", "29 Bit Extended", "Last Value"});
        messageTypeCombo_->setCurrentText("Last Value");
        messagesForm->addRow("Type:", messageTypeCombo_);

        defaultsLayout->addWidget(signalsGroup);
        defaultsLayout->addWidget(messagesGroup);
        defaultsLayout->addStretch(1);

        tabs->addTab(defaultsTab, "Defaults");
        rootLayout->addWidget(tabs);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        rootLayout->addWidget(buttons);

        load();
    }

    void load()
    {
        QSettings settings;
        signalByteOrderCombo_->setCurrentText(settings.value("defaults/signals/byteOrder", "Last Value").toString());
        signalValueTypeCombo_->setCurrentText(settings.value("defaults/signals/valueType", "Last Value").toString());
        messageTypeCombo_->setCurrentText(settings.value("defaults/messages/type", "Last Value").toString());
    }

    void save() const
    {
        QSettings settings;
        settings.setValue("defaults/signals/byteOrder", signalByteOrderCombo_->currentText());
        settings.setValue("defaults/signals/valueType", signalValueTypeCombo_->currentText());
        settings.setValue("defaults/messages/type", messageTypeCombo_->currentText());
    }

private:
    QComboBox* signalByteOrderCombo_ = nullptr;
    QComboBox* signalValueTypeCombo_ = nullptr;
    QComboBox* messageTypeCombo_ = nullptr;
};

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
                // Confirm the highlighted item and close the popup.
                const int idx = view()->currentIndex().row();
                if (idx >= 0) { setCurrentIndex(idx); }
                hidePopup();
            } else {
                showPopup();
            }
            e->accept();
            return;
        }
        // Allow Up/Down to navigate the open popup or cycle while closed.
        QComboBox::keyPressEvent(e);
    }
};

// Delegate for the "Message" column of the signals-view table.
// Creates an editor combo box only when the user activates a cell,
// so the table builds instantly instead of creating thousands of real widgets.
class MessagePickerDelegate : public QStyledItemDelegate {
public:
    explicit MessagePickerDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void setMessageNames(const QStringList& names) { msgNames_ = names; }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
                          const QModelIndex&) const override {
        auto* combo = new FocusComboBox(parent);
        combo->addItems(msgNames_);
        return combo;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        static_cast<QComboBox*>(editor)->setCurrentText(
            index.data(Qt::DisplayRole).toString());
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override {
        model->setData(index, static_cast<QComboBox*>(editor)->currentText(),
                       Qt::EditRole);
    }

private:
    QStringList msgNames_;
};

// Dialog for editing per-object attribute values (BA_ lines).
// Shows one row per applicable DbcAttributeDef; uses an appropriate editor widget per type.
// Enum values are stored internally as their integer index string (matches DBC BA_ format).
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

                // Character-level validator (type-specific)
                if (def.valueType == DbcAttributeDef::ValueType::Integer) {
                    le->setValidator(new QRegularExpressionValidator(
                        QRegularExpression(R"RE(-?\d*)RE"), le));
                } else if (def.valueType == DbcAttributeDef::ValueType::Hex) {
                    // Allow plain digits or 0x-prefixed hex
                    le->setValidator(new QRegularExpressionValidator(
                        QRegularExpression(R"RE(-?(?:0[xX])?[0-9A-Fa-f]*)RE"), le));
                } else if (def.valueType == DbcAttributeDef::ValueType::Float) {
                    le->setValidator(new QDoubleValidator(le));
                }

                // On focus-out / Enter: validate format and range
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
                                errMsg = QString("'%1': \"" + valStr + "\" is not a valid floating-point number.").arg(capName);
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
                                errMsg = QString("'%1': \"" + valStr + "\" is not a valid %2.").arg(capName, typeName);
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
                            // Defer the popup so we don't re-enter the event loop
                            // inside editingFinished, which can cause double-firing.
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

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            // Validate each numeric entry is within its allowed range.
            for (const EditorEntry& e : editors_) {
                if (!e.lineEdit) { continue; } // enum handled by combo bounds
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
                    // Integer or Hex (base-0 auto-detects 0x prefix)
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

    QMap<QString, QString> result() const {
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
        QString minimum;   // empty for String/Enumeration
        QString maximum;
        QLineEdit* lineEdit = nullptr;
        QComboBox* combo    = nullptr;
    };
    QVector<EditorEntry> editors_;
};

// Dialog for editing a (name, rawValueâ†’label) value table, used for both
// per-signal VAL_ entries and global VAL_TABLE_ definitions.
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

        // â”€â”€ Name row â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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

        // â”€â”€ Entries table â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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

    QTableWidget* table_   = nullptr;
    QLineEdit*    nameEdit_ = nullptr;
    QComboBox*    globalCombo_ = nullptr;
    QList<DbcValueTable> globalTables_;
};
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Qt DBC Tool - Database Editor");

    createActions();
    createMenus();
    createToolbar();
    createLayout();

    refreshAll();
    statusBar()->showMessage("Ready", 2000);
}

void MainWindow::createActions()
{
    newAction_ = new QAction("Create Database", this);
    newAction_->setShortcut(QKeySequence::New);
    connect(newAction_, &QAction::triggered, this, &MainWindow::onNewDatabase);
    addAction(newAction_);

    openAction_ = new QAction("Open Database", this);
    openAction_->setShortcut(QKeySequence::Open);
    connect(openAction_, &QAction::triggered, this, &MainWindow::onOpenDatabase);
    addAction(openAction_);

    importAttributeDefinitionsAction_ = new QAction("Import Attribute Definitions", this);
    connect(importAttributeDefinitionsAction_, &QAction::triggered, this, &MainWindow::onImportAttributeDefinitions);
    addAction(importAttributeDefinitionsAction_);

    saveAction_ = new QAction("Save", this);
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::onSaveDatabase);
    addAction(saveAction_);

    saveAsAction_ = new QAction("Save As", this);
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, &MainWindow::onSaveDatabaseAs);
    addAction(saveAsAction_);

    closeDatabaseAction_ = new QAction("Close Database", this);
    connect(closeDatabaseAction_, &QAction::triggered, this, &MainWindow::onCloseDatabase);
    addAction(closeDatabaseAction_);

    exitAction_ = new QAction("Exit", this);
    exitAction_->setShortcut(QKeySequence("Alt+F4"));
    connect(exitAction_, &QAction::triggered, this, &QWidget::close);
    addAction(exitAction_);

    addNodeAction_ = new QAction("Add Node", this);
    connect(addNodeAction_, &QAction::triggered, this, &MainWindow::onAddNode);
    addAction(addNodeAction_);

    deleteNodeAction_ = new QAction("Delete Node", this);
    connect(deleteNodeAction_, &QAction::triggered, this, &MainWindow::onDeleteNode);

    deleteNodeWithAttributesAction_ = new QAction("Delete Node with Attributes", this);
    connect(deleteNodeWithAttributesAction_, &QAction::triggered, this, &MainWindow::onDeleteNodeWithAttributes);

    addMessageAction_ = new QAction("Add Message", this);
    connect(addMessageAction_, &QAction::triggered, this, &MainWindow::onAddMessage);
    addAction(addMessageAction_);

    addSignalAction_ = new QAction("Add Signal", this);
    connect(addSignalAction_, &QAction::triggered, this, &MainWindow::onAddSignal);
    addAction(addSignalAction_);

    editNewAction_ = new QAction("New", this);
    connect(editNewAction_, &QAction::triggered, this, &MainWindow::onAddMessage);
    addAction(editNewAction_);

    editAction_ = new QAction("Edit", this);
    connect(editAction_, &QAction::triggered, this, &MainWindow::onEditSelected);
    addAction(editAction_);

    editCommonAttributesAction_ = new QAction("Edit Common Attributes", this);
    connect(editCommonAttributesAction_, &QAction::triggered, this, &MainWindow::onEditCommonAttributes);
    addAction(editCommonAttributesAction_);

    copyAction_ = new QAction("Copy", this);
    copyAction_->setShortcut(QKeySequence::Copy);
    connect(copyAction_, &QAction::triggered, this, &MainWindow::onCopySelection);
    addAction(copyAction_);

    pasteAction_ = new QAction("Paste", this);
    pasteAction_->setShortcut(QKeySequence::Paste);
    connect(pasteAction_, &QAction::triggered, this, &MainWindow::onPasteSelection);
    addAction(pasteAction_);

    settingsAction_ = new QAction("Settings...", this);
    connect(settingsAction_, &QAction::triggered, this, &MainWindow::onOpenSettings);
    addAction(settingsAction_);

    viewOverviewAction_ = new QAction("Overview", this);
    connect(viewOverviewAction_, &QAction::triggered, this, &MainWindow::onViewOverview);
    addAction(viewOverviewAction_);

    viewNodesAction_ = new QAction("Nodes", this);
    connect(viewNodesAction_, &QAction::triggered, this, &MainWindow::onViewNodes);
    addAction(viewNodesAction_);

    viewMessagesAction_ = new QAction("Messages", this);
    connect(viewMessagesAction_, &QAction::triggered, this, &MainWindow::onViewMessages);
    addAction(viewMessagesAction_);

    viewSignalsAction_ = new QAction("Signals", this);
    connect(viewSignalsAction_, &QAction::triggered, this, &MainWindow::onViewSignals);
    addAction(viewSignalsAction_);

    viewValueTablesAction_ = new QAction("Value Tables", this);
    connect(viewValueTablesAction_, &QAction::triggered, this, &MainWindow::onViewValueTables);
    addAction(viewValueTablesAction_);

    viewAttributesAction_ = new QAction("Attributes", this);
    connect(viewAttributesAction_, &QAction::triggered, this, &MainWindow::onViewAttributes);
    addAction(viewAttributesAction_);

    deleteMessageAction_ = new QAction("Delete Message", this);
    connect(deleteMessageAction_, &QAction::triggered, this, &MainWindow::onDeleteMessage);

    deleteMessageWithAttributesAction_ = new QAction("Delete Message with Attributes", this);
    connect(deleteMessageWithAttributesAction_, &QAction::triggered, this, &MainWindow::onDeleteMessageWithAttributes);

    deleteSignalFromViewAction_ = new QAction("Delete Signal", this);
    connect(deleteSignalFromViewAction_, &QAction::triggered, this, &MainWindow::onDeleteSignalFromView);

    deleteAction_ = new QAction("Delete", this);
    deleteAction_->setShortcut(QKeySequence::Delete);
    connect(deleteAction_, &QAction::triggered, this, &MainWindow::onDeleteSelection);
    addAction(deleteAction_);
}

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction(newAction_);
    fileMenu->addAction(openAction_);
    openRecentMenu_ = fileMenu->addMenu("Open Recent");
    updateRecentFilesMenu();
    fileMenu->addAction(importAttributeDefinitionsAction_);
    fileMenu->addSeparator();
    fileMenu->addAction(saveAction_);
    fileMenu->addAction(saveAsAction_);
    fileMenu->addSeparator();
    fileMenu->addAction(closeDatabaseAction_);
    fileMenu->addAction(exitAction_);

    QMenu* editMenu = menuBar()->addMenu("Edit");
    editMenu->addAction(editNewAction_);
    editMenu->addAction(editAction_);
    editMenu->addAction(editCommonAttributesAction_);
    editMenu->addSeparator();
    editMenu->addAction(copyAction_);
    editMenu->addAction(pasteAction_);
    editMenu->addAction(deleteAction_);

    QMenu* viewMenu = menuBar()->addMenu("View");
    viewMenu->addAction(viewOverviewAction_);

    QMenu* listMenu = viewMenu->addMenu("List >>");
    listMenu->addAction(viewNodesAction_);
    listMenu->addAction(viewMessagesAction_);
    listMenu->addAction(viewSignalsAction_);
    listMenu->addAction(viewValueTablesAction_);
    listMenu->addAction(viewAttributesAction_);

    QMenu* optionsMenu = menuBar()->addMenu("Options");
    optionsMenu->addAction(settingsAction_);
}

void MainWindow::createToolbar()
{
    QToolBar* fileBar = addToolBar("File");
    fileBar->setMovable(false);
    fileBar->addAction(newAction_);
    fileBar->addAction(openAction_);
    fileBar->addAction(saveAction_);

    QToolBar* viewBar = addToolBar("View");
    viewBar->setMovable(false);
    viewBar->addAction(viewOverviewAction_);
    viewBar->addAction(viewNodesAction_);
    viewBar->addAction(viewMessagesAction_);
    viewBar->addAction(viewSignalsAction_);
    viewBar->addAction(viewValueTablesAction_);
    viewBar->addAction(viewAttributesAction_);
}

void MainWindow::createLayout()
{
    hierarchyTree_ = new QTreeWidget(this);
    hierarchyTree_->setHeaderLabel("Database Structure");
    hierarchyTree_->setMinimumWidth(280);
    connect(hierarchyTree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onTreeSelectionChanged);

    overviewWidget_ = new OverviewWidget(&database_, this);
    connect(overviewWidget_, &OverviewWidget::dataModified, this, [this]() {
        isDirty_ = true;
        refreshAll();
    });
    connect(overviewWidget_, &OverviewWidget::messageSelected, this, [this](int index) {
        currentMessageIndex_ = index;
    });
    connect(overviewWidget_, &OverviewWidget::requestAddMessage, this, &MainWindow::onAddMessage);
    connect(overviewWidget_, &OverviewWidget::requestAddSignal, this, &MainWindow::onAddSignal);

    // â”€â”€ Nodes view (index 1) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    nodesWidget_ = new NodesWidget(&database_, this);
    connect(nodesWidget_, &NodesWidget::dataModified, this, [this]() {
        isDirty_ = true;
        refreshAll();
    });

    // â”€â”€ Messages view (index 2) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    messagesWidget_ = new MessagesWidget(&database_, this);
    connect(messagesWidget_, &MessagesWidget::dataModified, this, [this]() {
        isDirty_ = true;
        refreshAll();
    });
    connect(messagesWidget_, &MessagesWidget::requestAddMessage, this, &MainWindow::onAddMessage);

    signalsWidget_ = new SignalsWidget(&database_, this);
    connect(signalsWidget_, &SignalsWidget::dataModified, this, [this]() {
        isDirty_ = true;
        refreshAll();
    });
    connect(signalsWidget_, &SignalsWidget::requestAddSignal, this, &MainWindow::onAddSignalForMessage);
    // â”€â”€ Value Tables view (global named VAL_TABLE_ definitions) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    valueTablesWidget_ = new ValueTablesWidget(&database_, this);
    connect(valueTablesWidget_, &ValueTablesWidget::dataModified, this, [this]() {
        isDirty_ = true;
        refreshHierarchy();
    });

    centerStack_ = new QStackedWidget(this);
    centerStack_->addWidget(overviewWidget_);     // 0: Overview
    centerStack_->addWidget(nodesWidget_);       // 1: Nodes
    centerStack_->addWidget(messagesWidget_);    // 2: Messages
    centerStack_->addWidget(signalsWidget_);     // 3: Signals
    centerStack_->addWidget(valueTablesWidget_); // 4: Value Tables

    // â”€â”€ Attributes view (index 5) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    attributesWidget_ = new AttributesWidget(&database_, this);
    connect(attributesWidget_, &AttributesWidget::dataModified, this, [this]() {
        isDirty_ = true;
        refreshHierarchy();
    });
    centerStack_->addWidget(attributesWidget_);  // 5: Attributes

    auto* contentSplitter = new QSplitter(Qt::Horizontal, this);
    contentSplitter->addWidget(hierarchyTree_);
    contentSplitter->addWidget(centerStack_);
    contentSplitter->setStretchFactor(0, 0);
    contentSplitter->setStretchFactor(1, 1);

    setCentralWidget(contentSplitter);

    bitLayoutDock_ = new QDockWidget("Bit Layout", this);
    bitLayoutDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    bitLayoutDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    bitLayoutDock_->setWidget(overviewWidget_->dockContentsWidget());
    addDockWidget(Qt::RightDockWidgetArea, bitLayoutDock_);
}

void MainWindow::refreshAll()
{
    refreshHierarchy();
    overviewWidget_->refresh();
    nodesWidget_->refresh();
    if (currentViewMode_ == ViewMode::Messages) { messagesWidget_->refresh(); }
    if (currentViewMode_ == ViewMode::Signals)  { signalsWidget_->refresh(); }
    valueTablesWidget_->refresh();
    attributesWidget_->refresh();

    QString title = "Qt DBC Tool - Database Editor";
    if (!currentFilePath_.isEmpty()) {
        title += " [" + currentFilePath_ + "]";
    }
    if (isDirty_) {
        title += " *";
    }
    setWindowTitle(title);
}

void MainWindow::refreshHierarchy()
{
    hierarchyTree_->setUpdatesEnabled(false);
    hierarchyTree_->clear();

    // Pre-compute which object types have applicable attribute definitions.
    // "Attributes" children are only added when there are defs to edit.
    bool hasNodeAttrs = false, hasMsgAttrs = false, hasSigAttrs = false;
    for (const DbcAttributeDef& def : database_.attributes) {
        if (def.objectType == DbcAttributeDef::ObjectType::Node)    hasNodeAttrs = true;
        if (def.objectType == DbcAttributeDef::ObjectType::Message)  hasMsgAttrs  = true;
        if (def.objectType == DbcAttributeDef::ObjectType::Signal)   hasSigAttrs  = true;
    }

    // Qt::UserRole   = primary index data (same encoding as before for normal items)
    // Qt::UserRole+1 = 0 (normal) | 1 (node attrs) | 2 (msg attrs) | 3 (sig attrs)

    // Build sorted index lists so the tree displays items alphabetically
    // without reordering the underlying database_ arrays.
    auto* rootNodes = new QTreeWidgetItem(hierarchyTree_, QStringList{"Nodes"});
    QVector<int> nodeOrder(database_.nodes.size());
    std::iota(nodeOrder.begin(), nodeOrder.end(), 0);
    std::sort(nodeOrder.begin(), nodeOrder.end(), [&](int a, int b) {
        return database_.nodes.at(a).name.compare(database_.nodes.at(b).name, Qt::CaseInsensitive) < 0;
    });
    for (int i : nodeOrder) {
        auto* nodeItem = new QTreeWidgetItem(rootNodes, QStringList{database_.nodes.at(i).name});
        nodeItem->setData(0, Qt::UserRole, -1000);
        if (hasNodeAttrs) {
            auto* attrItem = new QTreeWidgetItem(nodeItem, QStringList{"Attributes"});
            attrItem->setData(0, Qt::UserRole,   i);   // nodeIndex
            attrItem->setData(0, Qt::UserRole+1, 1);   // type=node attrs
        }
    }

    auto* rootMessages = new QTreeWidgetItem(hierarchyTree_, QStringList{"Messages"});
    QVector<int> msgOrder(database_.messages.size());
    std::iota(msgOrder.begin(), msgOrder.end(), 0);
    std::sort(msgOrder.begin(), msgOrder.end(), [&](int a, int b) {
        return database_.messages.at(a).name.compare(database_.messages.at(b).name, Qt::CaseInsensitive) < 0;
    });
    for (int i : msgOrder) {
        const DbcMessage& msg = database_.messages.at(i);
        auto* msgItem = new QTreeWidgetItem(rootMessages, QStringList{QString("%1 (0x%2)").arg(msg.name, QString::number(msg.id, 16).toUpper())});
        msgItem->setData(0, Qt::UserRole, i + 1);
        if (hasMsgAttrs) {
            auto* attrItem = new QTreeWidgetItem(msgItem, QStringList{"Attributes"});
            attrItem->setData(0, Qt::UserRole,   i);   // msgIndex
            attrItem->setData(0, Qt::UserRole+1, 2);   // type=msg attrs
        }
    }

    auto* rootSignals = new QTreeWidgetItem(hierarchyTree_, QStringList{"Signals"});
    // Collect all (msgIndex, sigIndex) pairs then sort by signal name.
    struct SigRef { int mi; int si; QString name; };
    QVector<SigRef> allSigs;
    for (int mi = 0; mi < database_.messages.size(); ++mi) {
        const DbcMessage& msg = database_.messages.at(mi);
        for (int si = 0; si < msg.signalList.size(); ++si) {
            allSigs.append({mi, si, msg.signalList.at(si).name});
        }
    }
    std::sort(allSigs.begin(), allSigs.end(), [](const SigRef& a, const SigRef& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    for (const SigRef& ref : allSigs) {
        auto* sigItem = new QTreeWidgetItem(rootSignals, QStringList{ref.name});
        sigItem->setData(0, Qt::UserRole, ((ref.mi + 1) << 16) | (ref.si + 1));
        if (hasSigAttrs) {
            auto* attrItem = new QTreeWidgetItem(sigItem, QStringList{"Attributes"});
            attrItem->setData(0, Qt::UserRole,   (ref.mi << 16) | ref.si);  // packed indices
            attrItem->setData(0, Qt::UserRole+1, 3);                          // type=sig attrs
        }
    }

    hierarchyTree_->collapseAll();
    hierarchyTree_->setUpdatesEnabled(true);
}


bool MainWindow::ensureReadyToDiscard()
{
    if (!isDirty_) {
        return true;
    }

    const auto answer = QMessageBox::question(this,
                                              "Unsaved Changes",
                                              "The database has unsaved changes. Do you want to save now?",
                                              QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                                              QMessageBox::Yes);

    if (answer == QMessageBox::Cancel) {
        return false;
    }

    if (answer == QMessageBox::Yes) {
        onSaveDatabase();
        return !isDirty_;
    }

    return true;
}

bool MainWindow::loadDatabaseFromPath(const QString& path)
{
    DbcDatabase loaded;
    QString error;
    if (!DbcDocument::loadFromFile(path, loaded, error)) {
        QMessageBox::critical(this, "Open Failed", error);
        return false;
    }

    if (!validateDatabaseRules(loaded, error)) {
        QMessageBox::critical(this, "Open Failed", QString("Validation failed:\n%1").arg(error));
        return false;
    }

    database_ = loaded;
    currentFilePath_ = path;
    currentMessageIndex_ = database_.messages.isEmpty() ? -1 : 0;
    isDirty_ = false;

    addRecentFile(path);
    refreshAll();
    statusBar()->showMessage("Database opened", 2000);
    return true;
}

bool MainWindow::saveToPath(const QString& path)
{
    QString error;
    if (!DbcDocument::saveToFile(path, database_, error)) {
        QMessageBox::critical(this, "Save Failed", error);
        return false;
    }

    currentFilePath_ = path;
    isDirty_ = false;
    statusBar()->showMessage("Database saved", 2000);
    refreshAll();
    return true;
}

void MainWindow::onNewDatabase()
{
    if (!ensureReadyToDiscard()) {
        return;
    }

    database_.clear();
    currentFilePath_.clear();
    currentMessageIndex_ = -1;
    isDirty_ = false;

    refreshAll();
}

void MainWindow::onOpenDatabase()
{
    if (!ensureReadyToDiscard()) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(this, "Open DBC", QString(), "DBC Files (*.dbc);;All Files (*)");
    if (path.isEmpty()) {
        return;
    }

    loadDatabaseFromPath(path);
}

void MainWindow::onSaveDatabase()
{
    if (currentFilePath_.isEmpty()) {
        onSaveDatabaseAs();
        return;
    }

    saveToPath(currentFilePath_);
}

void MainWindow::onSaveDatabaseAs()
{
    const QString path = QFileDialog::getSaveFileName(this, "Save DBC", currentFilePath_.isEmpty() ? "database.dbc" : currentFilePath_, "DBC Files (*.dbc);;All Files (*)");
    if (path.isEmpty()) {
        return;
    }

    saveToPath(path);
}

void MainWindow::onCloseDatabase()
{
    if (!ensureReadyToDiscard()) {
        return;
    }

    database_.clear();
    currentFilePath_.clear();
    currentMessageIndex_ = -1;
    isDirty_ = false;

    refreshAll();
    statusBar()->showMessage("Database closed", 2000);
}

void MainWindow::onImportAttributeDefinitions()
{
    QFileDialog::getOpenFileName(this, "Import Attribute Definitions", QString(), "DBC Files (*.dbc);;All Files (*)");
    QMessageBox::information(this, "Import Attribute Definitions", "Import Attribute Definitions is not implemented yet.");
}

void MainWindow::onAddNode()    { if (nodesWidget_) nodesWidget_->onAddNode(); }
void MainWindow::onDeleteNode() { if (nodesWidget_) nodesWidget_->onDeleteNode(); }
void MainWindow::onDeleteNodeWithAttributes() { if (nodesWidget_) nodesWidget_->onDeleteNodeWithAttributes(); }

void MainWindow::onDeleteMessage()    { if (messagesWidget_) messagesWidget_->onDeleteMessage(); }
void MainWindow::onDeleteMessageWithAttributes() { if (messagesWidget_) messagesWidget_->onDeleteMessageWithAttributes(); }

void MainWindow::onDeleteSignalFromView()
{
    if (signalsWidget_) { signalsWidget_->onDeleteSignalFromView(); }
}

void MainWindow::onAddSignalForMessage(int messageIndex)
{
    const int savedIdx = currentMessageIndex_;
    currentMessageIndex_ = messageIndex;
    onAddSignal();
    currentMessageIndex_ = savedIdx;
}

void MainWindow::onAddMessage()
{
    QStringList nodeNames;
    for (const DbcNode& node : database_.nodes) {
        nodeNames.append(node.name);
    }
    AddMessageDialog dialog(nodeNames, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    DbcMessage message;
    message.name = dialog.messageName();
    if (!isValidSymbolName(message.name)) {
        QMessageBox::warning(this,
                             "Invalid Message",
                             "Name must start with a letter or underscore and contain only A-Z, a-z, 0-9, or underscore.");
        return;
    }

    for (const DbcMessage& existing : database_.messages) {
        if (existing.name == message.name) {
            QMessageBox::warning(this, "Invalid Message", QString("Message name already exists: %1").arg(message.name));
            return;
        }
        if (existing.id == dialog.messageId()) {
            QMessageBox::warning(this,
                                 "Invalid Message",
                                 QString("Message ID already exists: 0x%1").arg(QString::number(dialog.messageId(), 16).toUpper()));
            return;
        }
    }

    message.id = dialog.messageId();
    message.isExtended = dialog.isExtended();
    message.dlc = dialog.dlc();
    message.cycleTimeMs = dialog.cycleTimeMs();
    message.transmitter = dialog.transmitter();

    database_.messages.append(message);
    currentMessageIndex_ = database_.messages.size() - 1;
    isDirty_ = true;

    refreshAll();
}

void MainWindow::onAddSignal()
{
    if (currentMessageIndex_ < 0 || currentMessageIndex_ >= database_.messages.size()) {
        QMessageBox::information(this, "No Message", "Select a message before adding a signal.");
        return;
    }

    // Collect signal names already in this message (duplicates are per-message only).
    const DbcMessage& targetMsg = database_.messages.at(currentMessageIndex_);
    QSet<QString> existingNamesInMsg;
    for (const DbcSignal& existingSignal : targetMsg.signalList) {
        existingNamesInMsg.insert(existingSignal.name);
    }

    // Find the first unused "New_Signal_N" name within this message.
    QString signalName;
    for (int n = 1; ; ++n) {
        const QString candidate = QString("New_Signal_%1").arg(n);
        if (!existingNamesInMsg.contains(candidate)) {
            signalName = candidate;
            break;
        }
    }

    DbcSignal dbcSig;
    dbcSig.name = signalName;
    dbcSig.mode = "Signal";
    dbcSig.startBit = 0;
    dbcSig.bitLength = 1;
    {
        QSettings s;
        const QString byteOrderPref = s.value("defaults/signals/byteOrder", "Last Value").toString();
        if (byteOrderPref == "Last Value") {
            dbcSig.byteOrder = s.value("defaults/signals/byteOrder/lastValue", "Intel").toString();
        } else if (byteOrderPref == "Big Endian") {
            dbcSig.byteOrder = "Motorola";
        } else {
            dbcSig.byteOrder = "Intel";
        }
    }
    dbcSig.valueType = "Unsigned";
    dbcSig.factor = 1.0;
    dbcSig.offset = 0.0;
    dbcSig.minimum = 0.0;
    dbcSig.maximum = 0.0;
    dbcSig.receivers = {"Vector__XXX"};

    database_.messages[currentMessageIndex_].signalList.append(dbcSig);
    isDirty_ = true;

    refreshAll();
}

void MainWindow::onOpenSettings()
{
    SettingsDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    dialog.save();
    statusBar()->showMessage("Settings saved", 2000);
}

void MainWindow::onEditSelected()
{
    QMessageBox::information(this, "Edit", "Edit is not implemented yet.");
}

void MainWindow::onEditCommonAttributes()
{
    QMessageBox::information(this, "Edit Common Attributes", "Edit Common Attributes is not implemented yet.");
}

void MainWindow::onCopySelection()
{
    QMessageBox::information(this, "Copy", "Copy is not implemented yet.");
}

void MainWindow::onPasteSelection()
{
    QMessageBox::information(this, "Paste", "Paste is not implemented yet.");
}

void MainWindow::onViewOverview()
{
    currentViewMode_ = ViewMode::Overview;
    centerStack_->setCurrentIndex(0);
    if (bitLayoutDock_) { bitLayoutDock_->show(); }
    statusBar()->showMessage("Overview", 1000);
}

void MainWindow::onViewNodes()
{
    currentViewMode_ = ViewMode::Nodes;
    centerStack_->setCurrentIndex(1);
    if (bitLayoutDock_) { bitLayoutDock_->hide(); }
    nodesWidget_->refresh();
    for (int i = 0; i < hierarchyTree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = hierarchyTree_->topLevelItem(i);
        if (item && item->text(0) == "Nodes") {
            hierarchyTree_->setCurrentItem(item);
            hierarchyTree_->scrollToItem(item);
            hierarchyTree_->setFocus();
            break;
        }
    }
    statusBar()->showMessage("List: Nodes", 1000);
}

void MainWindow::onViewMessages()
{
    currentViewMode_ = ViewMode::Messages;
    centerStack_->setCurrentIndex(2);
    if (bitLayoutDock_) { bitLayoutDock_->show(); }
    messagesWidget_->refresh();
    statusBar()->showMessage("List: Messages", 1000);
}

void MainWindow::onViewSignals()
{
    currentViewMode_ = ViewMode::Signals;
    centerStack_->setCurrentIndex(3);
    if (bitLayoutDock_) { bitLayoutDock_->hide(); }
    signalsWidget_->refresh();
    statusBar()->showMessage("List: Signals", 1000);
}

void MainWindow::onOpenRecentFile()
{
    auto* action = qobject_cast<QAction*>(sender());
    if (!action) {
        return;
    }

    const QString path = action->data().toString();
    if (path.isEmpty()) {
        return;
    }

    if (!ensureReadyToDiscard()) {
        return;
    }

    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, "Missing File", QString("File not found:\n%1").arg(path));

        QSettings settings;
        QStringList files = settings.value("recentFiles").toStringList();
        files.removeAll(path);
        settings.setValue("recentFiles", files);
        updateRecentFilesMenu();
        return;
    }

    loadDatabaseFromPath(path);
}

void MainWindow::updateRecentFilesMenu()
{
    if (!openRecentMenu_) {
        return;
    }

    openRecentMenu_->clear();

    QSettings settings;
    QStringList files = settings.value("recentFiles").toStringList();

    if (files.isEmpty()) {
        QAction* noRecent = openRecentMenu_->addAction("(No Recent Files)");
        noRecent->setEnabled(false);
        return;
    }

    const int maxItems = qMin(files.size(), 10);
    for (int i = 0; i < maxItems; ++i) {
        const QString& path = files.at(i);
        QAction* recentAction = openRecentMenu_->addAction(QString("%1. %2").arg(i + 1).arg(path));
        recentAction->setData(path);
        connect(recentAction, &QAction::triggered, this, &MainWindow::onOpenRecentFile);
    }

    openRecentMenu_->addSeparator();
    QAction* clearRecent = openRecentMenu_->addAction("Clear Recent Files");
    connect(clearRecent, &QAction::triggered, this, [this]() {
        QSettings settings;
        settings.setValue("recentFiles", QStringList{});
        updateRecentFilesMenu();
    });
}

void MainWindow::addRecentFile(const QString& path)
{
    QSettings settings;
    QStringList files = settings.value("recentFiles").toStringList();
    files.removeAll(path);
    files.prepend(path);

    while (files.size() > 10) {
        files.removeLast();
    }

    settings.setValue("recentFiles", files);
    updateRecentFilesMenu();
}

void MainWindow::onDeleteSelection()
{
    // Delegate to widget-owned delete handlers for views that manage it internally.
    if (currentViewMode_ == ViewMode::ValueTables) { return; }
    if (currentViewMode_ == ViewMode::Attributes) {
        attributesWidget_->deleteSelected();
        return;
    }
    if (currentViewMode_ == ViewMode::Nodes) {
        nodesWidget_->deleteSelected();
        return;
    }
    if (currentViewMode_ == ViewMode::Messages) {
        messagesWidget_->deleteSelected();
        return;
    }
    if (currentViewMode_ == ViewMode::Signals) {
        signalsWidget_->deleteSelected();
        return;
    }
    if (currentViewMode_ == ViewMode::Overview) {
        overviewWidget_->deleteSelected();
        return;
    }
}

void MainWindow::onTreeSelectionChanged()
{
    const QList<QTreeWidgetItem*> selectedItems = hierarchyTree_->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }

    QTreeWidgetItem* selectedItem = selectedItems.first();

    // â”€â”€ "Attributes" child items (Qt::UserRole+1 != 0) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    const int attrType = selectedItem->data(0, Qt::UserRole+1).toInt();
    if (attrType != 0) {
        const int packed = selectedItem->data(0, Qt::UserRole).toInt();

        if (attrType == 1) {
            // Node attributes
            const int nodeIdx = packed;
            if (nodeIdx < 0 || nodeIdx >= database_.nodes.size()) return;
            DbcNode& node = database_.nodes[nodeIdx];

            QList<DbcAttributeDef> defs;
            for (const DbcAttributeDef& d : database_.attributes)
                if (d.objectType == DbcAttributeDef::ObjectType::Node) defs.append(d);

            ObjectAttrDialog dlg(
                QString("Attributes \u2013 Node: %1").arg(node.name),
                defs, node.attrValues, this);
            if (dlg.exec() == QDialog::Accepted) {
                node.attrValues = dlg.result();
                isDirty_ = true;
            }

        } else if (attrType == 2) {
            // Message attributes
            const int msgIdx = packed;
            if (msgIdx < 0 || msgIdx >= database_.messages.size()) return;
            DbcMessage& msg = database_.messages[msgIdx];

            QList<DbcAttributeDef> defs;
            for (const DbcAttributeDef& d : database_.attributes)
                if (d.objectType == DbcAttributeDef::ObjectType::Message) defs.append(d);

            ObjectAttrDialog dlg(
                QString("Attributes \u2013 Message: %1").arg(msg.name),
                defs, msg.attrValues, this);
            if (dlg.exec() == QDialog::Accepted) {
                msg.attrValues = dlg.result();
                isDirty_ = true;
            }

        } else if (attrType == 3) {
            // Signal attributes â€” packed = (msgIdx << 16) | sigIdx
            const int msgIdx = (packed >> 16) & 0xFFFF;
            const int sigIdx = packed & 0xFFFF;
            if (msgIdx < 0 || msgIdx >= database_.messages.size()) return;
            DbcMessage& msg = database_.messages[msgIdx];
            if (sigIdx < 0 || sigIdx >= msg.signalList.size()) return;
            DbcSignal& sig = msg.signalList[sigIdx];

            QList<DbcAttributeDef> defs;
            for (const DbcAttributeDef& d : database_.attributes)
                if (d.objectType == DbcAttributeDef::ObjectType::Signal) defs.append(d);

            ObjectAttrDialog dlg(
                QString("Attributes \u2013 Signal: %1 (in %2)").arg(sig.name, msg.name),
                defs, sig.attrValues, this);
            if (dlg.exec() == QDialog::Accepted) {
                sig.attrValues = dlg.result();
                isDirty_ = true;
            }
        }

        // Deselect the "Attributes" item so the user can click it again.
        hierarchyTree_->clearSelection();
        return;
    }

    if (!selectedItem->parent()) {
        const QString rootName = selectedItem->text(0);
        if (rootName == "Nodes") {
            currentViewMode_ = ViewMode::Nodes;
            centerStack_->setCurrentIndex(1);
            nodesWidget_->refresh();
            return;
        }
        if (rootName == "Messages") {
            currentViewMode_ = ViewMode::Messages;
            centerStack_->setCurrentIndex(2);
            messagesWidget_->refresh();
            return;
        }
        if (rootName == "Signals") {
            currentViewMode_ = ViewMode::Signals;
            centerStack_->setCurrentIndex(3);
            signalsWidget_->refresh();
            return;
        }
    }

    const int marker = selectedItem->data(0, Qt::UserRole).toInt();

    // marker == 0: root items (no UserRole set), marker < 0: node list items (-1000)
    if (marker < 0) {
        // Node child â€” navigate to Nodes view.
        currentViewMode_ = ViewMode::Nodes;
        centerStack_->setCurrentIndex(1);
        if (bitLayoutDock_) { bitLayoutDock_->hide(); }
        nodesWidget_->refresh();
        return;
    }
    if (marker == 0) {
        return;
    }

    if ((marker >> 16) > 0) {
        // Signal node: encoded as ((messageIndex+1) << 16) | (signalIndex+1)
        const int messageIndex = (marker >> 16) - 1;
        const int signalIndex  = (marker & 0xFFFF) - 1;

        // Navigate to the Signals view.
        currentViewMode_ = ViewMode::Signals;
        centerStack_->setCurrentIndex(3);
        if (bitLayoutDock_) { bitLayoutDock_->hide(); }
        signalsWidget_->refresh();

        // Identify the signal name and select its row.
        QString sigName;
        if (messageIndex >= 0 && messageIndex < database_.messages.size()) {
            const DbcMessage& msg = database_.messages.at(messageIndex);
            if (signalIndex >= 0 && signalIndex < msg.signalList.size()) {
                sigName = msg.signalList.at(signalIndex).name;
            }
        }
        if (!sigName.isEmpty()) {
            signalsWidget_->selectSignal(sigName);
        }
    } else {
        // Message node: encoded as messageIndex + 1
        const int messageIndex = marker - 1;

        // Navigate to the Messages view.
        currentViewMode_ = ViewMode::Messages;
        centerStack_->setCurrentIndex(2);
        if (bitLayoutDock_) { bitLayoutDock_->show(); }
        messagesWidget_->refresh();

        currentMessageIndex_ = messageIndex;
        overviewWidget_->setCurrentMessage(messageIndex);
    }
}

// â”€â”€ Value Tables tab â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void MainWindow::onViewValueTables()
{
    currentViewMode_ = ViewMode::ValueTables;
    centerStack_->setCurrentIndex(4);
    if (bitLayoutDock_) { bitLayoutDock_->hide(); }
    valueTablesWidget_->refresh();
    statusBar()->showMessage("Value Tables", 1000);
}


// â”€â”€ Attributes view â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void MainWindow::onViewAttributes()
{
    currentViewMode_ = ViewMode::Attributes;
    centerStack_->setCurrentIndex(5);
    if (bitLayoutDock_) { bitLayoutDock_->hide(); }
    attributesWidget_->refresh();
    statusBar()->showMessage("Attributes", 1000);
}

