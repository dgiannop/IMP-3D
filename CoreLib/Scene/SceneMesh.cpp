#include "SceneMesh.hpp"

#include "MeshGpuResources.hpp"

SceneMesh::SceneMesh() : m_mesh{std::make_unique<SysMesh>()}
{
}

SceneMesh::SceneMesh(std::string_view name) : m_mesh{std::make_unique<SysMesh>()}, m_name{name}
{
}

SceneMesh::~SceneMesh()
{
}

std::string_view SceneMesh::name() const noexcept
{
    return m_name;
}

void SceneMesh::idle(Scene* scene)
{
}

glm::mat4 SceneMesh::model() const noexcept
{
    return m_model;
}

void SceneMesh::model(const glm::mat4& mtx) noexcept
{
    m_model = mtx;
    m_changeCounter->change();
}

bool SceneMesh::visible() const noexcept
{
    return m_visible;
}

void SceneMesh::visible(bool value) noexcept
{
    m_visible = value;
    m_changeCounter->change();
}

bool SceneMesh::selected() const noexcept
{
    return m_selected;
}

void SceneMesh::selected(bool value) noexcept
{
    m_selected = value;
    m_changeCounter->change();
}

SysMesh* SceneMesh::sysMesh()
{
    return m_mesh.get();
}

const SysMesh* SceneMesh::sysMesh() const
{
    return m_mesh.get();
}

MeshGpuResources* SceneMesh::gpu() const noexcept
{
    return m_gpu.get();
}

void SceneMesh::gpu(std::unique_ptr<MeshGpuResources> gpu)
{
    m_gpu = std::move(gpu);
    m_changeCounter->change();
}

void SceneMesh::subdivisionLevel(int levelDelta)
{
    const int prevLevel = m_subdivisionLevel;
    m_subdivisionLevel  = std::clamp(m_subdivisionLevel + levelDelta, 0, 4);

    if (m_subdivisionLevel != prevLevel)
        m_changeCounter->change();
}

int SceneMesh::subdivisionLevel() const
{
    return m_subdivisionLevel;
}

SubdivEvaluator* SceneMesh::subdiv() noexcept
{
    return &m_subdiv;
}

const SubdivEvaluator* SceneMesh::subdiv() const noexcept
{
    return &m_subdiv;
}

SysCounterPtr SceneMesh::changeCounter() const noexcept
{
    return m_changeCounter;
}
