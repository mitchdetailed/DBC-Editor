#pragma once

#include "dbcmodel.h"

#include <QPoint>
#include <QWidget>

class QTableWidget;

class SignalsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SignalsWidget(DbcDatabase* db, QWidget* parent = nullptr);

    void refresh();
    void deleteSelected();
    void selectSignal(const QString& sigName);

signals:
    void dataModified();
    void requestAddSignal(int messageIndex);

public slots:
    void onDeleteSignalFromView();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    DbcDatabase*  db_;
    QTableWidget* table_ = nullptr;
    QPoint        dragStartPos_;
    bool          dragging_ = false;
};
