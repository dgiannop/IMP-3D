#pragma once

#include <SysCounter.hpp>
#include <SysMesh.hpp>
#include <memory>
#include <string>
#include <string_view>

#include "MeshGpuResources.hpp"
#include "SceneObject.hpp"
#include "SubdivEvaluator.hpp"

class Viewport;
class Scene;

class SceneMesh final : public SceneObject
{
public:
    SceneMesh();
    SceneMesh(std::string_view name);

    ~SceneMesh();

    std::string_view name() const noexcept;

    void idle(Scene* scene) override;

    glm::mat4 model() const noexcept override;

    void model(const glm::mat4& mtx) noexcept;

    bool visible() const noexcept override;

    void visible(bool value) noexcept override;

    bool selected() const noexcept override;

    void selected(bool value) noexcept override;

    SysMesh* sysMesh();

    const SysMesh* sysMesh() const;

    MeshGpuResources* gpu() const noexcept;

    void gpu(std::unique_ptr<MeshGpuResources> gpu);

    void subdivisionLevel(int levelDelta);

    const int subdivisionLevel() const;

    SubdivEvaluator* subdiv() noexcept;

    const SubdivEvaluator* subdiv() const noexcept;

    SysCounterPtr changeCounter() const noexcept;

private:
    std::unique_ptr<SysMesh>          m_mesh;
    std::unique_ptr<MeshGpuResources> m_gpu;
    glm::mat4                         m_model    = glm::mat4{1.f};
    bool                              m_visible  = true;
    bool                              m_selected = true;
    std::string                       m_name;
    SysCounterPtr                     m_changeCounter    = std::make_shared<SysCounter>();
    SubdivEvaluator                   m_subdiv           = {};
    int                               m_subdivisionLevel = 0;
};
