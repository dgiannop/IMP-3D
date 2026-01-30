//============================================================
// LightHandler.hpp
//============================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "HoleList.hpp"
#include "Light.hpp"
#include "SysCounter.hpp"

/**
 * @brief Owns scene Light data with stable IDs (HoleList-backed).
 *
 * Pattern matches ImageHandler / MaterialHandler:
 *  - Light.hpp      : pure data (serializable, undo-friendly)
 *  - LightHandler   : ownership + creation/destruction + stable IDs
 *
 * Notes:
 *  - Undo/redo is intentionally not implemented yet; it can be added later by
 *    wrapping mutations in History actions.
 *  - SceneObject behavior does not belong here; that is handled by a SceneLight
 *    wrapper that references LightId and provides transform/selection/visibility.
 */
class LightHandler final
{
public:
    LightHandler();
    ~LightHandler() = default;

    LightHandler(const LightHandler&)            = delete;
    LightHandler& operator=(const LightHandler&) = delete;

    LightHandler(LightHandler&&) noexcept            = default;
    LightHandler& operator=(LightHandler&&) noexcept = default;

public:
    void clear() noexcept;

    /**
     * @brief Creates a new Light with a stable ID.
     * @param name User-facing display name.
     * @param type Light type (Directional/Point/Spot).
     * @return Stable LightId for the created light.
     */
    [[nodiscard]] LightId createLight(std::string_view name, LightType type) noexcept;

    /**
     * @brief Creates a new Light by cloning a source Light.
     * @param src Source light data.
     * @return Stable LightId for the created light.
     */
    [[nodiscard]] LightId createLight(const Light& src) noexcept;

    bool destroyLight(LightId id) noexcept;

    [[nodiscard]] Light*       light(LightId id) noexcept;
    [[nodiscard]] const Light* light(LightId id) const noexcept;

    /**
     * @brief Returns all alive light IDs.
     *
     * Intended for UI lists and renderer iteration.
     * Use light(id) to access the Light itself.
     */
    [[nodiscard]] std::vector<LightId> allLights() const;

    [[nodiscard]] bool setEnabled(LightId id, bool enabled) noexcept;

    [[nodiscard]] SysCounterPtr changeCounter() const noexcept { return m_changeCounter; }

private:
    HoleList<Light> m_lights = {};
    SysCounterPtr   m_changeCounter;

private:
    static void sanitize(Light& l) noexcept;
};
