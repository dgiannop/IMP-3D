#include "GridRendererVK.hpp"

#include <cmath>   // std::abs
#include <cstddef> // offsetof
#include <filesystem>
#include <glm/vec3.hpp>
#include <iostream>
#include <vector>

#include "ShaderStage.hpp"
#include "VkPipelineHelpers.hpp"
#include "VkUtilities.hpp"

// ---------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------
GridRendererVK::GridRendererVK(VulkanContext* ctx) : m_ctx(ctx)
{
    // IMPORTANT:
    // Do NOT create GPU buffers here if you want clean lifetime control.
    // createDeviceResources() will be called by the Renderer after device init.
}

GridRendererVK::~GridRendererVK() noexcept
{
    // Final safety net; prefer explicit shutdown path.
    destroyDeviceResources();
}

// ---------------------------------------------------------
// Device resources
// ---------------------------------------------------------
void GridRendererVK::createDeviceResources()
{
    if (!m_ctx)
        return;

    // If already created, do nothing
    if (m_vertexBuffer.valid() && m_vertexCount > 0)
        return;

    createGridData();
}

void GridRendererVK::destroyDeviceResources() noexcept
{
    destroySwapchainResources();

    m_vertexBuffer.destroy();
    m_vertexCount = 0;
}

// ---------------------------------------------------------
// Swapchain resources
// ---------------------------------------------------------
void GridRendererVK::destroySwapchainResources() noexcept
{
    if (!m_ctx)
        return;

    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(m_ctx->device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------
// Grid data generation (world-space lines on XZ plane)
// ---------------------------------------------------------
void GridRendererVK::createGridData(float halfExtent, float spacing)
{
    if (!m_ctx)
        return;

    std::vector<GridVert> verts;
    verts.reserve(2000);

    const int steps = static_cast<int>((halfExtent * 2.0f) / spacing);

    // Visual tuning (minor / major / axis)
    const glm::vec4 gridColor{0.13f, 0.13f, 0.14f, 0.18f};
    const glm::vec4 majorColor{0.19f, 0.19f, 0.20f, 0.24f};
    const glm::vec4 axisColor{0.24f, 0.24f, 0.26f, 0.60f};

    const float eps       = 1e-6f;
    const float majorStep = spacing * 10.0f;

    auto isZero = [&](float v) noexcept -> bool {
        return std::abs(v) < eps;
    };

    auto isMajor = [&](float v) noexcept -> bool {
        // "Major" lines at multiples of majorStep.
        // Using fmod on abs() keeps it symmetric around 0.
        const float m = std::fmod(std::abs(v), majorStep);
        return m < eps || std::abs(m - majorStep) < eps;
    };

    auto lineColor = [&](float v) noexcept -> glm::vec4 {
        if (isZero(v))
            return axisColor;
        if (isMajor(v))
            return majorColor;
        return gridColor;
    };

    // Lines parallel to Z (varying X)
    for (int i = 0; i <= steps; ++i)
    {
        const float     x = -halfExtent + i * spacing;
        const glm::vec4 c = lineColor(x);

        verts.push_back({glm::vec3{x, 0.0f, -halfExtent}, c});
        verts.push_back({glm::vec3{x, 0.0f, halfExtent}, c});
    }

    // Lines parallel to X (varying Z)
    for (int i = 0; i <= steps; ++i)
    {
        const float     z = -halfExtent + i * spacing;
        const glm::vec4 c = lineColor(z);

        verts.push_back({glm::vec3{-halfExtent, 0.0f, z}, c});
        verts.push_back({glm::vec3{halfExtent, 0.0f, z}, c});
    }

    m_vertexCount = static_cast<uint32_t>(verts.size());
    const VkDeviceSize sizeBytes =
        static_cast<VkDeviceSize>(verts.size() * sizeof(GridVert));

    // HOST_VISIBLE so upload() is allowed (simple path)
    m_vertexBuffer.create(
        m_ctx->device,
        m_ctx->physicalDevice,
        sizeBytes,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (!m_vertexBuffer.valid())
    {
        std::cerr << "GridRendererVK: Failed to create vertex buffer.\n";
        m_vertexCount = 0;
        return;
    }

    m_vertexBuffer.upload(verts.data(), sizeBytes);
}

// ---------------------------------------------------------
// Pipeline creation (swapchain-dependent)
// ---------------------------------------------------------
bool GridRendererVK::createPipeline(VkRenderPass     renderPass,
                                    VkPipelineLayout sharedLayout)
{
    if (!m_ctx)
        return false;

    VkDevice device = m_ctx->device;

    // If we already have a pipeline, destroy it first (swapchain resource)
    destroySwapchainResources();

    const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

    ShaderStage vert = vkutil::loadStage(
        device,
        shaderDir,
        "Grid.vert.spv",
        VK_SHADER_STAGE_VERTEX_BIT);

    ShaderStage frag = vkutil::loadStage(
        device,
        shaderDir,
        "Grid.frag.spv",
        VK_SHADER_STAGE_FRAGMENT_BIT);

    if (!vert.isValid() || !frag.isValid())
    {
        std::cerr << "GridRendererVK: Failed to load grid shaders.\n";
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        vert.stageInfo(),
        frag.stageInfo(),
    };

    // -----------------------------------------------------
    // Vertex input: interleaved pos + color
    // binding 0:
    //   location 0 -> vec3 position
    //   location 1 -> vec4 color
    // -----------------------------------------------------
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(GridVert);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2]{};

    attrs[0].location = 0;
    attrs[0].binding  = 0;
    attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset   = static_cast<uint32_t>(offsetof(GridVert, pos));

    attrs[1].location = 1;
    attrs[1].binding  = 0;
    attrs[1].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[1].offset   = static_cast<uint32_t>(offsetof(GridVert, color));

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions    = attrs;

    // Input assembly: lines
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology               = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    ia.primitiveRestartEnable = VK_FALSE;

    // Viewport/scissor are dynamic
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    // Rasterization: enable depth bias (dynamic)
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.depthClampEnable        = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.polygonMode             = VK_POLYGON_MODE_FILL;
    rs.cullMode                = VK_CULL_MODE_NONE;
    rs.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.depthBiasEnable         = VK_TRUE; // required for vkCmdSetDepthBias
    rs.lineWidth               = 1.0f;

    // Multisampling: match swapchain sample count
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = m_ctx->sampleCount;
    ms.sampleShadingEnable  = VK_FALSE;

    // Depth/stencil: test on, no depth writes
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable       = VK_TRUE;
    ds.depthWriteEnable      = VK_FALSE;
    ds.depthCompareOp        = VK_COMPARE_OP_LESS_OR_EQUAL;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable     = VK_FALSE;

    // Color blend: alpha blending
    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                         VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT |
                         VK_COLOR_COMPONENT_A_BIT;

    att.blendEnable         = VK_TRUE;
    att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.colorBlendOp        = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.logicOpEnable   = VK_FALSE;
    cb.attachmentCount = 1;
    cb.pAttachments    = &att;

    // Dynamic state: viewport, scissor, depth bias
    VkDynamicState dynStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_BIAS,
    };

    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = static_cast<uint32_t>(std::size(dynStates));
    dyn.pDynamicStates    = dynStates;

    VkGraphicsPipelineCreateInfo info{};
    info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount          = 2;
    info.pStages             = stages;
    info.pVertexInputState   = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState      = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState   = &ms;
    info.pDepthStencilState  = &ds;
    info.pColorBlendState    = &cb;
    info.pDynamicState       = &dyn;
    info.layout              = sharedLayout;
    info.renderPass          = renderPass;
    info.subpass             = 0;

    if (vkCreateGraphicsPipelines(device,
                                  VK_NULL_HANDLE,
                                  1,
                                  &info,
                                  nullptr,
                                  &m_pipeline) != VK_SUCCESS)
    {
        std::cerr << "GridRendererVK: Failed to create graphics pipeline.\n";
        return false;
    }

    return true;
}

// ---------------------------------------------------------
// Render
// ---------------------------------------------------------
void GridRendererVK::render(VkCommandBuffer cmd)
{
    if (m_pipeline == VK_NULL_HANDLE)
        return;

    // If vertex buffer was never created (or got destroyed), don't draw.
    if (m_vertexCount == 0 || !m_vertexBuffer.valid())
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // Depth bias so the grid is stable against the ground plane.
    vkCmdSetDepthBias(cmd, 1.0f, 0.0f, 1.0f);

    VkBuffer     vb   = m_vertexBuffer.buffer();
    VkDeviceSize offs = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offs);

    vkCmdDraw(cmd, m_vertexCount, 1, 0, 0);
}
