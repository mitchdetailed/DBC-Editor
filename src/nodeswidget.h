#pragma once

#include "dbcmodel.h"

#include <QPoint>
#include <QTableWidget>
#include <QWidget>

class NodesWidget : public QWidget
{
    Q_OBJECT
public:
    explicit NodesWidget(DbcDatabase* db, QWidget* parent = nullptr);

    void refresh();
    void deleteSelected();

signals:
    void dataModified();

public slots:
    void onAddNode();
    void onDeleteNode();
    void onDeleteNodeWithAttributes();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    DbcDatabase*  db_;
    QTableWidget* table_ = nullptr;
};
