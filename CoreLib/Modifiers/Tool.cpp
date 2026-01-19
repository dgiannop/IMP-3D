#include "Tool.hpp"

#include "Scene.hpp"

void Tool::deactivate(Scene* scene)
{
    scene->commitMeshChanges();
}

void Tool::idle(Scene* scene)
{
    if (propertyValuesChanged())
    {
        propertiesChanged(scene);
    }
}
