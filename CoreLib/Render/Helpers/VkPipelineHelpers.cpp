#include "VkPipelineHelpers.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <iostream>

#include "ShaderStage.hpp"

namespace vkutil
{

    ShaderStage loadStage(VkDevice                     device,
                          const std::filesystem::path& dir,
                          const char*                  filename,
                          VkShaderStageFlagBits        stage)
    {
        return ShaderStage::fromSpirvFile(device, dir / filename, stage);
    }

    void makeSolidVertexInput(VkPipelineVertexInputStateCreateInfo& vi,
                              VkVertexInputBindingDescription (&bindings)[4],
                              VkVertexInputAttributeDescription (&attrs)[4])
    {
        // bindings: pos / nrm / uv / materialId

        // Binding 0: position
        bindings[0].binding   = 0;
        bindings[0].stride    = sizeof(glm::vec3);
        bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        // Binding 1: normal
        bindings[1].binding   = 1;
        bindings[1].stride    = sizeof(glm::vec3);
        bindings[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        // Binding 2: uv
        bindings[2].binding   = 2;
        bindings[2].stride    = sizeof(glm::vec2);
        bindings[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        // Binding 3: materialId
        bindings[3].binding   = 3;
        bindings[3].stride    = sizeof(std::int32_t);
        bindings[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        // attributes

        // location 0: pos
        attrs[0].location = 0;
        attrs[0].binding  = 0;
        attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset   = 0;

        // location 1: normal
        attrs[1].location = 1;
        attrs[1].binding  = 1;
        attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset   = 0;

        // location 2: uv
        attrs[2].location = 2;
        attrs[2].binding  = 2;
        attrs[2].format   = VK_FORMAT_R32G32_SFLOAT;
        attrs[2].offset   = 0;

        // location 3: materialId
        attrs[3].location = 3;
        attrs[3].binding  = 3;
        attrs[3].format   = VK_FORMAT_R32_SINT; // matches int inMaterialId
        attrs[3].offset   = 0;

        vi                                 = {};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 4;
        vi.pVertexBindingDescriptions      = bindings;
        vi.vertexAttributeDescriptionCount = 4;
        vi.pVertexAttributeDescriptions    = attrs;
    }

    void makeLineVertexInput(VkPipelineVertexInputStateCreateInfo& vi,
                             VkVertexInputBindingDescription&      binding,
                             VkVertexInputAttributeDescription&    attr)
    {
        binding.binding   = 0;
        binding.stride    = sizeof(glm::vec3);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        attr.location = 0;
        attr.binding  = 0;
        attr.format   = VK_FORMAT_R32G32B32_SFLOAT;
        attr.offset   = 0;

        vi                                 = {};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 1;
        vi.pVertexBindingDescriptions      = &binding;
        vi.vertexAttributeDescriptionCount = 1;
        vi.pVertexAttributeDescriptions    = &attr;
    }

    VkPipelineLayout createPipelineLayout(VkDevice                     device,
                                          const VkDescriptorSetLayout* setLayouts,
                                          uint32_t                     setLayoutCount,
                                          const VkPushConstantRange*   pushConstants,
                                          uint32_t                     pushConstantCount)
    {
        VkPipelineLayoutCreateInfo pl{};
        pl.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount         = setLayoutCount;
        pl.pSetLayouts            = setLayouts;
        pl.pushConstantRangeCount = pushConstantCount;
        pl.pPushConstantRanges    = pushConstants;

        VkPipelineLayout layout = VK_NULL_HANDLE;
        if (vkCreatePipelineLayout(device, &pl, nullptr, &layout) != VK_SUCCESS)
        {
            std::cerr << "RendererVK: vkCreatePipelineLayout failed.\n";
            return VK_NULL_HANDLE;
        }
        return layout;
    }

    VkPipeline createMeshPipeline(const VulkanContext&                        ctx,
                                  VkRenderPass                                rp,
                                  VkPipelineLayout                            layout,
                                  const VkPipelineShaderStageCreateInfo*      stages,
                                  uint32_t                                    stageCount,
                                  const VkPipelineVertexInputStateCreateInfo* vertexInput,
                                  const MeshPipelinePreset&                   preset)
    {
        // Input assembly
        VkPipelineInputAssemblyStateCreateInfo ia = {};
        ia.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology                               = preset.topology;
        ia.primitiveRestartEnable                 = VK_FALSE;

        // Viewport/scissor (dynamic)
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount  = 1;

        // Rasterization
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.depthClampEnable        = VK_FALSE;
        rs.rasterizerDiscardEnable = VK_FALSE;
        rs.polygonMode             = preset.polygonMode;
        rs.cullMode                = preset.cullMode;
        rs.frontFace               = preset.frontFace;
        rs.depthBiasEnable         = preset.enableDepthBias ? VK_TRUE : VK_FALSE;
        rs.depthBiasConstantFactor = 0.0f;
        rs.depthBiasClamp          = 0.0f;
        rs.depthBiasSlopeFactor    = 0.0f;
        rs.lineWidth               = 1.0f;

        // Multisampling
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples  = ctx.sampleCount;
        ms.sampleShadingEnable   = preset.sampleShadingEnable ? VK_TRUE : VK_FALSE;
        ms.minSampleShading      = preset.minSampleShading; // ignored if disabled
        ms.alphaToCoverageEnable = preset.alphaToCoverageEnable ? VK_TRUE : VK_FALSE;
        ms.alphaToOneEnable      = VK_FALSE;

        // Depth/stencil
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable       = preset.depthTest ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable      = preset.depthWrite ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp        = preset.depthCompareOp;
        ds.depthBoundsTestEnable = VK_FALSE;
        ds.stencilTestEnable     = VK_FALSE;

        // Color blend attachment
        VkPipelineColorBlendAttachmentState att = {};
        const VkColorComponentFlags         rgbaMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;

        // NEW: allow depth-only pipelines to disable color writes.
        att.colorWriteMask = preset.colorWrite ? rgbaMask : 0;

        att.blendEnable = preset.enableBlend ? VK_TRUE : VK_FALSE;
        if (preset.enableBlend)
        {
            att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            att.colorBlendOp        = VK_BLEND_OP_ADD;
            att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            att.alphaBlendOp        = VK_BLEND_OP_ADD;
        }

        VkPipelineColorBlendStateCreateInfo cb = {};
        cb.sType                               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.logicOpEnable                       = VK_FALSE;
        cb.attachmentCount                     = 1;
        cb.pAttachments                        = &att;

        // Dynamic state
        const VkDynamicState dynStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
        };

        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 3;
        dyn.pDynamicStates    = dynStates;

        // Fill desc on the stack
        vkutil::GraphicsPipelineDesc desc{};
        desc.renderPass    = rp;
        desc.subpass       = 0;
        desc.layout        = layout;
        desc.stages        = stages;
        desc.stageCount    = stageCount;
        desc.vertexInput   = vertexInput;
        desc.inputAssembly = &ia;
        desc.viewport      = &vp;
        desc.rasterization = &rs;
        desc.multisample   = &ms;
        desc.depthStencil  = &ds;
        desc.colorBlend    = &cb;
        desc.dynamicState  = &dyn;

        // Create the pipeline now, while all pointers are valid
        return vkutil::createGraphicsPipeline(ctx.device, desc);
    }

} // namespace vkutil
