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

void buildGpuLightsUBO(const HeadlightSettings& headlight,
                       const Viewport&          vp,
                       const Scene*             scene,
                       GpuLightsUBO&            out) noexcept
{
    out         = {};
    out.info    = glm::uvec4(0, 0, 0, 0);
    out.ambient = glm::vec4(
        1.0f,
        1.0f,
        1.0f,
        0.25f // intentionally high
    );

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
    // 2) Scene lights (WORLD -> VIEW) - later
    // ------------------------------------------------------------
    // When you add lights to Scene, do it like this:
    //   - SceneLight stores world position/direction
    //   - Here we transform using vp.viewMatrix()
    //
    // Example transformation pattern:
    //
    // const glm::mat4 V  = vp.viewMatrix();
    // const glm::mat3 V3 = glm::mat3(V);
    //
    // for (const SceneLight& sl : scene->lights())
    // {
    //     GpuLight l{};
    //     if (sl.type == Directional)
    //     {
    //         glm::vec3 dirVS = safeNormalize(V3 * sl.directionWS);
    //         l.pos_type = vec4(0,0,0,type);
    //         l.dir_range = vec4(dirVS, 0);
    //     }
    //     else
    //     {
    //         glm::vec3 posVS = glm::vec3(V * glm::vec4(sl.positionWS, 1));
    //         l.pos_type = vec4(posVS, type);
    //         ...
    //     }
    //     l.color_intensity = vec4(sl.color, sl.intensity);
    //     appendLight(out, l);
    // }
    (void)vp;
    (void)scene;
}
