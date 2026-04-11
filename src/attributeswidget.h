#pragma once

#include "dbcmodel.h"

#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QTableWidget>
#include <QWidget>

class AttributesWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AttributesWidget(DbcDatabase* db, QWidget* parent = nullptr);

    void refresh();
    void deleteSelected();

signals:
    void dataModified();

private slots:
    void onAddAttribute();
    void onDeleteAttribute();
    void onAttrDefTableItemChanged(QTableWidgetItem* item);
    void onAddEnumValue();
    void onDeleteEnumValue();
    void onAttrEnumItemChanged(QTableWidgetItem* item);

private:
    void refreshAttrTableRow(int row);
    void refreshAttrEnumPanel();
    void refreshAttrEnumCombo();
    void validateAttrNumericFields(int row);

    DbcDatabase*    db_;

    QTableWidget*   attrDefTable_         = nullptr;
    QWidget*        attrEnumPanel_        = nullptr;
    QComboBox*      attrEnumDefaultCombo_ = nullptr;
    QTableWidget*   attrEnumTable_        = nullptr;
    QLabel*         attrValidationLabel_  = nullptr;

    int  currentAttrIndex_ = -1;
    bool attrFormUpdating_ = false;
};
