//============================================================
// RtPipeline.cpp
//============================================================
#include "RtPipeline.hpp"

#include <filesystem>
#include <iostream>
#include <utility>

#include "ShaderStage.hpp"
#include "VkPipelineHelpers.hpp"

namespace
{
    // (Unused right now, but harmless if you want to keep it around)
    static VkPipelineShaderStageCreateInfo makeStage(VkShaderModule        mod,
                                                     VkShaderStageFlagBits stage) noexcept
    {
        VkPipelineShaderStageCreateInfo s{};
        s.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        s.stage  = stage;
        s.module = mod;
        s.pName  = "main";
        return s;
    }
} // namespace

namespace vkrt
{
    // ------------------------------------------------------------
    // Move
    // ------------------------------------------------------------

    RtPipeline::RtPipeline(RtPipeline&& other) noexcept
    {
        moveFrom(std::move(other));
    }

    RtPipeline& RtPipeline::operator=(RtPipeline&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            moveFrom(std::move(other));
        }
        return *this;
    }

    void RtPipeline::moveFrom(RtPipeline&& other) noexcept
    {
        m_device   = other.m_device;
        m_layout   = other.m_layout;
        m_pipeline = other.m_pipeline;

        other.m_device   = VK_NULL_HANDLE;
        other.m_layout   = VK_NULL_HANDLE;
        other.m_pipeline = VK_NULL_HANDLE;
    }

    // ------------------------------------------------------------
    // Destroy
    // ------------------------------------------------------------

    void RtPipeline::destroy() noexcept
    {
        if (!m_device)
            return;

        if (m_pipeline)
        {
            vkDestroyPipeline(m_device, m_pipeline, nullptr);
            m_pipeline = VK_NULL_HANDLE;
        }

        if (m_layout)
        {
            vkDestroyPipelineLayout(m_device, m_layout, nullptr);
            m_layout = VK_NULL_HANDLE;
        }

        m_device = VK_NULL_HANDLE;
    }

    // ------------------------------------------------------------
    // Create scene RT pipeline (MULTI SET LAYOUT)
    // ------------------------------------------------------------

    bool RtPipeline::createScenePipeline(const VulkanContext&         ctx,
                                         const VkDescriptorSetLayout* setLayouts,
                                         uint32_t                     setLayoutCount)
    {
        destroy();

        if (!rtReady(ctx) || !ctx.device || !ctx.rtDispatch || !setLayouts || setLayoutCount == 0)
            return false;

        m_device = ctx.device;

        // ------------------------------------------------------------
        // Pipeline layout (set layouts provided by caller)
        // ------------------------------------------------------------
        VkPipelineLayoutCreateInfo pl{};
        pl.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount = setLayoutCount;
        pl.pSetLayouts    = setLayouts;

        if (vkCreatePipelineLayout(ctx.device, &pl, nullptr, &m_layout) != VK_SUCCESS)
            return false;

        const std::filesystem::path shaderDir = std::filesystem::path(SHADER_BIN_DIR);

        ShaderStage rgen =
            vkutil::loadStage(ctx.device, shaderDir, "RtScene.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR);

        ShaderStage rmiss =
            vkutil::loadStage(ctx.device, shaderDir, "RtScene.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR);

        ShaderStage rchit =
            vkutil::loadStage(ctx.device, shaderDir, "RtScene.rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

        if (!rgen.isValid() || !rmiss.isValid() || !rchit.isValid())
        {
            std::cerr << "RtPipeline: Failed to load RT scene shaders.\n";
            destroy();
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[3] = {
            rgen.stageInfo(),  // index 0
            rmiss.stageInfo(), // index 1
            rchit.stageInfo(), // index 2
        };

        VkRayTracingShaderGroupCreateInfoKHR groups[3]{};

        for (auto& g : groups)
        {
            g.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            g.generalShader      = VK_SHADER_UNUSED_KHR;
            g.closestHitShader   = VK_SHADER_UNUSED_KHR;
            g.anyHitShader       = VK_SHADER_UNUSED_KHR;
            g.intersectionShader = VK_SHADER_UNUSED_KHR;
        }

        // Raygen
        groups[0].type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[0].generalShader = 0;

        // Miss
        groups[1].type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[1].generalShader = 1;

        // Closest hit (triangles)
        groups[2].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        groups[2].closestHitShader = 2;

        VkRayTracingPipelineCreateInfoKHR ci{};
        ci.sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        ci.stageCount                   = 3;
        ci.pStages                      = stages;
        ci.groupCount                   = 3;
        ci.pGroups                      = groups;
        ci.maxPipelineRayRecursionDepth = 1;
        ci.layout                       = m_layout;

        VkPipeline pipe = VK_NULL_HANDLE;
        VkResult   res =
            ctx.rtDispatch->vkCreateRayTracingPipelinesKHR(
                ctx.device,
                VK_NULL_HANDLE,
                VK_NULL_HANDLE,
                1,
                &ci,
                nullptr,
                &pipe);

        if (res != VK_SUCCESS || pipe == VK_NULL_HANDLE)
        {
            std::cerr << "RtPipeline: vkCreateRayTracingPipelinesKHR(scene) failed.\n";
            destroy();
            return false;
        }

        m_pipeline = pipe;
        return true;
    }

} // namespace vkrt
