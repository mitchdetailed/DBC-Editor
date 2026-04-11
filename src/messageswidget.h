#pragma once

#include "dbcmodel.h"

#include <QPoint>
#include <QTableWidget>
#include <QWidget>

class MessagesWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MessagesWidget(DbcDatabase* db, QWidget* parent = nullptr);

    void refresh();
    void deleteSelected();

signals:
    void dataModified();
    void requestAddMessage();

public slots:
    void onDeleteMessage();
    void onDeleteMessageWithAttributes();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    DbcDatabase*  db_;
    QTableWidget* table_ = nullptr;
    QPoint        dragStartPos_;
    bool          dragging_ = false;
};
