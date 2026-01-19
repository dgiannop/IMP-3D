#include "SysMeshScene.hpp"

#include <memory>

#include "History.hpp"
#include "SysMesh.hpp"

SysMeshScene::SysMeshScene() :
    m_sceneHistory{nullptr} // pass scene ptr or other data if needed
{
}

SysMeshScene::~SysMeshScene()
{
    m_sceneHistory.freeze();
}

void SysMeshScene::commitMeshChanges()
{
    // Collect per-mesh histories into a single atomic action.
    // If nothing changed, we discard it.
    auto sceneTransaction = std::make_unique<History>(nullptr);

    for (SysMesh* mesh : selectedMeshes())
    {
        if (!mesh)
            continue;

        History* h = mesh->history();
        if (!h)
            continue;

        if (h->can_undo())
        {
            sceneTransaction->insert(mesh->release_history());
        }
        else
        {
            // if nothing to commit, freeze local history barrier.
            h->freeze();
        }
    }

    if (sceneTransaction->can_undo())
    {
        // Store the transaction in the scene history (ownership transfers)
        m_sceneHistory.insert(std::move(sceneTransaction));
    }
}

void SysMeshScene::abortMeshChanges()
{
    for (SysMesh* mesh : selectedMeshes())
    {
        if (!mesh)
            continue;

        History* h = mesh->history();
        if (!h)
            continue;

        h->undo();
    }
}

bool SysMeshScene::hasPendingMeshChanges() const
{
    for (SysMesh* mesh : selectedMeshes())
    {
        if (!mesh)
            continue;

        History* h = mesh->history();
        if (h && h->can_undo())
            return true;
    }
    return false;
}
