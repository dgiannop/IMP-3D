#pragma once

#include <SysMesh.hpp>
#include <SysMeshScene.hpp>
#include <vulkan/vulkan_core.h>

#include "CoreTypes.hpp"
#include "GpuResources/TextureHandler.hpp"
#include "MaterialHandler.hpp"
#include "SceneMesh.hpp"
#include "SceneObject.hpp"
#include "SceneQueryCpu.hpp"
#include "SceneQueryEmbree.hpp"
#include "SceneSnap.hpp"
#include "VulkanContext.hpp"

class Scene : public SysMeshScene
{
public:
    Scene();
    ~Scene();

    bool initDevice(const VulkanContext& ctx);

    bool initSwapchain(VkRenderPass rp);

    void renderPrePass(Viewport* vp, VkCommandBuffer cmd, uint32_t frameIndex);

    void destroySwapchainResources();

    void destroy();

    void clear();

    SceneMesh* createSceneMesh(std::string_view name = {});

    std::vector<SceneMesh*> sceneMeshes();

    const std::vector<SceneMesh*> sceneMeshes() const;

    std::vector<std::unique_ptr<SceneObject>>& sceneObjects();

    const std::vector<std::unique_ptr<SceneObject>>& sceneObjects() const;

    std::vector<SysMesh*> meshes() const override;

    std::vector<SysMesh*> selectedMeshes() const override;

    std::vector<SysMesh*> visibleMeshes() const override;

    std::vector<SysMesh*> activeMeshes() const override;

    void selectionMode(SelectionMode mode) noexcept;

    SelectionMode selectionMode() const noexcept;

    void clearSelection() noexcept;

    SceneQuery* sceneQuery() noexcept;

    ImageHandler* imageHandler() noexcept;

    const ImageHandler* imageHandler() const noexcept;

    MaterialHandler* materialHandler() noexcept;

    const MaterialHandler* materialHandler() const noexcept;

    TextureHandler* textureHandler() noexcept;

    class Renderer* renderer() noexcept;

    const class Renderer* renderer() const noexcept;

    void setActiveViewport(Viewport* vp) noexcept;

    Viewport* activeViewport() const noexcept;

    SceneSnap& snap() noexcept;

    const SceneSnap& snap() const;

    void showSceneGrid(bool show) noexcept;

    bool showSceneGrid() const noexcept;

    void subdivisionLevel(int levelDelta) noexcept;

    SceneStats stats() const noexcept;

    bool needsRender() noexcept;

    void markModified() noexcept;

    void idle();

    void render(class Viewport* vp, VkCommandBuffer cmd, uint32_t frameIndex);

    SysCounterPtr changeCounter() const noexcept;

    SysCounterPtr contentChangeCounter() const noexcept;

private:
    std::vector<std::unique_ptr<SceneObject>> m_sceneObjects;
    std::unique_ptr<class Renderer>           m_renderer;
    SysCounterPtr                             m_sceneChangeCounter;
    SysMonitor                                m_sceneChangeMonitor;
    std::unique_ptr<ImageHandler>             m_imageHandler;
    std::unique_ptr<TextureHandler>           m_textureHandler;
    std::unique_ptr<MaterialHandler>          m_materialHandler;
    SysMonitor                                m_materialChangeMonitor;
    SelectionMode                             m_selectionMode = SelectionMode::VERTS;
    Viewport*                                 m_activeViewport = nullptr;
    std::unique_ptr<SceneQueryEmbree>         m_sceneQuery;
    SceneSnap                                 m_snap;
    bool                                      m_showGrid;

    SysCounterPtr m_sceneQueryCounter;
    SysMonitor    m_sceneQueryMonitor;

    SysCounterPtr m_contentChangeCounter;
};
