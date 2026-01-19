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
// #include "Viewport.hpp"
#include "VulkanContext.hpp"

class Scene;
class Viewport;
class Tool;
class Command;
class MaterialEditor;
class PropertyBase;

class Core
{
public:
    Core();

    ~Core();

    void initializeDevice(const VulkanContext& ctx);

    void initializeSwapchain(VkRenderPass renderPass);

    void renderPrePass(Viewport* vp, VkCommandBuffer cmd, uint32_t frameIndex);

    void destroySwapchainResources();

    void destroy();

    Viewport* createViewport();

    void initializeViewport(Viewport*) noexcept;

    void resizeViewport(Viewport*, int, int) noexcept;

    void viewportRotate(Viewport* vp, float deltaX, float deltaY) noexcept;

    void viewportPan(Viewport*, float, float) noexcept;

    void viewportZoom(Viewport*, float, float) noexcept;

    void viewMode(Viewport* vp, ViewMode mode) noexcept;

    ViewMode viewMode(Viewport* vp) const noexcept;

    void drawMode(Viewport* vp, DrawMode mode) noexcept;

    DrawMode drawMode(Viewport* vp) const noexcept;

    void mousePressEvent(Viewport* vp, CoreEvent event) noexcept;

    void mouseMoveEvent(Viewport*, CoreEvent event) noexcept;

    void mouseDragEvent(Viewport*, CoreEvent event) noexcept;

    void mouseReleaseEvent(Viewport*, CoreEvent event) noexcept;

    void mouseWheelEvent(Viewport* vp, CoreEvent event) noexcept;

    bool keyPressEvent(Viewport* vp, CoreEvent event) noexcept;

    void setActiveViewport(Viewport* vp) noexcept;

    void setActiveTool(const std::string& name);

    bool runCommand(const std::string& name);

    bool runAction(const std::string& name, int value = 0);

    void selectionMode(SelectionMode mode);

    SelectionMode selectionMode() const;

    SceneStats sceneStats() const noexcept;

    void idle();

    bool needsRender() noexcept;

    void render(Viewport* vp, VkCommandBuffer cmd = nullptr, uint32_t frameIndex = 0);

    // Viewport* activeViewport() const;

    // File commands called from MainWindow
    bool requestNew() noexcept;
    bool requestExit() noexcept;

    bool newFile();
    bool openFile(const std::filesystem::path& path);
    bool saveFile();
    bool saveFileAs(const std::filesystem::path& path);
    bool importFile(const std::filesystem::path& path);
    bool exportFile(const std::filesystem::path& path);

    /// @return Current document file path, or empty if unnamed/unsaved
    std::string filePath() const noexcept;

    [[nodiscard]] MaterialEditor*       materialEditor() noexcept;
    [[nodiscard]] const MaterialEditor* materialEditor() const noexcept;

    void assignMaterial(int32_t materialId) noexcept;

    bool toolPropertyGroupChanged() noexcept;

    bool toolPropertyValuesChanged() noexcept;

    const std::vector<std::unique_ptr<PropertyBase>>& toolProperties() const noexcept;

    // Scene grid
    void showSceneGrid(bool show) noexcept;

    bool showSceneGrid() const noexcept;

private:
    std::vector<std::unique_ptr<Viewport>> m_viewports;
    std::unique_ptr<Scene>                 m_scene;
    std::unique_ptr<CoreDocument>          m_document;
    std::unique_ptr<MaterialEditor>        m_materialEditor;

    glm::vec3 m_pan{0.f};
    glm::vec3 m_rot{ -30.f, 30.f, 0.f };
    float     m_dist = -6.f;

    // Tools & commands
    std::unique_ptr<Tool> m_activeTool;

    ItemFactory<Tool> m_toolFactory;
    ItemFactory<Command> m_commandFactory;
};
