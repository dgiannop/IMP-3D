//=============================================================================
// Scene.hpp
//=============================================================================
#pragma once

#include <SysMesh.hpp>
#include <SysMeshScene.hpp>
#include <vulkan/vulkan_core.h>

#include "CoreTypes.hpp"
#include "GpuResources/TextureHandler.hpp"
#include "LightHandler.hpp"
#include "LightingSettings.hpp"
#include "MaterialHandler.hpp"
#include "ObjectOverlaySystem.hpp"
#include "SceneMesh.hpp"
#include "SceneQueryCpu.hpp"
#include "SceneQueryEmbree.hpp"
#include "SceneSnap.hpp"
#include "VulkanContext.hpp"

class Viewport;
class Renderer;
class SceneObject;
class SceneLight;

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
     * @brief Create and add a new SceneLight.
     * @param name light name
     * @param type light type (ex: LightType::Directional)
     * @return Pointer to the created SceneLight
     */
    SceneLight* createSceneLight(std::string_view name, LightType type);

    /**
     * @brief Create and add a new SceneLight from a fully specified Light definition.
     * @param light Initial light parameters (name, type, color, intensity, transform, etc.).
     * @return Pointer to the created SceneLight, or nullptr on failure.
     *
     * This overload is intended for importers (e.g. glTF/OBJ) and tools that already
     * have the complete Light definition available. It inserts the Light into the
     * LightHandler (owning storage) and then creates a SceneLight scene object that
     * references it via a stable LightId.
     *
     * All scene lights should be created through this function (or the name/type
     * overload) so the Scene can centrally track content changes, maintain invariants.
     */
    SceneLight* createSceneLight(const Light& light);

    /**
     * @brief Retrieve all SceneMeshes.
     * @return Vector of SceneMesh pointers
     */
    [[nodiscard]] std::vector<SceneMesh*> sceneMeshes();

    /**
     * @brief Retrieve all SceneMeshes (const).
     * @return Vector of SceneMesh pointers
     */
    [[nodiscard]] std::vector<SceneMesh*> sceneMeshes() const;

    /**
     * @brief Retrieve all SceneLights (const).
     * @return Vector of SceneLight pointers
     */
    [[nodiscard]] std::vector<SceneLight*> sceneLights() const;

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

    // ------------------------------------------------------------
    // SysMeshScene interface
    // ------------------------------------------------------------

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

    // ------------------------------------------------------------
    // Selection
    // ------------------------------------------------------------

    /** @brief Set current selection mode. */
    void selectionMode(SelectionMode mode) noexcept;

    /** @brief Get current selection mode. */
    [[nodiscard]] SelectionMode selectionMode() const noexcept;

    /**
     * @brief Clear all selection across the scene.
     *
     * Clears mesh element selection (verts/edges/polys) as well as object
     * selection (OBJECTS mode). This is intended to represent a full-scene
     * "clear selection" action.
     */
    void clearSelection() noexcept;

    /**
     * @brief Clear mesh element selection only (verts/edges/polys).
     *
     * This does not affect SceneObject selection. It is useful for cases
     * where the caller wants to preserve OBJECTS selection while resetting
     * mesh element selection.
     */
    void clearMeshSelection() noexcept;

    // ------------------------------------------------------------
    // Queries / handlers
    // ------------------------------------------------------------

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
    [[nodiscard]] Renderer* renderer() noexcept;

    /** @brief Access renderer (const). */
    [[nodiscard]] const Renderer* renderer() const noexcept;

    // ------------------------------------------------------------
    // Viewport / snapping
    // ------------------------------------------------------------

    /** @brief Set the active viewport. */
    void setActiveViewport(Viewport* vp) noexcept;

    /** @brief Get the active viewport. */
    [[nodiscard]] Viewport* activeViewport() const noexcept;

    /** @brief Access snapping system. */
    [[nodiscard]] SceneSnap& snap() noexcept;

    /** @brief Access snapping system (const). */
    [[nodiscard]] const SceneSnap& snap() const;

    // ------------------------------------------------------------
    // Render flags / settings
    // ------------------------------------------------------------

    /** @brief Enable or disable the scene grid. */
    void showSceneGrid(bool show) noexcept;

    /** @brief Query whether the scene grid is visible. */
    [[nodiscard]] bool showSceneGrid() const noexcept;

    /**
     * @brief Retrieve current lighting settings.
     *
     * This is scene-owned render policy (headlight/scene lights/exposure/etc).
     * Intended for Core/UI access.
     */
    [[nodiscard]] const LightingSettings& lightingSettings() const noexcept;

    /**
     * @brief Apply lighting settings.
     *
     * Stores the new settings and forwards them to the renderer so
     * subsequent draws use the updated lighting policy.
     *
     * @param settings New lighting settings
     */
    void setLightingSettings(const LightingSettings& settings) noexcept;

    // ------------------------------------------------------------
    // Object selection (OBJECTS mode)
    // ------------------------------------------------------------

    /**
     * @brief Retrieve the first selected SceneObject, if any.
     *
     * In OBJECTS selection mode, selection is stored on SceneObject instances
     * via SceneObject::selected(). This helper is intended for tools and gizmos
     * that operate on a single active object.
     *
     * @return Pointer to a selected object, or nullptr if none selected.
     */
    [[nodiscard]] SceneObject* selectedObject() noexcept;

    /** @brief Retrieve the first selected SceneObject (const), if any. */
    [[nodiscard]] const SceneObject* selectedObject() const noexcept;

    /**
     * @brief Clear object selection.
     *
     * Clears SceneObject::selected() for all objects.
     */
    void clearObjectSelection() noexcept;

    /**
     * @brief Set a single selected object.
     *
     * Clears current object selection first, then selects the given object.
     * Passing nullptr clears selection.
     *
     * @param obj Object to select, or nullptr to clear selection.
     */
    void setSelectedObject(SceneObject* obj) noexcept;

    // ------------------------------------------------------------
    // Object overlays
    // ------------------------------------------------------------

    /**
     * @brief Access the object overlay system.
     *
     * This system builds and owns the OverlayHandler used to visualize
     * selectable non-mesh objects (lights/cameras/etc).
     */
    [[nodiscard]] ObjectOverlaySystem& objectOverlays() noexcept;

    /** @brief Access the object overlay system (const). */
    [[nodiscard]] const ObjectOverlaySystem& objectOverlays() const noexcept;

    // ------------------------------------------------------------
    // Misc
    // ------------------------------------------------------------

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
     * @param fc Frame context
     */
    void renderPrePass(Viewport* vp, const RenderFrameContext& fc);

    /**
     * @brief Render the scene.
     *
     * @param vp Viewport to render
     * @param fc Frame context
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
    bool m_showGrid = true;

    /** @brief Scene-owned lighting settings (render policy). */
    LightingSettings m_lightingSettings = {};

    /** @brief Scene query change counter. */
    SysCounterPtr m_sceneQueryCounter;

    /** @brief Scene query change monitor. */
    SysMonitor m_sceneQueryMonitor;

    /** @brief Content-only change counter. */
    SysCounterPtr m_contentChangeCounter;

    /** @brief Scene-level overlays for OBJECTS selection mode. */
    ObjectOverlaySystem m_objectOverlays = {};
};
