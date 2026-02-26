#include "Renderer.hpp"

#include <SysMesh.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

#include "GpuResources/GpuMaterial.hpp"
#include "GpuResources/MeshGpuResources.hpp"
#include "GpuResources/TextureHandler.hpp"
#include "GridRendererVK.hpp"
#include "RenderGeometry.hpp"
#include "Scene.hpp"
#include "SceneMesh.hpp"
#include "ShaderStage.hpp"
#include "Viewport.hpp"
#include "VkPipelineHelpers.hpp"
#include "VkUtilities.hpp"

//==================================================================
// Init / Lifetime
//==================================================================

Renderer::Renderer() noexcept = default;

bool Renderer::initDevice(const VulkanContext& ctx)
{
    m_ctx            = ctx;
    m_framesInFlight = std::clamp(m_ctx.framesInFlight, 1u, vkcfg::kMaxFramesInFlight);

    m_materialCounterPerFrame = {};
    m_materialCount           = 0;

    m_viewportUbos.clear();

    if (!createDescriptors(m_framesInFlight))
        return false;

    if (!createPipelineLayout())
        return false;

    m_grid = std::make_unique<GridRendererVK>(&m_ctx);
    m_grid->createDeviceResources();

    // ------------------------------------------------------------
    // RT module init (device lifetime)
    // ------------------------------------------------------------
    if (rtReady(m_ctx))
    {
        if (!m_rt.initDevice(m_ctx, m_descriptorSetLayout.layout(), m_materialSetLayout.layout()))
        {
            std::cerr << "RtRenderer::initDevice() failed.\n";
            return false;
        }
    }

    return true;
}

bool Renderer::initSwapchain(VkRenderPass renderPass)
{
    destroyPipelines();

    if (!createPipelines(renderPass))
        return false;

    if (m_grid)
    {
        m_grid->destroySwapchainResources();
        if (!m_grid->createPipelines(renderPass, m_pipelineLayout))
            return false;
    }

    // ------------------------------------------------------------
    // RT module swapchain init (present pipeline depends on render pass)
    // ------------------------------------------------------------
    if (rtReady(m_ctx))
    {
        if (!m_rt.initSwapchain(renderPass))
        {
            std::cerr << "RtRenderer::initSwapchain() failed.\n";
            return false;
        }
    }

    return true;
}

void Renderer::destroySwapchainResources() noexcept
{
    if (m_grid)
        m_grid->destroySwapchainResources();

    m_rt.destroySwapchainResources();
    destroyPipelines();
}

void Renderer::shutdown() noexcept
{
    if (m_ctx.device)
        vkDeviceWaitIdle(m_ctx.device);

    destroySwapchainResources();

    // Per-viewport Camera + Lights state
    for (auto& [vp, state] : m_viewportUbos)
    {
        const uint32_t fi = std::min(m_framesInFlight, vkcfg::kMaxFramesInFlight);
        for (uint32_t i = 0; i < fi; ++i)
        {
            state.cameraBuffers[i].destroy();
            state.cameraBuffers[i] = {};

            state.lightBuffers[i].destroy();
            state.lightBuffers[i] = {};

            state.uboSets[i] = {};
        }
    }
    m_viewportUbos.clear();

    // Destroy per-frame material buffers explicitly.
    for (uint32_t i = 0; i < m_framesInFlight; ++i)
    {
        if (m_materialBuffers[i].valid())
        {
            m_materialBuffers[i].destroy();
            m_materialBuffers[i] = {};
        }
    }

    for (uint32_t i = 0; i < std::min(m_framesInFlight, vkcfg::kMaxFramesInFlight); ++i)
        m_materialSets[i] = {};

    m_descriptorPool.destroy();
    m_descriptorSetLayout.destroy();
    m_materialSetLayout.destroy();

    if (m_pipelineLayout != VK_NULL_HANDLE && m_ctx.device)
    {
        vkDestroyPipelineLayout(m_ctx.device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    if (m_grid)
        m_grid->destroyDeviceResources();
    m_grid.reset();

    m_overlayVertexBuffer.destroy();
    m_overlayVertexCapacity = 0;

    if (m_overlayFillVertexBuffer.valid())
        m_overlayFillVertexBuffer.destroy();
    m_overlayFillVertexCapacity = 0;

    // ------------------------------------------------------------
    // RT module shutdown (device lifetime)
    // ------------------------------------------------------------
    m_rt.shutdown();

    m_materialCount  = 0;
    m_framesInFlight = 0;
    m_ctx            = {};
}

void Renderer::idle(Scene* scene)
{
    m_rt.idle(scene);
}

void Renderer::waitDeviceIdle() noexcept
{
    if (!m_ctx.device)
        return;

    vkDeviceWaitIdle(m_ctx.device);
}

void Renderer::setLightingSettings(const LightingSettings& settings) noexcept
{
    m_lightingSettings = settings;

    // Mirror the headlight controls into the renderer headlight.
    m_headlight.enabled   = m_lightingSettings.useHeadlight;
    m_headlight.intensity = m_lightingSettings.headlightIntensity;
}

const LightingSettings& Renderer::lightingSettings() const noexcept
{
    return m_lightingSettings;
}

//==================================================================
// Shared per-viewport/per-frame globals (set=0)
//==================================================================

void Renderer::updateViewportFrameGlobals(Viewport* vp, Scene* scene, uint32_t frameIndex) noexcept
{
    if (!vp || !scene)
        return;

    if (frameIndex >= m_framesInFlight)
        return;

    ViewportUboState& vpUbo = ensureViewportUboState(vp, frameIndex);

    const uint32_t fi = std::min(m_framesInFlight, vkcfg::kMaxFramesInFlight);
    if (frameIndex >= fi)
        return;

    if (!vpUbo.cameraBuffers[frameIndex].valid() || !vpUbo.lightBuffers[frameIndex].valid())
        return;

    // CameraUBO
    {
        CameraUBO ubo{};

        const glm::mat4 proj = vp->projection();
        const glm::mat4 view = vp->view();

        ubo.proj     = proj;
        ubo.view     = view;
        ubo.viewProj = proj * view;

        ubo.invProj     = glm::inverse(proj);
        ubo.invView     = glm::inverse(view);
        ubo.invViewProj = glm::inverse(ubo.viewProj);

        const glm::vec3 camPos = vp->cameraPosition();
        ubo.camPos             = glm::vec4(camPos, 1.0f);

        const float fw   = static_cast<float>(vp->width());
        const float fh   = static_cast<float>(vp->height());
        const float invW = (fw > 0.0f) ? 1.0f / fw : 0.0f;
        const float invH = (fh > 0.0f) ? 1.0f / fh : 0.0f;

        ubo.viewport   = glm::vec4(fw, fh, invW, invH);
        ubo.clearColor = vp->clearColor();

        vpUbo.cameraBuffers[frameIndex].upload(&ubo, sizeof(ubo));
    }

    // Lights
    {
        GpuLightsUBO lights{};
        buildGpuLightsUBO(
            m_lightingSettings,
            m_headlight,
            *vp,
            scene,
            lights);

        vpUbo.lightBuffers[frameIndex].upload(&lights, sizeof(lights));
    }
}

//==================================================================
// renderPrePass
//   - runs all MeshGpuResources::update() OUTSIDE render pass
//   - updates set=0 (Camera+Lights) buffers for this viewport/frame
//   - dispatches RT trace OUTSIDE render pass via RtRenderer (if enabled)
//==================================================================

void Renderer::renderPrePass(Viewport* vp, Scene* scene, const RenderFrameContext& fc)
{
    if (!vp || !scene || fc.cmd == VK_NULL_HANDLE)
        return;

    if (fc.frameIndex >= m_framesInFlight)
        return;

    const uint32_t frameIdx = fc.frameIndex;

    // 1) Update ALL MeshGpuResources here (outside render pass)
    forEachVisibleMesh(scene, [&](SceneMesh* /*sm*/, MeshGpuResources* gpu) {
        gpu->update(fc);
    });

    // 2) Update set=0 buffers (Camera+Lights) for this viewport/frame
    updateViewportFrameGlobals(vp, scene, frameIdx);

    // 3) If RT: make sure set=1 materials are ready, then dispatch RT (outside render pass)
    if (vp->drawMode() == DrawMode::RAY_TRACE && rtReady(m_ctx))
    {
        if (scene->materialHandler() && scene->textureHandler())
        {
            const uint64_t matCounter =
                scene->materialHandler()->changeCounter() ? scene->materialHandler()->changeCounter()->value() : 0ull;

            if (m_materialCounterPerFrame[frameIdx] != matCounter)
            {
                const auto& mats = scene->materialHandler()->materials();
                uploadMaterialsToGpu(mats, *scene->textureHandler(), frameIdx, fc);
                updateMaterialTextureTable(*scene->textureHandler(), frameIdx);

                m_materialCounterPerFrame[frameIdx] = matCounter;
            }
        }

        ViewportUboState& vpUbo = ensureViewportUboState(vp, frameIdx);

        const VkDescriptorSet set0 = vpUbo.uboSets[frameIdx].set();
        const VkDescriptorSet set1 = m_materialSets[frameIdx].set();

        m_rt.renderPrePass(vp, scene, fc, set0, set1);
    }
}

//==================================================================
// Render (RT present OR raster)
//   - INSIDE render pass
//==================================================================

void Renderer::render(Viewport* vp, Scene* scene, const RenderFrameContext& fc)
{
    std::cerr << "Render called\n";
    if (!vp || !scene || fc.cmd == VK_NULL_HANDLE)
        return;

    if (fc.frameIndex >= m_framesInFlight)
        return;

    if (m_pipelineLayout == VK_NULL_HANDLE)
        return;

    const VkCommandBuffer cmd      = fc.cmd;
    const uint32_t        frameIdx = fc.frameIndex;

    const uint32_t w = static_cast<uint32_t>(vp->width());
    const uint32_t h = static_cast<uint32_t>(vp->height());

    const glm::vec4 solidEdgeColor{0.10f, 0.10f, 0.10f, 0.5f};
    const glm::vec4 wireVisibleColor{0.85f, 0.85f, 0.85f, 1.0f};
    const glm::vec4 wireHiddenColor{0.85f, 0.85f, 0.85f, 0.25f};

    auto bindSet0 = [&]() -> bool {
        ViewportUboState& vpUbo = ensureViewportUboState(vp, frameIdx);

        const uint32_t fi = std::min(m_framesInFlight, vkcfg::kMaxFramesInFlight);
        if (frameIdx >= fi)
            return false;

        VkDescriptorSet gfxSet0 = vpUbo.uboSets[frameIdx].set();
        if (gfxSet0 == VK_NULL_HANDLE)
            return false;

        vkCmdBindDescriptorSets(cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelineLayout,
                                0,
                                1,
                                &gfxSet0,
                                0,
                                nullptr);

        return true;
    };

    // ------------------------------------------------------------
    // RT PRESENT PATH (inside render pass)
    // ------------------------------------------------------------
    if (vp->drawMode() == DrawMode::RAY_TRACE)
    {
        if (!rtReady(m_ctx))
            return;

        vkutil::setViewportAndScissor(cmd, w, h);

        // Fullscreen present of the RT output for this viewport+frame
        m_rt.present(cmd, vp, fc);

        // Re-bind set=0 so selection/grid/overlays can render on top
        if (!bindSet0())
            return;

        vkutil::setViewportAndScissor(cmd, w, h);

        drawSelection(cmd, vp, scene);

        return;
    }

    // ------------------------------------------------------------
    // NORMAL GRAPHICS PATH
    // ------------------------------------------------------------
    if (!bindSet0())
        return;

    vkutil::setViewportAndScissor(cmd, w, h);

    // Solid / Shaded
    if (vp->drawMode() != DrawMode::WIREFRAME)
    {
        const bool isShaded = (vp->drawMode() == DrawMode::SHADED);

        VkPipeline triPipe = isShaded ? m_shadedPipeline.handle() : m_solidPipeline.handle();

        // Ensure materials (needed for shaded; harmless for solid if shaders ignore)
        if (scene->materialHandler() && scene->textureHandler())
        {
            const uint64_t matCounter =
                scene->materialHandler()->changeCounter() ? scene->materialHandler()->changeCounter()->value() : 0ull;

            if (m_materialCounterPerFrame[frameIdx] != matCounter)
            {
                const auto& mats = scene->materialHandler()->materials();
                uploadMaterialsToGpu(mats, *scene->textureHandler(), frameIdx, fc);
                updateMaterialTextureTable(*scene->textureHandler(), frameIdx);

                m_materialCounterPerFrame[frameIdx] = matCounter;
            }
        }

        // Bind set=1 (materials + texture table)
        {
            VkDescriptorSet set1 = m_materialSets[frameIdx].set();
            vkCmdBindDescriptorSets(cmd,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout,
                                    1,
                                    1,
                                    &set1,
                                    0,
                                    nullptr);
        }

        if (triPipe != VK_NULL_HANDLE)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, triPipe);

            forEachVisibleMesh(scene, [&](SceneMesh* sm, MeshGpuResources* gpu) {
                PushConstants pc{};
                pc.model = sm->model();
                pc.color = glm::vec4(0, 0, 0, 1);

                vkCmdPushConstants(cmd,
                                   m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   sizeof(PushConstants),
                                   &pc);

                const render::geom::GfxMeshGeometry geo = render::geom::selectGfxGeometry(sm, gpu);
                if (!geo.valid())
                    return;

                VkBuffer     bufs[4] = {geo.posBuffer, geo.nrmBuffer, geo.uvBuffer, geo.matBuffer};
                VkDeviceSize offs[4] = {0, 0, 0, 0};

                vkCmdBindVertexBuffers(cmd, 0, 4, bufs, offs);
                vkCmdDraw(cmd, geo.vertexCount, 1, 0, 0);
            });
        }

        constexpr bool drawEdgesInSolid = true;
        if (!isShaded && drawEdgesInSolid && m_wireOverlayPipeline.valid())
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_wireOverlayPipeline.handle());

            forEachVisibleMesh(scene, [&](SceneMesh* sm, MeshGpuResources* gpu) {
                const bool useSubdiv = (sm->subdivisionLevel() > 0);

                PushConstants pc{};
                pc.model = sm->model();
                pc.color = solidEdgeColor;

                vkCmdPushConstants(cmd,
                                   m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   sizeof(PushConstants),
                                   &pc);

                const render::geom::WireDrawGeo wgeo = render::geom::selectWireGeometry(gpu, useSubdiv);
                if (!wgeo.valid())
                    return;

                VkDeviceSize voff = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &wgeo.posVb, &voff);
                vkCmdBindIndexBuffer(cmd, wgeo.idxIb, 0, wgeo.idxType);
                vkCmdDrawIndexed(cmd, wgeo.idxCount, 1, 0, 0, 0);
            });
        }
    }
    // Wireframe mode (hidden-line)
    else
    {
        // 1) depth-only triangles
        if (m_depthOnlyPipeline.valid())
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_depthOnlyPipeline.handle());

            forEachVisibleMesh(scene, [&](SceneMesh* sm, MeshGpuResources* gpu) {
                PushConstants pc{};
                pc.model = sm->model();
                pc.color = glm::vec4(0, 0, 0, 0);

                vkCmdPushConstants(cmd,
                                   m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   sizeof(PushConstants),
                                   &pc);

                const render::geom::GfxMeshGeometry geo = render::geom::selectGfxGeometry(sm, gpu);
                if (!geo.valid())
                    return;

                VkBuffer     bufs[4] = {geo.posBuffer, geo.nrmBuffer, geo.uvBuffer, geo.matBuffer};
                VkDeviceSize offs[4] = {0, 0, 0, 0};

                vkCmdBindVertexBuffers(cmd, 0, 4, bufs, offs);
                vkCmdDraw(cmd, geo.vertexCount, 1, 0, 0);
            });
        }

        auto drawEdges = [&](const GraphicsPipeline& pipeline, const glm::vec4& color) {
            if (!pipeline.valid())
                return;

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());

            forEachVisibleMesh(scene, [&](SceneMesh* sm, MeshGpuResources* gpu) {
                const bool useSubdiv = (sm->subdivisionLevel() > 0);

                PushConstants pc{};
                pc.model = sm->model();
                pc.color = color;

                vkCmdPushConstants(cmd,
                                   m_pipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   sizeof(PushConstants),
                                   &pc);

                const render::geom::WireDrawGeo wgeo = render::geom::selectWireGeometry(gpu, useSubdiv);
                if (!wgeo.valid())
                    return;

                VkDeviceSize voff = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &wgeo.posVb, &voff);
                vkCmdBindIndexBuffer(cmd, wgeo.idxIb, 0, wgeo.idxType);
                vkCmdDrawIndexed(cmd, wgeo.idxCount, 1, 0, 0, 0);
            });
        };

        drawEdges(m_wireHiddenPipeline, wireHiddenColor);
        drawEdges(m_wirePipeline, wireVisibleColor);
    }

    drawSelection(cmd, vp, scene);

    if (scene->showSceneGrid())
        drawSceneGrid(cmd, vp, scene);
}

//==================================================================
// drawOverlays / drawSelection / drawSceneGrid / ensureOverlayVertexCapacity
//==================================================================

void Renderer::drawOverlays(VkCommandBuffer cmd, Viewport* vp, const OverlayHandler& overlays)
{
    if (!vp)
        return;

    const std::vector<OverlayHandler::Overlay>& ovs = overlays.overlays();
    if (ovs.empty())
        return;

    std::vector<OverlayVertex>     lineVertices = {};
    std::vector<OverlayFillVertex> fillVertices = {};

    lineVertices.reserve(512);
    fillVertices.reserve(512);

    auto pushLine = [&](const glm::vec3& a, const glm::vec3& b, float thickness, const glm::vec4& color) {
        OverlayVertex v0 = {};
        v0.pos           = a;
        v0.thickness     = thickness;
        v0.color         = color;
        lineVertices.push_back(v0);

        OverlayVertex v1 = {};
        v1.pos           = b;
        v1.thickness     = thickness;
        v1.color         = color;
        lineVertices.push_back(v1);
    };

    auto pushTri = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec4& color) {
        OverlayFillVertex v0 = {};
        v0.pos               = a;
        v0.color             = color;
        fillVertices.push_back(v0);

        OverlayFillVertex v1 = {};
        v1.pos               = b;
        v1.color             = color;
        fillVertices.push_back(v1);

        OverlayFillVertex v2 = {};
        v2.pos               = c;
        v2.color             = color;
        fillVertices.push_back(v2);
    };

    auto triangulateFan = [&](const std::vector<glm::vec3>& pts, const glm::vec4& color) {
        if (pts.size() < 3)
            return;

        const glm::vec3& v0 = pts[0];
        for (size_t i = 1; i + 1 < pts.size(); ++i)
            pushTri(v0, pts[i], pts[i + 1], color);
    };

    for (const OverlayHandler::Overlay& ov : ovs)
    {
        for (const OverlayHandler::Line& L : ov.lines)
            pushLine(L.a, L.b, L.thickness, L.color);

        for (const OverlayHandler::Polygon& P : ov.polygons)
        {
            const std::vector<glm::vec3>& pts = P.verts;
            if (pts.size() < 3)
                continue;

            if (P.filled)
            {
                triangulateFan(pts, P.color);
            }
            else
            {
                const float thicknessPx = (P.thicknessPx > 0.0f) ? P.thicknessPx : 2.5f;

                for (size_t i = 0; i < pts.size(); ++i)
                {
                    const glm::vec3& a = pts[i];
                    const glm::vec3& b = pts[(i + 1) % pts.size()];
                    pushLine(a, b, thicknessPx, P.color);
                }
            }
        }

        for (const OverlayHandler::Point& pt : ov.points)
        {
            const float pxW = vp->pixelScale(pt.p);
            const float sW  = std::max(0.00001f, pxW * (pt.size * 0.5f));

            const glm::vec3 r = vp->rightDirection();
            const glm::vec3 u = vp->upDirection();

            constexpr float kPointThicknessPx = 2.0f;

            pushLine(pt.p - r * sW, pt.p + r * sW, kPointThicknessPx, pt.color);
            pushLine(pt.p - u * sW, pt.p + u * sW, kPointThicknessPx, pt.color);
        }
    }

    if (!fillVertices.empty())
    {
        const std::size_t fillCount = fillVertices.size();

        ensureOverlayFillVertexCapacity(fillCount);
        if (m_overlayFillVertexBuffer.valid() && m_overlayFillPipeline.valid())
        {
            const VkDeviceSize byteSize = static_cast<VkDeviceSize>(fillCount * sizeof(OverlayFillVertex));
            m_overlayFillVertexBuffer.upload(fillVertices.data(), byteSize);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_overlayFillPipeline.handle());

            PushConstants pc = {};
            pc.model         = glm::mat4(1.0f);
            pc.color         = glm::vec4(1.0f);
            pc.overlayParams = glm::vec4(static_cast<float>(vp->width()),
                                         static_cast<float>(vp->height()),
                                         1.0f,
                                         0.0f);

            vkCmdPushConstants(cmd,
                               m_pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0,
                               sizeof(PushConstants),
                               &pc);

            VkBuffer     vb      = m_overlayFillVertexBuffer.buffer();
            VkDeviceSize offset0 = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset0);

            vkCmdDraw(cmd, static_cast<uint32_t>(fillCount), 1, 0, 0);
        }
    }

    if (lineVertices.empty())
        return;

    const std::size_t lineCount = lineVertices.size();

    ensureOverlayVertexCapacity(lineCount);
    if (!m_overlayVertexBuffer.valid())
        return;

    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(lineCount * sizeof(OverlayVertex));
    m_overlayVertexBuffer.upload(lineVertices.data(), byteSize);

    if (!m_overlayPipeline.valid())
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_overlayPipeline.handle());

    PushConstants pc = {};
    pc.model         = glm::mat4(1.0f);
    pc.color         = glm::vec4(1.0f);
    pc.overlayParams = glm::vec4(static_cast<float>(vp->width()),
                                 static_cast<float>(vp->height()),
                                 1.0f,
                                 0.0f);

    vkCmdPushConstants(cmd,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(PushConstants),
                       &pc);

    VkBuffer     vb      = m_overlayVertexBuffer.buffer();
    VkDeviceSize offset0 = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset0);

    vkCmdDraw(cmd, static_cast<uint32_t>(lineCount), 1, 0, 0);
}

void Renderer::ensureOverlayVertexCapacity(std::size_t requiredVertexCount)
{
    if (requiredVertexCount == 0)
        return;

    if (requiredVertexCount <= m_overlayVertexCapacity && m_overlayVertexBuffer.valid())
        return;

    if (m_overlayVertexBuffer.valid())
        m_overlayVertexBuffer.destroy();

    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(requiredVertexCount * sizeof(OverlayVertex));

    m_overlayVertexBuffer.create(m_ctx.device,
                                 m_ctx.physicalDevice,
                                 bufferSize,
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 true);

    if (!m_overlayVertexBuffer.valid())
    {
        m_overlayVertexCapacity = 0;
        return;
    }

    m_overlayVertexCapacity = requiredVertexCount;
}

void Renderer::ensureOverlayFillVertexCapacity(std::size_t requiredVertexCount)
{
    if (requiredVertexCount == 0)
        return;

    if (requiredVertexCount <= m_overlayFillVertexCapacity && m_overlayFillVertexBuffer.valid())
        return;

    if (m_overlayFillVertexBuffer.valid())
        m_overlayFillVertexBuffer.destroy();

    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(requiredVertexCount * sizeof(OverlayFillVertex));

    m_overlayFillVertexBuffer.create(m_ctx.device,
                                     m_ctx.physicalDevice,
                                     bufferSize,
                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                     true);

    if (!m_overlayFillVertexBuffer.valid())
    {
        m_overlayFillVertexCapacity = 0;
        return;
    }

    m_overlayFillVertexCapacity = requiredVertexCount;
}

//==================================================================
// drawSelection
//==================================================================

void Renderer::drawSelection(VkCommandBuffer cmd, Viewport* vp, Scene* scene)
{
    if (!scene || !vp)
        return;

    if (!m_selVertPipeline.valid() && !m_selEdgePipeline.valid() && !m_selPolyPipeline.valid())
        return;

    VkDeviceSize zeroOffset = 0;

    const glm::vec4 selColorVisible = glm::vec4(1.0f, 0.55f, 0.10f, 0.6f);
    const glm::vec4 selColorHidden  = glm::vec4(1.0f, 0.55f, 0.10f, 0.3f);

    const bool showOccluded = (vp->drawMode() == DrawMode::WIREFRAME);

    auto pushPC = [&](SceneMesh& sm, const glm::vec4& color) {
        PushConstants pc{};
        pc.model = sm.model();
        pc.color = color;

        vkCmdPushConstants(cmd,
                           m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(PushConstants),
                           &pc);
    };

    auto drawHidden = [&](SceneMesh& sm, VkPipeline pipeline, uint32_t indexCount) {
        if (!showOccluded)
            return;
        if (pipeline == VK_NULL_HANDLE || indexCount == 0)
            return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);
        pushPC(sm, selColorHidden);
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
    };

    auto drawVisible = [&](SceneMesh& sm, VkPipeline pipeline, uint32_t indexCount) {
        if (pipeline == VK_NULL_HANDLE || indexCount == 0)
            return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdSetDepthBias(cmd, -1.0f, 0.0f, -1.0f);
        pushPC(sm, selColorVisible);
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
    };

    const render::geom::SelPipelines pipes{
        .vertVis = m_selVertPipeline.handle(),
        .vertHid = m_selVertHiddenPipeline.handle(),
        .edgeVis = m_selEdgePipeline.handle(),
        .edgeHid = m_selEdgeHiddenPipeline.handle(),
        .polyVis = m_selPolyPipeline.handle(),
        .polyHid = m_selPolyHiddenPipeline.handle(),
    };

    const SelectionMode mode = scene->selectionMode();

    forEachVisibleMesh(scene, [&](SceneMesh* sm, MeshGpuResources* gpu) {
        const bool useSubdiv = (sm->subdivisionLevel() > 0);

        const render::geom::SelDrawGeo geo =
            render::geom::selectSelGeometry(gpu, useSubdiv, mode, pipes);

        if (!geo.valid())
            return;

        vkCmdBindVertexBuffers(cmd, 0, 1, &geo.posVb, &zeroOffset);
        vkCmdBindIndexBuffer(cmd, geo.selIb, 0, VK_INDEX_TYPE_UINT32);

        drawHidden(*sm, geo.pipeHid, geo.selCount);
        drawVisible(*sm, geo.pipeVis, geo.selCount);
    });

    vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);
}

void Renderer::drawSceneGrid(VkCommandBuffer cmd, Viewport* vp, Scene* scene)
{
    if (!vp || !scene)
        return;

    if (!scene->showSceneGrid())
        return;

    if (!m_grid)
        return;

    if (m_pipelineLayout == VK_NULL_HANDLE)
        return;

    glm::mat4 gridModel = render::geom::gridModelFor(vp->viewMode());

    PushConstants pc{};
    pc.model         = gridModel;
    pc.color         = glm::vec4(0, 0, 0, 0);
    pc.overlayParams = glm::vec4(0.0f);

    vkCmdPushConstants(cmd,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(PushConstants),
                       &pc);

    const bool xrayGrid = (vp->drawMode() == DrawMode::WIREFRAME);
    m_grid->render(cmd, xrayGrid);
}

//==================================================================
// Pipeline destruction (swapchain-level)
//==================================================================

void Renderer::destroyPipelines() noexcept
{
    if (!m_ctx.device)
        return;

    vkDeviceWaitIdle(m_ctx.device);

    auto destroyPipe = [](GraphicsPipeline& p) noexcept {
        p.destroy();
    };

    destroyPipe(m_solidPipeline);
    destroyPipe(m_shadedPipeline);
    destroyPipe(m_depthOnlyPipeline);
    destroyPipe(m_wirePipeline);
    destroyPipe(m_wireHiddenPipeline);
    destroyPipe(m_wireOverlayPipeline);
    destroyPipe(m_overlayPipeline);
    destroyPipe(m_overlayFillPipeline);

    destroyPipe(m_selVertPipeline);
    destroyPipe(m_selEdgePipeline);
    destroyPipe(m_selPolyPipeline);
    destroyPipe(m_selVertHiddenPipeline);
    destroyPipe(m_selEdgeHiddenPipeline);
    destroyPipe(m_selPolyHiddenPipeline);
}

//==================================================================
// Descriptors + pipeline layout (device-level)
//==================================================================

bool Renderer::createDescriptors(uint32_t framesInFlight)
{
    VkDevice device = m_ctx.device;
    if (!device)
        return false;

    m_framesInFlight  = std::clamp(framesInFlight, 1u, vkcfg::kMaxFramesInFlight);
    const uint32_t fi = m_framesInFlight;

    m_viewportUbos.clear();

    m_descriptorPool.destroy();
    m_descriptorSetLayout.destroy();
    m_materialSetLayout.destroy();

    // set = 0 : Frame globals
    {
        DescriptorBindingInfo uboBindings[2] = {};

        uboBindings[0].binding = 0;
        uboBindings[0].type    = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBindings[0].stages  = VK_SHADER_STAGE_VERTEX_BIT |
                                VK_SHADER_STAGE_GEOMETRY_BIT |
                                VK_SHADER_STAGE_FRAGMENT_BIT |
                                VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                                VK_SHADER_STAGE_MISS_BIT_KHR |
                                VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        uboBindings[0].count = 1;

        uboBindings[1].binding = 1;
        uboBindings[1].type    = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBindings[1].stages  = VK_SHADER_STAGE_ALL;
        uboBindings[1].count   = 1;

        if (!m_descriptorSetLayout.create(device, std::span{uboBindings, 2}))
        {
            std::cerr << "RendererVK: Failed to create Frame Globals DescriptorSetLayout.\n";
            return false;
        }
    }

    // set = 1 : Materials + texture table
    {
        DescriptorBindingInfo matBindings[2] = {};

        matBindings[0].binding = 0;
        matBindings[0].type    = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        matBindings[0].stages  = VK_SHADER_STAGE_FRAGMENT_BIT |
                                VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                                VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
        matBindings[0].count = 1;

        matBindings[1].binding = 1;
        matBindings[1].type    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        matBindings[1].stages  = VK_SHADER_STAGE_FRAGMENT_BIT |
                                VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                                VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
        matBindings[1].count = vkcfg::kMaxTextureCount;

        if (!m_materialSetLayout.create(device, std::span{matBindings, 2}))
        {
            std::cerr << "RendererVK: Failed to create material DescriptorSetLayout.\n";
            return false;
        }
    }

    const uint32_t rasterSetCount   = fi * kMaxViewports;
    const uint32_t materialSetCount = fi;

    std::array<VkDescriptorPoolSize, 3> poolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, rasterSetCount * 2u},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, materialSetCount},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, materialSetCount * vkcfg::kMaxTextureCount},
    };

    const uint32_t maxSets = rasterSetCount + materialSetCount;

    if (!m_descriptorPool.create(device, poolSizes, maxSets))
    {
        std::cerr << "RendererVK: Failed to create shared DescriptorPool.\n";
        return false;
    }

    for (uint32_t i = 0; i < fi; ++i)
    {
        m_materialSets[i] = {};

        if (!m_materialSets[i].allocate(device, m_descriptorPool.pool(), m_materialSetLayout.layout()))
        {
            std::cerr << "RendererVK: Failed to allocate material DescriptorSet for frame " << i << ".\n";
            return false;
        }
    }

    return true;
}

bool Renderer::createPipelineLayout() noexcept
{
    if (!m_ctx.device)
        return false;

    if (m_pipelineLayout != VK_NULL_HANDLE)
        return true;

    VkDescriptorSetLayout setLayouts[2] = {
        m_descriptorSetLayout.layout(),
        m_materialSetLayout.layout(),
    };

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                         VK_SHADER_STAGE_GEOMETRY_BIT |
                         VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size   = sizeof(PushConstants);

    m_pipelineLayout = vkutil::createPipelineLayout(m_ctx.device, setLayouts, 2, &pcRange, 1);
    return (m_pipelineLayout != VK_NULL_HANDLE);
}

//==================================================================
// Materials
//==================================================================

void Renderer::uploadMaterialsToGpu(const std::vector<Material>& materials,
                                    TextureHandler&              texHandler,
                                    uint32_t                     frameIndex,
                                    const RenderFrameContext&    fc)
{
    if (frameIndex >= m_framesInFlight)
        return;

    m_materialCount = static_cast<uint32_t>(materials.size());
    if (m_materialCount == 0)
    {
        if (m_materialBuffers[frameIndex].valid())
        {
            m_materialSets[frameIndex].writeStorageBuffer(m_ctx.device,
                                                          0,
                                                          m_materialBuffers[frameIndex].buffer(),
                                                          0,
                                                          0);
        }
        return;
    }

    std::vector<GpuMaterial> gpuMats = {};
    buildGpuMaterialArray(materials, texHandler, gpuMats);

    const VkDeviceSize sizeBytes = static_cast<VkDeviceSize>(gpuMats.size() * sizeof(GpuMaterial));

    GpuBuffer& buf = m_materialBuffers[frameIndex];

    if (!buf.valid() || buf.size() < sizeBytes)
    {
        if (buf.valid())
        {
            if (fc.deferred)
            {
                GpuBuffer old = std::move(buf);
                fc.deferred->enqueue(frameIndex, [b = std::move(old)]() mutable {
                    b.destroy();
                });
            }
            else
            {
                buf.destroy();
            }
        }

        buf = {}; // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
        buf.create(m_ctx.device,
                   m_ctx.physicalDevice,
                   sizeBytes,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   false);
    }

    buf.upload(gpuMats.data(), sizeBytes);

    m_materialSets[frameIndex].writeStorageBuffer(m_ctx.device,
                                                  0,
                                                  buf.buffer(),
                                                  sizeBytes,
                                                  0);
}

void Renderer::updateMaterialTextureTable(const TextureHandler& textureHandler, uint32_t frameIndex)
{
    if (frameIndex >= m_framesInFlight)
        return;

    VkDevice device = m_ctx.device;
    if (!device)
        return;

    const GpuTexture* fallback = textureHandler.fallbackTexture();
    VkImageView       fbView   = (fallback ? fallback->view : VK_NULL_HANDLE);
    VkSampler         fbSamp   = (fallback ? fallback->sampler : VK_NULL_HANDLE);

    if (fbView == VK_NULL_HANDLE || fbSamp == VK_NULL_HANDLE)
        return;

    std::vector<VkDescriptorImageInfo> infos = {};
    infos.resize(vkcfg::kMaxTextureCount);

    for (uint32_t i = 0; i < vkcfg::kMaxTextureCount; ++i)
    {
        VkDescriptorImageInfo info = {};
        info.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        info.imageView             = fbView;
        info.sampler               = fbSamp;
        infos[i]                   = info;
    }

    const int texCount = static_cast<int>(textureHandler.size());
    const int count    = std::min(texCount, static_cast<int>(vkcfg::kMaxTextureCount));

    for (int i = 0; i < count; ++i)
    {
        const GpuTexture* tex = textureHandler.get(i);
        if (!tex)
            continue;

        VkImageView view = (tex->view != VK_NULL_HANDLE) ? tex->view : fbView;
        VkSampler   samp = (tex->sampler != VK_NULL_HANDLE) ? tex->sampler : fbSamp;

        VkDescriptorImageInfo info = {};
        info.imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        info.imageView             = view;
        info.sampler               = samp;

        infos[static_cast<size_t>(i)] = info;
    }

    m_materialSets[frameIndex].writeCombinedImageSamplerArray(device, 1, infos);
}

//==================================================================
// Pipelines (swapchain-level)
//==================================================================

bool Renderer::createPipelines(VkRenderPass renderPass)
{
    if (m_pipelineLayout == VK_NULL_HANDLE)
    {
        std::cerr << "RendererVK: createPipelines called before pipeline layout was created.\n";
        return false;
    }

    if (!m_ctx.device)
        return false;

    VkVertexInputBindingDescription      solidBindings[4] = {};
    VkVertexInputAttributeDescription    solidAttrs[4]    = {};
    VkPipelineVertexInputStateCreateInfo viSolid{};
    vkutil::makeSolidVertexInput(viSolid, solidBindings, solidAttrs);

    VkVertexInputBindingDescription      lineBinding{};
    VkVertexInputAttributeDescription    lineAttr{};
    VkPipelineVertexInputStateCreateInfo viLines{};
    vkutil::makeLineVertexInput(viLines, lineBinding, lineAttr);

    VkVertexInputBindingDescription overlayBinding{};
    overlayBinding.binding   = 0;
    overlayBinding.stride    = sizeof(OverlayVertex);
    overlayBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription overlayAttrs[3]{};

    overlayAttrs[0].location = 0;
    overlayAttrs[0].binding  = 0;
    overlayAttrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    overlayAttrs[0].offset   = static_cast<uint32_t>(offsetof(OverlayVertex, pos));

    overlayAttrs[1].location = 1;
    overlayAttrs[1].binding  = 0;
    overlayAttrs[1].format   = VK_FORMAT_R32_SFLOAT;
    overlayAttrs[1].offset   = static_cast<uint32_t>(offsetof(OverlayVertex, thickness));

    overlayAttrs[2].location = 2;
    overlayAttrs[2].binding  = 0;
    overlayAttrs[2].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    overlayAttrs[2].offset   = static_cast<uint32_t>(offsetof(OverlayVertex, color));

    VkPipelineVertexInputStateCreateInfo viOverlay{};
    viOverlay.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    viOverlay.vertexBindingDescriptionCount   = 1;
    viOverlay.pVertexBindingDescriptions      = &overlayBinding;
    viOverlay.vertexAttributeDescriptionCount = 3;
    viOverlay.pVertexAttributeDescriptions    = overlayAttrs;

    VkVertexInputBindingDescription overlayFillBinding{};
    overlayFillBinding.binding   = 0;
    overlayFillBinding.stride    = sizeof(OverlayFillVertex);
    overlayFillBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription overlayFillAttrs[2]{};

    overlayFillAttrs[0].location = 0;
    overlayFillAttrs[0].binding  = 0;
    overlayFillAttrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    overlayFillAttrs[0].offset   = static_cast<uint32_t>(offsetof(OverlayFillVertex, pos));

    overlayFillAttrs[1].location = 1;
    overlayFillAttrs[1].binding  = 0;
    overlayFillAttrs[1].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    overlayFillAttrs[1].offset   = static_cast<uint32_t>(offsetof(OverlayFillVertex, color));

    VkPipelineVertexInputStateCreateInfo viOverlayFill{};
    viOverlayFill.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    viOverlayFill.vertexBindingDescriptionCount   = 1;
    viOverlayFill.pVertexBindingDescriptions      = &overlayFillBinding;
    viOverlayFill.vertexAttributeDescriptionCount = 2;
    viOverlayFill.pVertexAttributeDescriptions    = overlayFillAttrs;

    if (!vkutil::createSolidPipeline(m_ctx, renderPass, m_pipelineLayout, m_ctx.sampleCount, viSolid, m_solidPipeline))
    {
        std::cerr << "RendererVK: createSolidPipeline failed.\n";
        return false;
    }

    if (!vkutil::createShadedPipeline(m_ctx, renderPass, m_pipelineLayout, m_ctx.sampleCount, viSolid, m_shadedPipeline))
    {
        std::cerr << "RendererVK: createShadedPipeline failed.\n";
        return false;
    }

    if (!vkutil::createDepthOnlyPipeline(m_ctx, renderPass, m_pipelineLayout, m_ctx.sampleCount, viSolid, m_depthOnlyPipeline))
    {
        std::cerr << "RendererVK: createDepthOnlyPipeline failed.\n";
        return false;
    }

    if (!vkutil::createWireframePipeline(m_ctx, renderPass, m_pipelineLayout, m_ctx.sampleCount, viLines, m_wirePipeline))
    {
        std::cerr << "RendererVK: createWireframePipeline failed.\n";
        return false;
    }

    if (!vkutil::createWireframeHiddenPipeline(m_ctx, renderPass, m_pipelineLayout, m_ctx.sampleCount, viLines, m_wireHiddenPipeline))
    {
        std::cerr << "RendererVK: createWireframeHiddenPipeline failed.\n";
        return false;
    }

    if (!vkutil::createWireframeDepthBiasPipeline(m_ctx, renderPass, m_pipelineLayout, m_ctx.sampleCount, viLines, m_wireOverlayPipeline))
    {
        std::cerr << "RendererVK: createWireframeDepthBiasPipeline failed.\n";
        return false;
    }

    if (!vkutil::createOverlayPipeline(m_ctx, renderPass, m_pipelineLayout, m_ctx.sampleCount, viOverlay, m_overlayPipeline))
    {
        std::cerr << "RendererVK: createOverlayPipeline failed.\n";
        return false;
    }

    if (!vkutil::createOverlayFillPipeline(m_ctx, renderPass, m_pipelineLayout, m_ctx.sampleCount, viOverlayFill, m_overlayFillPipeline))
    {
        std::cerr << "RendererVK: createOverlayFillPipeline failed.\n";
        return false;
    }

    // ------------------------------------------------------------
    // Selection pipelines (unchanged)
    // ------------------------------------------------------------
    const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

    ShaderStage selVert =
        vkutil::loadStage(m_ctx.device, shaderDir, "Selection.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    ShaderStage selFrag =
        vkutil::loadStage(m_ctx.device, shaderDir, "Selection.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    ShaderStage selVertFrag =
        vkutil::loadStage(m_ctx.device, shaderDir, "SelectionVert.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    if (!selVert.isValid() || !selFrag.isValid() || !selVertFrag.isValid())
    {
        std::cerr << "RendererVK: Failed to load selection shaders.\n";
        return false;
    }

    VkPipelineShaderStageCreateInfo selStages[2] = {
        selVert.stageInfo(),
        selFrag.stageInfo(),
    };

    VkPipelineShaderStageCreateInfo selVertStages[2] = {
        selVert.stageInfo(),
        selVertFrag.stageInfo(),
    };

    vkutil::MeshPipelinePreset selVertPreset{
        .topology            = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
        .polygonMode         = VK_POLYGON_MODE_FILL,
        .cullMode            = VK_CULL_MODE_NONE,
        .frontFace           = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest           = true,
        .depthWrite          = false,
        .depthCompareOp      = VK_COMPARE_OP_LESS_OR_EQUAL,
        .enableBlend         = true,
        .enableDepthBias     = true,
        .colorWrite          = true,
        .sampleShadingEnable = false,
        .minSampleShading    = 1.0f,
    };

    vkutil::MeshPipelinePreset selEdgePreset{
        .topology            = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        .polygonMode         = VK_POLYGON_MODE_FILL,
        .cullMode            = VK_CULL_MODE_NONE,
        .frontFace           = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest           = true,
        .depthWrite          = false,
        .depthCompareOp      = VK_COMPARE_OP_LESS_OR_EQUAL,
        .enableBlend         = true,
        .enableDepthBias     = true,
        .colorWrite          = true,
        .sampleShadingEnable = false,
        .minSampleShading    = 1.0f,
    };

    vkutil::MeshPipelinePreset selPolyPreset{
        .topology            = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .polygonMode         = VK_POLYGON_MODE_FILL,
        .cullMode            = VK_CULL_MODE_NONE,
        .frontFace           = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthTest           = true,
        .depthWrite          = false,
        .depthCompareOp      = VK_COMPARE_OP_LESS_OR_EQUAL,
        .enableBlend         = true,
        .enableDepthBias     = true,
        .colorWrite          = true,
        .sampleShadingEnable = false,
        .minSampleShading    = 1.0f,
    };

    vkutil::MeshPipelinePreset selVertHiddenPreset = selVertPreset;
    selVertHiddenPreset.depthCompareOp             = VK_COMPARE_OP_GREATER;

    vkutil::MeshPipelinePreset selEdgeHiddenPreset = selEdgePreset;
    selEdgeHiddenPreset.depthCompareOp             = VK_COMPARE_OP_GREATER;

    vkutil::MeshPipelinePreset selPolyHiddenPreset = selPolyPreset;
    selPolyHiddenPreset.depthCompareOp             = VK_COMPARE_OP_GREATER;

    auto wrapSelPipeline = [&](GraphicsPipeline& dst, VkPipeline src) -> bool {
        if (!src)
            return false;
        dst.destroy();
        dst.m_device   = m_ctx.device;
        dst.m_pipeline = src;
        return true;
    };

    {
        VkPipeline p = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selVertStages, 2, &viLines, selVertPreset);
        if (!wrapSelPipeline(m_selVertPipeline, p))
        {
            std::cerr << "RendererVK: createMeshPipeline(selection verts) failed.\n";
            return false;
        }
    }

    {
        VkPipeline p = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selStages, 2, &viLines, selEdgePreset);
        if (!wrapSelPipeline(m_selEdgePipeline, p))
        {
            std::cerr << "RendererVK: createMeshPipeline(selection edges) failed.\n";
            return false;
        }
    }

    {
        VkPipeline p = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selStages, 2, &viLines, selPolyPreset);
        if (!wrapSelPipeline(m_selPolyPipeline, p))
        {
            std::cerr << "RendererVK: createMeshPipeline(selection polys) failed.\n";
            return false;
        }
    }

    {
        VkPipeline p = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selVertStages, 2, &viLines, selVertHiddenPreset);
        if (!wrapSelPipeline(m_selVertHiddenPipeline, p))
        {
            std::cerr << "RendererVK: createMeshPipeline(selection verts hidden) failed.\n";
            return false;
        }
    }

    {
        VkPipeline p = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selStages, 2, &viLines, selEdgeHiddenPreset);
        if (!wrapSelPipeline(m_selEdgeHiddenPipeline, p))
        {
            std::cerr << "RendererVK: createMeshPipeline(selection edges hidden) failed.\n";
            return false;
        }
    }

    {
        VkPipeline p = createMeshPipeline(m_ctx, renderPass, m_pipelineLayout, selStages, 2, &viLines, selPolyHiddenPreset);
        if (!wrapSelPipeline(m_selPolyHiddenPipeline, p))
        {
            std::cerr << "RendererVK: createMeshPipeline(selection polys hidden) failed.\n";
            return false;
        }
    }

    return true;
}

//==================================================================
// Per-viewport Camera + Lights UBO (lazy allocation)
//==================================================================

Renderer::ViewportUboState& Renderer::ensureViewportUboState(Viewport* vp, uint32_t frameIdx)
{
    ViewportUboState& s = m_viewportUbos[vp];

    if (frameIdx >= m_framesInFlight || frameIdx >= vkcfg::kMaxFramesInFlight)
        return s;

    bool needWrite = false;

    if (s.uboSets[frameIdx].set() == VK_NULL_HANDLE)
    {
        if (!s.uboSets[frameIdx].allocate(m_ctx.device, m_descriptorPool.pool(), m_descriptorSetLayout.layout()))
        {
            std::cerr << "RendererVK: Failed to allocate frame-globals UBO set for viewport frame " << frameIdx << ".\n";
            return s;
        }
        needWrite = true;
    }

    if (!s.cameraBuffers[frameIdx].valid())
    {
        s.cameraBuffers[frameIdx].create(m_ctx.device,
                                         m_ctx.physicalDevice,
                                         sizeof(CameraUBO),
                                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                         true);

        if (!s.cameraBuffers[frameIdx].valid())
        {
            std::cerr << "RendererVK: Failed to create Camera UBO for viewport frame " << frameIdx << ".\n";
            return s;
        }
        needWrite = true;
    }

    if (!s.lightBuffers[frameIdx].valid())
    {
        s.lightBuffers[frameIdx].create(m_ctx.device,
                                        m_ctx.physicalDevice,
                                        sizeof(GpuLightsUBO),
                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        true);

        if (!s.lightBuffers[frameIdx].valid())
        {
            std::cerr << "RendererVK: Failed to create Lights UBO for viewport frame " << frameIdx << ".\n";
            return s;
        }

        GpuLightsUBO zero = {};
        s.lightBuffers[frameIdx].upload(&zero, sizeof(zero));
        needWrite = true;
    }

    if (needWrite)
    {
        s.uboSets[frameIdx].writeUniformBuffer(m_ctx.device, 0, s.cameraBuffers[frameIdx].buffer(), sizeof(CameraUBO));
        s.uboSets[frameIdx].writeUniformBuffer(m_ctx.device, 1, s.lightBuffers[frameIdx].buffer(), sizeof(GpuLightsUBO));
    }

    return s;
}

//==================================================================
// forEachVisibleMesh
//==================================================================

template<typename Fn>
void Renderer::forEachVisibleMesh(Scene* scene, Fn&& fn)
{
    if (!scene)
        return;

    for (SceneMesh* sm : scene->sceneMeshes())
    {
        if (!sm || !sm->visible())
            continue;

        if (!sm->gpu())
            sm->gpu(std::make_unique<MeshGpuResources>(&m_ctx, sm));

        MeshGpuResources* gpu = sm->gpu();
        if (!gpu)
            continue;

        fn(sm, gpu);
    }
}
