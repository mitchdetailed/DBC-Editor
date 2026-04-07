#pragma once

#include "bitlayoutwidget.h"
#include "dbcmodel.h"

#include <QMainWindow>
#include <QPoint>

class QDockWidget;
class QAction;
class QMenu;
class QComboBox;
class QListWidget;
class QTableWidget;
class QTableWidgetItem;
class QScrollArea;
class QLineEdit;
class QStackedWidget;
class QTreeWidget;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT

    enum class ViewMode { Overview, Nodes, Messages, Signals, ValueTables, Attributes };

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onNewDatabase();
    void onOpenDatabase();
    void onSaveDatabase();
    void onSaveDatabaseAs();
    void onCloseDatabase();
    void onImportAttributeDefinitions();

    void onAddNode();
    void onDeleteNode();
    void onDeleteNodeWithAttributes();
    void onAddMessage();
    void onDeleteMessage();
    void onDeleteMessageWithAttributes();
    void onAddSignal();
    void onDeleteSignalFromView();
    void onEditSelected();
    void onEditCommonAttributes();
    void onCopySelection();
    void onPasteSelection();
    void onOpenSettings();
    void onViewOverview();
    void onViewNodes();
    void onViewMessages();
    void onViewSignals();
    void onViewValueTables();
    void onViewAttributes();
    void onOpenRecentFile();
    void onDeleteSelection();

    void onMessageSelectionChanged();
    void onMessageTableItemChanged(QTableWidgetItem* item);
    void onTreeSelectionChanged();
    void onSignalTableItemChanged(QTableWidgetItem* item);
    void onAddValueTable();
    void onDeleteValueTable();
    void onValueTableSelected();
    void onAddValueTableEntry();
    void onDeleteValueTableEntry();
    void onValueTableEntryItemChanged(QTableWidgetItem* item);

    void onAddAttribute();
    void onDeleteAttribute();
    void onAttributeSelected();
    void onAddEnumValue();
    void onDeleteEnumValue();
    void onAttrEnumItemChanged(QTableWidgetItem* item);
    void onAttrFieldChanged();

private:
    void createActions();
    void createMenus();
    void createToolbar();
    void createLayout();

    void refreshAll();
    void refreshHierarchy();
    void refreshMessageTable();
    void refreshSignalTable();
    void refreshMultiplexorSignalDropdown();
    void refreshBitLayoutForMultiplexorSelection();
    void refreshNodesView();
    void refreshMessagesView();
    void refreshSignalsView();

    void refreshValueTablesView();
    void sortAndRefreshEntries(int tableIndex);
    void refreshAttributesView();
    void refreshAttrFormFromModel();
    void updateModelFromAttrForm();
    void refreshAttrEnumCombo();
    static int attrStackPageForValueTypeIndex(int vtComboIndex);
    void setCurrentMessageIndex(int index);
    int currentSelectedSignalIndex() const;

    bool ensureReadyToDiscard();
    bool loadDatabaseFromPath(const QString& path);
    bool saveToPath(const QString& path);
    void updateRecentFilesMenu();
    void addRecentFile(const QString& path);

private:
    DbcDatabase database_;
    QString currentFilePath_;
    bool isDirty_ = false;

    int currentMessageIndex_ = -1;
    ViewMode currentViewMode_ = ViewMode::Overview;

    QStackedWidget* centerStack_ = nullptr;
    QTreeWidget* hierarchyTree_ = nullptr;
    QTableWidget* messageTable_ = nullptr;
    QTableWidget* signalTable_ = nullptr;
    QTableWidget* nodesViewTable_ = nullptr;
    QTableWidget* messagesViewTable_ = nullptr;
    QTableWidget* signalsViewTable_ = nullptr;
    QListWidget*  valueTablesListWidget_   = nullptr;
    QTableWidget* valueTablesEntriesTable_ = nullptr;
    int           currentValueTableIndex_  = -1;

    // Attributes view
    QListWidget*    attrListWidget_        = nullptr;
    QWidget*        attrFormWidget_        = nullptr;
    QLineEdit*      attrNameEdit_          = nullptr;
    QComboBox*      attrObjTypeCombo_      = nullptr;
    QComboBox*      attrValueTypeCombo_    = nullptr;
    QStackedWidget* attrValueStack_        = nullptr;
    QLineEdit*      attrDefaultNumEdit_    = nullptr;
    QLineEdit*      attrMinEdit_           = nullptr;
    QLineEdit*      attrMaxEdit_           = nullptr;
    QLineEdit*      attrDefaultStrEdit_    = nullptr;
    QComboBox*      attrEnumDefaultCombo_  = nullptr;
    QTableWidget*   attrEnumTable_         = nullptr;
    QLabel*         attrValidationLabel_   = nullptr;
    int             currentAttrIndex_      = -1;
    bool            attrFormUpdating_      = false;

    // ── drag state (for custom drag initiation in eventFilter) ───────────────
    QPoint        dragStartPos_;
    QTableWidget* dragSourceTable_ = nullptr;

    BitLayoutWidget* bitLayoutWidget_ = nullptr;
    QDockWidget*     bitLayoutDock_   = nullptr;
    QComboBox* multiplexorSignalCombo_ = nullptr;

    QAction* newAction_ = nullptr;
    QAction* openAction_ = nullptr;
    QMenu* openRecentMenu_ = nullptr;
    QAction* importAttributeDefinitionsAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* closeDatabaseAction_ = nullptr;
    QAction* exitAction_ = nullptr;

    QAction* addNodeAction_ = nullptr;
    QAction* deleteNodeAction_ = nullptr;
    QAction* deleteNodeWithAttributesAction_ = nullptr;
    QAction* addMessageAction_ = nullptr;
    QAction* deleteMessageAction_ = nullptr;
    QAction* deleteMessageWithAttributesAction_ = nullptr;
    QAction* addSignalAction_ = nullptr;
    QAction* deleteSignalFromViewAction_ = nullptr;
    QAction* editNewAction_ = nullptr;
    QAction* editAction_ = nullptr;
    QAction* editCommonAttributesAction_ = nullptr;
    QAction* copyAction_ = nullptr;
    QAction* pasteAction_ = nullptr;
    QAction* settingsAction_ = nullptr;
    QAction* viewOverviewAction_ = nullptr;
    QAction* viewNodesAction_ = nullptr;
    QAction* viewMessagesAction_ = nullptr;
    QAction* viewSignalsAction_ = nullptr;
    QAction* viewValueTablesAction_ = nullptr;
    QAction* viewAttributesAction_  = nullptr;
    QAction* deleteAction_ = nullptr;
};
