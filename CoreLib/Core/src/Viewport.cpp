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

void Viewport::zoom(float deltaX, float deltaY) noexcept
{
    const float delta = (std::abs(deltaY) > std::abs(deltaX)) ? deltaY : deltaX;

    // How far the camera currently is from the pan anchor.
    const float absDist = std::abs(m_dist);

    // Travel distance this scroll step: proportional to current distance so
    // zoom feels linear on screen at any scale. Floor prevents stalling when
    // already very close.
    constexpr float kZoomSpeed  = 0.0015f;
    constexpr float kMinAbsStep = 0.0001f;
    const float     travel      = std::max(absDist * kZoomSpeed, kMinAbsStep) * delta;

    // Move the pan anchor forward along the view direction by the same amount.
    // This dollies the orbit pivot toward the geometry so the camera actually
    // closes in on what is visible, regardless of where it sits in world space.
    // m_matInvView is valid after the first apply() call, which always precedes
    // any user interaction.
    const glm::vec3 forward = glm::normalize(glm::vec3(m_matInvView * glm::vec4(0.f, 0.f, -1.f, 0.f)));
    m_pan += forward * travel;

    // Also shrink the orbit radius so the camera physically moves closer.
    // Clamp to a tiny minimum so we never reach zero or flip sign.
    constexpr float kMinDist = 0.0001f;
    constexpr float kMaxDist = 100000.f;
    m_dist                   = glm::clamp(m_dist + travel, -kMaxDist, -kMinDist);

    m_changeCounter->change();
}

void Viewport::rotate(float deltaX, float deltaY) noexcept
{
    m_rot.x -= deltaX;
    m_rot.y -= deltaY;
    m_changeCounter->change();
}

glm::vec3 Viewport::project(const glm::vec3& world) const noexcept
{
    if (m_width <= 0 || m_height <= 0)
        return glm::vec3(0.0f);

    const glm::vec4 clip = m_matViewProj * glm::vec4(world, 1.0f);

    if (clip.w == 0.0f)
        return glm::vec3(0.0f);

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;

    const float w = static_cast<float>(m_width);
    const float h = static_cast<float>(m_height);

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

    const float ndcX = (screen.x / w) * 2.0f - 1.0f;
    const float ndcY = (screen.y / h) * 2.0f - 1.0f;
    const float ndcZ = screen.z;

    const glm::vec4 clip(ndcX, ndcY, ndcZ, 1.0f);
    const glm::vec4 worldH = m_matInvViewProj * clip;

    if (worldH.w == 0.0f)
        return glm::vec3(0.0f);

    return glm::vec3(worldH) / worldH.w;
}

float Viewport::pixelScale(const glm::vec3& world) const noexcept
{
    const glm::vec3 p0 = project(world);
    const glm::vec3 p1 = p0 + glm::vec3(10.0f, 0.0f, 0.0f);

    const glm::vec3 w0 = unproject(p0);
    const glm::vec3 w1 = unproject(p1);

    return glm::length(w1 - w0) * 0.1f;
}

float Viewport::pixelScale() const noexcept
{
    return pixelScale(m_pan);
}

float Viewport::pointDepth(const glm::vec3& point) const noexcept
{
    const glm::vec4 pt = m_matViewProj * glm::vec4(point, 1.0f);

    if (pt.w <= 0.0f)
        return -1.0f;

    return pt.z / pt.w;
}

float Viewport::linearDepth(const glm::vec3& point) const noexcept
{
    const glm::vec4 v = m_matView * glm::vec4(point, 1.0f);
    return -v.z;
}

glm::mat4 Viewport::frustum(float fovDeg,
                            float viewportWidth,
                            float viewportHeight,
                            float nearPlane,
                            float farPlane) const noexcept
{
    constexpr float kRefHeightPx = 2000.0f;

    if (viewportWidth <= 1.0f || viewportHeight <= 1.0f)
        return glm::mat4(1.0f);

    const float fovRad = glm::radians(fovDeg);
    const float fPx    = 0.5f * kRefHeightPx / glm::tan(0.5f * fovRad);

    const float top    = nearPlane * (0.5f * viewportHeight / fPx);
    const float bottom = -top;
    const float right  = top * (viewportWidth / viewportHeight);
    const float left   = -right;

    glm::mat4 proj = glm::frustumRH_ZO(left, right, bottom, top, nearPlane, farPlane);
    proj[1][1] *= -1.0f;

    return proj;
}

un::ray Viewport::ray(float x, float y) const
{
    un::ray r = {};
    r.org     = unproject(glm::vec3(x, y, 0.0f));
    r.dir     = unproject(glm::vec3(x, y, 1.0f)) - r.org;
    r.dir     = un::safe_normalize(r.dir);
    r.inv     = 1.0f / r.dir;

    return r;
}

bool Viewport::rayPlaneHit(float            x,
                           float            y,
                           const glm::vec3& planePoint,
                           const glm::vec3& planeNormal,
                           glm::vec3&       outHit) const noexcept
{
    const un::ray r = ray(x, y);

    const float denom = glm::dot(planeNormal, r.dir);

    if (std::abs(denom) < 1e-6f)
        return false;

    const float t = glm::dot(planePoint - r.org, planeNormal) / denom;

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
    const glm::vec3 planeNormal = viewDirection();
    return rayPlaneHit(x, y, planePoint, planeNormal, outHit);
}

glm::vec3 Viewport::cameraPosition() const
{
    return glm::vec3(m_matInvView[3]);
}

glm::vec3 Viewport::viewDirection() const
{
    const glm::vec4 forwardView(0.f, 0.f, -1.f, 0.f);
    const glm::vec4 forwardWorld = m_matInvView * forwardView;
    return glm::normalize(glm::vec3(forwardWorld));
}

glm::vec3 Viewport::rightDirection() const
{
    const glm::vec4 rightView(1.f, 0.f, 0.f, 0.f);
    const glm::vec4 rightWorld = m_matInvView * rightView;
    return glm::normalize(glm::vec3(rightWorld));
}

glm::vec3 Viewport::upDirection() const
{
    const glm::vec4 upView(0.f, 1.f, 0.f, 0.f);
    const glm::vec4 upWorld = m_matInvView * upView;
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

    m_matModel = glm::mat4(1.0f);

    m_matView = glm::translate(glm::mat4(1.0f), glm::vec3(0.f, 0.f, m_dist));

    // Near plane must stay large enough to preserve depth buffer precision.
    // Too small a near/far ratio causes back faces to bleed through front faces.
    // 0.01 is a safe floor for 32-bit depth with geometry up to 100000 units away.
    const float absDist   = std::abs(m_dist);
    const float nearPlane = std::max(absDist * 0.01f, 0.01f);
    const float farPlane  = std::max(absDist * 10.0f, nearPlane * 10000.0f);

    if (m_viewMode == ViewMode::PERSPECTIVE)
    {
        constexpr float fovDeg      = 45.0f;
        const float     aspectRatio = (h > 0.0f) ? (w / h) : 1.0f;

        m_matProj = glm::perspectiveRH_ZO(glm::radians(fovDeg), aspectRatio, nearPlane, farPlane);
        m_matProj[1][1] *= -1.0f;
    }
    else
    {
        const float orthoHalfH  = std::max(1e-6f, absDist * 0.4f);
        const float aspectRatio = (h > 0.0f) ? (w / h) : 1.0f;
        const float orthoHalfW  = orthoHalfH * aspectRatio;

        m_matProj = glm::orthoRH_ZO(-orthoHalfW, orthoHalfW, -orthoHalfH, orthoHalfH, nearPlane, farPlane);
        m_matProj[1][1] *= -1.0f;
    }

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

    // Pan is the orbit pivot in world space.
    m_matView = m_matView * glm::translate(glm::mat4(1.0f), -m_pan);

    m_matViewProj    = m_matProj * m_matView;
    m_matInvView     = glm::inverse(m_matView);
    m_matInvViewProj = glm::inverse(m_matViewProj);
}
