#pragma once

#include <SysMesh.hpp>
#include <SysMeshScene.hpp>
#include <vulkan/vulkan_core.h>

#include "CoreTypes.hpp"
#include "GpuResources/TextureHandler.hpp"
#include "LightHandler.hpp"
#include "MaterialHandler.hpp"
#include "SceneMesh.hpp"
#include "SceneObject.hpp"
#include "SceneQueryCpu.hpp"
#include "SceneQueryEmbree.hpp"
#include "SceneSnap.hpp"
#include "VulkanContext.hpp"

class Viewport;
class Renderer;

/**
 * @brief Scene-level container and coordinator.
 *
 * Scene owns all scene objects, meshes, materials, renderers,
 * selection state, snapping, and query systems.
 *
 * It implements SysMeshScene to expose mesh-level access
 * to tools, commands, and queries in a scene-wide context.
 *
 * Responsibilities:
 * - Own SceneObjects and SceneMeshes
 * - Manage selection and visibility
 * - Drive rendering (delegated to Renderer)
 * - Provide scene queries (CPU / Embree)
 * - Own material, texture, and image handlers
 * - Track scene and content change counters
 */
class Scene : public SysMeshScene
{
public:
    /** @brief Construct an empty scene. */
    Scene();

    /** @brief Destroy scene and all owned resources. */
    ~Scene();

    /**
     * @brief Initialize device-level GPU resources.
     * @param ctx Vulkan context
     * @return True on success
     */
    [[nodiscard]] bool initDevice(const VulkanContext& ctx);

    /**
     * @brief Initialize swapchain-dependent resources.
     * @param rp Active render pass
     * @return True on success
     */
    [[nodiscard]] bool initSwapchain(VkRenderPass rp);

    /** @brief Destroy swapchain-dependent resources. */
    void destroySwapchainResources();

    /** @brief Destroy all GPU and CPU resources owned by the scene. */
    void destroy();

    /** @brief Clear the scene contents. */
    void clear();

    /**
     * @brief Create and add a new SceneMesh.
     * @param name Optional mesh name
     * @return Pointer to the created SceneMesh
     */
    [[nodiscard]] SceneMesh* createSceneMesh(std::string_view name = {});

    /**
     * @brief Retrieve all SceneMeshes.
     * @return Vector of SceneMesh pointers
     */
    [[nodiscard]] std::vector<SceneMesh*> sceneMeshes();

    /**
     * @brief Retrieve all SceneMeshes (const).
     * @return Vector of SceneMesh pointers
     */
    [[nodiscard]] const std::vector<SceneMesh*> sceneMeshes() const;

    /**
     * @brief Access scene objects.
     * @return Vector of SceneObject ownership pointers
     */
    [[nodiscard]] std::vector<std::unique_ptr<SceneObject>>& sceneObjects();

    /**
     * @brief Access scene objects (const).
     * @return Vector of SceneObject ownership pointers
     */
    [[nodiscard]] const std::vector<std::unique_ptr<SceneObject>>& sceneObjects() const;

    /**
     * @brief All meshes in the scene.
     * @return Vector of SysMesh pointers
     */
    [[nodiscard]] std::vector<SysMesh*> meshes() const override;

    /**
     * @brief Selected meshes in the scene.
     * @return Vector of SysMesh pointers
     */
    [[nodiscard]] std::vector<SysMesh*> selectedMeshes() const override;

    /**
     * @brief Visible meshes in the scene.
     * @return Vector of SysMesh pointers
     */
    [[nodiscard]] std::vector<SysMesh*> visibleMeshes() const override;

    /**
     * @brief Active meshes (selection-aware).
     * @return Vector of SysMesh pointers
     */
    [[nodiscard]] std::vector<SysMesh*> activeMeshes() const override;

    /** @brief Set current selection mode. */
    void selectionMode(SelectionMode mode) noexcept;

    /** @brief Get current selection mode. */
    [[nodiscard]] SelectionMode selectionMode() const noexcept;

    /** @brief Clear all selection. */
    void clearSelection() noexcept;

    /**
     * @brief Access active scene query system.
     * @return SceneQuery pointer
     */
    [[nodiscard]] SceneQuery* sceneQuery() noexcept;

    /** @brief Access image handler. */
    [[nodiscard]] ImageHandler* imageHandler() noexcept;

    /** @brief Access image handler (const). */
    [[nodiscard]] const ImageHandler* imageHandler() const noexcept;

    /** @brief Access material handler. */
    [[nodiscard]] MaterialHandler* materialHandler() noexcept;

    /** @brief Access material handler (const). */
    [[nodiscard]] const MaterialHandler* materialHandler() const noexcept;

    /** @brief Access texture handler. */
    [[nodiscard]] TextureHandler* textureHandler() noexcept;

    /** @brief Access light handler. */
    [[nodiscard]] LightHandler* lightHandler() noexcept;

    /** @brief Access light handler (const). */
    [[nodiscard]] const LightHandler* lightHandler() const noexcept;

    /** @brief Access renderer. */
    [[nodiscard]] class Renderer* renderer() noexcept;

    /** @brief Access renderer (const). */
    [[nodiscard]] const class Renderer* renderer() const noexcept;

    /** @brief Set the active viewport. */
    void setActiveViewport(Viewport* vp) noexcept;

    /** @brief Get the active viewport. */
    [[nodiscard]] Viewport* activeViewport() const noexcept;

    /** @brief Access snapping system. */
    [[nodiscard]] SceneSnap& snap() noexcept;

    /** @brief Access snapping system (const). */
    [[nodiscard]] const SceneSnap& snap() const;

    /** @brief Enable or disable the scene grid. */
    void showSceneGrid(bool show) noexcept;

    /** @brief Query whether the scene grid is visible. */
    [[nodiscard]] bool showSceneGrid() const noexcept;

    /**
     * @brief Adjust subdivision level.
     * @param levelDelta Relative subdivision change
     */
    void subdivisionLevel(int levelDelta) noexcept;

    /**
     * @brief Retrieve scene statistics.
     * @return SceneStats structure
     */
    [[nodiscard]] SceneStats stats() const noexcept;

    /**
     * @brief Check whether rendering is required.
     * @return True if a redraw is needed
     */
    [[nodiscard]] bool needsRender() noexcept;

    /** @brief Mark the scene as modified. */
    void markModified() noexcept;

    /** @brief Perform idle-time updates. */
    void idle();

    /**
     * @brief Perform pre-render pass work (compute / ray tracing).
     *
     * Called before beginning the render pass.
     *
     * @param vp Active viewport
     * @param cmd Command buffer
     * @param frameIndex Frame-in-flight index
     */
    void renderPrePass(Viewport* vp, const RenderFrameContext& fc);

    /**
     * @brief Render the scene.
     *
     * @param vp Viewport to render
     * @param fc Command buffer
     * @param frameIndex Frame-in-flight index
     */
    void render(Viewport* vp, const RenderFrameContext& fc);

    /** @brief Scene change counter (topology + selection). */
    [[nodiscard]] SysCounterPtr changeCounter() const noexcept;

    /** @brief Content-only change counter (geometry, materials). */
    [[nodiscard]] SysCounterPtr contentChangeCounter() const noexcept;

private:
    /** @brief Scene objects (meshes, cameras, lights, etc). */
    std::vector<std::unique_ptr<SceneObject>> m_sceneObjects;

    /** @brief Renderer instance. */
    std::unique_ptr<Renderer> m_renderer;

    /** @brief Scene change counter. */
    SysCounterPtr m_sceneChangeCounter;

    /** @brief Scene change monitor. */
    SysMonitor m_sceneChangeMonitor;

    /** @brief Image handler. */
    std::unique_ptr<ImageHandler> m_imageHandler;

    /** @brief Texture handler. */
    std::unique_ptr<TextureHandler> m_textureHandler;

    /** @brief Material handler. */
    std::unique_ptr<MaterialHandler> m_materialHandler;

    /** @brief Light handler. */
    std::unique_ptr<LightHandler> m_lightHandler = {};

    /** @brief Material change monitor. */
    SysMonitor m_materialChangeMonitor;

    /** @brief Current selection mode. */
    SelectionMode m_selectionMode = SelectionMode::VERTS;

    /** @brief Active viewport. */
    Viewport* m_activeViewport = nullptr;

    /** @brief Scene query backend (Embree). */
    std::unique_ptr<SceneQueryEmbree> m_sceneQuery;

    /** @brief Snapping system. */
    SceneSnap m_snap;

    /** @brief Scene grid visibility flag. */
    bool m_showGrid;

    /** @brief Scene query change counter. */
    SysCounterPtr m_sceneQueryCounter;

    /** @brief Scene query change monitor. */
    SysMonitor m_sceneQueryMonitor;

    /** @brief Content-only change counter. */
    SysCounterPtr m_contentChangeCounter;
};
