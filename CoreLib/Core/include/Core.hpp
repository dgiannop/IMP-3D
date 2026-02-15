//=============================================================================
// Core.hpp
//=============================================================================
#pragma once

#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "CoreDocument.hpp"
#include "CoreTypes.hpp"
#include "ItemFactory.hpp"
#include "LightingSettings.hpp"
#include "Scene.hpp"
#include "SceneFormat.hpp"
#include "VulkanContext.hpp"

class Viewport;
class Tool;
class Command;
class MaterialEditor;
class PropertyBase;
class SceneLight;

/**
 * @brief Central application controller.
 *
 * Core is the main coordination layer between the UI, scene data,
 * tools/commands, file I/O, and viewport input dispatch.
 *
 * Core is UI-agnostic; UI layers call into Core in response to user interaction.
 */
class Core
{
public:
    /** @brief Constructs an empty Core instance. */
    Core();

    /** @brief Destroys Core and all owned subsystems. */
    ~Core();

    // ------------------------------------------------------------
    // Device / swapchain lifetime
    // ------------------------------------------------------------

    /**
     * @brief Initialize device-level Vulkan resources.
     * @param ctx Initialized Vulkan context
     */
    void initializeDevice(const VulkanContext& ctx);

    /**
     * @brief Initialize swapchain-dependent resources.
     * @param renderPass Active render pass used for rendering
     */
    void initializeSwapchain(VkRenderPass renderPass);

    /** @brief Destroy swapchain-dependent resources. */
    void destroySwapchainResources();

    /** @brief Fully destroy all GPU and CPU resources. */
    void destroy();

    // ------------------------------------------------------------
    // Viewports
    // ------------------------------------------------------------

    /**
     * @brief Create a new viewport instance.
     * @return Pointer to the newly created viewport
     */
    Viewport* createViewport();

    /**
     * @brief Initialize a viewport after creation.
     * @param vp Viewport to initialize
     */
    void initializeViewport(Viewport* vp) noexcept;

    /**
     * @brief Resize a viewport.
     * @param vp Viewport to resize
     * @param width New width in pixels
     * @param height New height in pixels
     */
    void resizeViewport(Viewport* vp, int width, int height) noexcept;

    /** @brief Rotate the viewport camera. */
    void viewportRotate(Viewport* vp, float deltaX, float deltaY) noexcept;

    /** @brief Pan the viewport camera. */
    void viewportPan(Viewport* vp, float deltaX, float deltaY) noexcept;

    /** @brief Zoom the viewport camera. */
    void viewportZoom(Viewport* vp, float deltaX, float deltaY) noexcept;

    /** @brief Set the view mode of a viewport. */
    void viewMode(Viewport* vp, ViewMode mode) noexcept;

    /** @brief Get the current view mode of a viewport. */
    ViewMode viewMode(Viewport* vp) const noexcept;

    /** @brief Set the draw mode of a viewport. */
    void drawMode(Viewport* vp, DrawMode mode) noexcept;

    /** @brief Get the current draw mode of a viewport. */
    DrawMode drawMode(Viewport* vp) const noexcept;

    // ------------------------------------------------------------
    // Input dispatch
    // ------------------------------------------------------------

    /** @brief Handle mouse press event. */
    void mousePressEvent(Viewport* vp, CoreEvent event) noexcept;

    /** @brief Handle mouse move event. */
    void mouseMoveEvent(Viewport* vp, CoreEvent event) noexcept;

    /** @brief Handle mouse drag event. */
    void mouseDragEvent(Viewport* vp, CoreEvent event) noexcept;

    /** @brief Handle mouse release event. */
    void mouseReleaseEvent(Viewport* vp, CoreEvent event) noexcept;

    /** @brief Handle mouse wheel event. */
    void mouseWheelEvent(Viewport* vp, CoreEvent event) noexcept;

    /**
     * @brief Handle key press event.
     * @return True if the event was consumed
     */
    bool keyPressEvent(Viewport* vp, CoreEvent event) noexcept;

    /** @brief Set the currently active viewport. */
    void setActiveViewport(Viewport* vp) noexcept;

    /** @brief Get the active viewport (last clicked). */
    Viewport* activeViewport() const noexcept;

    // ------------------------------------------------------------
    // Tools & commands
    // ------------------------------------------------------------

    /**
     * @brief Activate a tool by name.
     * @throws std::runtime_error if tool is not registered
     */
    void setActiveTool(const std::string& name);

    /**
     * @brief Execute a command by name.
     * @return True if command was executed
     */
    bool runCommand(const std::string& name);

    /**
     * @brief Execute an action by name.
     * @param name Action identifier
     * @param value Optional action value
     * @return True if action was executed
     */
    bool runAction(const std::string& name, int value = 0);

    /** @brief Set the active selection mode. */
    void selectionMode(SelectionMode mode);

    /** @brief Get the active selection mode. */
    SelectionMode selectionMode() const;

    // ------------------------------------------------------------
    // Scene state & rendering
    // ------------------------------------------------------------

    /**
     * @brief Retrieve scene statistics.
     * @return SceneStats structure
     */
    SceneStats sceneStats() const noexcept;

    /** @brief Scene-stats change stamp for UI polling (monotonic). */
    [[nodiscard]] uint64_t sceneStatsStamp() const noexcept;

    /** @brief Perform idle-time updates (tools, scene). */
    void idle();

    /**
     * @brief Check whether a render is required.
     * @return True if rendering is needed
     */
    bool needsRender() noexcept;

    /**
     * @brief Perform pre-render pass work (e.g. ray tracing, compute).
     *
     * Called before beginning the render pass.
     */
    void renderPrePass(Viewport* vp, const RenderFrameContext& fc);

    /**
     * @brief Render the scene for a viewport.
     */
    void render(Viewport* vp, const RenderFrameContext& fc);

    // ------------------------------------------------------------
    // File operations (triggered by UI)
    // ------------------------------------------------------------

    /** @brief Request creation of a new document. */
    bool requestNew() noexcept;

    /** @brief Request application exit (with save prompt if needed). */
    bool requestExit() noexcept;

    /** @brief Create a new empty file. */
    bool newFile();

    /** @brief Open a file from disk. */
    bool openFile(const std::filesystem::path& path);

    /** @brief Save the current file. */
    bool saveFile();

    /** @brief Save the current file to a new path. */
    bool saveFileAs(const std::filesystem::path& path);

    /** @brief Import geometry from file. */
    bool importFile(const std::filesystem::path& path);

    /** @brief Export scene to file. */
    bool exportFile(const std::filesystem::path& path);

    /**
     * @return Current document file path, or empty if unnamed/unsaved
     */
    std::string filePath() const noexcept;

    // ------------------------------------------------------------
    // Materials
    // ------------------------------------------------------------

    /** @brief Access the material editor facade. */
    [[nodiscard]] MaterialEditor* materialEditor() noexcept;

    /** @brief Access the material editor facade (const). */
    [[nodiscard]] const MaterialEditor* materialEditor() const noexcept;

    /**
     * @brief Assign material to selected polygons or entire scene.
     * @param materialId Material identifier
     */
    void assignMaterial(int32_t materialId) noexcept;

    // ------------------------------------------------------------
    // Images / textures
    // ------------------------------------------------------------

    /** @brief Access the image handler (images list used by Texture Editor). */
    [[nodiscard]] ImageHandler* imageHandler() noexcept;

    /** @brief Access the image handler (const). */
    [[nodiscard]] const ImageHandler* imageHandler() const noexcept;

    /** @brief Access the texture handler (GPU textures), if needed by UI later. */
    [[nodiscard]] TextureHandler* textureHandler() noexcept;

    /** @brief Access the texture handler (const). */
    [[nodiscard]] const TextureHandler* textureHandler() const noexcept;

    // ------------------------------------------------------------
    // Tool properties (UI polling)
    // ------------------------------------------------------------

    /** @brief Check if tool property structure has changed. */
    bool toolPropertyGroupChanged() noexcept;

    /** @brief Check if tool property values have changed. */
    bool toolPropertyValuesChanged() noexcept;

    /** @brief Access current tool properties. */
    const std::vector<std::unique_ptr<PropertyBase>>& toolProperties() const noexcept;

    // ------------------------------------------------------------
    // Lighting settings (UI façade)
    // ------------------------------------------------------------

    /** @brief Retrieve current lighting settings from the active scene. */
    [[nodiscard]] LightingSettings lightingSettings() const noexcept;

    /** @brief Apply lighting settings to the active scene. */
    void setLightingSettings(const LightingSettings& settings) noexcept;

    // ------------------------------------------------------------
    // Scene lights (UI façade)
    // ------------------------------------------------------------

    /**
     * @brief Create a new light in the active scene.
     * @param name Display name for the light
     * @param type Light type (Directional / Point / Spot)
     * @return Pointer to the created SceneLight, or nullptr on failure
     */
    [[nodiscard]] SceneLight* createLight(std::string_view name, LightType type);

    /**
     * @brief Retrieve all lights in the active scene.
     * @return Vector of SceneLight pointers (Scene-owned)
     */
    [[nodiscard]] std::vector<SceneLight*> sceneLights() const;

    /** @brief Enable or disable a light by id. */
    void setLightEnabled(LightId id, bool enabled) noexcept;

    /** @brief Set the object-to-world transform of a light by id. */
    void setLightTransform(LightId id, const glm::mat4& m) noexcept;

    // ------------------------------------------------------------
    // Scene grid
    // ------------------------------------------------------------

    /** @brief Enable or disable the scene grid. */
    void showSceneGrid(bool show) noexcept;

    /** @brief Query whether the scene grid is visible. */
    bool showSceneGrid() const noexcept;

    // ------------------------------------------------------------
    // Render culling
    // ------------------------------------------------------------

    /** @brief Enable or disable culling. */
    void cullingEnabled(bool enabled) noexcept;

    /** @brief Query whether the culling is enabled. */
    bool cullingEnabled() const noexcept;

    /**
     * @brief Retrieve a monotonically increasing scene change stamp.
     *
     * Intended for UI polling.
     */
    [[nodiscard]] uint64_t sceneChangeStamp() const noexcept;

    /**
     * @brief Content-only scene change stamp.
     *
     * Increments only when scene data changes (topology, deform, materials, lights).
     * Intended for UI panels that depend on persistent scene data.
     */
    [[nodiscard]] uint64_t sceneContentChangeStamp() const noexcept;

private:
    /** @brief All active viewports. */
    std::vector<std::unique_ptr<Viewport>> m_viewports;

    /** @brief Active scene. */
    std::unique_ptr<Scene> m_scene;

    /** @brief Active document (file state). */
    std::unique_ptr<CoreDocument> m_document;

    /** @brief Material editor facade. */
    std::unique_ptr<MaterialEditor> m_materialEditor;

    /** @brief Cached camera pan. */
    glm::vec3 m_pan{0.f};

    /** @brief Cached camera rotation. */
    glm::vec3 m_rot{-30.f, 30.f, 0.f};

    /** @brief Cached camera distance. */
    float m_dist = -6.f;

    // ------------------------------------------------------------
    // Tools & commands
    // ------------------------------------------------------------

    /** @brief Currently active tool. */
    std::unique_ptr<Tool> m_activeTool;

    /** @brief Tool factory. */
    ItemFactory<Tool> m_toolFactory;

    /** @brief Command factory. */
    ItemFactory<Command> m_commandFactory;
};
