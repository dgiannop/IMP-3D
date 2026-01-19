#include "RtPresentPipeline.hpp"

void RtPresentPipeline::destroy(const VulkanContext& ctx) noexcept
{
    if (ctx.device && m_pipeline)
        vkDestroyPipeline(ctx.device, m_pipeline, nullptr);
    m_pipeline = VK_NULL_HANDLE;

    if (ctx.device && m_layout)
        vkDestroyPipelineLayout(ctx.device, m_layout, nullptr);
    m_layout = VK_NULL_HANDLE;
}

bool RtPresentPipeline::create(const VulkanContext&  ctx,
                               VkRenderPass          renderPass,
                               VkSampleCountFlagBits sampleCount,
                               VkDescriptorSetLayout setLayout,
                               VkShaderModule        fullscreenVs,
                               VkShaderModule        presentFs)
{
    if (!ctx.device || !renderPass || !setLayout || !fullscreenVs || !presentFs)
        return false;

    destroy(ctx);

    VkPipelineLayoutCreateInfo plci = {};
    plci.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount             = 1;
    plci.pSetLayouts                = &setLayout;

    if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &m_layout) != VK_SUCCESS)
        return false;

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage                           = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module                          = fullscreenVs;
    stages[0].pName                           = "main";

    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = presentFs;
    stages[1].pName  = "main";

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType                                = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType                             = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount                     = 1;
    vp.scissorCount                      = 1;

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode                            = VK_POLYGON_MODE_FILL;
    rs.cullMode                               = VK_CULL_MODE_NONE;
    rs.frontFace                              = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth                              = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType                                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples                 = sampleCount;

    VkPipelineColorBlendAttachmentState cba = {};
    cba.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType                               = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount                     = 1;
    cb.pAttachments                        = &cba;

    VkDynamicState                   dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds           = {};
    ds.sType                                      = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount                          = 2;
    ds.pDynamicStates                             = dynStates;

    VkGraphicsPipelineCreateInfo gp = {};
    gp.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount                   = 2;
    gp.pStages                      = stages;
    gp.pVertexInputState            = &vi;
    gp.pInputAssemblyState          = &ia;
    gp.pViewportState               = &vp;
    gp.pRasterizationState          = &rs;
    gp.pMultisampleState            = &ms;
    gp.pColorBlendState             = &cb;
    gp.pDynamicState                = &ds;
    gp.layout                       = m_layout;
    gp.renderPass                   = renderPass;
    gp.subpass                      = 0;

    if (vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &gp, nullptr, &m_pipeline) != VK_SUCCESS)
    {
        destroy(ctx);
        return false;
    }

    return true;
}
