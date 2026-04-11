#pragma once

#include "dbcmodel.h"

#include <QListWidget>
#include <QTableWidget>
#include <QWidget>

class ValueTablesWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ValueTablesWidget(DbcDatabase* db, QWidget* parent = nullptr);

    void refresh();
    void deleteSelected();

signals:
    void dataModified();

private slots:
    void onAddValueTable();
    void onDeleteValueTable();
    void onValueTableSelected();
    void onAddValueTableEntry();
    void onDeleteValueTableEntry();
    void onValueTableEntryItemChanged(QTableWidgetItem* item);

private:
    void refreshValueTablesView();
    void sortAndRefreshEntries(int tableIndex);

    DbcDatabase*  db_;
    QListWidget*  listWidget_    = nullptr;
    QTableWidget* entriesTable_  = nullptr;
    int           currentIndex_  = -1;
};
