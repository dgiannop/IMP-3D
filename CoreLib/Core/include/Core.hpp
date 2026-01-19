#pragma once

#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "CoreDocument.hpp"
#include "CoreTypes.hpp"
#include "ItemFactory.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"
#include "SceneFormat.hpp"
#include "VulkanContext.hpp"

class Scene;
class Viewport;
class Tool;
class Command;
class MaterialEditor;
class PropertyBase;

/**
 * @brief Central application controller.
 *
 * Core is the main coordination layer between the UI, scene data,
 * rendering backend, tools, commands, and file I/O.
 *
 * Responsibilities:
 * - Owns the active Scene and CoreDocument
 * - Manages Viewports and dispatches input events
 * - Manages active tools, commands, and actions
 * - Owns and drives the Renderer
 * - Handles file operations (new, open, save, import/export)
 *
 * Core itself is UI-agnostic; UI layers call into Core in response
 * to user interaction.
 */
class Core
{
public:
    /** @brief Constructs an empty Core instance. */
    Core();

    /** @brief Destroys Core and all owned subsystems. */
    ~Core();

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

    /**
     * @brief Perform pre-render pass work (e.g. ray tracing, compute).
     *
     * Called before beginning the render pass.
     *
     * @param vp Viewport being rendered
     * @param cmd Command buffer for the current frame
     * @param frameIndex Frame-in-flight index
     */
    void renderPrePass(Viewport* vp, VkCommandBuffer cmd, uint32_t frameIndex);

    /** @brief Destroy swapchain-dependent resources. */
    void destroySwapchainResources();

    /** @brief Fully destroy all GPU and CPU resources. */
    void destroy();

    /**
     * @brief Create a new viewport instance.
     * @return Pointer to the newly created viewport
     */
    Viewport* createViewport();

    /**
     * @brief Initialize a viewport after creation.
     * @param vp Viewport to initialize
     */
    void initializeViewport(Viewport*) noexcept;

    /**
     * @brief Resize a viewport.
     * @param vp Viewport to resize
     * @param width New width in pixels
     * @param height New height in pixels
     */
    void resizeViewport(Viewport*, int, int) noexcept;

    /** @brief Rotate the viewport camera. */
    void viewportRotate(Viewport* vp, float deltaX, float deltaY) noexcept;

    /** @brief Pan the viewport camera. */
    void viewportPan(Viewport*, float, float) noexcept;

    /** @brief Zoom the viewport camera. */
    void viewportZoom(Viewport*, float, float) noexcept;

    /** @brief Set the view mode of a viewport. */
    void viewMode(Viewport* vp, ViewMode mode) noexcept;

    /** @brief Get the current view mode of a viewport. */
    ViewMode viewMode(Viewport* vp) const noexcept;

    /** @brief Set the draw mode of a viewport. */
    void drawMode(Viewport* vp, DrawMode mode) noexcept;

    /** @brief Get the current draw mode of a viewport. */
    DrawMode drawMode(Viewport* vp) const noexcept;

    /** @brief Handle mouse press event. */
    void mousePressEvent(Viewport* vp, CoreEvent event) noexcept;

    /** @brief Handle mouse move event. */
    void mouseMoveEvent(Viewport*, CoreEvent event) noexcept;

    /** @brief Handle mouse drag event. */
    void mouseDragEvent(Viewport*, CoreEvent event) noexcept;

    /** @brief Handle mouse release event. */
    void mouseReleaseEvent(Viewport*, CoreEvent event) noexcept;

    /** @brief Handle mouse wheel event. */
    void mouseWheelEvent(Viewport* vp, CoreEvent event) noexcept;

    /**
     * @brief Handle key press event.
     * @return True if the event was consumed
     */
    bool keyPressEvent(Viewport* vp, CoreEvent event) noexcept;

    /** @brief Set the currently active viewport. */
    void setActiveViewport(Viewport* vp) noexcept;

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

    /**
     * @brief Retrieve scene statistics.
     * @return SceneStats structure
     */
    SceneStats sceneStats() const noexcept;

    /** @brief Perform idle-time updates (tools, UI sync, etc). */
    void idle();

    /**
     * @brief Check whether a render is required.
     * @return True if rendering is needed
     */
    bool needsRender() noexcept;

    /**
     * @brief Render the scene for a viewport.
     *
     * @param vp Viewport to render
     * @param cmd Command buffer (optional)
     * @param frameIndex Frame-in-flight index
     */
    void render(Viewport* vp, VkCommandBuffer cmd = nullptr, uint32_t frameIndex = 0);

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

    /** @brief Access the material editor facade. */
    [[nodiscard]] MaterialEditor* materialEditor() noexcept;

    /** @brief Access the material editor facade (const). */
    [[nodiscard]] const MaterialEditor* materialEditor() const noexcept;

    /**
     * @brief Assign material to selected polygons or entire scene.
     * @param materialId Material identifier
     */
    void assignMaterial(int32_t materialId) noexcept;

    /** @brief Check if tool property structure has changed. */
    bool toolPropertyGroupChanged() noexcept;

    /** @brief Check if tool property values have changed. */
    bool toolPropertyValuesChanged() noexcept;

    /** @brief Access current tool properties. */
    const std::vector<std::unique_ptr<PropertyBase>>& toolProperties() const noexcept;

    // ------------------------------------------------------------
    // Scene grid
    // ------------------------------------------------------------

    /** @brief Enable or disable the scene grid. */
    void showSceneGrid(bool show) noexcept;

    /** @brief Query whether the scene grid is visible. */
    bool showSceneGrid() const noexcept;

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
