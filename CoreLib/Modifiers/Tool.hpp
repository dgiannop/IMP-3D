#pragma once

#include "CoreTypes.hpp"
#include "Property.hpp"

class Scene;
class Viewport;
class OverlayHandler;
/**
 * @class Tool
 * @brief Base class for interactive editor tools.
 *
 * A Tool encapsulates user-interaction logic (mouse/keyboard input,
 * property handling, and optional rendering overlays). Tools are
 * activated/deactivated by the Scene and may maintain configurable
 * state through PropertyGroup.
 */
class Tool : public PropertyGroup
{
public:
    /** @brief Virtual destructor. */
    virtual ~Tool() = default;

    /**
     * @brief Activate the tool.
     *
     * Called when the tool becomes the active tool in a Scene.
     *
     * @param scene The scene in which the tool is activated.
     */
    virtual void activate(Scene* scene) = 0;

    /**
     * @brief Deactivate the tool.
     *
     * Called by Scene when the tool is no longer active.
     *
     * @param scene The scene from which the tool is deactivated.
     */
    void deactivate(Scene* scene);

    /**
     * @brief Handle changes to tool properties.
     *
     * Called whenever a property belonging to the tool changes.
     *
     * @param scene The current scene context.
     */
    virtual void propertiesChanged(Scene* scene) = 0;

    /**
     * @brief Handle mouse button press.
     *
     * Subclasses must implement this to respond to mouse-down events.
     *
     * @param vp     The viewport receiving the event.
     * @param scene  The active scene.
     * @param event  The event data (position, button, modifiers, etc.).
     */
    virtual void mouseDown(Viewport* vp, Scene* scene, const CoreEvent& event) = 0;

    /**
     * @brief Handle mouse move.
     *
     * Default implementation does nothing; subclasses may override.
     *
     * @param vp     The viewport receiving the event.
     * @param scene  The active scene.
     * @param event  The event data.
     */
    virtual void mouseMove(Viewport* vp, Scene* scene, const CoreEvent& event)
    {
    }

    /**
     * @brief Handle mouse dragging (mouse move with button pressed).
     *
     * @param vp     The viewport receiving the event.
     * @param scene  The active scene.
     * @param event  The event data.
     */
    virtual void mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& event) = 0;

    /**
     * @brief Handle mouse button release.
     *
     * @param vp     The viewport receiving the event.
     * @param scene  The active scene.
     * @param event  The event data.
     */
    virtual void mouseUp(Viewport* vp, Scene* scene, const CoreEvent& event) = 0;

    /**
     * @brief Handle keyboard key press.
     *
     * Subclasses may override to intercept key input.
     *
     * @param vp     The viewport receiving the event.
     * @param scene  The active scene.
     * @param event  The event data.
     * @return true if the tool handled the key, false otherwise.
     */
    virtual bool keyPress(Viewport* vp, Scene* scene, const CoreEvent& event)
    {
        return false;
    }

    /**
     * @brief Optional per-frame rendering hook.
     *
     * Allows a tool to draw overlays or visual aids. Default is no-op.
     *
     * @param vp     The target viewport.
     * @param scene  The active scene.
     */
    virtual void render(Viewport* vp, Scene* scene)
    {
    }

    /**
     * @brief Idle callback.
     *
     * Called periodically when the application is idle.
     *
     * @param scene The active scene.
     */
    void idle(Scene* scene);

    /**
     * @brief Optional overlay provider.
     *
     * Expose overlay handler so Renderer can draw it.
     * Defaults to nullptr for tools without overlays.
     */
    virtual OverlayHandler* overlayHandler()
    {
        return nullptr;
    }
};
