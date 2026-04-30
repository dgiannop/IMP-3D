//=============================================================================
// TreeGeneratorTool.hpp
//=============================================================================
#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "PrimitiveGizmo.hpp"
#include "Scene.hpp"
#include "Tool.hpp"

class SceneMesh;
class SysMesh;

class TreeGeneratorTool final : public Tool
{
public:
    TreeGeneratorTool();
    ~TreeGeneratorTool() override = default;

    void activate(Scene* scene) override;
    void propertiesChanged(Scene* scene) override;

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event) override;
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event) override;
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event) override;

    void            render(Viewport* vp, Scene* scene) override;
    OverlayHandler* overlayHandler() override;

public:
    struct Settings
    {
        int32_t seed = 1;
        int32_t lod  = 0;

        float height      = 5.5f;
        float crownRadius = 2.0f;
        float trunkRadius = 0.28f;

        int32_t attractionPoints = 450;
        int32_t maxIterations    = 140;

        float killDistance      = 0.18f;
        float influenceDistance = 1.05f;
        float growStep          = 0.16f;

        float trunkHeightRatio = 0.32f;
        float upwardBias       = 0.25f;
        float randomness       = 0.16f;

        int32_t tubeSides = 8;

        bool    createLeaves = true;
        int32_t leafCards    = 350;
        float   leafSize     = 0.18f;

        uint32_t barkMaterial = 0;
        uint32_t leafMaterial = 1;
    };

    static void generateTree(
        SysMesh*          mesh,
        const Settings&   settings,
        const glm::vec3&  center,
        const glm::ivec3& axis);

private:
    SceneMesh* m_sceneMesh = nullptr;

    Settings m_settings;

    glm::vec3  m_center{0.0f};
    glm::vec3  m_size{1.0f};
    glm::ivec3 m_axis{0, 1, 0};

    PrimitiveGizmo m_gizmo{&m_center, &m_size};
};
