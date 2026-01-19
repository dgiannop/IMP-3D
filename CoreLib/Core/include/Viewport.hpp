// ============================================================================
// Viewport.hpp  (Vulkan conventions: RH + ZO + projection Y-flip)
// ============================================================================

#pragma once

#include <SysCounter.hpp>
#include <cstdint>
#include <glm/glm.hpp>

#include "CoreTypes.hpp"
#include "CoreUtilities.hpp"

/**
 * @brief Viewport camera state and matrix utilities (Vulkan conventions).
 *
 * Conventions:
 *  - Right-handed view/projection.
 *  - Clip/NDC Z range is [0, 1] (ZO).
 *  - Projection matrix is Y-flipped so screen space is top-left origin, Y down.
 *
 * Screen space for project/unproject/ray:
 *  - x/y are pixels, origin top-left, y down.
 *  - z is depth in [0,1] (Vulkan depth semantics).
 */
class Viewport
{
public:
    Viewport() = delete;

    /**
     * @brief Constructs a viewport using shared camera state references.
     * @param pan  Shared world-space pan anchor.
     * @param rot  Shared rotation (degrees): rot.x yaw, rot.y pitch.
     * @param dist Shared distance along view translation (+Z in this setup).
     */
    explicit Viewport(glm::vec3& pan, glm::vec3& rot, float& dist);

    /**
     * @brief Initializes viewport-side state.
     *
     * Rendering resources are owned by the renderer; this method is a no-op by default.
     * It remains as a symmetry hook for future extensions.
     */
    void initialize() const noexcept;

    /**
     * @brief Resizes the viewport in pixels.
     * @param width  New width in pixels (clamped to >= 0).
     * @param height New height in pixels (clamped to >= 0).
     */
    void resize(int32_t width, int32_t height) noexcept;

    /**
     * @brief Sets the background clear color.
     *
     * In Vulkan this value is typically consumed by the render pass clear values.
     */
    void clearColor(const glm::vec4& color) noexcept;

    /**
     * @brief Returns the current clear color.
     */
    [[nodiscard]] glm::vec4 clearColor() const noexcept;

    /**
     * @brief Clears the viewport.
     *
     * Vulkan clears are performed via render pass clear values; this is a no-op.
     */
    void clear() const;

    /**
     * @brief Cleans up viewport-side state.
     *
     * Rendering resources are owned by the renderer; this is a no-op by default.
     */
    void cleanup() noexcept;

    /**
     * @brief Returns the current view mode.
     */
    [[nodiscard]] ViewMode viewMode() const noexcept;

    /**
     * @brief Returns the current draw mode.
     */
    [[nodiscard]] DrawMode drawMode() const noexcept;

    /**
     * @brief Sets the view mode and marks the viewport as changed.
     */
    void viewMode(ViewMode mode) noexcept;

    /**
     * @brief Sets the draw mode and marks the viewport as changed.
     */
    void drawMode(DrawMode mode) noexcept;

    /**
     * @brief Pans by a pixel delta in screen space.
     *
     * The pan anchor is projected to screen, offset by delta, and unprojected back,
     * preserving its depth for consistent panning behavior.
     */
    void pan(float deltaX, float deltaY) noexcept;

    /**
     * @brief Zooms by a pixel delta in screen space.
     *
     * This modifies the shared distance reference.
     */
    void zoom(float deltaX, float deltaY) noexcept;

    /**
     * @brief Rotates by a pixel delta in screen space.
     *
     * This modifies the shared rotation reference (degrees).
     */
    void rotate(float deltaX, float deltaY) noexcept;

    /**
     * @brief Projects a world-space point to screen space.
     * @return (x,y) in pixels (top-left origin, y down), z in [0,1].
     */
    [[nodiscard]] glm::vec3 project(const glm::vec3& world) const noexcept;

    /**
     * @brief Unprojects a screen-space point to world space.
     * @param screen (x,y) in pixels (top-left origin), z in [0,1].
     * @return World-space point. Returns (0,0,0) if viewport is invalid.
     */
    [[nodiscard]] glm::vec3 unproject(const glm::vec3& screen) const noexcept;

    /**
     * @brief Estimates world units per pixel around a world-space point.
     *
     * Uses a 10-pixel offset for stability and converts to ~1 pixel scale.
     */
    [[nodiscard]] float pixelScale(const glm::vec3& world) const noexcept;

    /**
     * @brief Estimates world units per pixel around the pan anchor.
     */
    [[nodiscard]] float pixelScale() const noexcept;

    /**
     * @brief Computes Vulkan-style non-linear depth for a world-space point.
     *
     * Returns NDC z in [0,1] for points in front of the camera. Returns -1 for
     * points behind the camera or invalid projection.
     */
    [[nodiscard]] float pointDepth(const glm::vec3& point) const noexcept;

    /**
     * @brief Computes linear view-space depth (distance along forward direction).
     *
     * Forward is -Z in view space; this returns -viewZ.
     */
    [[nodiscard]] float linearDepth(const glm::vec3& point) const noexcept;

    /**
     * @brief Builds a Vulkan-friendly frustum projection (RH, ZO, Y flipped).
     *
     * Intended for optional resize-invariant focal-length-in-pixels style projection.
     */
    [[nodiscard]] glm::mat4 frustum(float fovDeg,
                                    float viewportWidth,
                                    float viewportHeight,
                                    float nearPlane,
                                    float farPlane) const noexcept;

    /**
     * @brief Constructs a world-space ray from screen coordinates.
     * @param x Pixel x coordinate (top-left origin).
     * @param y Pixel y coordinate (top-left origin).
     */
    [[nodiscard]] un::ray ray(float x, float y) const;

    /**
     * @brief Intersects a screen-space ray with a world-space plane.
     *
     * The plane is defined by a point and a normal in world space.
     * This function is commonly used for constrained gizmo dragging
     * (axis-aligned or planar movement).
     *
     * @param x            Screen-space x coordinate in pixels (top-left origin).
     * @param y            Screen-space y coordinate in pixels (top-left origin).
     * @param planePoint   Any point lying on the plane (world space).
     * @param planeNormal  Plane normal (world space, does not need to be normalized).
     * @param outHit       Receives the intersection point in world space if successful.
     *
     * @return true if the ray intersects the plane in front of the camera,
     *         false if the ray is parallel to the plane or the intersection lies behind.
     */
    [[nodiscard]] bool rayPlaneHit(float            x,
                                   float            y,
                                   const glm::vec3& planePoint,
                                   const glm::vec3& planeNormal,
                                   glm::vec3&       outHit) const noexcept;

    /**
     * @brief Intersects a screen-space ray with a plane facing the camera.
     *
     * The plane is perpendicular to the camera view direction and passes
     * through the given point. This is typically used for unconstrained
     * (screen-plane) dragging in move/translate tools.
     *
     * @param x            Screen-space x coordinate in pixels (top-left origin).
     * @param y            Screen-space y coordinate in pixels (top-left origin).
     * @param planePoint   Point defining the plane position (world space).
     * @param outHit       Receives the intersection point in world space if successful.
     *
     * @return true if the ray intersects the view plane in front of the camera,
     *         false otherwise.
     */
    [[nodiscard]] bool rayViewPlaneHit(float            x,
                                       float            y,
                                       const glm::vec3& planePoint,
                                       glm::vec3&       outHit) const noexcept;

    /**
     * @brief Returns the camera position in world space.
     */
    [[nodiscard]] glm::vec3 cameraPosition() const;

    /**
     * @brief Returns the camera forward direction in world space.
     */
    [[nodiscard]] glm::vec3 viewDirection() const;

    /**
     * @brief Returns the camera right direction in world space.
     */
    [[nodiscard]] glm::vec3 rightDirection() const;

    /**
     * @brief Returns the camera up direction in world space.
     */
    [[nodiscard]] glm::vec3 upDirection() const;

    /**
     * @brief Returns the viewport width in pixels.
     */
    [[nodiscard]] int32_t width() const noexcept;

    /**
     * @brief Returns the viewport height in pixels.
     */
    [[nodiscard]] int32_t height() const noexcept;

    /**
     * @brief Returns width/height, or 1 if height is 0.
     */
    [[nodiscard]] float aspect() const noexcept;

    /**
     * @brief Returns the projection matrix (reference).
     */
    [[nodiscard]] const glm::mat4& projection() const noexcept;

    /**
     * @brief Returns the view matrix (reference).
     */
    [[nodiscard]] const glm::mat4& view() const noexcept;

    /**
     * @brief Returns the model matrix (reference).
     */
    [[nodiscard]] const glm::mat4& model() const noexcept;

    /**
     * @brief Returns the change counter for dependency tracking.
     */
    [[nodiscard]] SysCounterPtr changeCounter() noexcept;

    /**
     * @brief Recomputes model/view/projection and cached derived matrices.
     *
     * This should be called after any change to pan/rot/dist, view mode, or size
     * if project/unproject/ray are used.
     */
    void apply() noexcept;

private:
    int32_t  m_viewportIndex{0};
    ViewMode m_viewMode = ViewMode::PERSPECTIVE;
    DrawMode m_drawMode = DrawMode::SOLID;
    int32_t  m_width    = 0;
    int32_t  m_height   = 0;

    glm::vec3& m_pan;
    glm::vec3& m_rot;
    float&     m_dist;

    glm::mat4 m_matProj  = glm::mat4(1.0f);
    glm::mat4 m_matView  = glm::mat4(1.0f);
    glm::mat4 m_matModel = glm::mat4(1.0f);

    glm::mat4 m_matViewProj    = glm::mat4(1.0f);
    glm::mat4 m_matInvViewProj = glm::mat4(1.0f);

    SysCounterPtr m_changeCounter = {};

    glm::vec4 m_clearColor{0.032f, 0.049f, 0.074f, 1.0f};
};
