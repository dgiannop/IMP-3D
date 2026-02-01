//=============================================================================
// SceneLightOverlays.hpp
//=============================================================================
#pragma once

class Scene;
class Viewport;
class OverlayHandler;

namespace scene_overlays
{
    void appendLights(Viewport* vp, Scene* scene, OverlayHandler& outOverlays);

} // namespace scene_overlays
