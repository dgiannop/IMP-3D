#include "GraphicsPipelines.hpp"

#include <array>
#include <filesystem>
#include <iostream>

#include "ShaderStage.hpp"
#include "VkPipelineHelpers.hpp"
#include "VkUtilities.hpp"

// ---------------------------------------------------------
// GraphicsPipeline destroy
// ---------------------------------------------------------
void GraphicsPipeline::destroy() noexcept
{
    if (!m_device)
        return;

    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }

    m_device = VK_NULL_HANDLE;
}

namespace
{

    inline bool checkCommonInputs(const VulkanContext& ctx, VkRenderPass renderPass, VkPipelineLayout layout) noexcept
    {
        return ctx.device != VK_NULL_HANDLE &&
               renderPass != VK_NULL_HANDLE &&
               layout != VK_NULL_HANDLE;
    }

    inline VkPipelineViewportStateCreateInfo makeViewportState() noexcept
    {
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount  = 1;
        return vp;
    }

    inline VkPipelineMultisampleStateCreateInfo makeMultisampleState(VkSampleCountFlagBits samples) noexcept
    {
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = samples;
        ms.sampleShadingEnable  = VK_FALSE;
        return ms;
    }

    inline VkPipelineDepthStencilStateCreateInfo makeDepthState(bool testEnable, bool writeEnable, VkCompareOp compareOp) noexcept
    {
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable       = testEnable ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable      = writeEnable ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp        = compareOp;
        ds.depthBoundsTestEnable = VK_FALSE;
        ds.stencilTestEnable     = VK_FALSE;
        return ds;
    }

    inline VkPipelineColorBlendStateCreateInfo makeOpaqueColorBlend(VkPipelineColorBlendAttachmentState& outAtt) noexcept
    {
        outAtt                = {};
        outAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT |
                                VK_COLOR_COMPONENT_A_BIT;
        outAtt.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.logicOpEnable   = VK_FALSE;
        cb.attachmentCount = 1;
        cb.pAttachments    = &outAtt;
        return cb;
    }

    inline VkPipelineColorBlendStateCreateInfo makeAlphaColorBlend(VkPipelineColorBlendAttachmentState& outAtt) noexcept
    {
        outAtt                = {};
        outAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT |
                                VK_COLOR_COMPONENT_A_BIT;

        outAtt.blendEnable         = VK_TRUE;
        outAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        outAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        outAtt.colorBlendOp        = VK_BLEND_OP_ADD;
        outAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        outAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        outAtt.alphaBlendOp        = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.logicOpEnable   = VK_FALSE;
        cb.attachmentCount = 1;
        cb.pAttachments    = &outAtt;
        return cb;
    }

    inline VkPipelineRasterizationStateCreateInfo makeRasterStateFillNoCull(bool depthBias = false) noexcept
    {
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.depthClampEnable        = VK_FALSE;
        rs.rasterizerDiscardEnable = VK_FALSE;
        rs.polygonMode             = VK_POLYGON_MODE_FILL;
        rs.cullMode                = VK_CULL_MODE_NONE;
        rs.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.depthBiasEnable         = depthBias ? VK_TRUE : VK_FALSE;
        rs.lineWidth               = 1.0f;
        return rs;
    }

    inline VkPipelineRasterizationStateCreateInfo makeRasterStateFillBackfaceCull(bool depthBias = false) noexcept
    {
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.depthClampEnable        = VK_FALSE;
        rs.rasterizerDiscardEnable = VK_FALSE;
        rs.polygonMode             = VK_POLYGON_MODE_FILL;
        rs.cullMode                = VK_CULL_MODE_BACK_BIT;
        rs.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.depthBiasEnable         = depthBias ? VK_TRUE : VK_FALSE;
        rs.lineWidth               = 1.0f;
        return rs;
    }

} // namespace

namespace vkutil
{

    // ---------------------------------------------------------
    // Solid pipeline
    // ---------------------------------------------------------
    bool createSolidPipeline(const VulkanContext&                        ctx,
                             VkRenderPass                                renderPass,
                             VkPipelineLayout                            layout,
                             VkSampleCountFlagBits                       sampleCount,
                             const VkPipelineVertexInputStateCreateInfo& vi,
                             GraphicsPipeline&                           out)
    {
        out.destroy();

        if (!checkCommonInputs(ctx, renderPass, layout))
            return false;

        out.m_device = ctx.device;

        const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

        ShaderStage vs =
            vkutil::loadStage(ctx.device, shaderDir, "SolidDraw.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        ShaderStage fs =
            vkutil::loadStage(ctx.device, shaderDir, "SolidDraw.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        if (!vs.isValid() || !fs.isValid())
        {
            std::cerr << "GraphicsPipelines: Failed to load SolidDraw shaders.\n";
            out.destroy();
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {
            vs.stageInfo(),
            fs.stageInfo(),
        };

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ia.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo vp = makeViewportState();
        // Match original: no cull
        VkPipelineRasterizationStateCreateInfo rs = makeRasterStateFillNoCull(false);
        VkPipelineMultisampleStateCreateInfo   ms = makeMultisampleState(sampleCount);
        // Match solidPreset: depth test/write ON, compare LESS
        VkPipelineDepthStencilStateCreateInfo ds =
            makeDepthState(true, true, VK_COMPARE_OP_LESS);

        VkPipelineColorBlendAttachmentState att{};
        VkPipelineColorBlendStateCreateInfo cb = makeOpaqueColorBlend(att);

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dyn =
            vkutil::makeDynamicState(dynStates, static_cast<uint32_t>(std::size(dynStates)));

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
        info.layout              = layout;
        info.renderPass          = renderPass;
        info.subpass             = 0;

        if (vkCreateGraphicsPipelines(ctx.device,
                                      VK_NULL_HANDLE,
                                      1,
                                      &info,
                                      nullptr,
                                      &out.m_pipeline) != VK_SUCCESS)
        {
            std::cerr << "GraphicsPipelines: vkCreateGraphicsPipelines(Solid) failed.\n";
            out.destroy();
            return false;
        }

        return true;
    }

    // ---------------------------------------------------------
    // Shaded pipeline
    // ---------------------------------------------------------
    bool createShadedPipeline(const VulkanContext&                        ctx,
                              VkRenderPass                                renderPass,
                              VkPipelineLayout                            layout,
                              VkSampleCountFlagBits                       sampleCount,
                              const VkPipelineVertexInputStateCreateInfo& vi,
                              GraphicsPipeline&                           out)
    {
        out.destroy();

        if (!checkCommonInputs(ctx, renderPass, layout))
            return false;

        out.m_device = ctx.device;

        const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

        ShaderStage vs =
            vkutil::loadStage(ctx.device, shaderDir, "ShadedDraw.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        ShaderStage fs =
            vkutil::loadStage(ctx.device, shaderDir, "ShadedDraw.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        if (!vs.isValid() || !fs.isValid())
        {
            std::cerr << "GraphicsPipelines: Failed to load ShadedDraw shaders.\n";
            out.destroy();
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {
            vs.stageInfo(),
            fs.stageInfo(),
        };

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ia.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo vp = makeViewportState();

        VkPipelineRasterizationStateCreateInfo rs = makeRasterStateFillNoCull(false);

        rs.cullMode = VK_CULL_MODE_BACK_BIT;
        // rs.cullMode = VK_CULL_MODE_NONE; // quick debug

        VkPipelineMultisampleStateCreateInfo ms = makeMultisampleState(sampleCount);

        VkPipelineDepthStencilStateCreateInfo ds =
            makeDepthState(true, true, VK_COMPARE_OP_LESS);

        VkPipelineColorBlendAttachmentState att{};
        VkPipelineColorBlendStateCreateInfo cb = makeOpaqueColorBlend(att);

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        VkPipelineDynamicStateCreateInfo dyn =
            vkutil::makeDynamicState(dynStates, static_cast<uint32_t>(std::size(dynStates)));

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
        info.layout              = layout;
        info.renderPass          = renderPass;
        info.subpass             = 0;

        if (vkCreateGraphicsPipelines(ctx.device,
                                      VK_NULL_HANDLE,
                                      1,
                                      &info,
                                      nullptr,
                                      &out.m_pipeline) != VK_SUCCESS)
        {
            std::cerr << "GraphicsPipelines: vkCreateGraphicsPipelines(Shaded) failed.\n";
            out.destroy();
            return false;
        }

        return true;
    }

    // ---------------------------------------------------------
    // Depth-only triangle prepass
    // ---------------------------------------------------------
    bool createDepthOnlyPipeline(const VulkanContext&                        ctx,
                                 VkRenderPass                                renderPass,
                                 VkPipelineLayout                            layout,
                                 VkSampleCountFlagBits                       sampleCount,
                                 const VkPipelineVertexInputStateCreateInfo& vi,
                                 GraphicsPipeline&                           out)
    {
        out.destroy();

        if (!checkCommonInputs(ctx, renderPass, layout))
            return false;

        out.m_device = ctx.device;

        const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

        // Only vertex shader; no fragment shader -> depth only.
        ShaderStage vs =
            vkutil::loadStage(ctx.device, shaderDir, "SolidDraw.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);

        if (!vs.isValid())
        {
            std::cerr << "GraphicsPipelines: Failed to load SolidDraw.vert for depth-only.\n";
            out.destroy();
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[1] = {
            vs.stageInfo(),
        };

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ia.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo      vp = makeViewportState();
        VkPipelineRasterizationStateCreateInfo rs = makeRasterStateFillNoCull(false);
        VkPipelineMultisampleStateCreateInfo   ms = makeMultisampleState(sampleCount);
        // depthOnlyPreset: test ON, write ON, compare LE
        VkPipelineDepthStencilStateCreateInfo ds =
            makeDepthState(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

        // Color writes OFF
        VkPipelineColorBlendAttachmentState att{};
        att.colorWriteMask = 0;
        att.blendEnable    = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.logicOpEnable   = VK_FALSE;
        cb.attachmentCount = 1;
        cb.pAttachments    = &att;

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dyn =
            vkutil::makeDynamicState(dynStates, static_cast<uint32_t>(std::size(dynStates)));

        VkGraphicsPipelineCreateInfo info{};
        info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount          = 1;
        info.pStages             = stages;
        info.pVertexInputState   = &vi;
        info.pInputAssemblyState = &ia;
        info.pViewportState      = &vp;
        info.pRasterizationState = &rs;
        info.pMultisampleState   = &ms;
        info.pDepthStencilState  = &ds;
        info.pColorBlendState    = &cb;
        info.pDynamicState       = &dyn;
        info.layout              = layout;
        info.renderPass          = renderPass;
        info.subpass             = 0;

        if (vkCreateGraphicsPipelines(ctx.device,
                                      VK_NULL_HANDLE,
                                      1,
                                      &info,
                                      nullptr,
                                      &out.m_pipeline) != VK_SUCCESS)
        {
            std::cerr << "GraphicsPipelines: vkCreateGraphicsPipelines(DepthOnly) failed.\n";
            out.destroy();
            return false;
        }

        return true;
    }

    // ---------------------------------------------------------
    // Wireframe
    // ---------------------------------------------------------
    bool createWireframePipeline(const VulkanContext&                        ctx,
                                 VkRenderPass                                renderPass,
                                 VkPipelineLayout                            layout,
                                 VkSampleCountFlagBits                       sampleCount,
                                 const VkPipelineVertexInputStateCreateInfo& vi,
                                 GraphicsPipeline&                           out)
    {
        out.destroy();

        if (!checkCommonInputs(ctx, renderPass, layout))
            return false;

        out.m_device = ctx.device;

        const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

        ShaderStage vs =
            vkutil::loadStage(ctx.device, shaderDir, "Wireframe.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        ShaderStage fs =
            vkutil::loadStage(ctx.device, shaderDir, "Wireframe.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        if (!vs.isValid() || !fs.isValid())
        {
            std::cerr << "GraphicsPipelines: Failed to load Wireframe shaders.\n";
            out.destroy();
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {
            vs.stageInfo(),
            fs.stageInfo(),
        };

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology               = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        ia.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo      vp = makeViewportState();
        VkPipelineRasterizationStateCreateInfo rs = makeRasterStateFillNoCull(false);
        VkPipelineMultisampleStateCreateInfo   ms = makeMultisampleState(sampleCount);
        // wirePreset: depth test ON, write OFF, compare LE
        VkPipelineDepthStencilStateCreateInfo ds =
            makeDepthState(true, false, VK_COMPARE_OP_LESS_OR_EQUAL);

        // Alpha blending so wireHiddenColor alpha is respected.
        VkPipelineColorBlendAttachmentState att{};
        VkPipelineColorBlendStateCreateInfo cb = makeAlphaColorBlend(att);

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dyn =
            vkutil::makeDynamicState(dynStates, static_cast<uint32_t>(std::size(dynStates)));

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
        info.layout              = layout;
        info.renderPass          = renderPass;
        info.subpass             = 0;

        if (vkCreateGraphicsPipelines(ctx.device,
                                      VK_NULL_HANDLE,
                                      1,
                                      &info,
                                      nullptr,
                                      &out.m_pipeline) != VK_SUCCESS)
        {
            std::cerr << "GraphicsPipelines: vkCreateGraphicsPipelines(Wireframe) failed.\n";
            out.destroy();
            return false;
        }

        return true;
    }

    // ---------------------------------------------------------
    // Wireframe (hidden, depthCompare GREATER)
    // ---------------------------------------------------------
    bool createWireframeHiddenPipeline(const VulkanContext&                        ctx,
                                       VkRenderPass                                renderPass,
                                       VkPipelineLayout                            layout,
                                       VkSampleCountFlagBits                       sampleCount,
                                       const VkPipelineVertexInputStateCreateInfo& vi,
                                       GraphicsPipeline&                           out)
    {
        out.destroy();

        if (!checkCommonInputs(ctx, renderPass, layout))
            return false;

        out.m_device = ctx.device;

        const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

        ShaderStage vs =
            vkutil::loadStage(ctx.device, shaderDir, "Wireframe.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        ShaderStage fs =
            vkutil::loadStage(ctx.device, shaderDir, "Wireframe.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        if (!vs.isValid() || !fs.isValid())
        {
            std::cerr << "GraphicsPipelines: Failed to load WireframeHidden shaders.\n";
            out.destroy();
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {
            vs.stageInfo(),
            fs.stageInfo(),
        };

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology               = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        ia.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo      vp = makeViewportState();
        VkPipelineRasterizationStateCreateInfo rs = makeRasterStateFillNoCull(false);
        VkPipelineMultisampleStateCreateInfo   ms = makeMultisampleState(sampleCount);
        // hiddenEdgePreset: depth test ON, write OFF, compare GREATER
        VkPipelineDepthStencilStateCreateInfo ds =
            makeDepthState(true, false, VK_COMPARE_OP_GREATER);

        VkPipelineColorBlendAttachmentState att{};
        VkPipelineColorBlendStateCreateInfo cb = makeAlphaColorBlend(att);

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dyn =
            vkutil::makeDynamicState(dynStates, static_cast<uint32_t>(std::size(dynStates)));

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
        info.layout              = layout;
        info.renderPass          = renderPass;
        info.subpass             = 0;

        if (vkCreateGraphicsPipelines(ctx.device,
                                      VK_NULL_HANDLE,
                                      1,
                                      &info,
                                      nullptr,
                                      &out.m_pipeline) != VK_SUCCESS)
        {
            std::cerr << "GraphicsPipelines: vkCreateGraphicsPipelines(WireframeHidden) failed.\n";
            out.destroy();
            return false;
        }

        return true;
    }

    // ---------------------------------------------------------
    // Wireframe (depth-bias overlay in SOLID mode)
    // ---------------------------------------------------------
    bool createWireframeDepthBiasPipeline(const VulkanContext&                        ctx,
                                          VkRenderPass                                renderPass,
                                          VkPipelineLayout                            layout,
                                          VkSampleCountFlagBits                       sampleCount,
                                          const VkPipelineVertexInputStateCreateInfo& vi,
                                          GraphicsPipeline&                           out)
    {
        out.destroy();

        if (!checkCommonInputs(ctx, renderPass, layout))
            return false;

        out.m_device = ctx.device;

        const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

        ShaderStage vs = vkutil::loadStage(ctx.device,
                                           shaderDir,
                                           "WireframeDepthBias.vert.spv",
                                           VK_SHADER_STAGE_VERTEX_BIT);
        ShaderStage fs =
            vkutil::loadStage(ctx.device, shaderDir, "Wireframe.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        if (!vs.isValid() || !fs.isValid())
        {
            std::cerr << "GraphicsPipelines: Failed to load WireframeDepthBias shaders.\n";
            out.destroy();
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {
            vs.stageInfo(),
            fs.stageInfo(),
        };

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology               = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        ia.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo      vp = makeViewportState();
        VkPipelineRasterizationStateCreateInfo rs = makeRasterStateFillNoCull(false);
        VkPipelineMultisampleStateCreateInfo   ms = makeMultisampleState(sampleCount);
        // Same depth as wirePreset
        VkPipelineDepthStencilStateCreateInfo ds =
            makeDepthState(true, false, VK_COMPARE_OP_LESS_OR_EQUAL);

        VkPipelineColorBlendAttachmentState att{};
        VkPipelineColorBlendStateCreateInfo cb = makeAlphaColorBlend(att);

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dyn =
            vkutil::makeDynamicState(dynStates, static_cast<uint32_t>(std::size(dynStates)));

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
        info.layout              = layout;
        info.renderPass          = renderPass;
        info.subpass             = 0;

        if (vkCreateGraphicsPipelines(ctx.device,
                                      VK_NULL_HANDLE,
                                      1,
                                      &info,
                                      nullptr,
                                      &out.m_pipeline) != VK_SUCCESS)
        {
            std::cerr << "GraphicsPipelines: vkCreateGraphicsPipelines(WireframeDepthBias) failed.\n";
            out.destroy();
            return false;
        }

        return true;
    }

    // ---------------------------------------------------------
    // Overlay (gizmos, handles)
    // ---------------------------------------------------------
    bool createOverlayPipeline(const VulkanContext&                        ctx,
                               VkRenderPass                                renderPass,
                               VkPipelineLayout                            layout,
                               VkSampleCountFlagBits                       sampleCount,
                               const VkPipelineVertexInputStateCreateInfo& vi,
                               GraphicsPipeline&                           out)
    {
        out.destroy();

        if (!checkCommonInputs(ctx, renderPass, layout))
            return false;

        out.m_device = ctx.device;

        const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

        ShaderStage vs =
            vkutil::loadStage(ctx.device, shaderDir, "Overlay.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        ShaderStage gs =
            vkutil::loadStage(ctx.device, shaderDir, "Overlay.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT);
        ShaderStage fs =
            vkutil::loadStage(ctx.device, shaderDir, "Overlay.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        if (!vs.isValid() || !gs.isValid() || !fs.isValid())
        {
            std::cerr << "GraphicsPipelines: Failed to load Overlay shaders.\n";
            out.destroy();
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[3] = {
            vs.stageInfo(),
            gs.stageInfo(),
            fs.stageInfo(),
        };

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology               = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        ia.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo      vp = makeViewportState();
        VkPipelineRasterizationStateCreateInfo rs = makeRasterStateFillNoCull(false);
        VkPipelineMultisampleStateCreateInfo   ms = makeMultisampleState(sampleCount);
        // overlayPreset: depth OFF
        VkPipelineDepthStencilStateCreateInfo ds =
            makeDepthState(false, false, VK_COMPARE_OP_LESS);

        VkPipelineColorBlendAttachmentState att{};
        VkPipelineColorBlendStateCreateInfo cb = makeAlphaColorBlend(att);

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dyn =
            vkutil::makeDynamicState(dynStates, static_cast<uint32_t>(std::size(dynStates)));

        VkGraphicsPipelineCreateInfo info{};
        info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount          = 3;
        info.pStages             = stages;
        info.pVertexInputState   = &vi;
        info.pInputAssemblyState = &ia;
        info.pViewportState      = &vp;
        info.pRasterizationState = &rs;
        info.pMultisampleState   = &ms;
        info.pDepthStencilState  = &ds;
        info.pColorBlendState    = &cb;
        info.pDynamicState       = &dyn;
        info.layout              = layout;
        info.renderPass          = renderPass;
        info.subpass             = 0;

        if (vkCreateGraphicsPipelines(ctx.device,
                                      VK_NULL_HANDLE,
                                      1,
                                      &info,
                                      nullptr,
                                      &out.m_pipeline) != VK_SUCCESS)
        {
            std::cerr << "GraphicsPipelines: vkCreateGraphicsPipelines(Overlay) failed.\n";
            out.destroy();
            return false;
        }

        return true;
    }

    // ---------------------------------------------------------
    // Overlay Fill (filled gizmos / circles / discs)
    // ---------------------------------------------------------
    bool createOverlayFillPipeline(const VulkanContext&                        ctx,
                                   VkRenderPass                                renderPass,
                                   VkPipelineLayout                            layout,
                                   VkSampleCountFlagBits                       sampleCount,
                                   const VkPipelineVertexInputStateCreateInfo& vi,
                                   GraphicsPipeline&                           out)
    {
        out.destroy();

        if (!checkCommonInputs(ctx, renderPass, layout))
            return false;

        out.m_device = ctx.device;

        const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

        ShaderStage vs =
            vkutil::loadStage(ctx.device, shaderDir, "OverlayFill.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        ShaderStage fs =
            vkutil::loadStage(ctx.device, shaderDir, "OverlayFill.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        if (!vs.isValid() || !fs.isValid())
        {
            std::cerr << "GraphicsPipelines: Failed to load OverlayFill shaders.\n";
            out.destroy();
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {
            vs.stageInfo(),
            fs.stageInfo(),
        };

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ia.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo      vp = makeViewportState();
        VkPipelineRasterizationStateCreateInfo rs = makeRasterStateFillNoCull(false);
        VkPipelineMultisampleStateCreateInfo   ms = makeMultisampleState(sampleCount);

        // overlayPreset: depth OFF
        VkPipelineDepthStencilStateCreateInfo ds =
            makeDepthState(false, false, VK_COMPARE_OP_LESS);

        VkPipelineColorBlendAttachmentState att{};
        VkPipelineColorBlendStateCreateInfo cb = makeAlphaColorBlend(att);

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dyn =
            vkutil::makeDynamicState(dynStates, static_cast<uint32_t>(std::size(dynStates)));

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
        info.layout              = layout;
        info.renderPass          = renderPass;
        info.subpass             = 0;

        if (vkCreateGraphicsPipelines(ctx.device,
                                      VK_NULL_HANDLE,
                                      1,
                                      &info,
                                      nullptr,
                                      &out.m_pipeline) != VK_SUCCESS)
        {
            std::cerr << "GraphicsPipelines: vkCreateGraphicsPipelines(OverlayFill) failed.\n";
            out.destroy();
            return false;
        }

        return true;
    }

    // ---------------------------------------------------------
    // Selection (triangles)
    // ---------------------------------------------------------
    bool createSelectionTriPipeline(const VulkanContext&                        ctx,
                                    VkRenderPass                                renderPass,
                                    VkPipelineLayout                            layout,
                                    VkSampleCountFlagBits                       sampleCount,
                                    const VkPipelineVertexInputStateCreateInfo& vi,
                                    GraphicsPipeline&                           out)
    {
        out.destroy();

        if (!checkCommonInputs(ctx, renderPass, layout))
            return false;

        out.m_device = ctx.device;

        const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

        ShaderStage vs =
            vkutil::loadStage(ctx.device, shaderDir, "Selection.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        ShaderStage fs =
            vkutil::loadStage(ctx.device, shaderDir, "Selection.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        if (!vs.isValid() || !fs.isValid())
        {
            std::cerr << "GraphicsPipelines: Failed to load Selection (tri) shaders.\n";
            out.destroy();
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {
            vs.stageInfo(),
            fs.stageInfo(),
        };

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ia.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo      vp = makeViewportState();
        VkPipelineRasterizationStateCreateInfo rs = makeRasterStateFillNoCull(true);
        VkPipelineMultisampleStateCreateInfo   ms = makeMultisampleState(sampleCount);
        // selPolyPreset: depth test ON, write OFF, compare LE
        VkPipelineDepthStencilStateCreateInfo ds =
            makeDepthState(true, false, VK_COMPARE_OP_LESS_OR_EQUAL);

        VkPipelineColorBlendAttachmentState att{};
        VkPipelineColorBlendStateCreateInfo cb = makeAlphaColorBlend(att);

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
        };
        VkPipelineDynamicStateCreateInfo dyn =
            vkutil::makeDynamicState(dynStates, static_cast<uint32_t>(std::size(dynStates)));

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
        info.layout              = layout;
        info.renderPass          = renderPass;
        info.subpass             = 0;

        if (vkCreateGraphicsPipelines(ctx.device,
                                      VK_NULL_HANDLE,
                                      1,
                                      &info,
                                      nullptr,
                                      &out.m_pipeline) != VK_SUCCESS)
        {
            std::cerr << "GraphicsPipelines: vkCreateGraphicsPipelines(SelectionTri) failed.\n";
            out.destroy();
            return false;
        }

        return true;
    }

    // ---------------------------------------------------------
    // Selection (verts / points)
    // ---------------------------------------------------------
    bool createSelectionVertPipeline(const VulkanContext&                        ctx,
                                     VkRenderPass                                renderPass,
                                     VkPipelineLayout                            layout,
                                     VkSampleCountFlagBits                       sampleCount,
                                     const VkPipelineVertexInputStateCreateInfo& vi,
                                     GraphicsPipeline&                           out)
    {
        out.destroy();

        if (!checkCommonInputs(ctx, renderPass, layout))
            return false;

        out.m_device = ctx.device;

        const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

        ShaderStage vs =
            vkutil::loadStage(ctx.device, shaderDir, "Selection.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        ShaderStage fs =
            vkutil::loadStage(ctx.device, shaderDir, "SelectionVert.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        if (!vs.isValid() || !fs.isValid())
        {
            std::cerr << "GraphicsPipelines: Failed to load SelectionVert shaders.\n";
            out.destroy();
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {
            vs.stageInfo(),
            fs.stageInfo(),
        };

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology               = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        ia.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo      vp = makeViewportState();
        VkPipelineRasterizationStateCreateInfo rs = makeRasterStateFillNoCull(true);
        VkPipelineMultisampleStateCreateInfo   ms = makeMultisampleState(sampleCount);
        // selVertPreset: depth test ON, write OFF, compare LE
        VkPipelineDepthStencilStateCreateInfo ds =
            makeDepthState(true, false, VK_COMPARE_OP_LESS_OR_EQUAL);

        VkPipelineColorBlendAttachmentState att{};
        VkPipelineColorBlendStateCreateInfo cb = makeAlphaColorBlend(att);

        VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
        };
        VkPipelineDynamicStateCreateInfo dyn =
            vkutil::makeDynamicState(dynStates, static_cast<uint32_t>(std::size(dynStates)));

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
        info.layout              = layout;
        info.renderPass          = renderPass;
        info.subpass             = 0;

        if (vkCreateGraphicsPipelines(ctx.device,
                                      VK_NULL_HANDLE,
                                      1,
                                      &info,
                                      nullptr,
                                      &out.m_pipeline) != VK_SUCCESS)
        {
            std::cerr << "GraphicsPipelines: vkCreateGraphicsPipelines(SelectionVert) failed.\n";
            out.destroy();
            return false;
        }

        return true;
    }

} // namespace vkutil
