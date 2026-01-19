#include "Core.hpp"

#include "Command.hpp"
#include "Config.hpp"
#include "MaterialEditor.hpp"
#include "SelectionUtils.hpp" // todo remove this and the functionality of assignMaterial from in here
#include "Tool.hpp"
#include "Viewport.hpp"

Core::Core() : m_scene{std::make_unique<Scene>()},
               m_document{std::make_unique<CoreDocument>(m_scene.get())},
               m_materialEditor{std::make_unique<MaterialEditor>(m_scene.get())}
{
    config::registerSceneFormats(m_document->formatFactory());
    config::registerTools(m_toolFactory);
    config::registerCommands(m_commandFactory);
}

Core::~Core()
{
    destroy();
}

void Core::initializeDevice(const VulkanContext& ctx)
{
    (void)m_scene->initDevice(ctx);
}

void Core::initializeSwapchain(VkRenderPass renderPass)
{
    (void)m_scene->initSwapchain(renderPass);
}

void Core::renderPrePass(Viewport* vp, VkCommandBuffer cmd, uint32_t frameIndex)
{
    if (!vp || !cmd || !m_scene)
        return;

    (void)m_scene->renderPrePass(vp, cmd, frameIndex);
}

void Core::destroySwapchainResources()
{
    m_scene->destroySwapchainResources();
}

void Core::destroy()
{
    if (m_scene)
    {
        m_scene->destroy();
        m_scene.reset();
    }
}

Viewport* Core::createViewport()
{
    m_viewports.emplace_back(std::make_unique<Viewport>(m_pan, m_rot, m_dist));
    m_viewports.back()->changeCounter()->addParent(m_scene->changeCounter());
    return m_viewports.back().get();
}

void Core::initializeViewport(Viewport* vp) noexcept
{
    vp->initialize();
    if (m_scene)
        m_scene->setActiveViewport(vp);
}

void Core::resizeViewport(Viewport* vp, int w, int h) noexcept
{
    vp->resize(w, h);
}

void Core::viewportRotate(Viewport* vp, float deltaX, float deltaY) noexcept
{
    vp->rotate(deltaX, deltaY);
}

void Core::viewportPan(Viewport* vp, float deltaX, float deltaY) noexcept
{
    vp->pan(deltaX, deltaY);
}

void Core::viewportZoom(Viewport* vp, float deltaX, float deltaY) noexcept
{
    vp->zoom(deltaX, deltaY);
}

void Core::viewMode(Viewport* vp, ViewMode mode) noexcept
{
    vp->viewMode(mode);
}

ViewMode Core::viewMode(Viewport* vp) const noexcept
{
    return vp->viewMode();
}

void Core::drawMode(Viewport* vp, DrawMode mode) noexcept
{
    vp->drawMode(mode);
}

DrawMode Core::drawMode(Viewport* vp) const noexcept
{
    return vp->drawMode();
}

void Core::mousePressEvent(Viewport* vp, CoreEvent event) noexcept
{
    if (m_activeTool)
    {
        m_activeTool->mouseDown(vp, m_scene.get(), event);
    }
}

void Core::mouseMoveEvent(Viewport* vp, CoreEvent event) noexcept
{
    if (m_activeTool)
    {
        m_activeTool->mouseMove(vp, m_scene.get(), event);
    }
}

void Core::mouseDragEvent(Viewport* vp, CoreEvent event) noexcept
{
    if (m_activeTool)
    {
        m_activeTool->mouseDrag(vp, m_scene.get(), event);
    }
}

void Core::mouseReleaseEvent(Viewport* vp, CoreEvent event) noexcept
{
    if (m_activeTool)
    {
        m_activeTool->mouseUp(vp, m_scene.get(), event);
    }
}

void Core::mouseWheelEvent(Viewport* vp, CoreEvent event) noexcept
{
}

bool Core::keyPressEvent(Viewport* vp, CoreEvent event) noexcept
{
    if (m_activeTool)
    {
        return m_activeTool->keyPress(vp, m_scene.get(), event);
    }
    return false;
}

void Core::setActiveViewport(Viewport* vp) noexcept
{
    if (m_scene)
        m_scene->setActiveViewport(vp);
}

void Core::setActiveTool(const std::string& name)
{
    if (m_activeTool)
    {
        m_activeTool->deactivate(m_scene.get());
    }

    m_activeTool = m_toolFactory.createItem(name);

    if (!m_activeTool)
    {
        throw std::runtime_error("Core::setActiveTool(): Tool \"" + name + "\" not found.");
    }

    if (m_activeTool)
    {
        m_activeTool->activate(m_scene.get());
    }
    m_scene->changeCounter()->change();
}

bool Core::runCommand(const std::string& name)
{
    if (m_activeTool)
    {
        m_activeTool->deactivate(m_scene.get());
    }

    auto command = m_commandFactory.createItem(name);
    if (!command)
    {
        throw std::runtime_error("Core::runCommand(): Command \"" + name + "\" not found.");
    }

    try
    {
        if (command->execute(m_scene.get()))
        {
            m_scene->commitMeshChanges();
            return true;
        }

        m_scene->abortMeshChanges();
        return false;
    }
    catch (...)
    {
        m_scene->abortMeshChanges();
        throw;
    }

    m_scene->abortMeshChanges();
    return false;
}

bool Core::runAction(const std::string& name, int value)
{
    if (name == "Subdivide")
    {
        m_scene->subdivisionLevel(value);
    }
    else if (name == "Undo")
    {
        // If a tool is mid-operation, first undo the preview (abort),
        // NOT the previously committed scene step.
        // if (m_scene->hasPendingMeshChanges())
        // {
        //     m_scene->abortMeshChanges();
        //     return true;
        // }
        m_scene->commitMeshChanges();

        if (m_scene->history().undo_step())
        {
            setActiveTool("SelectTool");
        }
    }
    else if (name == "Redo")
    {
        // Redo while preview edits exist is undefined/confusing; ignore/disable.
        // if (m_scene->hasPendingMeshChanges())
        //     return true;

        if (m_scene->history().redo_step())
            return true;
    }
    else if (name == "ToggleSnapping")
    {
        // std::cerr << "ToggleSnapping = " << value << std::endl;
    }
    else
    {
        throw std::runtime_error("Core::runAction(): Action \"" + name + "\" not found.");
    }
    return true;
}

void Core::selectionMode(SelectionMode mode)
{
    m_scene->selectionMode(mode);
}

SelectionMode Core::selectionMode() const
{
    return m_scene->selectionMode();
}

SceneStats Core::sceneStats() const noexcept
{
    return m_scene ? m_scene->stats() : SceneStats{};
}

void Core::idle()
{
    if (m_scene)
        m_scene->idle();

    if (m_activeTool)
    {
        m_activeTool->idle(m_scene.get());
    }

    // if (m_sceneMonitor->changed())
    // {
    //     m_sceneInfo->update(m_scene.get());
    // }
}

bool Core::needsRender() noexcept
{
    return m_scene && m_scene->needsRender();
}

void Core::render(Viewport* vp, VkCommandBuffer cmd, uint32_t frameIndex)
{
    if (!m_scene)
        return;

    m_scene->render(vp, cmd, frameIndex);

    if (m_activeTool)
    {
        m_activeTool->render(vp, m_scene.get());
    }

    auto renderer = m_scene->renderer();
    if (renderer && m_activeTool)
    {
        if (OverlayHandler* oh = m_activeTool->overlayHandler())
        {
            renderer->drawOverlays(cmd, vp, *oh);
        }
    }
}

// Viewport* Core::activeViewport() const
// {
//     return nullptr;
//     // m_viewports.back().get();
// }

bool Core::requestNew() noexcept
{
    if (!m_document)
        return true;

    return m_document->requestNew();
}

bool Core::requestExit() noexcept
{
    if (!m_document)
        return true;

    return m_document->requestExit();
}

bool Core::newFile()
{
    if (!m_document)
        return false;

    return m_document->newFile();
}

bool Core::openFile(const std::filesystem::path& path)
{
    if (!m_document)
        return false;

    LoadOptions opt{};
    opt.mergeIntoExisting = false;
    opt.triangulate       = false;

    return m_document->openFile(path, opt, nullptr);
}

bool Core::saveFile()
{
    if (!m_document)
        return false;

    SaveOptions opt{};
    opt.selectedOnly   = false;
    opt.compressNative = false;
    opt.triangulate    = false;

    return m_document->save(opt, nullptr);
}

bool Core::saveFileAs(const std::filesystem::path& path)
{
    if (!m_document)
        return false;

    SaveOptions opt{};
    opt.selectedOnly   = false;
    opt.compressNative = false;
    opt.triangulate    = false;

    return m_document->saveAs(path, opt, nullptr);
}

bool Core::importFile(const std::filesystem::path& path)
{
    if (!m_document)
        return false;

    LoadOptions opt{};
    opt.mergeIntoExisting = true;
    opt.triangulate       = false;

    return m_document->importFile(path, opt, nullptr);
}

bool Core::exportFile(const std::filesystem::path& path)
{
    if (!m_document)
        return false;

    SaveOptions opt{};
    opt.selectedOnly   = false;
    opt.compressNative = false;
    opt.triangulate    = false;

    return m_document->exportFile(path, opt, nullptr);
}

std::string Core::filePath() const noexcept
{
    if (!m_document || !m_document->hasFilePath())
        return {};

    // UI-friendly: return filename only, not full path
    return m_document->filePath().filename().string();
}

MaterialEditor* Core::materialEditor() noexcept
{
    return m_materialEditor.get();
}

const MaterialEditor* Core::materialEditor() const noexcept
{
    return m_materialEditor.get();
}

void Core::assignMaterial(int32_t materialId) noexcept
{
    if (!m_scene)
        return;

    if (materialId < 0)
        return;

    const bool useSelection = sel::has_selection(m_scene.get());

    for (SceneMesh* sm : m_scene->sceneMeshes())
    {
        if (!sm)
            continue;

        SysMesh* mesh = sm->sysMesh();
        if (!mesh)
            continue;

        if (useSelection)
        {
            // Assign only to selected polys
            const auto& selPolys = mesh->selected_polys();
            for (int32_t pi : selPolys)
            {
                // optional: mesh->poly_valid(pi)
                mesh->set_poly_material(pi, static_cast<uint32_t>(materialId));
            }
        }
        else
        {
            // Assign to all polys
            const int32_t polyCount = mesh->num_polys();
            for (int32_t pi = 0; pi < polyCount; ++pi)
            {
                // optional: if (!mesh->poly_valid(pi)) continue;
                mesh->set_poly_material(pi, static_cast<uint32_t>(materialId));
            }
        }
    }

    m_scene->changeCounter()->change();
}

bool Core::toolPropertyGroupChanged() noexcept
{
    if (m_activeTool)
    {
        return m_activeTool->propertyGroupChanged();
    }
    return false;
}

bool Core::toolPropertyValuesChanged() noexcept
{
    // if (m_activeTool)
    // return m_activeTool->propertyValuesChanged(); // todo: already used in Tool.cpp
    return true;
}

const std::vector<std::unique_ptr<PropertyBase>>& Core::toolProperties() const noexcept
{
    static const std::vector<std::unique_ptr<PropertyBase>> kEmpty = {};
    if (m_activeTool)
        return m_activeTool->properties();
    return kEmpty;
}

void Core::showSceneGrid(bool show) noexcept
{
    if (m_scene)
        m_scene->showSceneGrid(show);
}

bool Core::showSceneGrid() const noexcept
{
    return m_scene && m_scene->showSceneGrid();
}
