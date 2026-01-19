#include "MainWindow.hpp"

#include <QCloseEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QShortcut>
#include <QTimer>
#include <QVulkanInstance>

#include "Core.hpp"
#include "SubWindows/MaterialAssignDialog.hpp"
#include "SubWindows/MaterialEditorDialog.hpp"
#include "SubWindows/PropertyWindow.hpp"
#include "SubWindows/SubWindowManager.hpp"
#include "ViewportManager.hpp"
#include "ui_MainWindow.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow), m_core(std::make_unique<Core>())
{
    ui->setupUi(this);

    resize(1280, 780);
    setWindowTitle("Imp3D");

    initSideMenu();

    // Top button menu
    connect(ui->menuBtnGeneral, &QPushButton::toggled, this, [=, this](bool checked) { ui->stackedWidget->setCurrentIndex(0); });
    connect(ui->menuBtnGeometry, &QPushButton::toggled, this, [=, this](bool checked) { ui->stackedWidget->setCurrentIndex(1); });
    connect(ui->menuBtnTopology, &QPushButton::toggled, this, [=, this](bool checked) { ui->stackedWidget->setCurrentIndex(2); });
    connect(ui->menuBtnMaps, &QPushButton::toggled, this, [=, this](bool checked) { ui->stackedWidget->setCurrentIndex(3); });

    // Side menu actions
    connect(ui->stackedWidget, &MenuStackedWidget::sideMenuButtonClicked, this, &MainWindow::sideMenuButtonClicked);

    // Selection buttons
    connect(ui->btnSelPoints, &QPushButton::clicked, this, [=, this](bool checked) {
        m_core->selectionMode(SelectionMode::VERTS);
        static_cast<MenuStackedWidget*>(ui->stackedWidget)->externalToolClicked();
        m_core->setActiveTool("SelectTool");
    });
    connect(ui->btnSelEdges, &QPushButton::clicked, this, [=, this](bool checked) {
        m_core->selectionMode(SelectionMode::EDGES);
        static_cast<MenuStackedWidget*>(ui->stackedWidget)->externalToolClicked();
        m_core->setActiveTool("SelectTool");
    });
    connect(ui->btnSelPolys, &QPushButton::clicked, this, [=, this](bool checked) {
        m_core->selectionMode(SelectionMode::POLYS);
        static_cast<MenuStackedWidget*>(ui->stackedWidget)->externalToolClicked();
        m_core->setActiveTool("SelectTool");
    });

    // ------------------------------------------------------------
    // Handler for window actions (MainMenu etc)
    // ------------------------------------------------------------

    for (QAction* action : findChildren<QAction*>())
    {
        connect(action, &QAction::triggered, this, &MainWindow::handleAction);
    }

    connect(ui->btnShowGrid, &QPushButton::toggled, this, [=, this](bool checked) {
        m_core->showSceneGrid(checked);
    });
    ui->btnShowGrid->setChecked(true);

    // ------------------------------------------------------------
    // Create shared Vulkan instance
    // ------------------------------------------------------------
    m_vkInstance = std::make_unique<QVulkanInstance>();
    m_vkInstance->setApiVersion(QVersionNumber(1, 3));

    enableVulkanValidationLayer();

    if (!m_vkInstance->create())
    {
        QMessageBox::critical(this,
                              tr("Vulkan Error"),
                              tr("Failed to create a Vulkan instance. "
                                 "This application requires a Vulkan-capable GPU and driver."));
        std::exit(EXIT_FAILURE);
    }

    // ------------------------------------------------------------
    // Central widget: ViewportManager (4 viewports)
    // ------------------------------------------------------------
    m_viewportManager = std::make_unique<ViewportManager>(ui->mainWidget, m_core.get(), m_vkInstance.get());

    // Ensure mainWidget has a layout
    if (!ui->mainWidget->layout())
    {
        auto* lay = new QVBoxLayout(ui->mainWidget);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
    }

    // Add to mainWidget
    ui->mainWidget->layout()->addWidget(m_viewportManager.get());

    // Panels (dialogs)
    {
        m_subWindowManager = std::make_unique<SubWindowManager>(this);
        m_subWindowManager->addSubWindow("ASSIGN_MAT_DIALOG", new MaterialAssignDialog(this));
        m_subWindowManager->addSubWindow("MAT_EDITOR_DIALOG", new MaterialEditorDialog(this));
        m_subWindowManager->addSubWindow("PROPERTIES_DIALOG", new PropertyWindow(this));

        connect(ui->btnNumPanel, &QPushButton::toggled, this, [=, this](bool checked) {
            if (checked)
                m_subWindowManager->showSubWindow("PROPERTIES_DIALOG");
            else
                m_subWindowManager->hideSubWindow("PROPERTIES_DIALOG");
        });

        connect(ui->btnMatEditor, &QPushButton::toggled, this, [=, this](bool checked) {
            if (checked)
                m_subWindowManager->showSubWindow("MAT_EDITOR_DIALOG");
            else
                m_subWindowManager->hideSubWindow("MAT_EDITOR_DIALOG");
        });

        connect(ui->btnMatEditorPlus, &QPushButton::toggled, this, [=, this](bool checked) {
            if (checked)
                m_subWindowManager->showSubWindow("ASSIGN_MAT_DIALOG");
            else
                m_subWindowManager->hideSubWindow("ASSIGN_MAT_DIALOG");
        });

        connect(m_subWindowManager.get(), &SubWindowManager::onSubWindowClosed, this, &MainWindow::onSubWindowClosed);
    }

    // Main menu actions (Cross-platform)
    {
        ui->actionNew->setShortcuts(QKeySequence::New);
        ui->actionOpen->setShortcuts(QKeySequence::Open);
        ui->actionSave->setShortcuts(QKeySequence::Save);
        ui->actionSaveAs->setShortcuts(QKeySequence::SaveAs);
        ui->actionExit->setShortcuts(QKeySequence::Quit);
        ui->actionUndo->setShortcuts(QKeySequence::Undo);
        ui->actionRedo->setShortcuts(QKeySequence::Redo);
        ui->actionSelectAll->setShortcuts(QKeySequence::SelectAll);
        ui->actionSelectNone->setShortcuts({
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), // Win/Linux
            QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_A)  // macOS
        });

        // Top horizontal menu shortcuts
        ui->menuBtnGeneral->setShortcut(Qt::Key_F1);
        ui->menuBtnGeometry->setShortcut(Qt::Key_F2);
        ui->menuBtnTopology->setShortcut(Qt::Key_F3);
        ui->menuBtnMaps->setShortcut(Qt::Key_F4);

        // Selection Mode shortcuts
        ui->actionSelectPoints->setShortcut(Qt::Key_1);
        ui->actionSelectEdges->setShortcut(Qt::Key_2);
        ui->actionSelectPolys->setShortcut(Qt::Key_3);
        ui->btnSelMaterial->setShortcut(Qt::Key_4);

        // Hide for now the Material selection
        ui->btnSelMaterial->setVisible(false);
    }

    m_uiTimer = new QTimer(this);
    m_uiTimer->setInterval(16); // ~60 fps
    connect(m_uiTimer, &QTimer::timeout, this, &MainWindow::onUiTick);
    m_uiTimer->start();
}

MainWindow::~MainWindow() noexcept
{
    // ------------------------------------------------------------
    // Vulkan teardown order:
    // 1) Destroy per-viewport swapchains (while surfaces/instance still exist)
    // 2) Destroy VulkanBackend VkDevice
    // 3) Destroy QVulkanInstance (VkInstance)
    // ------------------------------------------------------------

    if (m_viewportManager)
        m_viewportManager->shutdownVulkan();

    m_viewportManager.reset();
    m_core.reset();

    // Safe to destroy VkInstance
    m_vkInstance.reset();

    delete ui;
}

void MainWindow::onUiTick()
{
    if (!m_core)
        return;

    // Core update (scene/tool logic, counters, etc)
    m_core->idle();

    // Update dialogs
    if (m_subWindowManager)
        m_subWindowManager->idleEvent(m_core.get());

    // Render
    if (m_viewportManager)
        m_viewportManager->idleEvent(m_core.get());
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    // Initial selected buttons
    ui->menuBtnGeneral->setChecked(true);
    ui->btnSelPoints->click();
}

void MainWindow::sideMenuButtonClicked(ButtonType type, const QString& id, bool checked, int delta)
{
    try
    {
        if (type == ButtonType::Command)
        {
            m_core->runCommand(id.toStdString());
            m_core->setActiveTool("SelectTool");
        }
        else if (type == ButtonType::Action)
        {
            m_core->runAction(id.toStdString(), delta);
        }
        else if (checked /*type == ButtonType::Tool*/)
        {
            m_core->setActiveTool(id.toStdString());
        }
        else
        {
            m_core->setActiveTool("SelectTool");
        }
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(this, "Tool Error", QString("Failed to activate tool:\n%1").arg(e.what()));
        static_cast<MenuStackedWidget*>(ui->stackedWidget)->externalToolClicked();
        m_core->setActiveTool("SelectTool");
    }
}

void MainWindow::closeEvent(QCloseEvent* ev)
{
    // Let existing actionExit logic handle it
    ui->actionExit->trigger();

    // If trigger caused close() to succeed, the window will be closing anyway.
    // To prevent immediate close before gating finishes:
    ev->ignore();
}

void MainWindow::onSubWindowClosed(QString name, int result)
{
    if (name == "PROPERTIES_DIALOG")
    {
        ui->btnNumPanel->setChecked(false);
    }
    else if (name == "MAT_EDITOR_DIALOG")
    {
        ui->btnMatEditor->setChecked(false);
    }
    else if (name == "ASSIGN_MAT_DIALOG")
    {
        ui->btnMatEditor->setChecked(false);
    }
    else if (name == "INFO_PANEL")
    {
        ui->btnInfoPanel->setChecked(false);
    }
    else if (name == "TEXTURE_PANEL")
    {
        ui->btnTexEditor->setChecked(false);
    }
}

namespace
{
    // ------------------------------------------------------------
    // Filters
    // ------------------------------------------------------------
    static QString openFilter()
    {
        return QObject::tr(
            "IMP3D Scene (*.imp);;"
            "3D Models (*.imp *.obj *.gltf *.glb);;"
            "OBJ Files (*.obj);;"
            "glTF Files (*.gltf *.glb);;"
            "All Files (*.*)");
    }

    static QString saveFilterNative()
    {
        return QObject::tr("IMP3D Scene (*.imp);;All Files (*.*)");
    }

    static QString importFilter()
    {
        return QObject::tr(
            "3D Models (*.obj *.gltf *.glb *.imp);;"
            "IMP3D Scene (*.imp);;"
            "OBJ Files (*.obj);;"
            "glTF Files (*.gltf *.glb);;"
            "All Files (*.*)");
    }

    static QString exportFilter()
    {
        return QObject::tr(
            "OBJ Files (*.obj);;"
            "glTF Files (*.gltf *.glb);;"
            "All Files (*.*)");
    }

    // ------------------------------------------------------------
    // Ensure extension (only for native Save/SaveAs)
    // ------------------------------------------------------------
    static QString ensureImpExtension(QString path)
    {
        if (path.isEmpty())
            return path;

        QFileInfo fi(path);
        if (fi.suffix().isEmpty())
            return path + ".imp";

        // If user typed something else, we keep it; CoreDocument can enforce/force .imp if desired.
        return path;
    }

    // ------------------------------------------------------------
    // Unsaved changes prompt
    // Returns: QMessageBox::Save / Discard / Cancel
    // ------------------------------------------------------------
    static QMessageBox::StandardButton askUnsavedChanges(QWidget* parent, const Core* core)
    {
        QString name;

        if (core)
        {
            const std::string path = core->filePath();
            if (!path.empty())
                name = QString::fromStdString(path);
        }

        if (name.isEmpty())
            name = QObject::tr("Untitled.imp");

        return QMessageBox::warning(
            parent,
            QObject::tr("Unsaved Changes"),
            QObject::tr("Save changes to %1?").arg(name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
    }

} // namespace

void MainWindow::handleAction()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action)
        return;

    const QString name = action->objectName();

    try
    {
        // File
        // ------------------------------------------------------------
        // File -> New
        // ------------------------------------------------------------
        if (name == "actionNew")
        {
            if (!m_core->requestNew())
            {
                const auto choice = askUnsavedChanges(this, m_core.get());
                if (choice == QMessageBox::Cancel)
                    return;

                if (choice == QMessageBox::Save)
                {
                    // Save (if no path, this will fail; then we do Save As)
                    if (!m_core->saveFile())
                    {
                        QString savePath = QFileDialog::getSaveFileName(
                            this,
                            tr("Save Scene"),
                            QString(),
                            saveFilterNative());

                        if (savePath.isEmpty())
                            return;

                        savePath = ensureImpExtension(savePath);
                        if (!m_core->saveFileAs(savePath.toStdString()))
                            return;
                    }
                }
                // Discard -> continue
            }

            m_core->newFile();
        }

        // ------------------------------------------------------------
        // File -> Open
        // ------------------------------------------------------------
        else if (name == "actionOpen")
        {
            if (!m_core->requestNew())
            {
                const auto choice = askUnsavedChanges(this, m_core.get());
                if (choice == QMessageBox::Cancel)
                    return;

                if (choice == QMessageBox::Save)
                {
                    if (!m_core->saveFile())
                    {
                        QString savePath = QFileDialog::getSaveFileName(
                            this,
                            tr("Save Scene"),
                            QString(),
                            saveFilterNative());

                        if (savePath.isEmpty())
                            return;

                        savePath = ensureImpExtension(savePath);
                        if (!m_core->saveFileAs(savePath.toStdString()))
                            return;
                    }
                }
            }

            const QString fileName = QFileDialog::getOpenFileName(
                this,
                tr("Open 3D File"),
                QString(),
                openFilter());

            if (fileName.isEmpty())
                return;

            m_core->openFile(fileName.toStdString());
        }

        // ------------------------------------------------------------
        // File -> Save
        // ------------------------------------------------------------
        else if (name == "actionSave")
        {
            // Try normal save; if there is no path, fall back to Save As dialog.
            if (!m_core->saveFile())
            {
                QString savePath = QFileDialog::getSaveFileName(
                    this,
                    tr("Save Scene"),
                    QString(),
                    saveFilterNative());

                if (savePath.isEmpty())
                    return;

                savePath = ensureImpExtension(savePath);
                m_core->saveFileAs(savePath.toStdString());
            }
        }

        // ------------------------------------------------------------
        // File -> Save As
        // ------------------------------------------------------------
        else if (name == "actionSaveAs")
        {
            QString savePath = QFileDialog::getSaveFileName(
                this,
                tr("Save Scene As"),
                QString(),
                saveFilterNative());

            if (savePath.isEmpty())
                return;

            savePath = ensureImpExtension(savePath);
            m_core->saveFileAs(savePath.toStdString());
        }

        // ------------------------------------------------------------
        // File -> Import (merge into existing)
        // ------------------------------------------------------------
        else if (name == "actionImport")
        {
            const QString fileName = QFileDialog::getOpenFileName(
                this,
                tr("Import 3D File"),
                QString(),
                importFilter());

            if (fileName.isEmpty())
                return;

            m_core->importFile(fileName.toStdString());
        }

        // ------------------------------------------------------------
        // File -> Export (does NOT change current document path)
        // ------------------------------------------------------------
        else if (name == "actionExport")
        {
            const QString fileName = QFileDialog::getSaveFileName(
                this,
                tr("Export"),
                QString(),
                exportFilter());

            if (fileName.isEmpty())
                return;

            m_core->exportFile(fileName.toStdString());
        }

        // ------------------------------------------------------------
        // File -> Exit
        // ------------------------------------------------------------
        else if (name == "actionExit")
        {
            if (!m_core->requestExit())
            {
                const auto choice = askUnsavedChanges(this, m_core.get());
                if (choice == QMessageBox::Cancel)
                    return;

                if (choice == QMessageBox::Save)
                {
                    if (!m_core->saveFile())
                    {
                        QString savePath = QFileDialog::getSaveFileName(
                            this,
                            tr("Save Scene"),
                            QString(),
                            saveFilterNative());

                        if (savePath.isEmpty())
                            return;

                        savePath = ensureImpExtension(savePath);
                        if (!m_core->saveFileAs(savePath.toStdString()))
                            return;
                    }
                }
            }

            close();
        }

        // Edit
        else if (name == "actionUndo")
        {
            m_core->runAction("Undo");
        }
        else if (name == "actionRedo")
        {
            m_core->runAction("Redo");
        }
        else if (name == "actionDelete")
        {
            m_core->runCommand("Delete");
        }
        else if (name == "actionDuplicate")
        {
            // m_core->setActiveTool("DuplicateTool");
            if (m_core->runCommand("DuplicatePolys"))
            {
                m_core->selectionMode(SelectionMode::POLYS);
                m_core->setActiveTool("SelectTool");
                ui->btnSelPolys->setChecked(true);

                m_core->setActiveTool("MoveTool");
                static_cast<MenuStackedWidget*>(ui->stackedWidget)->setToolChecked("MoveTool", true);
            }
        }
        else if (name == "actionAssignMaterial")
        {
            m_subWindowManager->showSubWindow("ASSIGN_MAT_DIALOG");
        }

        // Select
        else if (name == "actionSelectAll")
        {
            m_core->runCommand("SelectAll");
        }
        else if (name == "actionSelectNone")
        {
            m_core->runCommand("SelectNone");
        }
        else if (name == "actionSelectConnected")
        {
            m_core->runCommand("SelectConnected");
        }
        else if (name == "actionSelectPoints")
        {
            m_core->selectionMode(SelectionMode::VERTS);
            m_core->setActiveTool("SelectTool");
            ui->btnSelPoints->setChecked(true);
        }
        else if (name == "actionSelectEdges")
        {
            m_core->selectionMode(SelectionMode::EDGES);
            m_core->setActiveTool("SelectTool");
            ui->btnSelEdges->setChecked(true);
        }
        else if (name == "actionSelectPolys")
        {
            m_core->selectionMode(SelectionMode::POLYS);
            m_core->setActiveTool("SelectTool");
            ui->btnSelPolys->setChecked(true);
        }
        else if (name == "actionSelectLoop")
        {
            m_core->runCommand("EdgeLoop");
        }
        else if (name == "actionSelectRing")
        {
            m_core->runCommand("EdgeRing");
        }

        // Mesh
        else if (name == "actionDivide")
        {
            m_core->runCommand("Divide");
        }
        else if (name == "actionTriangulate")
        {
            m_core->runCommand("Triangulate");
        }
        else if (name == "actionDissolve")
        {
            m_core->runCommand("Dissolve");
        }
        else if (name == "actionMergeByDistance")
        {
            m_core->runCommand("MergeByDistance");
        }
        else if (name == "actionReverseWinding")
        {
            m_core->runCommand("ReverseWinding");
        }
        else if (name == "actionRestOnGround")
        {
            m_core->runCommand("RestOnGround");
        }
        else if (name == "actionCenter")
        {
            m_core->runCommand("Center");
        }
        else if (name == "actionCreatePoly")
        {
            m_core->runCommand("CreatePoly");
        }

        // Tools
        else if (name == "actionSelect")
        {
            m_core->setActiveTool("SelectTool");
            static_cast<MenuStackedWidget*>(ui->stackedWidget)->externalToolClicked();
        }
        else if (name == "actionMove")
        {
            m_core->setActiveTool("MoveTool");
            static_cast<MenuStackedWidget*>(ui->stackedWidget)->setToolChecked("MoveTool", true);
        }
        else if (name == "actionExtrude")
        {
            m_core->setActiveTool("ExtrudeTool");
            static_cast<MenuStackedWidget*>(ui->stackedWidget)->setToolChecked("ExtrudeTool", true);
        }
        else if (name == "actionInset")
        {
            m_core->setActiveTool("InsetTool");
            static_cast<MenuStackedWidget*>(ui->stackedWidget)->setToolChecked("InsetTool", true);
        }
        else if (name == "actionBevel")
        {
            m_core->setActiveTool("BevelTool");
            static_cast<MenuStackedWidget*>(ui->stackedWidget)->setToolChecked("BevelTool", true);
        }

        else if (name == "actionBoxTool")
        {
            m_core->setActiveTool("BoxTool");
        }
        else if (name == "actionSphereTool")
        {
            m_core->setActiveTool("SphereTool");
        }
        else if (name == "actionCylinder")
        {
            m_core->setActiveTool("CylinderTool");
        }

        else if (name == "actionMockTool")
        {
            m_core->setActiveTool("MockTool");
        }

        // View
        else if (name == "actionToggleGrid")
        {
            m_core->showSceneGrid(!m_core->showSceneGrid());
            ui->btnShowGrid->setChecked(m_core->showSceneGrid());
        }
        else if (name == "actionFitToView")
        {
            m_core->runCommand("FitToView");
        }
        else if (name == "actionToolProperties")
        {
            m_subWindowManager->showSubWindow("PROPERTIES_DIALOG");
        }
        else if (name == "actionMaterialEditor")
        {
            m_subWindowManager->showSubWindow("MAT_EDITOR_DIALOG");
        }
    }
    catch (const std::exception& e)
    {
        qWarning() << e.what();
        QMessageBox::critical(this, tr("Error"), QString::fromLocal8Bit(e.what()));
    }
    catch (...)
    {
        qWarning() << "Unknown exception in MainWindow::handleAction";
        QMessageBox::critical(this, tr("Error"), tr("An unknown error occurred while processing the action."));
    }
}

void MainWindow::initSideMenu()
{
    MenuStackedWidget* menuStackedWidget = findChild<MenuStackedWidget*>("stackedWidget");
    if (!menuStackedWidget)
        return;

    // Pages: 0=General, 1=Geometry, 2=Topology, 3=Maps
    [[maybe_unused]] QWidget* pageGeneral  = menuStackedWidget->addNewPage(); // 0
    [[maybe_unused]] QWidget* pageGeometry = menuStackedWidget->addNewPage(); // 1
    [[maybe_unused]] QWidget* pageTopology = menuStackedWidget->addNewPage(); // 2
    [[maybe_unused]] QWidget* pageMaps     = menuStackedWidget->addNewPage(); // 3

    // General (front page: primitives + utilities + render subdivision)
    menuStackedWidget->addLabel(0, "Primitives");
    menuStackedWidget->addButton(0, "Box", ButtonType::Tool, "BoxTool", QKeySequence(Qt::SHIFT | Qt::Key_B));
    menuStackedWidget->addButton(0, "Sphere", ButtonType::Tool, "SphereTool", QKeySequence(Qt::SHIFT | Qt::Key_S));
    menuStackedWidget->addButton(0, "Cylinder", ButtonType::Tool, "CylinderTool", QKeySequence(Qt::SHIFT | Qt::Key_C));
    menuStackedWidget->addButton(0, "Quad Sphere", ButtonType::Tool, "QuadBallTool", QKeySequence(Qt::SHIFT | Qt::Key_Q));

    menuStackedWidget->addButton(0, "Plane", ButtonType::Tool, "PlaneTool", QKeySequence(Qt::SHIFT | Qt::Key_P));
    menuStackedWidget->addButton(0, "Pipe", ButtonType::Tool, "PipeTool", QKeySequence(Qt::SHIFT | Qt::Key_I));
    menuStackedWidget->addButton(0, "Torus", ButtonType::Tool, "TorusTool");

    menuStackedWidget->addLabel(0, "Utilities");
    menuStackedWidget->addButton(0, "Center", ButtonType::Command, "Center");
    menuStackedWidget->addButton(0, "Rest On Ground", ButtonType::Command, "RestOnGround");
    menuStackedWidget->addButton(0, "Delete", ButtonType::Command, "Delete" /*, QKeySequence::Delete*/);
    menuStackedWidget->addButton(0, "Fit To View", ButtonType::Command, "FitToView");

    menuStackedWidget->addLabel(0, "Subdivision");
    menuStackedWidget->addIncrementControl(0, "Subdivide", "Subdivide", QKeySequence(Qt::Key_Minus), QKeySequence(Qt::Key_Equal));
    menuStackedWidget->addButton(0, "Freeze", ButtonType::Command, "Freeze");
    menuStackedWidget->addButton(0, "Triangulate", ButtonType::Command, "Triangulate");

    // Geometry (modify surfaces)
    menuStackedWidget->addLabel(1, "Modify");
    menuStackedWidget->addButton(1, "Move", ButtonType::Tool, "MoveTool", QKeySequence(Qt::Key_W));
    menuStackedWidget->addButton(1, "Rotate", ButtonType::Tool, "RotateTool", QKeySequence(Qt::Key_E));
    menuStackedWidget->addButton(1, "Scale", ButtonType::Tool, "ScaleTool", QKeySequence(Qt::Key_R));
    menuStackedWidget->addButton(1, "Stretch", ButtonType::Tool, "StretchTool", QKeySequence(Qt::Key_S));

    menuStackedWidget->addLabel(1, "Deform");
    menuStackedWidget->addButton(1, "Bend", ButtonType::Command, "BendTool");
    menuStackedWidget->addButton(1, "Randomize/Jitter", ButtonType::Tool, "RandomizeTool");

    // Topology (extend/structure tools)
    menuStackedWidget->addLabel(2, "Extend");
    menuStackedWidget->addButton(2, "Extrude", ButtonType::Tool, "ExtrudeTool", QKeySequence(Qt::CTRL | Qt::Key_E));
    menuStackedWidget->addButton(2, "Inset", ButtonType::Tool, "InsetTool", QKeySequence(Qt::Key_I));
    menuStackedWidget->addButton(2, "Bevel", ButtonType::Tool, "BevelTool");
    menuStackedWidget->addButton(2, "Edge Cut", ButtonType::Tool, "EdgeCutTool");
    menuStackedWidget->addButton(2, "Knife", ButtonType::Tool, "KnifeTool");

    menuStackedWidget->addLabel(2, "Structure");
    menuStackedWidget->addButton(2, "Create Polygon", ButtonType::Command, "CreatePoly", QKeySequence(Qt::Key_P));
    menuStackedWidget->addButton(2, "Connect", ButtonType::Command, "Connect", QKeySequence(Qt::Key_C));
    menuStackedWidget->addButton(2, "Divide", ButtonType::Command, "Divide");
    menuStackedWidget->addButton(2, "Dissolve", ButtonType::Command, "Dissolve");

    // Maps (UVs / vertex maps)
    // menuStackedWidget->addLabel(3, "UV / Maps");

    menuStackedWidget->addLabel(3, "Normals");
    menuStackedWidget->addButton(3, "Flip", ButtonType::Command, "FlipNormals");
    menuStackedWidget->addButton(3, "Smooth", ButtonType::Command, "SmoothNormals");
    menuStackedWidget->addButton(3, "Flatten", ButtonType::Command, "FlattenNormals");
    // menuStackedWidget->addButton(3, "Unwrap", ButtonType::Command, "UnwrapUV");
    // menuStackedWidget->addButton(3, "Relax",  ButtonType::Command, "RelaxUV");
    // menuStackedWidget->addButton(3, "Pack",   ButtonType::Command, "PackUV");
    menuStackedWidget->addLabel(3, "Mesh");
    menuStackedWidget->addButton(3, "Unwrap Mesh", ButtonType::Command, "UnwrapLSCM");

    menuStackedWidget->adjustPageSize();
}

void MainWindow::enableVulkanValidationLayer()
{
#ifndef NDEBUG
    m_vkInstance->setLayers({"VK_LAYER_KHRONOS_validation"});

    const auto supported = m_vkInstance->supportedLayers();
    if (!supported.contains("VK_LAYER_KHRONOS_validation"))
    {
        qWarning() << "VK_LAYER_KHRONOS_validation not available on this system";
    }

    const auto activeLayers = m_vkInstance->layers();
    if (activeLayers.contains("VK_LAYER_KHRONOS_validation"))
        qDebug() << "Validation layer ENABLED.";
    else
        qWarning() << "Validation layer NOT enabled.";
#endif
}
