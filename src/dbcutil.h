#pragma once

#include "dbcmodel.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

// ── MIME type strings for DBC drag-and-drop ─────────────────────────────────
inline const char* kMimeSignal  = "application/x-dbc-signal";
inline const char* kMimeMessage = "application/x-dbc-message";

// ── QTableWidgetItem that sorts numerically ──────────────────────────────────
class NumericTableWidgetItem : public QTableWidgetItem
{
public:
    explicit NumericTableWidgetItem(double value)
        : QTableWidgetItem(QString::number(value, 'g', 12)), numericValue_(value) {}
    explicit NumericTableWidgetItem(int value)
        : QTableWidgetItem(QString::number(value)), numericValue_(static_cast<double>(value)) {}
    bool operator<(const QTableWidgetItem& other) const override
    {
        const auto* o = dynamic_cast<const NumericTableWidgetItem*>(&other);
        if (o) { return numericValue_ < o->numericValue_; }
        return QTableWidgetItem::operator<(other);
    }
private:
    double numericValue_;
};

// ── Symbol name validation ───────────────────────────────────────────────────
inline bool isValidSymbolName(const QString& name)
{
    static const QRegularExpression kRegex("^[A-Za-z_][A-Za-z0-9_]*$");
    return kRegex.match(name).hasMatch();
}

inline bool validateDatabaseRules(const DbcDatabase& database, QString& error)
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

// ── Multi-line comment dialog ────────────────────────────────────────────────
inline void openCommentEditor(QWidget* parent, const QString& currentText,
                              const std::function<void(const QString&)>& onAccepted)
{
    auto* dlg = new QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dlg->setWindowTitle("Edit Comment");
    dlg->resize(520, 300);
    auto* layout = new QVBoxLayout(dlg);
    auto* edit = new QPlainTextEdit(dlg);
    edit->setPlainText(currentText);
    layout->addWidget(edit, 1);
    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                       Qt::Horizontal, dlg);
    layout->addWidget(btnBox);
    QObject::connect(btnBox, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(btnBox, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    if (dlg->exec() == QDialog::Accepted) {
        onAccepted(edit->toPlainText());
    }
    delete dlg;
}
