#pragma once

#include <glm/glm.hpp>
#include <string>
#include <string_view>

class Viewport;

namespace tst // for now to don't interfier with existing in use classes
{

    class Overlay
    {
    public:
        void beginOverlay(int32_t id) noexcept;

        void addLine(glm::vec3 pt0, glm::vec3 pt1, float thickness, glm::vec4 color) noexcept;

        // More to come later

        void axisHint(glm::ivec3 axis);

        void endOverlay() noexcept;
    };

    class OverlayHandle : public Overlay
    {
    public:
        virtual ~OverlayHandle() = default;

        virtual void beginDrag(Viewport* vp, float x, float y) = 0;

        virtual void drag(Viewport* vp, float x, float y) = 0;

        virtual void endDrag(Viewport* vp, float x, float y) = 0;

        virtual glm::vec3 position() const = 0;

        virtual glm::ivec3 axisHint() const = 0;
    };

    class OverlayHandler
    {
    public:
        // Don't know yet but this is the public to tool interface etc
    private:
    };

} // namespace
