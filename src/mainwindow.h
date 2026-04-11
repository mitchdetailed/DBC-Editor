#pragma once

#include "attributeswidget.h"
#include "dbcmodel.h"
#include "messageswidget.h"
#include "nodeswidget.h"
#include "overviewwidget.h"
#include "signalswidget.h"
#include "valuetableswidget.h"

#include <QMainWindow>

class QDockWidget;
class QAction;
class QMenu;
class QStackedWidget;
class QTreeWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

    enum class ViewMode { Overview, Nodes, Messages, Signals, ValueTables, Attributes };

public:
    explicit MainWindow(QWidget* parent = nullptr);

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
    void onAddSignalForMessage(int messageIndex);
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

    void onTreeSelectionChanged();

private:
    void createActions();
    void createMenus();
    void createToolbar();
    void createLayout();

    void refreshAll();
    void refreshHierarchy();

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

    QStackedWidget*    centerStack_        = nullptr;
    QTreeWidget*       hierarchyTree_      = nullptr;
    OverviewWidget*    overviewWidget_     = nullptr;
    NodesWidget*       nodesWidget_        = nullptr;
    MessagesWidget*    messagesWidget_     = nullptr;
    SignalsWidget*     signalsWidget_      = nullptr;
    ValueTablesWidget* valueTablesWidget_  = nullptr;
    AttributesWidget*  attributesWidget_   = nullptr;

    QDockWidget* bitLayoutDock_ = nullptr;

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
