#include "RtPresentPipeline.hpp"

#include <filesystem>
#include <iostream>

#include "ShaderStage.hpp"
#include "VkPipelineHelpers.hpp"

// SHADER_BIN_DIR is provided via compile definitions in CMake.

namespace vkrt
{

    // ------------------------------------------------------------
    // Move support
    // ------------------------------------------------------------

    RtPresentPipeline::RtPresentPipeline(RtPresentPipeline&& other) noexcept
    {
        moveFrom(std::move(other));
    }

    RtPresentPipeline& RtPresentPipeline::operator=(RtPresentPipeline&& other) noexcept
    {
        if (this != &other)
        {
            // We require a VkDevice to properly destroy; callers should
            // call destroy(device) explicitly. For moves, we can just
            // drop our handles and steal others.
            m_layout   = VK_NULL_HANDLE;
            m_pipeline = VK_NULL_HANDLE;

            moveFrom(std::move(other));
        }
        return *this;
    }

    void RtPresentPipeline::moveFrom(RtPresentPipeline&& other) noexcept
    {
        m_layout   = other.m_layout;
        m_pipeline = other.m_pipeline;

        other.m_layout   = VK_NULL_HANDLE;
        other.m_pipeline = VK_NULL_HANDLE;
    }

    // ------------------------------------------------------------
    // Destroy
    // ------------------------------------------------------------

    void RtPresentPipeline::destroy(VkDevice device) noexcept
    {
        if (!device)
            return;

        if (m_pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device, m_pipeline, nullptr);
            m_pipeline = VK_NULL_HANDLE;
        }

        if (m_layout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device, m_layout, nullptr);
            m_layout = VK_NULL_HANDLE;
        }
    }

    // ------------------------------------------------------------
    // Create
    // ------------------------------------------------------------

    bool RtPresentPipeline::create(VkDevice              device,
                                   VkRenderPass          renderPass,
                                   VkSampleCountFlagBits sampleCount,
                                   VkDescriptorSetLayout setLayout)
    {
        destroy(device);

        if (!device || !renderPass || !setLayout)
            return false;

        // --------------------------------------------------------
        // Pipeline layout (single RT descriptor set)
        // --------------------------------------------------------
        VkPipelineLayoutCreateInfo plci{};
        plci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1;
        plci.pSetLayouts    = &setLayout;

        if (vkCreatePipelineLayout(device, &plci, nullptr, &m_layout) != VK_SUCCESS)
        {
            std::cerr << "RtPresentPipeline: vkCreatePipelineLayout() failed.\n";
            destroy(device);
            return false;
        }

        // --------------------------------------------------------
        // Load shaders
        // --------------------------------------------------------
        const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

        ShaderStage vs =
            vkutil::loadStage(device, shaderDir, "RtPresent.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        ShaderStage fs =
            vkutil::loadStage(device, shaderDir, "RtPresent.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        if (!vs.isValid() || !fs.isValid())
        {
            std::cerr << "RtPresentPipeline: Failed to load RtPresent shaders.\n";
            destroy(device);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {
            vs.stageInfo(),
            fs.stageInfo(),
        };

        // --------------------------------------------------------
        // Fixed-function state
        // --------------------------------------------------------
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ia.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount  = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.depthClampEnable        = VK_FALSE;
        rs.rasterizerDiscardEnable = VK_FALSE;
        rs.polygonMode             = VK_POLYGON_MODE_FILL;
        rs.cullMode                = VK_CULL_MODE_NONE;
        rs.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.depthBiasEnable         = VK_FALSE;
        rs.lineWidth               = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = sampleCount;
        ms.sampleShadingEnable  = VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable       = VK_FALSE;
        ds.depthWriteEnable      = VK_FALSE;
        ds.depthBoundsTestEnable = VK_FALSE;
        ds.stencilTestEnable     = VK_FALSE;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                             VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT |
                             VK_COLOR_COMPONENT_A_BIT;
        cba.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1;
        cb.pAttachments    = &cba;

        VkDynamicState dynStates[2] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates    = dynStates;

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount          = 2;
        gp.pStages             = stages;
        gp.pVertexInputState   = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState      = &vp;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState   = &ms;
        gp.pDepthStencilState  = &ds;
        gp.pColorBlendState    = &cb;
        gp.pDynamicState       = &dyn;
        gp.layout              = m_layout;
        gp.renderPass          = renderPass;
        gp.subpass             = 0;

        if (vkCreateGraphicsPipelines(device,
                                      VK_NULL_HANDLE,
                                      1,
                                      &gp,
                                      nullptr,
                                      &m_pipeline) != VK_SUCCESS)
        {
            std::cerr << "RtPresentPipeline: vkCreateGraphicsPipelines() failed.\n";
            destroy(device);
            return false;
        }

        return true;
    }

} // namespace vkrt
