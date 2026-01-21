#include "Scene.hpp"

#include "Renderer.hpp"
#include "Viewport.hpp"

Scene::Scene() : m_renderer{std::make_unique<Renderer>()},
                 m_sceneChangeCounter{std::make_shared<SysCounter>()},
                 m_sceneChangeMonitor{m_sceneChangeCounter},
                 m_materialHandler{std::make_unique<MaterialHandler>()},
                 m_imageHandler{std::make_unique<ImageHandler>()},
                 m_materialChangeMonitor{m_materialHandler->changeCounter()},
                 m_sceneQuery{std::make_unique<SceneQueryEmbree>()},
                 m_showGrid{true},
                 m_sceneQueryCounter{std::make_shared<SysCounter>()},
                 m_sceneQueryMonitor{m_sceneQueryCounter},
                 m_contentChangeCounter{std::make_shared<SysCounter>()}
{
    m_materialHandler->changeCounter()->addParent(m_sceneChangeCounter);
    m_imageHandler->changeCounter()->addParent(m_sceneChangeCounter);
    m_contentChangeCounter->addParent(m_sceneChangeCounter);
    // Ensure default material at index 0
    m_materialHandler->createMaterial("Default");
}

Scene::~Scene()
{
    destroy();
}

bool Scene::initDevice(const VulkanContext& ctx)
{
    m_textureHandler = std::make_unique<TextureHandler>(ctx, m_imageHandler.get());
    return m_renderer->initDevice(ctx);
}

bool Scene::initSwapchain(VkRenderPass rp)
{
    return m_renderer && m_renderer->initSwapchain(rp);
}

void Scene::destroySwapchainResources()
{
    if (m_renderer)
        m_renderer->destroySwapchainResources();
}

void Scene::destroy()
{
    destroySwapchainResources();

    m_sceneObjects.clear();

    if (m_renderer)
    {
        m_renderer->shutdown();
        m_renderer.reset();
    }
}

void Scene::clear()
{
    if (m_renderer)
        m_renderer->waitDeviceIdle();

    history().clear();

    m_sceneObjects.clear();

    m_materialHandler->clear();
    // Ensure default material at index 0
    m_materialHandler->createMaterial("Default");

    m_sceneChangeCounter->change();
}

SceneMesh* Scene::createSceneMesh(std::string_view name)
{
    auto sm = std::make_unique<SceneMesh>(name);
    sm->sysMesh()->change_counter()->addParent(m_sceneChangeCounter);

    sm->sysMesh()->topology_counter()->addParent(m_sceneQueryCounter);
    sm->sysMesh()->deform_counter()->addParent(m_sceneQueryCounter);

    sm->changeCounter()->addParent(m_sceneChangeCounter);

    SceneMesh* ptr = sm.get();
    m_sceneObjects.push_back(std::move(sm));

    return ptr;
}

std::vector<SceneMesh*> Scene::sceneMeshes()
{
    std::vector<SceneMesh*> result;
    result.reserve(m_sceneObjects.size());

    for (const auto& obj : m_sceneObjects)
    {
        if (auto* mesh = dynamic_cast<SceneMesh*>(obj.get()))
            result.push_back(mesh);
    }
    return result;
}

const std::vector<SceneMesh*> Scene::sceneMeshes() const
{
    std::vector<SceneMesh*> result;
    result.reserve(m_sceneObjects.size());

    for (const auto& obj : m_sceneObjects)
    {
        if (auto* mesh = dynamic_cast<SceneMesh*>(obj.get()))
            result.push_back(mesh);
    }
    return result;
}

std::vector<std::unique_ptr<SceneObject>>& Scene::sceneObjects()
{
    return m_sceneObjects;
}

const std::vector<std::unique_ptr<SceneObject>>& Scene::sceneObjects() const
{
    return m_sceneObjects;
}

std::vector<SysMesh*> Scene::meshes() const
{
    std::vector<SysMesh*> result;
    result.reserve(m_sceneObjects.size());

    for (const auto& obj : m_sceneObjects)
    {
        if (auto* mesh = dynamic_cast<SceneMesh*>(obj.get()))
        {
            result.push_back(mesh->sysMesh());
        }
    }
    return result;
}

std::vector<SysMesh*> Scene::selectedMeshes() const
{
    return meshes();
}

std::vector<SysMesh*> Scene::visibleMeshes() const
{
    return meshes();
}

std::vector<SysMesh*> Scene::activeMeshes() const
{
    return meshes();
}

void Scene::selectionMode(SelectionMode mode) noexcept
{
    m_selectionMode = mode;
    m_sceneChangeCounter->change();
}

SelectionMode Scene::selectionMode() const noexcept
{
    return m_selectionMode;
}

void Scene::clearSelection() noexcept
{
    for (SysMesh* mesh : meshes())
    {
        mesh->clear_selected_verts();
        mesh->clear_selected_edges();
        mesh->clear_selected_polys();
    }
}

SceneQuery* Scene::sceneQuery() noexcept
{
    return m_sceneQuery.get();
}

ImageHandler* Scene::imageHandler() noexcept
{
    return m_imageHandler.get();
}

const ImageHandler* Scene::imageHandler() const noexcept
{
    return m_imageHandler.get();
}

MaterialHandler* Scene::materialHandler() noexcept
{
    return m_materialHandler.get();
}

const MaterialHandler* Scene::materialHandler() const noexcept
{
    return m_materialHandler.get();
}

TextureHandler* Scene::textureHandler() noexcept
{
    return m_textureHandler.get();
}

Renderer* Scene::renderer() noexcept
{
    return m_renderer.get();
}

const Renderer* Scene::renderer() const noexcept
{
    return m_renderer.get();
}

void Scene::setActiveViewport(Viewport* vp) noexcept
{
    m_activeViewport = vp;
}

Viewport* Scene::activeViewport() const noexcept
{
    return m_activeViewport;
}

SceneSnap& Scene::snap() noexcept
{
    return m_snap;
}

const SceneSnap& Scene::snap() const
{
    return m_snap;
}

void Scene::showSceneGrid(bool show) noexcept
{
    if (m_showGrid != show)
    {
        m_showGrid = show;
        m_sceneChangeCounter->change();
    }
}

bool Scene::showSceneGrid() const noexcept
{
    return m_showGrid;
}

void Scene::subdivisionLevel(int level) noexcept
{
    for (SceneMesh* mesh : sceneMeshes())
    {
        if (mesh->selected() && mesh->visible())
        {
            mesh->subdivisionLevel(level);
        }
    }

    m_sceneChangeCounter->change();
}

SceneStats Scene::stats() const noexcept
{
    SceneStats s = {};

    for (const SysMesh* mesh : meshes())
    {
        if (!mesh)
            continue;

        s.verts += mesh->num_verts();
        s.polys += mesh->num_polys();

        if (int normMap = mesh->map_find(0); normMap > -1)
        {
            s.norms += mesh->map_buffer_size(normMap);
        }

        if (int textMap = mesh->map_find(1); textMap > -1)
        {
            s.uvPos += mesh->map_buffer_size(textMap);
        }
    }

    return s;
}

bool Scene::needsRender() noexcept
{
    return m_sceneChangeMonitor.changed();
}

void Scene::markModified() noexcept
{
    m_sceneChangeCounter->change();
}

void Scene::idle()
{
    if (m_sceneQueryMonitor.changed())
    {
        if (m_contentChangeCounter)
            m_contentChangeCounter->change();

        // TICK(EMBREE);
        m_sceneQuery->rebuild(this);
        // TOCK(EMBREE);
    }

    for (const auto& obj : m_sceneObjects)
    {
        if (obj)
        {
            obj->idle(this);
        }
    }

    if (m_renderer)
    {
        m_renderer->idle(this);
    }
}

void Scene::renderPrePass(Viewport* vp, const RenderFrameContext& fc)
{
    if (!vp || !fc.cmd)
        return;

    // IMPORTANT:
    // renderPrePass is where we do any work that must happen OUTSIDE a render pass.
    // This includes MeshGpuResources uploads/updates (raster) and RT dispatch (ray tracing).
    // So it must run for ALL draw modes.
    if (m_renderer)
        m_renderer->renderPrePass(vp, this, fc);
}

// void Scene::render(Viewport* vp, VkCommandBuffer cmd, uint32_t frameIndex)
void Scene::render(Viewport* vp, const RenderFrameContext& fc)
{
    vp->apply();
    m_renderer->render(vp, this, fc);
}

SysCounterPtr Scene::changeCounter() const noexcept
{
    return m_sceneChangeCounter;
}

SysCounterPtr Scene::contentChangeCounter() const noexcept
{
    return m_contentChangeCounter;
}
