#pragma once

#include "bitlayoutwidget.h"
#include "dbcmodel.h"

#include <QWidget>

class QComboBox;
class QTableWidget;
class QTableWidgetItem;

class OverviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit OverviewWidget(DbcDatabase* db, QWidget* parent = nullptr);

    void refresh();
    void refreshBitLayout();
    void deleteSelected();

    // Called by MainWindow when the hierarchy tree selects a message.
    void setCurrentMessage(int index);
    int  currentMessageIndex() const;
    int  currentSelectedSignalIndex() const;

    // Returns the widget to embed in the bit-layout dock (created by MainWindow).
    QWidget* dockContentsWidget() const;

signals:
    void dataModified();
    void messageSelected(int index);
    void requestAddMessage();
    void requestAddSignal();

private slots:
    void onMessageSelectionChanged();
    void onMessageTableItemChanged(QTableWidgetItem* item);
    void onSignalTableItemChanged(QTableWidgetItem* item);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void refreshMessageTable();
    void refreshSignalTable();
    void refreshMultiplexorSignalDropdown();

    DbcDatabase*     db_;
    int              currentMessageIndex_ = -1;

    QTableWidget*    messageTable_            = nullptr;
    QTableWidget*    signalTable_             = nullptr;
    QComboBox*       multiplexorSignalCombo_  = nullptr;
    BitLayoutWidget* bitLayoutWidget_         = nullptr;
    QWidget*         dockContentsWidget_      = nullptr;
};
