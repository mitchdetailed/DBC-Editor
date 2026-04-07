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
#include <QInputDialog>
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

        // Signal names must be unique within a message, but the same name is
        // allowed in different messages (standard DBC behaviour).
        QSet<QString> signalNames;
        for (const DbcSignal& dbcSig : message.signalList) {
            if (!isValidSymbolName(dbcSig.name)) {
                error = QString("Invalid signal name: %1\nAllowed pattern: [A-Za-z_][A-Za-z0-9_]*").arg(dbcSig.name);
                return false;
            }

            if (signalNames.contains(dbcSig.name)) {
                error = QString("Duplicate signal name '%1' in message '%2'").arg(dbcSig.name, message.name);
                return false;
            }
            signalNames.insert(dbcSig.name);
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

// Dialog for editing a (name, rawValue→label) value table, used for both
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

        // ── Name row ──────────────────────────────────────────────────────
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

        // ── Entries table ─────────────────────────────────────────────────
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

    messageTable_ = new QTableWidget(this);
    messageTable_->setColumnCount(8);
    messageTable_->setHorizontalHeaderLabels({"Name", "Type", "ID", "DLC", "Cycle Time", "Transmitter", "Comment", "Attributes"});
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
        contextMenu.addAction(addMessageAction_);

        const QModelIndexList selectedRows = messageTable_->selectionModel() ? messageTable_->selectionModel()->selectedRows() : QModelIndexList{};
        if (!selectedRows.isEmpty()) {
            contextMenu.addSeparator();
            contextMenu.addAction(deleteAction_);
        }

        contextMenu.exec(messageTable_->viewport()->mapToGlobal(pos));
    });
    connect(messageTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::onMessageSelectionChanged);
    connect(messageTable_, &QTableWidget::itemChanged,          this, &MainWindow::onMessageTableItemChanged);
    connect(messageTable_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        auto* nameItem = messageTable_->item(row, 0);
        if (!nameItem) { return; }
        const int msgIndex = nameItem->data(Qt::UserRole).toInt();
        if (msgIndex < 0 || msgIndex >= database_.messages.size()) { return; }
        if (col == 6) {
            const QString current = database_.messages.at(msgIndex).comment;
            openCommentEditor(this, current, [this, row, msgIndex](const QString& text) {
                if (msgIndex < 0 || msgIndex >= database_.messages.size()) { return; }
                database_.messages[msgIndex].comment = text;
                isDirty_ = true;
                auto* cell = messageTable_->item(row, 6);
                if (cell) { const QSignalBlocker b(messageTable_); cell->setText(text); }
            });
        } else if (col == 7) {
            DbcMessage& msg = database_.messages[msgIndex];
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
        }
    });

    signalTable_ = new QTableWidget(this);
    signalTable_->setColumnCount(14);
    signalTable_->setHorizontalHeaderLabels({"Name", "Mode", "Startbit", "Length [Bit]", "Byte Order", "Type", "Factor", "Offset", "Minimum", "Maximum", "Unit", "Comment", "Value Table", "Attributes"});
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
        contextMenu.addAction(addSignalAction_);
        const QModelIndexList selected = signalTable_->selectionModel()
            ? signalTable_->selectionModel()->selectedRows() : QModelIndexList{};
        if (!selected.isEmpty()) {
            contextMenu.addSeparator();
            auto* calcAction = new QAction("Calculate Min / Max", &contextMenu);
            connect(calcAction, &QAction::triggered, this, [this, selected]() {
                if (currentMessageIndex_ < 0 || currentMessageIndex_ >= database_.messages.size()) { return; }
                const int row = selected.first().row();
                QTableWidgetItem* ni = signalTable_->item(row, 0);
                if (!ni) { return; }
                const int sigIndex = ni->data(Qt::UserRole).toInt();
                DbcMessage& msg = database_.messages[currentMessageIndex_];
                if (sigIndex < 0 || sigIndex >= msg.signalList.size()) { return; }
                DbcSignal& sig = msg.signalList[sigIndex];

                // Compute raw range from type + bit length
                double rawMin = 0.0, rawMax = 0.0;
                const int bits = sig.bitLength;
                if (sig.valueType == "Unsigned") {
                    rawMin = 0.0;
                    rawMax = (bits >= 64) ? 1.8446744073709552e19  // 2^64 - 1
                                         : static_cast<double>((quint64(1) << bits) - 1);
                } else if (sig.valueType == "Signed") {
                    const int clampedBits = qMin(bits, 63);
                    rawMin = -static_cast<double>(qint64(1) << (clampedBits - 1));
                    rawMax =  static_cast<double>((qint64(1) << (clampedBits - 1)) - 1);
                } else if (sig.valueType == "Float") {
                    rawMin = -static_cast<double>(std::numeric_limits<float>::max());
                    rawMax =  static_cast<double>(std::numeric_limits<float>::max());
                } else { // Double
                    rawMin = -std::numeric_limits<double>::max();
                    rawMax =  std::numeric_limits<double>::max();
                }

                // Apply factor/offset (swap if factor is negative)
                if (sig.factor >= 0.0) {
                    sig.minimum = rawMin * sig.factor + sig.offset;
                    sig.maximum = rawMax * sig.factor + sig.offset;
                } else {
                    sig.minimum = rawMax * sig.factor + sig.offset;
                    sig.maximum = rawMin * sig.factor + sig.offset;
                }
                isDirty_ = true;

                // Refresh the two cells
                const QSignalBlocker b(signalTable_);
                if (auto* minItem = signalTable_->item(row, 8)) { minItem->setText(QString::number(sig.minimum, 'g', 10)); }
                if (auto* maxItem = signalTable_->item(row, 9)) { maxItem->setText(QString::number(sig.maximum, 'g', 10)); }
            });
            contextMenu.addAction(calcAction);
            contextMenu.addSeparator();
            auto* removeAction = new QAction("Remove Signal", &contextMenu);
            connect(removeAction, &QAction::triggered, this, &MainWindow::onDeleteSelection);
            contextMenu.addAction(removeAction);
        }
        contextMenu.exec(signalTable_->viewport()->mapToGlobal(pos));
    });
    connect(signalTable_, &QTableWidget::itemChanged,
            this, &MainWindow::onSignalTableItemChanged);
    connect(signalTable_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (col == 11) {
            if (currentMessageIndex_ < 0 || currentMessageIndex_ >= database_.messages.size()) { return; }
            auto* nameItem = signalTable_->item(row, 0);
            if (!nameItem) { return; }
            const QString sigName = nameItem->text();
            auto& signalList = database_.messages[currentMessageIndex_].signalList;
            int si = -1;
            for (int i = 0; i < signalList.size(); ++i) {
                if (signalList.at(i).name == sigName) { si = i; break; }
            }
            if (si < 0) { return; }
            const QString current = signalList.at(si).comment;
            openCommentEditor(this, current, [this, row, si](const QString& text) {
                if (currentMessageIndex_ < 0 || currentMessageIndex_ >= database_.messages.size()) { return; }
                auto& sl = database_.messages[currentMessageIndex_].signalList;
                if (si < 0 || si >= sl.size()) { return; }
                sl[si].comment = text;
                isDirty_ = true;
                auto* cell = signalTable_->item(row, 11);
                if (cell) { const QSignalBlocker b(signalTable_); cell->setText(text); }
            });
        } else if (col == 13) {
            if (currentMessageIndex_ < 0 || currentMessageIndex_ >= database_.messages.size()) { return; }
            auto* nameItem = signalTable_->item(row, 0);
            if (!nameItem) { return; }
            const QString sigName = nameItem->text();
            DbcMessage& msg = database_.messages[currentMessageIndex_];
            for (DbcSignal& sig : msg.signalList) {
                if (sig.name != sigName) { continue; }
                QList<DbcAttributeDef> defs;
                for (const DbcAttributeDef& d : database_.attributes)
                    if (d.objectType == DbcAttributeDef::ObjectType::Signal) defs.append(d);
                ObjectAttrDialog dlg(
                    QString("Attributes \u2013 Signal: %1 (in %2)").arg(sigName, msg.name),
                    defs, sig.attrValues, this);
                if (dlg.exec() == QDialog::Accepted) {
                    sig.attrValues = dlg.result();
                    isDirty_ = true;
                }
                break;
            }
        }
    });

    // Deselect when clicking empty space in the signal table.
    signalTable_->viewport()->installEventFilter(this);

    // Highlight the current (focused) cell so the user always sees where they are.
    // QComboBox:focus covers the dropdown cell widgets (Byte Order, Type, Mode).
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

    nodesViewTable_ = new QTableWidget(this);
    nodesViewTable_->setColumnCount(4);
    nodesViewTable_->setHorizontalHeaderLabels({"Name", "Address", "Comment", "Attributes"});
    nodesViewTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    nodesViewTable_->horizontalHeader()->setStretchLastSection(true);
    nodesViewTable_->horizontalHeader()->setSectionsClickable(true);
    nodesViewTable_->horizontalHeader()->setSortIndicatorShown(true);
    nodesViewTable_->horizontalHeader()->setSectionsMovable(true);
    nodesViewTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    nodesViewTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    nodesViewTable_->setSortingEnabled(true);
    nodesViewTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    nodesViewTable_->setAcceptDrops(true);
    nodesViewTable_->setDragDropMode(QAbstractItemView::DropOnly);
    nodesViewTable_->setDropIndicatorShown(true);
    nodesViewTable_->viewport()->installEventFilter(this);
    connect(nodesViewTable_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu contextMenu(this);
        contextMenu.addAction(addNodeAction_);
        const QModelIndexList selected = nodesViewTable_->selectionModel()
            ? nodesViewTable_->selectionModel()->selectedRows() : QModelIndexList{};
        if (!selected.isEmpty()) {
            contextMenu.addSeparator();
            contextMenu.addAction(deleteNodeAction_);
            contextMenu.addAction(deleteNodeWithAttributesAction_);
        }
        contextMenu.exec(nodesViewTable_->viewport()->mapToGlobal(pos));
    });
    connect(nodesViewTable_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        auto* nameItem = nodesViewTable_->item(row, 0);
        if (!nameItem) { return; }
        const QString nodeName = nameItem->text();
        if (col == 2) {
            QString current;
            for (const DbcNode& n : database_.nodes) {
                if (n.name == nodeName) { current = n.comment; break; }
            }
            openCommentEditor(this, current, [this, row, nodeName](const QString& text) {
                for (DbcNode& n : database_.nodes) {
                    if (n.name != nodeName) { continue; }
                    n.comment = text;
                    isDirty_ = true;
                    auto* cell = nodesViewTable_->item(row, 2);
                    if (cell) { cell->setText(text); }
                    break;
                }
            });
        } else if (col == 3) {
            for (DbcNode& n : database_.nodes) {
                if (n.name != nodeName) { continue; }
                QList<DbcAttributeDef> defs;
                for (const DbcAttributeDef& d : database_.attributes)
                    if (d.objectType == DbcAttributeDef::ObjectType::Node) defs.append(d);
                ObjectAttrDialog dlg(
                    QString("Attributes \u2013 Node: %1").arg(nodeName),
                    defs, n.attrValues, this);
                if (dlg.exec() == QDialog::Accepted) {
                    n.attrValues = dlg.result();
                    isDirty_ = true;
                }
                break;
            }
        }
    });

    messagesViewTable_ = new QTableWidget(this);
    messagesViewTable_->setColumnCount(8);
    messagesViewTable_->setHorizontalHeaderLabels({"Name", "ID", "ID-Format", "DLC", "Cycle Time", "Transmitter", "Comment", "Attributes"});
    messagesViewTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    messagesViewTable_->horizontalHeader()->setStretchLastSection(true);
    messagesViewTable_->horizontalHeader()->setSectionsClickable(true);
    messagesViewTable_->horizontalHeader()->setSortIndicatorShown(true);
    messagesViewTable_->horizontalHeader()->setSectionsMovable(true);
    messagesViewTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    messagesViewTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    messagesViewTable_->setSortingEnabled(true);
    messagesViewTable_->setDragEnabled(true);
    messagesViewTable_->setAcceptDrops(true);
    messagesViewTable_->setDragDropMode(QAbstractItemView::DragDrop);
    messagesViewTable_->setDropIndicatorShown(true);
    messagesViewTable_->viewport()->installEventFilter(this);
    messagesViewTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(messagesViewTable_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu contextMenu(this);
        contextMenu.addAction(addMessageAction_);
        const QModelIndexList selected = messagesViewTable_->selectionModel()
            ? messagesViewTable_->selectionModel()->selectedRows() : QModelIndexList{};
        if (!selected.isEmpty()) {
            contextMenu.addSeparator();
            contextMenu.addAction(deleteMessageAction_);
            contextMenu.addAction(deleteMessageWithAttributesAction_);
        }
        contextMenu.exec(messagesViewTable_->viewport()->mapToGlobal(pos));
    });
    connect(messagesViewTable_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        auto* nameItem = messagesViewTable_->item(row, 0);
        if (!nameItem) { return; }
        const QString msgName = nameItem->text();
        if (col == 6) {
            QString current;
            for (const DbcMessage& m : database_.messages) {
                if (m.name == msgName) { current = m.comment; break; }
            }
            openCommentEditor(this, current, [this, row, msgName](const QString& text) {
                for (DbcMessage& m : database_.messages) {
                    if (m.name != msgName) { continue; }
                    m.comment = text;
                    isDirty_ = true;
                    auto* cell = messagesViewTable_->item(row, 6);
                    if (cell) { cell->setText(text); }
                    break;
                }
            });
        } else if (col == 7) {
            for (DbcMessage& m : database_.messages) {
                if (m.name != msgName) { continue; }
                QList<DbcAttributeDef> defs;
                for (const DbcAttributeDef& d : database_.attributes)
                    if (d.objectType == DbcAttributeDef::ObjectType::Message) defs.append(d);
                ObjectAttrDialog dlg(
                    QString("Attributes \u2013 Message: %1").arg(msgName),
                    defs, m.attrValues, this);
                if (dlg.exec() == QDialog::Accepted) {
                    m.attrValues = dlg.result();
                    isDirty_ = true;
                }
                break;
            }
        }
    });

    signalsViewTable_ = new QTableWidget(this);
    signalsViewTable_->setColumnCount(13);
    signalsViewTable_->setHorizontalHeaderLabels({"Name", "Length", "Byte Order", "Value Type", "Factor", "Offset", "Minimum", "Maximum", "Unit", "ValueTable", "Comment", "Message(s)", "Attributes"});
    signalsViewTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    signalsViewTable_->horizontalHeader()->setStretchLastSection(true);
    signalsViewTable_->horizontalHeader()->setSectionsClickable(true);
    signalsViewTable_->horizontalHeader()->setSortIndicatorShown(true);
    signalsViewTable_->horizontalHeader()->setSectionsMovable(true);
    signalsViewTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    signalsViewTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    signalsViewTable_->setSortingEnabled(true);
    signalsViewTable_->setDragEnabled(true);
    signalsViewTable_->setDragDropMode(QAbstractItemView::DragOnly);
    signalsViewTable_->viewport()->installEventFilter(this);
    signalsViewTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(signalsViewTable_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu contextMenu(this);
        const QModelIndexList selected = signalsViewTable_->selectionModel()
            ? signalsViewTable_->selectionModel()->selectedRows() : QModelIndexList{};

        // "Add Signal" – always available when there is at least one message.
        if (!database_.messages.isEmpty()) {
            auto* addAction = new QAction("Add Signal", &contextMenu);
            connect(addAction, &QAction::triggered, this, [this, &selected]() {
                // Determine which message to add to.
                int targetMsgIdx = -1;
                if (!selected.isEmpty()) {
                    // Use the owning message of the selected signal.
                    auto* nameItem = signalsViewTable_->item(selected.first().row(), 0);
                    if (nameItem) {
                        const QString owningMsg = nameItem->data(Qt::UserRole).toString();
                        for (int i = 0; i < database_.messages.size(); ++i) {
                            if (database_.messages.at(i).name == owningMsg) {
                                targetMsgIdx = i;
                                break;
                            }
                        }
                    }
                }
                if (targetMsgIdx < 0) {
                    // No row selected — ask the user which message to add to.
                    QStringList msgNames;
                    for (const DbcMessage& m : database_.messages) { msgNames << m.name; }
                    bool ok = false;
                    const QString chosen = QInputDialog::getItem(
                        this, "Add Signal", "Select target message:", msgNames, 0, false, &ok);
                    if (!ok) { return; }
                    for (int i = 0; i < database_.messages.size(); ++i) {
                        if (database_.messages.at(i).name == chosen) {
                            targetMsgIdx = i;
                            break;
                        }
                    }
                }
                if (targetMsgIdx < 0) { return; }
                const int savedIdx = currentMessageIndex_;
                currentMessageIndex_ = targetMsgIdx;
                onAddSignal();
                currentMessageIndex_ = savedIdx;
            });
            contextMenu.addAction(addAction);
        }

        if (!selected.isEmpty()) {
            contextMenu.addAction(deleteSignalFromViewAction_);
        }
        contextMenu.exec(signalsViewTable_->viewport()->mapToGlobal(pos));
    });

    // Click Comment column (col 10) in the signals view to open the multi-line editor.
    connect(signalsViewTable_, &QTableWidget::cellClicked,
            this, [this](int row, int col) {
        auto* nameItem = signalsViewTable_->item(row, 0);
        if (!nameItem) { return; }
        const QString sigName = nameItem->text();
        const QString msgName = nameItem->data(Qt::UserRole).toString();
        if (col == 10) {
            QString current;
            for (const DbcMessage& m : database_.messages) {
                if (m.name != msgName) { continue; }
                for (const DbcSignal& sig : m.signalList) {
                    if (sig.name == sigName) { current = sig.comment; break; }
                }
                break;
            }
            openCommentEditor(this, current, [this, row, sigName, msgName](const QString& text) {
                for (DbcMessage& m : database_.messages) {
                    if (m.name != msgName) { continue; }
                    for (DbcSignal& sig : m.signalList) {
                        if (sig.name != sigName) { continue; }
                        sig.comment = text;
                        isDirty_ = true;
                        auto* cell = signalsViewTable_->item(row, 10);
                        if (cell) { cell->setText(text); }
                        break;
                    }
                    break;
                }
            });
        } else if (col == 12) {
            for (DbcMessage& m : database_.messages) {
                if (m.name != msgName) { continue; }
                for (DbcSignal& sig : m.signalList) {
                    if (sig.name != sigName) { continue; }
                    QList<DbcAttributeDef> defs;
                    for (const DbcAttributeDef& d : database_.attributes)
                        if (d.objectType == DbcAttributeDef::ObjectType::Signal) defs.append(d);
                    ObjectAttrDialog dlg(
                        QString("Attributes \u2013 Signal: %1 (in %2)").arg(sigName, msgName),
                        defs, sig.attrValues, this);
                    if (dlg.exec() == QDialog::Accepted) {
                        sig.attrValues = dlg.result();
                        isDirty_ = true;
                    }
                    return;
                }
                break;
            }
        }
    });

    // Double-click ValueTable column (col 9) in the signals view to open the editor.
    connect(signalsViewTable_, &QTableWidget::cellDoubleClicked,
            this, [this](int row, int col) {
        if (col != 9) { return; }
        auto* nameItem = signalsViewTable_->item(row, 0);
        if (!nameItem) { return; }
        const QString sigName = nameItem->text();
        const QString msgName = nameItem->data(Qt::UserRole).toString();
        for (DbcMessage& msg : database_.messages) {
            if (msg.name != msgName) { continue; }
            for (DbcSignal& sig : msg.signalList) {
                if (sig.name != sigName) { continue; }
                ValueTableEditorDialog dlg(
                    QString("Value Table \u2013 %1").arg(sigName),
                    sig.valueTableName,
                    sig.valueEntries,
                    database_.valueTables,
                    this);
                if (dlg.exec() == QDialog::Accepted) {
                    sig.valueTableName = dlg.name();
                    sig.valueEntries   = dlg.entries();
                    isDirty_ = true;
                    auto* vtItem = signalsViewTable_->item(row, 9);
                    if (vtItem) { vtItem->setText(sig.valueTableName); }
                }
                return;
            }
            break;
        }
    });

    // Column 11 uses a delegate instead of per-row combo box widgets.
    auto* msgPickerDelegate = new MessagePickerDelegate(signalsViewTable_);
    signalsViewTable_->setItemDelegateForColumn(11, msgPickerDelegate);

    connect(signalsViewTable_, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem* item) {
                if (item->column() != 11) { return; }
                const int row = item->row();
                const QTableWidgetItem* nameItem = signalsViewTable_->item(row, 0);
                if (!nameItem) { return; }
                const QString sigName    = nameItem->text();
                const QString oldMsgName = nameItem->data(Qt::UserRole).toString();
                const QString newMsgName = item->text();
                if (newMsgName == oldMsgName || newMsgName.isEmpty()) { return; }

                DbcSignal movedSig;
                bool found = false;
                for (DbcMessage& m : database_.messages) {
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

                for (DbcMessage& m : database_.messages) {
                    if (m.name == newMsgName) {
                        for (const DbcSignal& s : m.signalList) {
                            if (s.name == sigName) {
                                QMessageBox::warning(nullptr, "Duplicate Signal Name",
                                    QString("Error, signal: %1 already exists in Message %2.")
                                        .arg(sigName, newMsgName));
                                // Revert the item text back to the old message name.
                                const QSignalBlocker blk(signalsViewTable_);
                                item->setText(oldMsgName);
                                return;
                            }
                        }
                        m.signalList.append(movedSig);
                        break;
                    }
                }
                isDirty_ = true;
                refreshSignalsView();
            });

    // ── Value Tables view (global named VAL_TABLE_ definitions) ─────────────
    auto* vtWidget = new QWidget(this);
    auto* vtMainLayout = new QVBoxLayout(vtWidget);
    vtMainLayout->setContentsMargins(4, 4, 4, 4);
    vtMainLayout->setSpacing(4);

    auto* vtSplitter = new QSplitter(Qt::Horizontal, vtWidget);

    // Left panel: named table list
    auto* vtLeftWidget = new QWidget(vtSplitter);
    auto* vtLeftLayout = new QVBoxLayout(vtLeftWidget);
    vtLeftLayout->setContentsMargins(0, 0, 0, 0);
    vtLeftLayout->setSpacing(4);
    auto* vtListHeader = new QLabel("Named Value Tables (VAL_TABLE_)", vtLeftWidget);
    QFont vtHeaderFont = vtListHeader->font();
    vtHeaderFont.setBold(true);
    vtListHeader->setFont(vtHeaderFont);
    vtListHeader->setContentsMargins(4, 4, 4, 4);
    valueTablesListWidget_ = new QListWidget(vtLeftWidget);
    auto* vtListBtnRow = new QHBoxLayout();
    auto* vtAddTableBtn = new QPushButton("Add Table", vtLeftWidget);
    auto* vtDelTableBtn = new QPushButton("Delete Table", vtLeftWidget);
    vtListBtnRow->addWidget(vtAddTableBtn);
    vtListBtnRow->addWidget(vtDelTableBtn);
    vtLeftLayout->addWidget(vtListHeader);
    vtLeftLayout->addWidget(valueTablesListWidget_, 1);
    vtLeftLayout->addLayout(vtListBtnRow);
    connect(vtAddTableBtn, &QPushButton::clicked, this, &MainWindow::onAddValueTable);
    connect(vtDelTableBtn, &QPushButton::clicked, this, &MainWindow::onDeleteValueTable);
    connect(valueTablesListWidget_, &QListWidget::currentRowChanged,
            this, &MainWindow::onValueTableSelected);

    // Right-click context menu on named table list
    valueTablesListWidget_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(valueTablesListWidget_, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        QMenu menu(this);
        auto* addAct = menu.addAction("Add Table");
        connect(addAct, &QAction::triggered, this, &MainWindow::onAddValueTable);
        const QListWidgetItem* clickedItem = valueTablesListWidget_->itemAt(pos);
        if (clickedItem) {
            menu.addSeparator();
            auto* renameAct = menu.addAction("Rename Table");
            auto* deleteAct = menu.addAction("Delete Table");
            connect(renameAct, &QAction::triggered, this, [this]() {
                if (currentValueTableIndex_ < 0 ||
                    currentValueTableIndex_ >= database_.valueTables.size()) { return; }
                const QString oldName = database_.valueTables[currentValueTableIndex_].name;
                bool ok = false;
                const QString newName = QInputDialog::getText(
                    this, "Rename Value Table", "New name:",
                    QLineEdit::Normal, oldName, &ok).trimmed();
                if (!ok || newName.isEmpty() || newName == oldName) { return; }
                for (const DbcValueTable& vt : database_.valueTables) {
                    if (vt.name == newName) {
                        QMessageBox::warning(this, "Duplicate Name",
                            QString("A value table named \"%1\" already exists.").arg(newName));
                        return;
                    }
                }
                // Update all signal references pointing to this table
                for (DbcMessage& msg : database_.messages) {
                    for (DbcSignal& sig : msg.signalList) {
                        if (sig.valueTableName == oldName) { sig.valueTableName = newName; }
                    }
                }
                database_.valueTables[currentValueTableIndex_].name = newName;
                isDirty_ = true;
                refreshValueTablesView();
            });
            connect(deleteAct, &QAction::triggered, this, &MainWindow::onDeleteValueTable);
        }
        menu.exec(valueTablesListWidget_->mapToGlobal(pos));
    });

    // Right panel: entry editor for selected named table
    auto* vtRightWidget = new QWidget(vtSplitter);
    auto* vtRightLayout = new QVBoxLayout(vtRightWidget);
    vtRightLayout->setContentsMargins(0, 0, 0, 0);
    vtRightLayout->setSpacing(4);
    auto* vtEntriesHeader = new QLabel("Entries (Raw Value \u2192 Label)", vtRightWidget);
    QFont vtEntriesFont = vtEntriesHeader->font();
    vtEntriesFont.setBold(true);
    vtEntriesHeader->setFont(vtEntriesFont);
    vtEntriesHeader->setContentsMargins(4, 4, 4, 4);
    valueTablesEntriesTable_ = new QTableWidget(vtRightWidget);
    valueTablesEntriesTable_->setColumnCount(2);
    valueTablesEntriesTable_->setHorizontalHeaderLabels({"Raw Value", "Label"});
    valueTablesEntriesTable_->horizontalHeader()->setStretchLastSection(true);
    valueTablesEntriesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    valueTablesEntriesTable_->horizontalHeader()->setSectionsMovable(true);
    valueTablesEntriesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    valueTablesEntriesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    auto* vtEntryBtnRow = new QHBoxLayout();
    auto* vtAddEntryBtn = new QPushButton("Add Entry", vtRightWidget);
    auto* vtDelEntryBtn = new QPushButton("Delete Entry", vtRightWidget);
    vtEntryBtnRow->addWidget(vtAddEntryBtn);
    vtEntryBtnRow->addWidget(vtDelEntryBtn);
    vtEntryBtnRow->addStretch();
    vtRightLayout->addWidget(vtEntriesHeader);
    vtRightLayout->addWidget(valueTablesEntriesTable_, 1);
    vtRightLayout->addLayout(vtEntryBtnRow);
    connect(vtAddEntryBtn, &QPushButton::clicked, this, &MainWindow::onAddValueTableEntry);
    connect(vtDelEntryBtn, &QPushButton::clicked, this, &MainWindow::onDeleteValueTableEntry);

    // Delete key scoped to the list widget deletes the selected table.
    {
        auto* sc = new QShortcut(QKeySequence::Delete, valueTablesListWidget_);
        sc->setContext(Qt::WidgetShortcut);
        connect(sc, &QShortcut::activated, this, &MainWindow::onDeleteValueTable);
    }
    // Delete key scoped to the entries table deletes the selected entry.
    {
        auto* sc = new QShortcut(QKeySequence::Delete, valueTablesEntriesTable_);
        sc->setContext(Qt::WidgetShortcut);
        connect(sc, &QShortcut::activated, this, &MainWindow::onDeleteValueTableEntry);
    }
    connect(valueTablesEntriesTable_, &QTableWidget::itemChanged,
            this, &MainWindow::onValueTableEntryItemChanged);

    // Right-click context menu on entries table
    valueTablesEntriesTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(valueTablesEntriesTable_, &QTableWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        if (currentValueTableIndex_ < 0) { return; }
        QMenu menu(this);
        auto* addAct = menu.addAction("Add Entry");
        connect(addAct, &QAction::triggered, this, &MainWindow::onAddValueTableEntry);
        const QModelIndex idx = valueTablesEntriesTable_->indexAt(pos);
        if (idx.isValid()) {
            menu.addSeparator();
            auto* delAct = menu.addAction("Delete Entry");
            connect(delAct, &QAction::triggered, this, &MainWindow::onDeleteValueTableEntry);
        }
        menu.exec(valueTablesEntriesTable_->mapToGlobal(pos));
    });

    vtSplitter->addWidget(vtLeftWidget);
    vtSplitter->addWidget(vtRightWidget);
    vtSplitter->setStretchFactor(0, 0);
    vtSplitter->setStretchFactor(1, 1);
    vtSplitter->setSizes({220, 500});
    vtMainLayout->addWidget(vtSplitter, 1);

    centerStack_ = new QStackedWidget(this);
    centerStack_->addWidget(tableSplitter);      // 0: Overview
    centerStack_->addWidget(nodesViewTable_);    // 1: Nodes
    centerStack_->addWidget(messagesViewTable_); // 2: Messages
    centerStack_->addWidget(signalsViewTable_);  // 3: Signals
    centerStack_->addWidget(vtWidget);           // 4: Value Tables

    // ── Attributes view (index 5) ─────────────────────────────────────────
    {
        auto* attrWidget    = new QWidget(this);
        auto* attrMainLayout = new QVBoxLayout(attrWidget);
        attrMainLayout->setContentsMargins(4, 4, 4, 4);
        attrMainLayout->setSpacing(4);

        auto* attrSplitter = new QSplitter(Qt::Horizontal, attrWidget);

        // ── Left: attribute list ─────────────────────────────────────────
        auto* attrLeft = new QWidget(attrSplitter);
        auto* attrLeftLayout = new QVBoxLayout(attrLeft);
        attrLeftLayout->setContentsMargins(0, 0, 0, 0);
        attrLeftLayout->setSpacing(4);

        auto* attrListHeader = new QLabel("Attribute Definitions (BA_DEF_)", attrLeft);
        QFont attrHFont = attrListHeader->font(); attrHFont.setBold(true); attrListHeader->setFont(attrHFont);
        attrListHeader->setContentsMargins(4, 4, 4, 4);
        attrListWidget_ = new QListWidget(attrLeft);

        auto* attrListBtnRow = new QHBoxLayout();
        auto* attrAddBtn = new QPushButton("Add Attribute", attrLeft);
        auto* attrDelBtn = new QPushButton("Delete Attribute", attrLeft);
        attrListBtnRow->addWidget(attrAddBtn);
        attrListBtnRow->addWidget(attrDelBtn);

        attrLeftLayout->addWidget(attrListHeader);
        attrLeftLayout->addWidget(attrListWidget_, 1);
        attrLeftLayout->addLayout(attrListBtnRow);

        connect(attrAddBtn, &QPushButton::clicked, this, &MainWindow::onAddAttribute);
        connect(attrDelBtn, &QPushButton::clicked, this, &MainWindow::onDeleteAttribute);
        connect(attrListWidget_, &QListWidget::currentRowChanged, this, &MainWindow::onAttributeSelected);

        // Right-click on attribute list
        attrListWidget_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(attrListWidget_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
            QMenu menu(this);
            auto* addAct = menu.addAction("Add Attribute");
            connect(addAct, &QAction::triggered, this, &MainWindow::onAddAttribute);
            if (attrListWidget_->itemAt(pos)) {
                menu.addSeparator();
                auto* delAct = menu.addAction("Delete Attribute");
                connect(delAct, &QAction::triggered, this, &MainWindow::onDeleteAttribute);
            }
            menu.exec(attrListWidget_->mapToGlobal(pos));
        });

        // ── Right: attribute editor form ─────────────────────────────────
        auto* attrRight = new QScrollArea(attrSplitter);
        attrRight->setWidgetResizable(true);
        attrRight->setFrameShape(QFrame::NoFrame);

        attrFormWidget_ = new QWidget();
        auto* formLayout = new QVBoxLayout(attrFormWidget_);
        formLayout->setContentsMargins(8, 8, 8, 8);
        formLayout->setSpacing(6);

        // ── Top fields (always visible) ──────────────────────────────────
        auto* topForm = new QFormLayout();
        topForm->setLabelAlignment(Qt::AlignRight);

        attrNameEdit_ = new QLineEdit(attrFormWidget_);
        attrNameEdit_->setPlaceholderText("Attribute name");
        topForm->addRow("Name:", attrNameEdit_);

        attrObjTypeCombo_ = new QComboBox(attrFormWidget_);
        attrObjTypeCombo_->addItems({"Network", "Node", "Message", "Signal"});
        topForm->addRow("Object Type:", attrObjTypeCombo_);

        attrValueTypeCombo_ = new QComboBox(attrFormWidget_);
        attrValueTypeCombo_->addItems({"Integer", "Float", "String", "Enumeration", "Hex"});
        topForm->addRow("Value Type:", attrValueTypeCombo_);

        formLayout->addLayout(topForm);

        auto* sep = new QFrame(attrFormWidget_);
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        formLayout->addWidget(sep);

        // ── Stacked widget for conditional fields ────────────────────────
        attrValueStack_ = new QStackedWidget(attrFormWidget_);
        formLayout->addWidget(attrValueStack_);
        formLayout->addStretch();

        // Stack page 0: Integer / Float / Hex
        {
            auto* pg = new QWidget();
            auto* pf = new QFormLayout(pg);
            pf->setLabelAlignment(Qt::AlignRight);
            attrDefaultNumEdit_ = new QLineEdit(pg);
            attrDefaultNumEdit_->setPlaceholderText("e.g. 0");
            attrMinEdit_ = new QLineEdit(pg);
            attrMinEdit_->setPlaceholderText("e.g. 0");
            attrMaxEdit_ = new QLineEdit(pg);
            attrMaxEdit_->setPlaceholderText("e.g. 255");
            pf->addRow("Default:", attrDefaultNumEdit_);
            pf->addRow("Minimum:", attrMinEdit_);
            pf->addRow("Maximum:", attrMaxEdit_);
            attrValidationLabel_ = new QLabel(pg);
            attrValidationLabel_->setWordWrap(true);
            attrValidationLabel_->setStyleSheet("color: red;");
            attrValidationLabel_->setAlignment(Qt::AlignCenter);
            attrValidationLabel_->hide();
            pf->addRow(attrValidationLabel_);
            attrValueStack_->addWidget(pg); // page 0
        }

        // Stack page 1: String
        {
            auto* pg = new QWidget();
            auto* pf = new QFormLayout(pg);
            pf->setLabelAlignment(Qt::AlignRight);
            attrDefaultStrEdit_ = new QLineEdit(pg);
            attrDefaultStrEdit_->setPlaceholderText("Default string value");
            pf->addRow("Default:", attrDefaultStrEdit_);
            attrValueStack_->addWidget(pg); // page 1
        }

        // Stack page 2: Enumeration
        {
            auto* pg = new QWidget();
            auto* pv = new QVBoxLayout(pg);
            pv->setContentsMargins(0, 0, 0, 0);
            auto* epf = new QFormLayout();
            epf->setLabelAlignment(Qt::AlignRight);
            attrEnumDefaultCombo_ = new QComboBox(pg);
            epf->addRow("Default:", attrEnumDefaultCombo_);
            pv->addLayout(epf);
            auto* enumLbl = new QLabel("Enumeration Values:", pg);
            QFont ef = enumLbl->font(); ef.setBold(true); enumLbl->setFont(ef);
            pv->addWidget(enumLbl);
            attrEnumTable_ = new QTableWidget(0, 1, pg);
            attrEnumTable_->setHorizontalHeaderLabels({"Value"});
            attrEnumTable_->horizontalHeader()->setStretchLastSection(true);
            attrEnumTable_->horizontalHeader()->setSectionsMovable(false);
            attrEnumTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
            attrEnumTable_->setSelectionMode(QAbstractItemView::SingleSelection);
            pv->addWidget(attrEnumTable_, 1);
            auto* enumBtnRow = new QHBoxLayout();
            auto* enumAddBtn = new QPushButton("Add Value", pg);
            auto* enumDelBtn = new QPushButton("Remove Value", pg);
            enumBtnRow->addWidget(enumAddBtn);
            enumBtnRow->addWidget(enumDelBtn);
            enumBtnRow->addStretch();
            pv->addLayout(enumBtnRow);
            connect(enumAddBtn, &QPushButton::clicked, this, &MainWindow::onAddEnumValue);
            connect(enumDelBtn, &QPushButton::clicked, this, &MainWindow::onDeleteEnumValue);
            connect(attrEnumTable_, &QTableWidget::itemChanged, this, &MainWindow::onAttrEnumItemChanged);
            // Right-click on enum table
            attrEnumTable_->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(attrEnumTable_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
                if (attrFormUpdating_ || currentAttrIndex_ < 0) { return; }
                QMenu menu(this);
                auto* addAct = menu.addAction("Add Value");
                connect(addAct, &QAction::triggered, this, &MainWindow::onAddEnumValue);
                if (attrEnumTable_->indexAt(pos).isValid()) {
                    auto* delAct = menu.addAction("Remove Value");
                    connect(delAct, &QAction::triggered, this, &MainWindow::onDeleteEnumValue);
                }
                menu.exec(attrEnumTable_->mapToGlobal(pos));
            });
            attrValueStack_->addWidget(pg); // page 2
        }

        attrRight->setWidget(attrFormWidget_);
        attrFormWidget_->setEnabled(false);

        // Wire form changes → model update
        connect(attrNameEdit_, &QLineEdit::textEdited, this, &MainWindow::onAttrFieldChanged);
        connect(attrObjTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            if (attrFormUpdating_) { return; }
            if (currentAttrIndex_ < 0 || currentAttrIndex_ >= database_.attributes.size()) { return; }
            // Update the objectType in the model first.
            DbcAttributeDef& attr = database_.attributes[currentAttrIndex_];
            attr.objectType = static_cast<DbcAttributeDef::ObjectType>(attrObjTypeCombo_->currentIndex());
            // Clear default / min / max so the user must re-enter values
            // appropriate for the new object type.
            attr.defaultValue.clear();
            attr.minimum.clear();
            attr.maximum.clear();
            isDirty_ = true;
            // Repopulate the form from the (now cleared) model — this is the
            // canonical way to keep the UI in sync and will blank the fields.
            refreshAttrFormFromModel();
        });
        connect(attrValueTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            attrValueStack_->setCurrentIndex(MainWindow::attrStackPageForValueTypeIndex(idx));
            const bool isHex = (idx == 4);
            attrDefaultNumEdit_->setPlaceholderText(isHex ? "e.g. 0x0"  : "e.g. 0");
            attrMinEdit_->setPlaceholderText       (isHex ? "e.g. 0x00" : "e.g. 0");
            attrMaxEdit_->setPlaceholderText       (isHex ? "e.g. 0xFF" : "e.g. 255");
            if (!attrFormUpdating_) { onAttrFieldChanged(); }
        });
        connect(attrDefaultNumEdit_,  &QLineEdit::textEdited, this, &MainWindow::onAttrFieldChanged);
        connect(attrMinEdit_,         &QLineEdit::textEdited, this, &MainWindow::onAttrFieldChanged);
        connect(attrMaxEdit_,         &QLineEdit::textEdited, this, &MainWindow::onAttrFieldChanged);
        connect(attrDefaultStrEdit_,  &QLineEdit::textEdited, this, &MainWindow::onAttrFieldChanged);

        // Validate format on focus-out for each numeric field.
        // We need a helper to keep the lambda terse.
        auto connectNumericValidation = [this](QLineEdit* field, const QString& fieldLabel) {
            connect(field, &QLineEdit::editingFinished, this, [this, field, fieldLabel]() {
                if (attrFormUpdating_) { return; }
                const QString valStr = field->text().trimmed();
                if (valStr.isEmpty()) { return; }
                const int vtIdx = attrValueTypeCombo_->currentIndex();
                // 0=Integer, 1=Float, 4=Hex → numeric pages;  2/3 never shown on page 0
                QString errMsg;
                if (vtIdx == 1) { // Float
                    bool ok = false;
                    valStr.toDouble(&ok);
                    if (!ok)
                        errMsg = QString("'%1': \"%2\" is not a valid floating-point number.")
                                     .arg(fieldLabel, valStr);
                } else if (vtIdx == 4) { // Hex
                    bool ok = false;
                    valStr.toLongLong(&ok, 0); // base-0 handles 0x prefix
                    if (!ok)
                        errMsg = QString("'%1': \"%2\" is not a valid hexadecimal integer (e.g. 0x1F or 31).")
                                     .arg(fieldLabel, valStr);
                } else { // Integer (vtIdx == 0)
                    bool ok = false;
                    valStr.toLongLong(&ok);
                    if (!ok)
                        errMsg = QString("'%1': \"%2\" is not a valid integer.")
                                     .arg(fieldLabel, valStr);
                }
                if (!errMsg.isEmpty()) {
                    QTimer::singleShot(0, this, [this, field, errMsg]() {
                        QMessageBox::warning(this, "Invalid Value", errMsg);
                        field->setFocus();
                        field->selectAll();
                    });
                }
            });
        };
        connectNumericValidation(attrDefaultNumEdit_, "Default");
        connectNumericValidation(attrMinEdit_,        "Minimum");
        connectNumericValidation(attrMaxEdit_,        "Maximum");
        connect(attrEnumDefaultCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            if (!attrFormUpdating_) { onAttrFieldChanged(); }
        });

        attrSplitter->addWidget(attrLeft);
        attrSplitter->addWidget(attrRight);
        attrSplitter->setStretchFactor(0, 0);
        attrSplitter->setStretchFactor(1, 1);
        attrSplitter->setSizes({240, 500});

        attrMainLayout->addWidget(attrSplitter, 1);
        centerStack_->addWidget(attrWidget);     // 5: Attributes
    }

    auto* contentSplitter = new QSplitter(Qt::Horizontal, this);
    contentSplitter->addWidget(hierarchyTree_);
    contentSplitter->addWidget(centerStack_);
    contentSplitter->setStretchFactor(0, 0);
    contentSplitter->setStretchFactor(1, 1);

    setCentralWidget(contentSplitter);

    bitLayoutWidget_ = new BitLayoutWidget();

    connect(bitLayoutWidget_, &BitLayoutWidget::signalMoved,
            this, [this](const QString& signalName, int newStartBit) {
        if (currentMessageIndex_ < 0 || currentMessageIndex_ >= database_.messages.size()) {
            return;
        }
        auto& msg = database_.messages[currentMessageIndex_];
        for (DbcSignal& sig : msg.signalList) {
            if (sig.name == signalName) {
                sig.startBit = newStartBit;
                break;
            }
        }
        isDirty_ = true;
        refreshAll();
    });

    // Bit layout click → select the matching row in the signal table.
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

    // Signal table selection → highlight the signal in the bit layout.
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

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(bitLayoutWidget_);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto* bitLayoutContainer = new QWidget(this);
    auto* bitLayoutLayout = new QVBoxLayout(bitLayoutContainer);
    bitLayoutLayout->setContentsMargins(8, 8, 8, 8);
    bitLayoutLayout->setSpacing(8);

    auto* muxRow = new QHBoxLayout();
    auto* muxLabel = new QLabel("Multiplexor Signal", bitLayoutContainer);
    multiplexorSignalCombo_ = new QComboBox(bitLayoutContainer);
    multiplexorSignalCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(multiplexorSignalCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        refreshBitLayoutForMultiplexorSelection();
    });
    muxRow->addWidget(muxLabel);
    muxRow->addWidget(multiplexorSignalCombo_, 1);

    bitLayoutLayout->addLayout(muxRow);
    bitLayoutLayout->addWidget(scrollArea, 1);

    bitLayoutDock_ = new QDockWidget("Bit Layout", this);
    bitLayoutDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    bitLayoutDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    bitLayoutDock_->setWidget(bitLayoutContainer);
    addDockWidget(Qt::RightDockWidgetArea, bitLayoutDock_);
}

void MainWindow::refreshAll()
{
    refreshHierarchy();
    refreshMessageTable();
    refreshSignalTable();
    refreshMultiplexorSignalDropdown();
    refreshNodesView();
    if (currentViewMode_ == ViewMode::Messages) { refreshMessagesView(); }
    if (currentViewMode_ == ViewMode::Signals)  { refreshSignalsView(); }
    refreshValueTablesView();
    refreshAttributesView();

    refreshBitLayoutForMultiplexorSelection();

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

void MainWindow::refreshMessageTable()
{
    const bool wasSorting = messageTable_->isSortingEnabled();
    messageTable_->setSortingEnabled(false);

    const int savedMessageIndex = currentMessageIndex_;

    messageTable_->clearContents();
    messageTable_->setRowCount(database_.messages.size());

    for (int row = 0; row < database_.messages.size(); ++row) {
        const DbcMessage& msg = database_.messages.at(row);
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

void MainWindow::refreshSignalTable()
{
    const bool wasSorting = signalTable_->isSortingEnabled();
    signalTable_->setSortingEnabled(false);

    // Block itemChanged so that repopulating the table doesn't trigger
    // onSignalTableItemChanged for every cell we write.
    const QSignalBlocker sigTableBlocker(signalTable_);

    signalTable_->clearContents();

    if (currentMessageIndex_ < 0 || currentMessageIndex_ >= database_.messages.size()) {
        signalTable_->setRowCount(0);
        signalTable_->setSortingEnabled(wasSorting);
        return;
    }

    const DbcMessage& message = database_.messages.at(currentMessageIndex_);
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
        modeValueEdit->setValidator(new QRegularExpressionValidator(QRegularExpression("^[0-9A-Fa-f]*$"), modeValueEdit));
        modeValueEdit->setMaxLength(8);
        modeValueEdit->setText(dbcSig.modeValueHex);
        modeValueEdit->setVisible(initialMode == "M=");
        modeLayout->addWidget(modeValueEdit);

        const QString initialModeDisplay = initialMode == "M="
                                               ? QString("M=%1").arg(dbcSig.modeValueHex)
                                               : initialMode;
        signalTable_->setItem(row, 1, new QTableWidgetItem(initialModeDisplay));
        signalTable_->setCellWidget(row, 1, modeContainer);

        connect(modeCombo, &QComboBox::currentTextChanged, this, [this, modeValueEdit, name = dbcSig.name](const QString& text) {
            if (currentMessageIndex_ < 0 || currentMessageIndex_ >= database_.messages.size()) {
                return;
            }
            auto& signalList = database_.messages[currentMessageIndex_].signalList;
            for (int i = 0; i < signalList.size(); ++i) {
                if (signalList[i].name != name) {
                    continue;
                }
                signalList[i].mode = text;
                if (text != "M=") {
                    signalList[i].modeValueHex.clear();
                }
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

            isDirty_ = true;
            refreshMultiplexorSignalDropdown();
            refreshBitLayoutForMultiplexorSelection();
        });

        connect(modeValueEdit, &QLineEdit::textChanged, this, [this, name = dbcSig.name, modeValueEdit](const QString& text) {
            if (currentMessageIndex_ < 0 || currentMessageIndex_ >= database_.messages.size()) {
                return;
            }
            auto& signalList = database_.messages[currentMessageIndex_].signalList;
            for (int i = 0; i < signalList.size(); ++i) {
                if (signalList[i].name != name) {
                    continue;
                }
                if (signalList[i].mode == "M=") {
                    signalList[i].modeValueHex = text.toUpper();
                    QModelIndex modelIndex = signalTable_->indexAt(modeValueEdit->parentWidget()->pos());
                    if (modelIndex.isValid()) {
                        QTableWidgetItem* modeItem = signalTable_->item(modelIndex.row(), 1);
                        if (modeItem) {
                            modeItem->setText(QString("M=%1").arg(text.toUpper()));
                        }
                    }
                    isDirty_ = true;
                    refreshMultiplexorSignalDropdown();
                    refreshBitLayoutForMultiplexorSelection();
                }
                break;
            }
        });

        signalTable_->setItem(row, 2, new NumericTableWidgetItem(dbcSignalLsb(dbcSig)));
        signalTable_->setItem(row, 3, new NumericTableWidgetItem(dbcSig.bitLength));

        // ── Byte Order – combo box ─────────────────────────────────────────
        {
            auto* byteOrderCombo = new FocusComboBox(signalTable_);
            byteOrderCombo->addItems({"Motorola", "Intel"});
            byteOrderCombo->setCurrentText(dbcSig.byteOrder);
            signalTable_->setItem(row, 4, new QTableWidgetItem(dbcSig.byteOrder));
            signalTable_->setCellWidget(row, 4, byteOrderCombo);
            connect(byteOrderCombo, &QComboBox::currentTextChanged,
                    this, [this, name = dbcSig.name](const QString& text) {
                if (currentMessageIndex_ < 0 || currentMessageIndex_ >= database_.messages.size()) {
                    return;
                }
                auto& signalList = database_.messages[currentMessageIndex_].signalList;
                for (DbcSignal& sig : signalList) {
                    if (sig.name != name) { continue; }
                    if (sig.byteOrder == text) { return; }
                    // Preserve LSB position across byte-order flip.
                    const int lsb = dbcSignalLsb(sig);
                    sig.byteOrder = text;
                    sig.startBit  = dbcStartBitFromLsb(sig, lsb);
                    // If the user preference is "Last Value", remember this choice.
                    {
                        QSettings s;
                        if (s.value("defaults/signals/byteOrder", "Last Value").toString() == "Last Value") {
                            s.setValue("defaults/signals/byteOrder/lastValue", text);
                        }
                    }
                    // Refresh the startbit cell (LSB stays the same, so value is unchanged,
                    // but force a repaint in case the model startBit changed internally).
                    for (int r = 0; r < signalTable_->rowCount(); ++r) {
                        QTableWidgetItem* ni = signalTable_->item(r, 0);
                        if (ni && ni->text() == name) {
                            const QSignalBlocker b(signalTable_);
                            QTableWidgetItem* sbItem = signalTable_->item(r, 2);
                            if (sbItem) {
                                sbItem->setText(QString::number(dbcSignalLsb(sig)));
                            }
                            break;
                        }
                    }
                    isDirty_ = true;
                    refreshBitLayoutForMultiplexorSelection();
                    break;
                }
            });
        }

        // ── Value Type – combo box ─────────────────────────────────────────
        {
            auto* typeCombo = new FocusComboBox(signalTable_);
            typeCombo->addItems({"Unsigned", "Signed", "Float", "Double"});
            typeCombo->setCurrentText(dbcSig.valueType);
            signalTable_->setItem(row, 5, new QTableWidgetItem(dbcSig.valueType));
            signalTable_->setCellWidget(row, 5, typeCombo);
            connect(typeCombo, &QComboBox::currentTextChanged,
                    this, [this, name = dbcSig.name](const QString& text) {
                if (currentMessageIndex_ < 0 || currentMessageIndex_ >= database_.messages.size()) {
                    return;
                }
                auto& signalList = database_.messages[currentMessageIndex_].signalList;
                for (DbcSignal& sig : signalList) {
                    if (sig.name != name) { continue; }
                    sig.valueType = text;
                    isDirty_ = true;
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

        // ── Value Table – button (col 12) ─────────────────────────────
        {
            auto vtBtnLabel = [](const DbcSignal& s) -> QString {
                return s.valueTableName;
            };
            auto* vtBtn = new QPushButton(vtBtnLabel(dbcSig), signalTable_);
            const QString sigName = dbcSig.name;
            connect(vtBtn, &QPushButton::clicked, this, [this, sigName]() {
                if (currentMessageIndex_ < 0 || currentMessageIndex_ >= database_.messages.size()) { return; }
                auto& msgSigs = database_.messages[currentMessageIndex_].signalList;
                for (DbcSignal& sig : msgSigs) {
                    if (sig.name != sigName) { continue; }
                    ValueTableEditorDialog dlg(
                        QString("Value Table \u2013 %1").arg(sigName),
                        sig.valueTableName,
                        sig.valueEntries,
                        database_.valueTables,
                        this);
                    if (dlg.exec() == QDialog::Accepted) {
                        sig.valueTableName = dlg.name();
                        sig.valueEntries   = dlg.entries();
                        isDirty_ = true;
                        // Update button text without full rebuild
                        for (int r = 0; r < signalTable_->rowCount(); ++r) {
                            auto* ni = signalTable_->item(r, 0);
                            if (ni && ni->text() == sigName) {
                                auto* btn = qobject_cast<QPushButton*>(signalTable_->cellWidget(r, 12));
                                if (btn) {
                                    btn->setText(sig.valueTableName);
                                }
                                break;
                            }
                        }
                    }
                    break;
                }
            });
            signalTable_->setCellWidget(row, 12, vtBtn);
        }

        // ── Attributes – clickable cell (col 13) ─────────────────────────
        {
            auto* ai = new QTableWidgetItem("Edit...");
            ai->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            ai->setForeground(QBrush(QColor("#0057b8")));
            signalTable_->setItem(row, 13, ai);
        }
    }

    signalTable_->setSortingEnabled(wasSorting);
}

void MainWindow::onSignalTableItemChanged(QTableWidgetItem* item)
{
    if (!item) {
        return;
    }

    const int col = item->column();
    // Columns handled here: 0=Name, 2=Startbit, 3=Length, 8=Minimum, 9=Maximum
    // (Byte Order col 4 and Type col 5 are handled by their combo box signals)
    if (col != 0 && col != 2 && col != 3 && col != 8 && col != 9) {
        return;
    }

    if (currentMessageIndex_ < 0 || currentMessageIndex_ >= database_.messages.size()) {
        return;
    }

    // Resolve the original signal index stored as UserRole on the Name item.
    QTableWidgetItem* nameItem = signalTable_->item(item->row(), 0);
    if (!nameItem) {
        return;
    }
    const int sigIndex = nameItem->data(Qt::UserRole).toInt();

    DbcMessage& msg = database_.messages[currentMessageIndex_];
    if (sigIndex < 0 || sigIndex >= msg.signalList.size()) {
        return;
    }

    DbcSignal& sig = msg.signalList[sigIndex];
    const QString text = item->text().trimmed();
    bool needsLayoutRefresh = false;

    if (col == 0) {
        // ── Name ────────────────────────────────────────────────────────────
        if (text == sig.name) {
            return;
        }
        if (!isValidSymbolName(text)) {
            const QSignalBlocker b(signalTable_);
            item->setText(sig.name);
            QMessageBox::warning(this, "Invalid Signal Name",
                QString("'%1' is not a valid signal name.\n\n"
                        "Names must start with a letter and contain only\n"
                        "letters, digits, and underscores (A-Z, 0-9, _).").arg(text));
            return;
        }
        // Signal names must be unique within the same message.
        for (int si = 0; si < msg.signalList.size(); ++si) {
            if (si == sigIndex) { continue; } // skip self
            if (msg.signalList.at(si).name == text) {
                const QSignalBlocker b(signalTable_);
                item->setText(sig.name);
                QMessageBox::warning(this, "Duplicate Signal Name",
                    QString("Error, signal: %1 already exists in Message %2.")
                        .arg(text, msg.name));
                return;
            }
        }
        sig.name = text;
        needsLayoutRefresh = true; // signal name label in bit layout changes

    } else if (col == 2) {
        // ── Startbit (displayed as LSB) ────────────────────────────────────
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
        // ── Length [Bit] ───────────────────────────────────────────────────
        bool ok = false;
        const int len = text.toInt(&ok);
        if (!ok || len < 1 || len == sig.bitLength) {
            const QSignalBlocker b(signalTable_);
            item->setText(QString::number(sig.bitLength));
            return;
        }
        // Preserve the LSB position: recalculate MSB startBit after length change.
        const int savedLsb = dbcSignalLsb(sig);
        sig.bitLength = len;
        sig.startBit  = dbcStartBitFromLsb(sig, savedLsb);
        // LSB hasn't moved, so the cell doesn't need updating.
        needsLayoutRefresh = true;

    } else if (col == 8) {
        // ── Minimum ───────────────────────────────────────────────────────
        bool ok = false;
        const double val = text.toDouble(&ok);
        if (!ok) {
            const QSignalBlocker b(signalTable_);
            item->setText(QString::number(sig.minimum, 'g', 10));
            return;
        }
        sig.minimum = val;
        isDirty_ = true;

    } else if (col == 9) {
        // ── Maximum ───────────────────────────────────────────────────────
        bool ok = false;
        const double val = text.toDouble(&ok);
        if (!ok) {
            const QSignalBlocker b(signalTable_);
            item->setText(QString::number(sig.maximum, 'g', 10));
            return;
        }
        sig.maximum = val;
        isDirty_ = true;
    }

    if (needsLayoutRefresh) {
        isDirty_ = true;
        refreshBitLayoutForMultiplexorSelection();
    }
}

void MainWindow::refreshMultiplexorSignalDropdown()
{
    if (!multiplexorSignalCombo_) {
        return;
    }

    const QSignalBlocker blocker(multiplexorSignalCombo_);
    multiplexorSignalCombo_->clear();

    if (currentMessageIndex_ < 0 || currentMessageIndex_ >= database_.messages.size()) {
        multiplexorSignalCombo_->addItem("(No message selected)");
        multiplexorSignalCombo_->setEnabled(false);
        return;
    }

    const DbcMessage& message = database_.messages.at(currentMessageIndex_);
    QString multiplexorName;
    QSet<quint32> multiplexedValues;

    for (const DbcSignal& sig : message.signalList) {
        if (sig.mode.compare("Multiplexor", Qt::CaseInsensitive) == 0) {
            if (multiplexorName.isEmpty()) {
                multiplexorName = sig.name;
            }
        }

        if (sig.mode.compare("M=", Qt::CaseInsensitive) == 0) {
            QString raw = sig.modeValueHex.trimmed();
            if (raw.startsWith("0x", Qt::CaseInsensitive)) {
                raw = raw.mid(2);
            }

            bool ok = false;
            const quint32 value = raw.toUInt(&ok, 16);
            if (ok) {
                multiplexedValues.insert(value);
            }
        }
    }

    if (multiplexorName.isEmpty()) {
        multiplexorName = "Multiplexor";
    }

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

void MainWindow::refreshBitLayoutForMultiplexorSelection()
{
    if (currentViewMode_ == ViewMode::Signals) {
        bitLayoutWidget_->setMessage(nullptr);
        return;
    }
    if (currentMessageIndex_ < 0 || currentMessageIndex_ >= database_.messages.size()) {
        bitLayoutWidget_->setMessage(nullptr);
        return;
    }

    const DbcMessage& sourceMessage = database_.messages.at(currentMessageIndex_);

    // Index 0 means "No Multiplexor" -> hide all multiplexed (M=) signals.
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
            if (raw.startsWith("0x", Qt::CaseInsensitive)) {
                raw = raw.mid(2);
            }

            bool valueOk = false;
            const quint32 value = raw.toUInt(&valueOk, 16);
            if (valueOk && value == selectedMuxValue) {
                filteredMessage.signalList.append(sig);
            }
            continue;
        }

        // Preserve unknown modes to avoid dropping user data from visualization.
        filteredMessage.signalList.append(sig);
    }

    bitLayoutWidget_->setMessage(&filteredMessage);
}

void MainWindow::refreshNodesView()
{
    const bool wasSorting = nodesViewTable_->isSortingEnabled();
    nodesViewTable_->setSortingEnabled(false);
    nodesViewTable_->clearContents();
    nodesViewTable_->setRowCount(database_.nodes.size());
    for (int row = 0; row < database_.nodes.size(); ++row) {
        const DbcNode& node = database_.nodes.at(row);
        {
            auto* ni = new QTableWidgetItem(node.name);
            ni->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            nodesViewTable_->setItem(row, 0, ni);
        }
        {
            auto* ai = new QTableWidgetItem(node.address);
            ai->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            nodesViewTable_->setItem(row, 1, ai);
        }
        {
            auto* ci = new QTableWidgetItem(node.comment);
            ci->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            nodesViewTable_->setItem(row, 2, ci);
        }
        {
            auto* ai = new QTableWidgetItem("Edit...");
            ai->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            ai->setForeground(QBrush(QColor("#0057b8")));
            nodesViewTable_->setItem(row, 3, ai);
        }
    }
    nodesViewTable_->setSortingEnabled(wasSorting);
}

void MainWindow::refreshMessagesView()
{
    const bool wasSorting = messagesViewTable_->isSortingEnabled();
    messagesViewTable_->setSortingEnabled(false);
    messagesViewTable_->setUpdatesEnabled(false);
    messagesViewTable_->clearContents();
    messagesViewTable_->setRowCount(database_.messages.size());
    for (int row = 0; row < database_.messages.size(); ++row) {
        const DbcMessage& msg = database_.messages.at(row);
        auto ro = [](const QString& text) -> QTableWidgetItem* {
            auto* it = new QTableWidgetItem(text);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            return it;
        };
        messagesViewTable_->setItem(row, 0, ro(msg.name));
        messagesViewTable_->setItem(row, 1, ro(QString("0x%1").arg(QString::number(msg.id, 16).toUpper())));
        messagesViewTable_->setItem(row, 2, ro(msg.isExtended ? "CAN Extended" : "CAN Standard"));
        messagesViewTable_->setItem(row, 3, ro(QString::number(msg.dlc)));
        messagesViewTable_->setItem(row, 4, ro(QString::number(msg.cycleTimeMs)));
        messagesViewTable_->setItem(row, 5, ro(msg.transmitter));
        {
            auto* ci = new QTableWidgetItem(msg.comment);
            ci->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            messagesViewTable_->setItem(row, 6, ci);
        }
        {
            auto* ai = new QTableWidgetItem("Edit...");
            ai->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            ai->setForeground(QBrush(QColor("#0057b8")));
            messagesViewTable_->setItem(row, 7, ai);
        }
    }
    messagesViewTable_->setSortingEnabled(wasSorting);
    messagesViewTable_->setUpdatesEnabled(true);
}

void MainWindow::refreshSignalsView()
{
    auto* msgPickerDelegate =
        static_cast<MessagePickerDelegate*>(signalsViewTable_->itemDelegateForColumn(11));

    const bool wasSorting = signalsViewTable_->isSortingEnabled();
    signalsViewTable_->setSortingEnabled(false);
    int totalSignals = 0;
    for (const DbcMessage& msg : database_.messages) {
        totalSignals += msg.signalList.size();
    }

    // Update the delegate's name list before populating rows.
    QStringList msgNames;
    for (const DbcMessage& m : database_.messages) { msgNames.append(m.name); }
    if (msgPickerDelegate) { msgPickerDelegate->setMessageNames(msgNames); }

    // Block itemChanged so the signal-move handler doesn't fire during rebuild.
    signalsViewTable_->blockSignals(true);
    signalsViewTable_->clearContents();
    signalsViewTable_->setRowCount(totalSignals);
    signalsViewTable_->setUpdatesEnabled(false);

    int row = 0;
    for (const DbcMessage& msg : database_.messages) {
        for (const DbcSignal& sig : msg.signalList) {
            auto ro = [](const QString& text) -> QTableWidgetItem* {
                auto* it = new QTableWidgetItem(text);
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                return it;
            };
            auto* nameItem = new QTableWidgetItem(sig.name);
            nameItem->setData(Qt::UserRole, msg.name); // store owning message
            nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            signalsViewTable_->setItem(row, 0,  nameItem);
            signalsViewTable_->setItem(row, 1,  ro(QString::number(sig.bitLength)));
            signalsViewTable_->setItem(row, 2,  ro(sig.byteOrder));
            signalsViewTable_->setItem(row, 3,  ro(sig.valueType));
            signalsViewTable_->setItem(row, 4,  ro(QString::number(sig.factor, 'g', 6)));
            signalsViewTable_->setItem(row, 5,  ro(QString::number(sig.offset, 'g', 6)));
            signalsViewTable_->setItem(row, 6,  ro(QString::number(sig.minimum, 'g', 6)));
            signalsViewTable_->setItem(row, 7,  ro(QString::number(sig.maximum, 'g', 6)));
            signalsViewTable_->setItem(row, 8,  ro(sig.unit));
            signalsViewTable_->setItem(row, 9,  ro(sig.valueTableName));
            {
                auto* ci = new QTableWidgetItem(sig.comment);
                ci->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                signalsViewTable_->setItem(row, 10, ci);
            }
            // Col 11 – plain text item; the delegate creates the combo on edit.
            signalsViewTable_->setItem(row, 11, new QTableWidgetItem(msg.name));
            {
                auto* ai = new QTableWidgetItem("Edit...");
                ai->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                ai->setForeground(QBrush(QColor("#0057b8")));
                signalsViewTable_->setItem(row, 12, ai);
            }

            ++row;
        }
    }
    signalsViewTable_->blockSignals(false);
    signalsViewTable_->setSortingEnabled(wasSorting);
    signalsViewTable_->setUpdatesEnabled(true);
}

void MainWindow::setCurrentMessageIndex(int index)
{
    if (index < 0 || index >= database_.messages.size()) {
        currentMessageIndex_ = -1;
    } else {
        currentMessageIndex_ = index;
    }

    refreshSignalTable();
    refreshMultiplexorSignalDropdown();
    refreshBitLayoutForMultiplexorSelection();
}

int MainWindow::currentSelectedSignalIndex() const
{
    const QModelIndexList indexes = signalTable_->selectionModel() ? signalTable_->selectionModel()->selectedRows() : QModelIndexList{};
    if (indexes.isEmpty()) {
        return -1;
    }

    const int row = indexes.first().row();
    QTableWidgetItem* item = signalTable_->item(row, 0);
    if (!item) {
        return -1;
    }
    return item->data(Qt::UserRole).toInt();
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

void MainWindow::onAddNode()
{
    bool ok = false;
    const QString nodeName = QInputDialog::getText(this, "Add Node", "Node name:", QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || nodeName.isEmpty()) {
        return;
    }

    for (const DbcNode& existing : database_.nodes) {
        if (existing.name == nodeName) {
            QMessageBox::warning(this, "Invalid Node", QString("Node name already exists: %1").arg(nodeName));
            return;
        }
    }

    if (!isValidSymbolName(nodeName)) {
        QMessageBox::warning(this, "Invalid Node Name",
            QString("'%1' is not a valid node name.\n\nNames must start with a letter or underscore\nand contain only A-Z, a-z, 0-9, or underscore.").arg(nodeName));
        return;
    }

    database_.nodes.append(DbcNode{nodeName, QString{}, QString{}});
    isDirty_ = true;
    refreshAll();
}

static const char* kMimeSignal  = "application/x-dbc-signal";
static const char* kMimeMessage = "application/x-dbc-message";

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    // Guard: widgets may not all be initialized yet during construction.
    if (!nodesViewTable_ || !signalTable_ || !messagesViewTable_ || !signalsViewTable_) {
        return QMainWindow::eventFilter(obj, event);
    }

    // ── clear node-view selection when clicking empty area ────────────────
    if (obj == nodesViewTable_->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (!nodesViewTable_->indexAt(me->pos()).isValid()) {
            nodesViewTable_->clearSelection();
        }
    }

    // ── clear signal table selection when clicking empty area ─────────────
    if (obj == signalTable_->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (!signalTable_->indexAt(me->pos()).isValid()) {
            signalTable_->clearSelection();
            signalTable_->setCurrentItem(nullptr);
        }
    }

    // ── drag initiation: signals view ─────────────────────────────────────
    if (obj == signalsViewTable_->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                dragStartPos_ = me->pos();
                dragSourceTable_ = signalsViewTable_;
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            if ((me->buttons() & Qt::LeftButton) && dragSourceTable_ == signalsViewTable_ &&
                (me->pos() - dragStartPos_).manhattanLength() >= QApplication::startDragDistance()) {
                const QModelIndex idx = signalsViewTable_->indexAt(dragStartPos_);
                if (idx.isValid()) {
                    QTableWidgetItem* nameItem = signalsViewTable_->item(idx.row(), 0);
                    QTableWidgetItem* msgItem  = signalsViewTable_->item(idx.row(), 11);
                    if (nameItem && msgItem) {
                        const QString payload = nameItem->text() + QChar('\x1E') + msgItem->text();
                        auto* mimeData = new QMimeData;
                        mimeData->setData(kMimeSignal, payload.toUtf8());
                        auto* drag = new QDrag(signalsViewTable_);
                        drag->setMimeData(mimeData);
                        drag->exec(Qt::MoveAction);
                        dragSourceTable_ = nullptr;
                        return true;
                    }
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            dragSourceTable_ = nullptr;
        }
    }

    // ── drag initiation: messages view ────────────────────────────────────
    if (obj == messagesViewTable_->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                dragStartPos_ = me->pos();
                dragSourceTable_ = messagesViewTable_;
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            if ((me->buttons() & Qt::LeftButton) && dragSourceTable_ == messagesViewTable_ &&
                (me->pos() - dragStartPos_).manhattanLength() >= QApplication::startDragDistance()) {
                const QModelIndex idx = messagesViewTable_->indexAt(dragStartPos_);
                if (idx.isValid()) {
                    QTableWidgetItem* nameItem = messagesViewTable_->item(idx.row(), 0);
                    if (nameItem) {
                        auto* mimeData = new QMimeData;
                        mimeData->setData(kMimeMessage, nameItem->text().toUtf8());
                        auto* drag = new QDrag(messagesViewTable_);
                        drag->setMimeData(mimeData);
                        drag->exec(Qt::CopyAction);
                        dragSourceTable_ = nullptr;
                        return true;
                    }
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            dragSourceTable_ = nullptr;
        }

        // ── drop: signal → message ────────────────────────────────────────
        if (event->type() == QEvent::DragEnter) {
            auto* de = static_cast<QDragEnterEvent*>(event);
            if (de->mimeData()->hasFormat(kMimeSignal)) {
                de->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::DragMove) {
            auto* dm = static_cast<QDragMoveEvent*>(event);
            if (dm->mimeData()->hasFormat(kMimeSignal)) {
                const QModelIndex idx = messagesViewTable_->indexAt(dm->position().toPoint());
                if (idx.isValid()) { dm->acceptProposedAction(); }
                else               { dm->ignore(); }
                return true;
            }
        }
        if (event->type() == QEvent::Drop) {
            auto* de = static_cast<QDropEvent*>(event);
            if (de->mimeData()->hasFormat(kMimeSignal)) {
                const QModelIndex idx = messagesViewTable_->indexAt(de->position().toPoint());
                if (!idx.isValid()) { de->ignore(); return true; }

                QTableWidgetItem* targetMsgItem = messagesViewTable_->item(idx.row(), 0);
                if (!targetMsgItem) { de->ignore(); return true; }
                const QString targetMsgName = targetMsgItem->text();

                const QString payload = QString::fromUtf8(de->mimeData()->data(kMimeSignal));
                const int sep = payload.indexOf(QChar('\x1E'));
                if (sep < 0) { de->ignore(); return true; }
                const QString sigName     = payload.left(sep);
                const QString sourceMsgName = payload.mid(sep + 1);

                if (sourceMsgName == targetMsgName) { de->ignore(); return true; }

                // Find source message and signal.
                DbcSignal movedSig;
                bool found = false;
                for (DbcMessage& srcMsg : database_.messages) {
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

                // Check for duplicate name in the target message before appending.
                bool nameConflict = false;
                for (DbcMessage& dstMsg : database_.messages) {
                    if (dstMsg.name != targetMsgName) { continue; }
                    for (const DbcSignal& s : dstMsg.signalList) {
                        if (s.name == sigName) {
                            nameConflict = true;
                            break;
                        }
                    }
                    if (!nameConflict) {
                        dstMsg.signalList.append(movedSig);
                    }
                    break;
                }

                if (nameConflict) {
                    // Re-add the signal to the source message (it was already removed).
                    for (DbcMessage& srcMsg : database_.messages) {
                        if (srcMsg.name == sourceMsgName) {
                            srcMsg.signalList.append(movedSig);
                            break;
                        }
                    }
                    QMessageBox::warning(nullptr, "Duplicate Signal Name",
                        QString("Error, signal: %1 already exists in Message %2.")
                            .arg(sigName, targetMsgName));
                    de->ignore();
                    return true;
                }

                isDirty_ = true;
                refreshAll();
                statusBar()->showMessage(
                    QString("Signal '%1' moved to message '%2'").arg(sigName, targetMsgName), 3000);
                de->acceptProposedAction();
                return true;
            }
        }
    }

    // ── drop: message → node ──────────────────────────────────────────────
    if (obj == nodesViewTable_->viewport()) {
        if (event->type() == QEvent::DragEnter) {
            auto* de = static_cast<QDragEnterEvent*>(event);
            if (de->mimeData()->hasFormat(kMimeMessage)) {
                de->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::DragMove) {
            auto* dm = static_cast<QDragMoveEvent*>(event);
            if (dm->mimeData()->hasFormat(kMimeMessage)) {
                const QModelIndex idx = nodesViewTable_->indexAt(dm->position().toPoint());
                if (idx.isValid()) { dm->acceptProposedAction(); }
                else               { dm->ignore(); }
                return true;
            }
        }
        if (event->type() == QEvent::Drop) {
            auto* de = static_cast<QDropEvent*>(event);
            if (de->mimeData()->hasFormat(kMimeMessage)) {
                const QModelIndex idx = nodesViewTable_->indexAt(de->position().toPoint());
                if (!idx.isValid()) { de->ignore(); return true; }

                QTableWidgetItem* nodeItem = nodesViewTable_->item(idx.row(), 0);
                if (!nodeItem) { de->ignore(); return true; }
                const QString nodeName   = nodeItem->text();
                const QString msgName    = QString::fromUtf8(de->mimeData()->data(kMimeMessage));

                for (DbcMessage& msg : database_.messages) {
                    if (msg.name == msgName) {
                        msg.transmitter = nodeName;
                        break;
                    }
                }

                isDirty_ = true;
                refreshAll();
                statusBar()->showMessage(
                    QString("Message '%1' transmitter set to '%2'").arg(msgName, nodeName), 3000);
                de->acceptProposedAction();
                return true;
            }
        }
    }

    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::onDeleteNode()
{
    const QModelIndexList selected = nodesViewTable_->selectionModel()
        ? nodesViewTable_->selectionModel()->selectedRows() : QModelIndexList{};
    if (selected.isEmpty()) {
        return;
    }

    // The table may be sorted; resolve the node name from the Name column
    const QString nodeName = nodesViewTable_->item(selected.first().row(), 0)
        ? nodesViewTable_->item(selected.first().row(), 0)->text() : QString{};
    if (nodeName.isEmpty()) {
        return;
    }

    const int confirm = QMessageBox::question(
        this, "Delete Node",
        QString("Delete node '%1'?").arg(nodeName),
        QMessageBox::Yes | QMessageBox::No);
    if (confirm != QMessageBox::Yes) {
        return;
    }

    for (int i = 0; i < database_.nodes.size(); ++i) {
        if (database_.nodes.at(i).name == nodeName) {
            database_.nodes.removeAt(i);
            break;
        }
    }

    for (DbcMessage& message : database_.messages) {
        if (message.transmitter == nodeName) {
            message.transmitter = "Vector__XXX";
        }
    }

    isDirty_ = true;
    refreshAll();
}

void MainWindow::onDeleteNodeWithAttributes()
{
    // Same as Delete Node for now
    onDeleteNode();
}

void MainWindow::onDeleteMessage()
{
    const QModelIndexList selected = messagesViewTable_->selectionModel()
        ? messagesViewTable_->selectionModel()->selectedRows() : QModelIndexList{};
    if (selected.isEmpty()) {
        return;
    }

    const QString msgName = messagesViewTable_->item(selected.first().row(), 0)
        ? messagesViewTable_->item(selected.first().row(), 0)->text() : QString{};
    if (msgName.isEmpty()) {
        return;
    }

    const int confirm = QMessageBox::question(
        this, "Delete Message",
        QString("Delete message '%1'?").arg(msgName),
        QMessageBox::Yes | QMessageBox::No);
    if (confirm != QMessageBox::Yes) {
        return;
    }

    for (int i = 0; i < database_.messages.size(); ++i) {
        if (database_.messages.at(i).name == msgName) {
            database_.messages.remove(i);
            if (currentMessageIndex_ >= database_.messages.size()) {
                currentMessageIndex_ = database_.messages.size() - 1;
            }
            break;
        }
    }
    isDirty_ = true;
    refreshAll();
}

void MainWindow::onDeleteMessageWithAttributes()
{
    // Same as Delete Message for now
    onDeleteMessage();
}

void MainWindow::onDeleteSignalFromView()
{
    const QModelIndexList selected = signalsViewTable_->selectionModel()
        ? signalsViewTable_->selectionModel()->selectedRows() : QModelIndexList{};
    if (selected.isEmpty()) {
        return;
    }

    const int row = selected.first().row();
    auto* nameItem = signalsViewTable_->item(row, 0);
    const QString sigName = nameItem ? nameItem->text() : QString{};
    const QString msgName = nameItem ? nameItem->data(Qt::UserRole).toString() : QString{};
    if (sigName.isEmpty() || msgName.isEmpty()) {
        return;
    }

    const int confirm = QMessageBox::question(
        this, "Delete Signal",
        QString("Delete signal '%1' from message '%2'?").arg(sigName, msgName),
        QMessageBox::Yes | QMessageBox::No);
    if (confirm != QMessageBox::Yes) {
        return;
    }

    for (DbcMessage& message : database_.messages) {
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
    isDirty_ = true;
    refreshAll();
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
    refreshNodesView();
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
    refreshMessagesView();
    statusBar()->showMessage("List: Messages", 1000);
}

void MainWindow::onViewSignals()
{
    currentViewMode_ = ViewMode::Signals;
    centerStack_->setCurrentIndex(3);
    if (bitLayoutDock_) { bitLayoutDock_->hide(); }
    refreshSignalsView();
    refreshBitLayoutForMultiplexorSelection();
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
    // Do not steal the Delete key when the Value Tables view is active.
    if (currentViewMode_ == ViewMode::ValueTables) { return; }
    const int signalIndex = currentSelectedSignalIndex();
    if (signalIndex >= 0 && currentMessageIndex_ >= 0 && currentMessageIndex_ < database_.messages.size()) {
        auto& message = database_.messages[currentMessageIndex_];
        if (signalIndex < message.signalList.size()) {
            message.signalList.remove(signalIndex);
            isDirty_ = true;
            refreshAll();
            return;
        }
    }

    const QModelIndexList selectedMessageRows = messageTable_->selectionModel() ? messageTable_->selectionModel()->selectedRows() : QModelIndexList{};
    if (!selectedMessageRows.isEmpty()) {
        const int selectedRow = selectedMessageRows.first().row();
        QTableWidgetItem* item = messageTable_->item(selectedRow, 0);
        if (!item) {
            return;
        }
        const int messageIndex = item->data(Qt::UserRole).toInt();
        if (messageIndex >= 0 && messageIndex < database_.messages.size()) {
            database_.messages.remove(messageIndex);
            if (database_.messages.isEmpty()) {
                currentMessageIndex_ = -1;
            } else if (currentMessageIndex_ >= database_.messages.size()) {
                currentMessageIndex_ = database_.messages.size() - 1;
            }
            isDirty_ = true;
            refreshAll();
            return;
        }
    }
}

void MainWindow::onMessageTableItemChanged(QTableWidgetItem* item)
{
    if (!item || item->column() != 0) { return; }

    QTableWidgetItem* nameItem = messageTable_->item(item->row(), 0);
    if (!nameItem) { return; }
    const int msgIndex = nameItem->data(Qt::UserRole).toInt();
    if (msgIndex < 0 || msgIndex >= database_.messages.size()) { return; }

    DbcMessage& msg = database_.messages[msgIndex];
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

    for (int mi = 0; mi < database_.messages.size(); ++mi) {
        if (mi == msgIndex) { continue; }
        if (database_.messages.at(mi).name == text) {
            const QSignalBlocker b(messageTable_);
            item->setText(msg.name);
            QMessageBox::warning(this, "Duplicate Message Name",
                QString("A message named '%1' already exists.").arg(text));
            return;
        }
    }

    msg.name = text;
    isDirty_ = true;
    // Update hierarchy tree label if it shows this message
    refreshHierarchy();
}

void MainWindow::onMessageSelectionChanged()
{
    const QModelIndexList selectedRows = messageTable_->selectionModel() ? messageTable_->selectionModel()->selectedRows() : QModelIndexList{};
    if (selectedRows.isEmpty()) {
        setCurrentMessageIndex(-1);
        return;
    }

    const int row = selectedRows.first().row();
    QTableWidgetItem* item = messageTable_->item(row, 0);
    if (!item) {
        setCurrentMessageIndex(-1);
        return;
    }

    setCurrentMessageIndex(item->data(Qt::UserRole).toInt());
}

void MainWindow::onTreeSelectionChanged()
{
    const QList<QTreeWidgetItem*> selectedItems = hierarchyTree_->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }

    QTreeWidgetItem* selectedItem = selectedItems.first();

    // ── "Attributes" child items (Qt::UserRole+1 != 0) ─────────────────────
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
            // Signal attributes — packed = (msgIdx << 16) | sigIdx
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
            refreshNodesView();
            return;
        }
        if (rootName == "Messages") {
            currentViewMode_ = ViewMode::Messages;
            centerStack_->setCurrentIndex(2);
            refreshMessagesView();
            return;
        }
        if (rootName == "Signals") {
            currentViewMode_ = ViewMode::Signals;
            centerStack_->setCurrentIndex(3);
            refreshSignalsView();
            return;
        }
    }

    const int marker = selectedItem->data(0, Qt::UserRole).toInt();

    // marker == 0: root items (no UserRole set), marker < 0: node list items (-1000)
    if (marker < 0) {
        // Node child — navigate to Nodes view.
        currentViewMode_ = ViewMode::Nodes;
        centerStack_->setCurrentIndex(1);
        if (bitLayoutDock_) { bitLayoutDock_->hide(); }
        refreshNodesView();
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
        refreshSignalsView();

        // Identify the signal name so we can find its row in signalsViewTable_.
        QString sigName;
        if (messageIndex >= 0 && messageIndex < database_.messages.size()) {
            const DbcMessage& msg = database_.messages.at(messageIndex);
            if (signalIndex >= 0 && signalIndex < msg.signalList.size()) {
                sigName = msg.signalList.at(signalIndex).name;
            }
        }
        if (!sigName.isEmpty()) {
            for (int row = 0; row < signalsViewTable_->rowCount(); ++row) {
                auto* item = signalsViewTable_->item(row, 0);
                if (item && item->text() == sigName) {
                    signalsViewTable_->selectRow(row);
                    signalsViewTable_->scrollToItem(item);
                    break;
                }
            }
        }
    } else {
        // Message node: encoded as messageIndex + 1
        const int messageIndex = marker - 1;

        // Navigate to the Messages view.
        currentViewMode_ = ViewMode::Messages;
        centerStack_->setCurrentIndex(2);
        if (bitLayoutDock_) { bitLayoutDock_->show(); }
        refreshMessagesView();

        setCurrentMessageIndex(messageIndex);
        for (int row = 0; row < messageTable_->rowCount(); ++row) {
            QTableWidgetItem* item = messageTable_->item(row, 0);
            if (item && item->data(Qt::UserRole).toInt() == messageIndex) {
                messageTable_->selectRow(row);
                break;
            }
        }
    }
}

// ── Value Tables tab ──────────────────────────────────────────────────────────

void MainWindow::onViewValueTables()
{
    currentViewMode_ = ViewMode::ValueTables;
    centerStack_->setCurrentIndex(4);
    if (bitLayoutDock_) { bitLayoutDock_->hide(); }
    refreshValueTablesView();
    statusBar()->showMessage("Value Tables", 1000);
}

void MainWindow::refreshValueTablesView()
{
    if (!valueTablesListWidget_ || !valueTablesEntriesTable_) { return; }

    // Sort tables alphabetically (case-insensitive)
    std::sort(database_.valueTables.begin(), database_.valueTables.end(),
              [](const DbcValueTable& a, const DbcValueTable& b) {
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });

    const QSignalBlocker listBlocker(valueTablesListWidget_);
    const QSignalBlocker entryBlocker(valueTablesEntriesTable_);

    const int prevRow = valueTablesListWidget_->currentRow();
    valueTablesListWidget_->clear();
    for (const DbcValueTable& vt : database_.valueTables) {
        valueTablesListWidget_->addItem(vt.name);
    }

    const int newRow = qBound(-1, prevRow, database_.valueTables.size() - 1);
    if (newRow >= 0) {
        valueTablesListWidget_->setCurrentRow(newRow);
        currentValueTableIndex_ = newRow;
        sortAndRefreshEntries(newRow);
    } else {
        currentValueTableIndex_ = -1;
        valueTablesEntriesTable_->setRowCount(0);
    }
}

void MainWindow::sortAndRefreshEntries(int tableIndex)
{
    if (!valueTablesEntriesTable_) { return; }
    if (tableIndex < 0 || tableIndex >= database_.valueTables.size()) { return; }

    auto& entries = database_.valueTables[tableIndex].entries;
    std::sort(entries.begin(), entries.end(), [](const DbcValueEntry& a, const DbcValueEntry& b) {
        return a.rawValue < b.rawValue;
    });

    const QSignalBlocker b(valueTablesEntriesTable_);
    valueTablesEntriesTable_->setRowCount(0);
    for (const DbcValueEntry& e : entries) {
        const int r = valueTablesEntriesTable_->rowCount();
        valueTablesEntriesTable_->insertRow(r);
        valueTablesEntriesTable_->setItem(r, 0, new QTableWidgetItem(QString::number(e.rawValue)));
        valueTablesEntriesTable_->setItem(r, 1, new QTableWidgetItem(e.label));
    }
}

void MainWindow::onAddValueTable()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "Add Value Table", "Table name:", QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) { return; }

    for (const DbcValueTable& vt : database_.valueTables) {
        if (vt.name == name) {
            QMessageBox::warning(this, "Duplicate Name",
                QString("A value table named \"%1\" already exists.").arg(name));
            return;
        }
    }

    database_.valueTables.append(DbcValueTable{name, {}});
    isDirty_ = true;
    // Sort now so refreshValueTablesView can find the right index to select
    std::sort(database_.valueTables.begin(), database_.valueTables.end(),
              [](const DbcValueTable& a, const DbcValueTable& b) {
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });
    const int newIdx = [&]() -> int {
        for (int i = 0; i < database_.valueTables.size(); ++i) {
            if (database_.valueTables[i].name == name) { return i; }
        }
        return database_.valueTables.size() - 1;
    }();
    refreshValueTablesView();
    valueTablesListWidget_->setCurrentRow(newIdx);
}

void MainWindow::onDeleteValueTable()
{
    if (currentValueTableIndex_ < 0 || currentValueTableIndex_ >= database_.valueTables.size()) { return; }

    const QString name = database_.valueTables[currentValueTableIndex_].name;
    const int confirm = QMessageBox::question(
        this, "Delete Value Table",
        QString("Delete value table \"%1\"?").arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (confirm != QMessageBox::Yes) { return; }

    database_.valueTables.removeAt(currentValueTableIndex_);
    isDirty_ = true;
    refreshValueTablesView();
}

void MainWindow::onValueTableSelected()
{
    if (!valueTablesListWidget_ || !valueTablesEntriesTable_) { return; }
    currentValueTableIndex_ = valueTablesListWidget_->currentRow();
    if (currentValueTableIndex_ < 0 || currentValueTableIndex_ >= database_.valueTables.size()) {
        const QSignalBlocker b(valueTablesEntriesTable_);
        valueTablesEntriesTable_->setRowCount(0);
        return;
    }
    sortAndRefreshEntries(currentValueTableIndex_);
}

void MainWindow::onAddValueTableEntry()
{
    if (currentValueTableIndex_ < 0 || currentValueTableIndex_ >= database_.valueTables.size()) { return; }

    const auto& existing = database_.valueTables[currentValueTableIndex_].entries;
    qint64 newRaw = 0;
    if (!existing.isEmpty()) {
        qint64 maxVal = existing.first().rawValue;
        for (const DbcValueEntry& e : existing) { maxVal = qMax(maxVal, e.rawValue); }
        newRaw = maxVal + 1;
    }
    const QString newLabel = QString("Description for the value '%1'").arg(newRaw);

    database_.valueTables[currentValueTableIndex_].entries.append(DbcValueEntry{newRaw, newLabel});
    isDirty_ = true;
    sortAndRefreshEntries(currentValueTableIndex_);
    // Scroll to and select the newly inserted row
    for (int r = 0; r < valueTablesEntriesTable_->rowCount(); ++r) {
        auto* it = valueTablesEntriesTable_->item(r, 0);
        if (it && it->text().toLongLong() == newRaw) {
            valueTablesEntriesTable_->scrollToItem(it);
            valueTablesEntriesTable_->setCurrentItem(it);
            valueTablesEntriesTable_->editItem(valueTablesEntriesTable_->item(r, 1));
            break;
        }
    }
}

void MainWindow::onDeleteValueTableEntry()
{
    if (currentValueTableIndex_ < 0 || currentValueTableIndex_ >= database_.valueTables.size()) { return; }
    const auto sel = valueTablesEntriesTable_->selectionModel()
        ? valueTablesEntriesTable_->selectionModel()->selectedRows() : QModelIndexList{};
    if (sel.isEmpty()) { return; }

    const int row = sel.first().row();
    auto& entries = database_.valueTables[currentValueTableIndex_].entries;
    if (row < 0 || row >= entries.size()) { return; }

    const qint64 rawVal = entries.at(row).rawValue;
    const QString label = entries.at(row).label;
    const int confirm = QMessageBox::question(
        this, "Confirm Delete Entry",
        QString("Delete entry %1 : %2?").arg(rawVal).arg(label),
        QMessageBox::Yes | QMessageBox::No);
    if (confirm != QMessageBox::Yes) { return; }

    const QSignalBlocker b(valueTablesEntriesTable_);
    valueTablesEntriesTable_->removeRow(row);
    entries.removeAt(row);
    isDirty_ = true;
}

void MainWindow::onValueTableEntryItemChanged(QTableWidgetItem* item)
{
    if (!item) { return; }
    if (currentValueTableIndex_ < 0 || currentValueTableIndex_ >= database_.valueTables.size()) { return; }

    auto& entries = database_.valueTables[currentValueTableIndex_].entries;
    const int row = item->row();
    if (row < 0 || row >= entries.size()) { return; }

    if (item->column() == 0) {
        bool ok = false;
        const QString text = item->text().trimmed();
        // Accept 0x/0X hex prefix as well as plain decimal
        const qint64 val = (text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
            ? text.mid(2).toLongLong(&ok, 16)
            : text.toLongLong(&ok);
        if (ok) {
            entries[row].rawValue = val;
            isDirty_ = true;
            // Rewrite the cell as plain decimal so it's stored cleanly
            {
                const QSignalBlocker b(valueTablesEntriesTable_);
                item->setText(QString::number(val));
            }
            // Re-sort entries by raw value after a change
            sortAndRefreshEntries(currentValueTableIndex_);
        } else {
            // Revert to stored value
            const QSignalBlocker b(valueTablesEntriesTable_);
            item->setText(QString::number(entries[row].rawValue));
        }
    } else if (item->column() == 1) {
        entries[row].label = item->text();
        isDirty_ = true;
    }
}

// ── Attributes view ──────────────────────────────────────────────────────────

int MainWindow::attrStackPageForValueTypeIndex(int idx)
{
    // Combo order: 0=Integer, 1=Float, 2=String, 3=Enumeration, 4=Hex
    if (idx == 2) { return 1; } // String
    if (idx == 3) { return 2; } // Enumeration
    return 0;                   // Integer / Float / Hex
}

void MainWindow::onViewAttributes()
{
    currentViewMode_ = ViewMode::Attributes;
    centerStack_->setCurrentIndex(5);
    if (bitLayoutDock_) { bitLayoutDock_->hide(); }
    refreshAttributesView();
    statusBar()->showMessage("Attributes", 1000);
}

void MainWindow::refreshAttributesView()
{
    if (!attrListWidget_) { return; }
    const QSignalBlocker lb(attrListWidget_);
    const int prevRow = attrListWidget_->currentRow();
    attrListWidget_->clear();
    for (const DbcAttributeDef& attr : database_.attributes) {
        attrListWidget_->addItem(attr.name);
    }
    const int newRow = qBound(-1, prevRow, database_.attributes.size() - 1);
    if (newRow >= 0) {
        attrListWidget_->setCurrentRow(newRow);
        currentAttrIndex_ = newRow;
        refreshAttrFormFromModel();
    } else {
        currentAttrIndex_ = -1;
        if (attrFormWidget_) { attrFormWidget_->setEnabled(false); }
    }
}

void MainWindow::refreshAttrFormFromModel()
{
    if (!attrFormWidget_ || currentAttrIndex_ < 0 || currentAttrIndex_ >= database_.attributes.size()) {
        if (attrFormWidget_) { attrFormWidget_->setEnabled(false); }
        return;
    }
    attrFormUpdating_ = true;
    attrFormWidget_->setEnabled(true);

    const DbcAttributeDef& attr = database_.attributes[currentAttrIndex_];

    attrNameEdit_->setText(attr.name);
    attrObjTypeCombo_->setCurrentIndex(static_cast<int>(attr.objectType));

    const int vtIdx = static_cast<int>(attr.valueType);
    attrValueTypeCombo_->setCurrentIndex(vtIdx);
    attrValueStack_->setCurrentIndex(attrStackPageForValueTypeIndex(vtIdx));
    {
        const bool isHex = (vtIdx == 4);
        attrDefaultNumEdit_->setPlaceholderText(isHex ? "e.g. 0x0"  : "e.g. 0");
        attrMinEdit_->setPlaceholderText       (isHex ? "e.g. 0x00" : "e.g. 0");
        attrMaxEdit_->setPlaceholderText       (isHex ? "e.g. 0xFF" : "e.g. 255");
    }

    // Numeric page (Int, Float, Hex)
    attrDefaultNumEdit_->setText(attr.defaultValue);
    attrMinEdit_->setText(attr.minimum);
    attrMaxEdit_->setText(attr.maximum);
    // Hide the validation warning whenever the form is refreshed from model.
    if (attrValidationLabel_) { attrValidationLabel_->hide(); }

    // String page
    attrDefaultStrEdit_->setText(attr.defaultValue);

    // Enum page
    {
        const QSignalBlocker eb(attrEnumTable_);
        attrEnumTable_->setRowCount(0);
        for (const QString& v : attr.enumValues) {
            const int r = attrEnumTable_->rowCount();
            attrEnumTable_->insertRow(r);
            attrEnumTable_->setItem(r, 0, new QTableWidgetItem(v));
        }
    }
    refreshAttrEnumCombo();

    attrFormUpdating_ = false;
}

void MainWindow::refreshAttrEnumCombo()
{
    if (!attrEnumDefaultCombo_ || currentAttrIndex_ < 0 ||
        currentAttrIndex_ >= database_.attributes.size()) { return; }
    const QSignalBlocker eb(attrEnumDefaultCombo_);
    const DbcAttributeDef& attr = database_.attributes[currentAttrIndex_];
    attrEnumDefaultCombo_->clear();
    attrEnumDefaultCombo_->addItems(attr.enumValues);
    // Restore selection to stored default value
    const int idx = attr.enumValues.indexOf(attr.defaultValue);
    attrEnumDefaultCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
}

void MainWindow::updateModelFromAttrForm()
{
    if (attrFormUpdating_ || currentAttrIndex_ < 0 || currentAttrIndex_ >= database_.attributes.size()) { return; }
    DbcAttributeDef& attr = database_.attributes[currentAttrIndex_];

    const QString newName = attrNameEdit_->text().trimmed();
    if (!newName.isEmpty() && newName != attr.name) {
        attr.name = newName;
        const QSignalBlocker lb(attrListWidget_);
        auto* item = attrListWidget_->item(currentAttrIndex_);
        if (item) { item->setText(newName); }
    }

    attr.objectType = static_cast<DbcAttributeDef::ObjectType>(attrObjTypeCombo_->currentIndex());
    attr.valueType  = static_cast<DbcAttributeDef::ValueType>(attrValueTypeCombo_->currentIndex());

    switch (attr.valueType) {
        case DbcAttributeDef::ValueType::Integer:
        case DbcAttributeDef::ValueType::Float:
            attr.defaultValue = attrDefaultNumEdit_->text().trimmed();
            attr.minimum      = attrMinEdit_->text().trimmed();
            attr.maximum      = attrMaxEdit_->text().trimmed();
            break;
        case DbcAttributeDef::ValueType::Hex: {
            auto hexNorm = [](const QString& s) -> QString {
                return (!s.isEmpty() && !s.startsWith("0x", Qt::CaseInsensitive)) ? "0x" + s : s;
            };
            attr.defaultValue = hexNorm(attrDefaultNumEdit_->text().trimmed());
            attr.minimum      = hexNorm(attrMinEdit_->text().trimmed());
            attr.maximum      = hexNorm(attrMaxEdit_->text().trimmed());
            { const QSignalBlocker b(attrDefaultNumEdit_); attrDefaultNumEdit_->setText(attr.defaultValue); }
            { const QSignalBlocker b(attrMinEdit_);        attrMinEdit_->setText(attr.minimum); }
            { const QSignalBlocker b(attrMaxEdit_);        attrMaxEdit_->setText(attr.maximum); }
            break;
        }
        case DbcAttributeDef::ValueType::String:
            attr.defaultValue = attrDefaultStrEdit_->text();
            attr.minimum.clear();
            attr.maximum.clear();
            break;
        case DbcAttributeDef::ValueType::Enumeration:
            if (attrEnumDefaultCombo_->currentIndex() >= 0) {
                attr.defaultValue = attrEnumDefaultCombo_->currentText();
            }
            attr.minimum.clear();
            attr.maximum.clear();
            break;
    }

    // ── Validate default is within [min, max] for numeric types ─────────────
    if (attrValidationLabel_) {
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

    isDirty_ = true;
}

void MainWindow::onAttrFieldChanged()
{
    updateModelFromAttrForm();
}

void MainWindow::onAddAttribute()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "Add Attribute", "Attribute name:",
        QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) { return; }

    for (const DbcAttributeDef& a : database_.attributes) {
        if (a.name == name) {
            QMessageBox::warning(this, "Duplicate Name",
                QString("An attribute named \"%1\" already exists.").arg(name));
            return;
        }
    }

    DbcAttributeDef attr;
    attr.name = name;
    database_.attributes.append(attr);
    isDirty_ = true;
    refreshAttributesView();
    const int newIdx = database_.attributes.size() - 1;
    attrListWidget_->setCurrentRow(newIdx);
}

void MainWindow::onDeleteAttribute()
{
    if (currentAttrIndex_ < 0 || currentAttrIndex_ >= database_.attributes.size()) { return; }
    const QString name = database_.attributes[currentAttrIndex_].name;
    if (QMessageBox::question(this, "Delete Attribute",
            QString("Delete attribute definition \"%1\"?").arg(name),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) { return; }

    database_.attributes.removeAt(currentAttrIndex_);
    isDirty_ = true;
    refreshAttributesView();
}

void MainWindow::onAttributeSelected()
{
    if (!attrListWidget_) { return; }
    currentAttrIndex_ = attrListWidget_->currentRow();
    refreshAttrFormFromModel();
}

void MainWindow::onAddEnumValue()
{
    if (attrFormUpdating_ || currentAttrIndex_ < 0 || currentAttrIndex_ >= database_.attributes.size()) { return; }
    DbcAttributeDef& attr = database_.attributes[currentAttrIndex_];
    attr.enumValues.append(QString("Value%1").arg(attr.enumValues.size()));
    isDirty_ = true;
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

void MainWindow::onDeleteEnumValue()
{
    if (attrFormUpdating_ || currentAttrIndex_ < 0 || currentAttrIndex_ >= database_.attributes.size()) { return; }
    const auto sel = attrEnumTable_->selectionModel()
        ? attrEnumTable_->selectionModel()->selectedRows() : QModelIndexList{};
    if (sel.isEmpty()) { return; }
    const int row = sel.first().row();
    DbcAttributeDef& attr = database_.attributes[currentAttrIndex_];
    if (row < 0 || row >= attr.enumValues.size()) { return; }
    {
        const QSignalBlocker eb(attrEnumTable_);
        attrEnumTable_->removeRow(row);
        attr.enumValues.removeAt(row);
    }
    isDirty_ = true;
    refreshAttrEnumCombo();
}

void MainWindow::onAttrEnumItemChanged(QTableWidgetItem* item)
{
    if (!item || attrFormUpdating_) { return; }
    if (currentAttrIndex_ < 0 || currentAttrIndex_ >= database_.attributes.size()) { return; }
    DbcAttributeDef& attr = database_.attributes[currentAttrIndex_];
    const int row = item->row();
    if (row < 0 || row >= attr.enumValues.size()) { return; }
    attr.enumValues[row] = item->text();
    isDirty_ = true;
    refreshAttrEnumCombo();
}
