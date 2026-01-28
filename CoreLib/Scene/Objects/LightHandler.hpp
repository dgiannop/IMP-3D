//============================================================
// LightHandler.hpp
//============================================================
#pragma once

#include <string>
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
 *  - No undo/redo yet (add later by wrapping mutations in History actions).
 *  - No SceneObject behavior here; that goes in SceneLight (wrapper).
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

    [[nodiscard]] LightId createLight(const std::string& name, LightType type) noexcept;
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
