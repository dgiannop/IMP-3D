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

/**
 * @brief Scene object that owns a SysMesh and its GPU resources.
 *
 * SceneMesh is the primary renderable/editable object in the scene.
 * It bridges CPU mesh data (SysMesh) with GPU representation (MeshGpuResources),
 * and stores object-level state like transform, visibility, and selection.
 *
 * Subdivision is managed via SubdivEvaluator, with a user-controlled
 * subdivision level that can be adjusted incrementally.
 */
class SceneMesh final : public SceneObject
{
public:
    /** @brief Construct a SceneMesh with an empty name. */
    SceneMesh();

    /**
     * @brief Construct a SceneMesh with a name.
     * @param name Mesh display name
     */
    explicit SceneMesh(std::string_view name);

    /** @brief Destroy the SceneMesh and owned resources. */
    ~SceneMesh();

    /**
     * @brief Get the mesh name.
     * @return Name view (lifetime owned by this object)
     */
    [[nodiscard]] std::string_view name() const noexcept;

    /** @copydoc SceneObject::idle */
    void idle(Scene* scene) override;

    /** @copydoc SceneObject::model */
    [[nodiscard]] glm::mat4 model() const noexcept override;

    /**
     * @brief Set the object-to-world transform.
     * @param mtx Model matrix
     */
    void model(const glm::mat4& mtx) noexcept;

    /** @copydoc SceneObject::visible */
    [[nodiscard]] bool visible() const noexcept override;

    /** @copydoc SceneObject::visible(bool) */
    void visible(bool value) noexcept override;

    /** @copydoc SceneObject::selected */
    [[nodiscard]] bool selected() const noexcept override;

    /** @copydoc SceneObject::selected(bool) */
    void selected(bool value) noexcept override;

    /**
     * @brief Access the owned SysMesh.
     * @return Mutable SysMesh pointer
     */
    [[nodiscard]] SysMesh* sysMesh();

    /**
     * @brief Access the owned SysMesh (const).
     * @return Const SysMesh pointer
     */
    [[nodiscard]] const SysMesh* sysMesh() const;

    /**
     * @brief Access GPU resources for this mesh.
     * @return MeshGpuResources pointer (may be null until assigned)
     */
    [[nodiscard]] MeshGpuResources* gpu() const noexcept;

    /**
     * @brief Assign GPU resources for this mesh.
     * @param gpu New GPU resource owner
     */
    void gpu(std::unique_ptr<MeshGpuResources> gpu);

    /**
     * @brief Adjust subdivision level by a delta.
     * @param levelDelta Relative subdivision change (+/-)
     */
    void subdivisionLevel(int levelDelta);

    /**
     * @brief Get current subdivision level.
     * @return Subdivision level
     */
    [[nodiscard]] int subdivisionLevel() const;

    /**
     * @brief Access subdivision evaluator.
     * @return SubdivEvaluator pointer
     */
    [[nodiscard]] SubdivEvaluator* subdiv() noexcept;

    /**
     * @brief Access subdivision evaluator (const).
     * @return SubdivEvaluator pointer
     */
    [[nodiscard]] const SubdivEvaluator* subdiv() const noexcept;

    /**
     * @brief Change counter for mesh object-level changes.
     *
     * Typically used to signal changes that require dependent systems
     * to refresh (e.g., GPU rebuild, BVH/RT updates, UI refresh).
     *
     * @return Shared change counter
     */
    [[nodiscard]] SysCounterPtr changeCounter() const noexcept;

private:
    /** @brief CPU mesh data (authoritative). */
    std::unique_ptr<SysMesh> m_mesh;

    /** @brief GPU-side resources for raster/RT rendering. */
    std::unique_ptr<MeshGpuResources> m_gpu;

    /** @brief Object-to-world transform. */
    glm::mat4 m_model = glm::mat4{1.f};

    /** @brief Visibility flag. */
    bool m_visible = true;

    /** @brief Selection flag. */
    bool m_selected = true;

    /** @brief Mesh name storage. */
    std::string m_name;

    /** @brief Per-object change counter. */
    SysCounterPtr m_changeCounter = std::make_shared<SysCounter>();

    /** @brief Subdivision evaluator state. */
    SubdivEvaluator m_subdiv = {};

    /** @brief Current subdivision level. */
    int m_subdivisionLevel = 0;
};
