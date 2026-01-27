#include "GpuLights.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>

#include "Scene.hpp"
#include "Viewport.hpp"

// local helper: append one light if capacity allows
namespace
{
    inline void appendLight(GpuLightsUBO& ubo, const GpuLight& l) noexcept
    {
        const uint32_t n = ubo.info.x;
        if (n >= kMaxGpuLights)
            return;

        ubo.lights[n] = l;
        ubo.info.x    = n + 1u;
    }

    inline glm::vec3 safeNormalize(const glm::vec3& v) noexcept
    {
        const float len2 = glm::dot(v, v);
        if (len2 <= 1e-20f)
            return glm::vec3(0, 0, -1);
        return v * (1.0f / std::sqrt(len2));
    }
} // namespace

/* later

// 1) modeling headlight (optional)
appendLight(...);

// 2) scene lights
for (const SceneLight& sl : scene->lights())
{
    GpuLight gl = {};
    // world → view transform
    appendLight(out, gl);
} The call site does not change at all.
 */
void buildGpuLightsUBO(const HeadlightSettings& headlight,
                       const Viewport& /*vp*/,
                       const Scene* /*scene*/,
                       GpuLightsUBO& out) noexcept
{
    out         = {};
    out.info    = glm::uvec4(0, 0, 0, 0);
    out.ambient = glm::vec4(1.f, 1.f, 1.f, 0.25f);

    // ------------------------------------------------------------
    // 1) Headlight (VIEW SPACE) - follows camera by design
    // ------------------------------------------------------------
    if (headlight.enabled)
    {
        GpuLight l{};
        l.pos_type        = glm::vec4(0, 0, 0, float(GpuLightType::Directional)); // position unused
        l.dir_range       = glm::vec4(safeNormalize(headlight.dirVS), 0.0f);
        l.color_intensity = glm::vec4(headlight.color, headlight.intensity);
        l.spot_params     = glm::vec4(0, 0, 0, 0);
        appendLight(out, l);
    }

    // ------------------------------------------------------------
    // 2) Scene lights (WORLD -> VIEW) - optional
    // ------------------------------------------------------------
    // When SceneLight support is added:
    //   - SceneLight stores world-space position/direction.
    //   - Lights are transformed into view space using the viewport view matrix.
    //   - Direction vectors use the upper-left 3x3 of the view matrix.
    //   - Positions use the full 4x4 view transform.
    //
}
