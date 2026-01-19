#include "Viewport.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

Viewport::Viewport(glm::vec3& pan, glm::vec3& rot, float& dist) :
    m_pan(pan),
    m_rot(rot),
    m_dist(dist),
    m_changeCounter(std::make_shared<SysCounter>())
{
}

void Viewport::initialize() const noexcept
{
    // No viewport-owned Vulkan resources.
}

void Viewport::resize(int32_t width, int32_t height) noexcept
{
    // Clamp to non-negative. A 0-sized viewport is treated as invalid for projection.
    width  = std::max<int32_t>(0, width);
    height = std::max<int32_t>(0, height);

    if (m_width == width && m_height == height)
        return;

    m_width  = width;
    m_height = height;
    m_changeCounter->change();
}

void Viewport::clearColor(const glm::vec4& color) noexcept
{
    if (m_clearColor != color)
    {
        m_clearColor = color;
        m_changeCounter->change();
    }
}

glm::vec4 Viewport::clearColor() const noexcept
{
    return m_clearColor;
}

void Viewport::clear() const
{
    // Vulkan clears are performed by the renderer via render pass clear values.
}

void Viewport::cleanup() noexcept
{
    // No viewport-owned Vulkan resources.
}

ViewMode Viewport::viewMode() const noexcept
{
    return m_viewMode;
}

DrawMode Viewport::drawMode() const noexcept
{
    return m_drawMode;
}

void Viewport::viewMode(ViewMode mode) noexcept
{
    if (m_viewMode == mode)
        return;

    m_viewMode = mode;
    m_changeCounter->change();
}

void Viewport::drawMode(DrawMode mode) noexcept
{
    if (m_drawMode == mode)
        return;

    m_drawMode = mode;
    m_changeCounter->change();
}

void Viewport::pan(float deltaX, float deltaY) noexcept
{
    // Pan by offsetting the projected pan anchor and unprojecting back at the same depth.
    glm::vec3 pt = project(m_pan);
    pt.x -= deltaX;
    pt.y -= deltaY;
    m_pan = unproject(pt);

    m_changeCounter->change();
}

void Viewport::zoom(float deltaX, float /*deltaY*/) noexcept
{
    // Zoom modifies camera distance. The scale factor is UI-tuned.
    m_dist += deltaX / 100.0f;
    m_changeCounter->change();
}

void Viewport::rotate(float deltaX, float deltaY) noexcept
{
    // Rotation stored in degrees. The mapping matches the existing interaction behavior.
    m_rot.x -= deltaX;
    m_rot.y -= deltaY;
    m_changeCounter->change();
}

glm::vec3 Viewport::project(const glm::vec3& world) const noexcept
{
    if (m_width <= 0 || m_height <= 0)
        return glm::vec3(0.0f);

    const glm::vec4 clip = m_matViewProj * glm::vec4(world, 1.0f);

    // clip.w == 0 indicates an invalid perspective divide.
    if (clip.w == 0.0f)
        return glm::vec3(0.0f);

    // NDC is derived by perspective divide. With RH_ZO, ndc.z is [0,1] for visible points.
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;

    const float w = static_cast<float>(m_width);
    const float h = static_cast<float>(m_height);

    // Projection includes a Y flip, so ndc.y already matches screen y-down coordinates.
    const float x = (ndc.x * 0.5f + 0.5f) * w;
    const float y = (ndc.y * 0.5f + 0.5f) * h;

    return glm::vec3(x, y, ndc.z);
}

glm::vec3 Viewport::unproject(const glm::vec3& screen) const noexcept
{
    if (m_width <= 0 || m_height <= 0)
        return glm::vec3(0.0f);

    const float w = static_cast<float>(m_width);
    const float h = static_cast<float>(m_height);

    // Convert from pixel coordinates to NDC.
    const float ndcX = (screen.x / w) * 2.0f - 1.0f;
    const float ndcY = (screen.y / h) * 2.0f - 1.0f;

    // Vulkan depth is already [0,1].
    const float ndcZ = screen.z;

    const glm::vec4 clip(ndcX, ndcY, ndcZ, 1.0f);
    const glm::vec4 worldH = m_matInvViewProj * clip;

    // worldH.w == 0 indicates an invalid homogeneous coordinate.
    if (worldH.w == 0.0f)
        return glm::vec3(0.0f);

    return glm::vec3(worldH) / worldH.w;
}

float Viewport::pixelScale(const glm::vec3& world) const noexcept
{
    // Uses a stable 10px step to reduce jitter from precision loss at high zoom.
    const glm::vec3 p0 = project(world);
    const glm::vec3 p1 = p0 + glm::vec3(10.0f, 0.0f, 0.0f);

    const glm::vec3 w0 = unproject(p0);
    const glm::vec3 w1 = unproject(p1);

    // Convert 10-pixel distance to approximately 1 pixel.
    return glm::length(w1 - w0) * 0.1f;
}

float Viewport::pixelScale() const noexcept
{
    return pixelScale(m_pan);
}

float Viewport::pointDepth(const glm::vec3& point) const noexcept
{
    // NDC depth is derived from clip-space z/w. With RH_ZO it matches Vulkan depth range [0,1].
    const glm::vec4 pt = m_matViewProj * glm::vec4(point, 1.0f);

    // w <= 0 indicates points behind the camera or invalid perspective divide.
    if (pt.w <= 0.0f)
        return -1.0f;

    return pt.z / pt.w;
}

float Viewport::linearDepth(const glm::vec3& point) const noexcept
{
    // View-space depth where forward is -Z; returns positive distance in front of camera.
    const glm::vec4 v = m_matView * glm::vec4(point, 1.0f);
    return -v.z;
}

glm::mat4 Viewport::frustum(float fovDeg,
                            float viewportWidth,
                            float viewportHeight,
                            float nearPlane,
                            float farPlane) const noexcept
{
    // Optional resize-invariant projection based on a reference focal length in pixels.
    constexpr float kRefHeightPx = 2000.0f;

    if (viewportWidth <= 1.0f || viewportHeight <= 1.0f)
        return glm::mat4(1.0f);

    const float aspectRatio = viewportWidth / viewportHeight;

    const float fovRad = glm::radians(fovDeg);
    const float fPx    = 0.5f * kRefHeightPx / glm::tan(0.5f * fovRad);

    const float top    = nearPlane * (0.5f * viewportHeight / fPx);
    const float bottom = -top;
    const float right  = top * aspectRatio;
    const float left   = -right;

    glm::mat4 proj = glm::frustumRH_ZO(left, right, bottom, top, nearPlane, farPlane);

    // Flips Y to align NDC with screen y-down coordinates.
    proj[1][1] *= -1.0f;

    return proj;
}

un::ray Viewport::ray(float x, float y) const
{
    // Constructs a ray by unprojecting near and far depths in Vulkan [0,1].
    un::ray r = {};
    r.org     = unproject(glm::vec3(x, y, 0.0f));
    r.dir     = unproject(glm::vec3(x, y, 1.0f)) - r.org;

    // Uses a safe normalization helper to avoid NaNs.
    r.dir = un::safe_normalize(r.dir);

    // Inverse direction is used for slab/BVH tests; INF is acceptable for 0 components.
    r.inv = 1.0f / r.dir;

    return r;
}

bool Viewport::rayPlaneHit(float            x,
                           float            y,
                           const glm::vec3& planePoint,
                           const glm::vec3& planeNormal,
                           glm::vec3&       outHit) const noexcept
{
    // Construct the world-space ray from screen coordinates.
    const un::ray r = ray(x, y);

    // Compute denominator of the ray-plane intersection equation.
    const float denom = glm::dot(planeNormal, r.dir);

    // Ray is parallel to the plane (or nearly so).
    if (std::abs(denom) < 1e-6f)
        return false;

    // Solve for ray parameter t.
    const float t = glm::dot(planePoint - r.org, planeNormal) / denom;

    // Intersection lies behind the ray origin.
    if (t < 0.0f)
        return false;

    outHit = r.org + r.dir * t;
    return true;
}

bool Viewport::rayViewPlaneHit(float            x,
                               float            y,
                               const glm::vec3& planePoint,
                               glm::vec3&       outHit) const noexcept
{
    // The view plane is perpendicular to the camera forward direction.
    const glm::vec3 planeNormal = viewDirection();

    return rayPlaneHit(x, y, planePoint, planeNormal, outHit);
}

glm::vec3 Viewport::cameraPosition() const
{
    // Extracts camera translation from inverse(view). Callers should cache if used frequently.
    return glm::vec3(glm::inverse(m_matView)[3]);
}

glm::vec3 Viewport::viewDirection() const
{
    // Forward is -Z in view space.
    const glm::vec4 forwardView(0.f, 0.f, -1.f, 0.f);
    const glm::vec4 forwardWorld = glm::inverse(m_matView) * forwardView;
    return glm::normalize(glm::vec3(forwardWorld));
}

glm::vec3 Viewport::rightDirection() const
{
    // Right is +X in view space.
    const glm::vec4 rightView(1.f, 0.f, 0.f, 0.f);
    const glm::vec4 rightWorld = glm::inverse(m_matView) * rightView;
    return glm::normalize(glm::vec3(rightWorld));
}

glm::vec3 Viewport::upDirection() const
{
    // Up is +Y in view space.
    const glm::vec4 upView(0.f, 1.f, 0.f, 0.f);
    const glm::vec4 upWorld = glm::inverse(m_matView) * upView;
    return glm::normalize(glm::vec3(upWorld));
}

int32_t Viewport::width() const noexcept
{
    return m_width;
}

int32_t Viewport::height() const noexcept
{
    return m_height;
}

float Viewport::aspect() const noexcept
{
    const float h = static_cast<float>(m_height);
    return (h > 0.0f) ? (static_cast<float>(m_width) / h) : 1.0f;
}

const glm::mat4& Viewport::projection() const noexcept
{
    return m_matProj;
}

const glm::mat4& Viewport::view() const noexcept
{
    return m_matView;
}

const glm::mat4& Viewport::model() const noexcept
{
    return m_matModel;
}

SysCounterPtr Viewport::changeCounter() noexcept
{
    return m_changeCounter;
}

// -----------------------------------------------------------------------------
// apply()
// -----------------------------------------------------------------------------

void Viewport::apply() noexcept
{
    const float w = static_cast<float>(m_width);
    const float h = static_cast<float>(m_height);

    // Model is identity for camera-only viewport utilities.
    m_matModel = glm::mat4(1.0f);

    // Camera translation along +Z (camera forward is -Z in view space).
    m_matView = glm::translate(glm::mat4(1.0f), glm::vec3(0.f, 0.f, m_dist));

    constexpr float nearPlane = 0.1f;
    constexpr float farPlane  = 5000.0f;

    // Projection is Vulkan-friendly (RH + ZO + Y flip).
    if (m_viewMode == ViewMode::PERSPECTIVE)
    {
        constexpr float fovDeg = 45.0f;

        const float aspectRatio = (h > 0.0f) ? (w / h) : 1.0f;

        m_matProj = glm::perspectiveRH_ZO(glm::radians(fovDeg), aspectRatio, nearPlane, farPlane);

        // Flips Y so NDC maps to screen y-down coordinates in project/unproject.
        m_matProj[1][1] *= -1.0f;
    }
    else
    {
        // Ortho size scales with distance to provide a DCC-like feel.
        const float orthoHalfH  = std::max(1e-6f, std::abs(m_dist) * 0.4f);
        const float aspectRatio = (h > 0.0f) ? (w / h) : 1.0f;
        const float orthoHalfW  = orthoHalfH * aspectRatio;

        m_matProj = glm::orthoRH_ZO(-orthoHalfW, orthoHalfW, -orthoHalfH, orthoHalfH, nearPlane, farPlane);
        m_matProj[1][1] *= -1.0f;
    }

    // View orientation based on view mode.
    switch (m_viewMode)
    {
        case ViewMode::PERSPECTIVE:
            m_matView = glm::rotate(m_matView, glm::radians(m_rot.y), glm::vec3(1.f, 0.f, 0.f));
            m_matView = glm::rotate(m_matView, glm::radians(m_rot.x), glm::vec3(0.f, 1.f, 0.f));
            break;

        case ViewMode::FRONT:
            break;

        case ViewMode::BACK:
            m_matView = glm::rotate(m_matView, glm::radians(180.0f), glm::vec3(0.f, 1.f, 0.f));
            break;

        case ViewMode::LEFT:
            m_matView = glm::rotate(m_matView, glm::radians(90.0f), glm::vec3(0.f, 1.f, 0.f));
            break;

        case ViewMode::RIGHT:
            m_matView = glm::rotate(m_matView, glm::radians(-90.0f), glm::vec3(0.f, 1.f, 0.f));
            break;

        case ViewMode::TOP:
            m_matView = glm::rotate(m_matView, glm::radians(90.0f), glm::vec3(1.f, 0.f, 0.f));
            break;

        case ViewMode::BOTTOM:
            m_matView = glm::rotate(m_matView, glm::radians(-90.0f), glm::vec3(1.f, 0.f, 0.f));
            break;
    }

    // Applies panning as a world-space translation.
    const glm::mat4 posMat = glm::translate(glm::mat4(1.0f), -m_pan);
    m_matView              = m_matView * posMat;

    // Caches derived matrices for project/unproject.
    m_matViewProj = m_matProj * m_matView;

    // Inversion is expected to succeed for typical camera matrices.
    // If issues arise, guard with a determinant check and fallback to identity.
    m_matInvViewProj = glm::inverse(m_matViewProj);
}
