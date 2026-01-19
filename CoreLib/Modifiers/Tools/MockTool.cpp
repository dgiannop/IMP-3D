#include "MockTool.hpp"

#include "HeMeshBridge.hpp"
#include "Scene.hpp"
#include "SelectionUtils.hpp"
#include "Viewport.hpp"

MockTool::MockTool() : m_amount{0.f}
{
    // addProperty("X", PropertyType::FLOAT, &m_amount.x);
    // addProperty("Y", PropertyType::FLOAT, &m_amount.y);
    // addProperty("Z", PropertyType::FLOAT, &m_amount.z);
}

void MockTool::activate(Scene* scene)
{
}

void MockTool::propertiesChanged(Scene* scene)
{
    // scene->abortMeshChanges();

    // if (un::is_zero(m_amount))
    // return;

    // do something with m_amount (for example extrude)

    // auto vertMap = sel::to_verts(scene);

    // for (auto& [mesh, verts] : vertMap)
    // {
    //     for (int32_t vi : verts)
    //     {
    //         const glm::vec3 pos = mesh->vert_position(vi);
    //         mesh->move_vert(vi, pos + m_amount);
    //     }
    // }
}

static void test_hemesh_bridge(SysMesh* mesh)
{
    HeExtractionOptions opt{};
    auto                extract = extract_selected_polys_to_hemesh(mesh, opt);

    // no-op edit: just rebuild
    HeMeshCommit commit =
        build_commit_replace_editable(mesh, extract, extract.mesh, opt);

    apply_commit(mesh, extract, commit, opt);
}

void MockTool::mouseDown(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    for (SceneMesh* sm : scene->sceneMeshes())
    {
        test_hemesh_bridge(sm->sysMesh());
    }
    // m_overlayHandler->mouseDown(vp, scene, ev);
}

void MockTool::mouseDrag(Viewport* vp, Scene* scene, const CoreEvent& ev)
{
    // m_overlayHandler->mouseDrag(vp, scene, ev);
}

void MockTool::mouseUp(Viewport*, Scene* scene, const CoreEvent& ev)
{
    // m_overlayHandler->mouseUp(vp, scene, ev);
}

void MockTool::render(Viewport* vp, Scene* scene)
{
    // m_overlayHandler->render(vp, scene);
}
