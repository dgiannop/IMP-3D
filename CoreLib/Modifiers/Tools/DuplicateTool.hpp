#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

#include "Tool.hpp"

class Scene;
class Viewport;
class SceneMesh;
struct CoreEvent;

class DuplicateTool final : public Tool
{
public:
    DuplicateTool();
    ~DuplicateTool() override = default;

    void activate(Scene* scene) override;
    void propertiesChanged(Scene* scene) override;

    void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event) override;
    void mouseMove(Viewport* vp, Scene* scene, const CoreEvent& event) override;
    void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event) override;
    void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event) override;

    bool keyPress(Viewport* vp, Scene* scene, const CoreEvent& event) override;

private:
    struct MoveSet
    {
        SceneMesh*             mesh       = nullptr;
        std::vector<int32_t>   movedVerts = {}; // SysMesh vert indices (the new verts we created)
        std::vector<glm::vec3> startPos   = {}; // parallel array to movedVerts
    };

private:
    void beginDuplicate(Scene* scene);
    void applyDelta(Scene* scene, Viewport* vp, const CoreEvent& event);

private:
    bool m_active = false;

    // mouse anchor
    glm::vec2 m_mouseStartPx = {0.0f, 0.0f};

    // cached delta (world)
    glm::vec3 m_delta = {0.0f, 0.0f, 0.0f};

    // one MoveSet per SceneMesh that we duplicated into
    std::vector<MoveSet> m_sets = {};
};
