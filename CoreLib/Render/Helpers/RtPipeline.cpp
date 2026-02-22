//============================================================
// RtPipeline.cpp
//============================================================
#include "RtPipeline.hpp"

#include <filesystem>
#include <iostream>
#include <utility>

#include "ShaderStage.hpp"
#include "VkPipelineHelpers.hpp"
#include "VulkanContext.hpp"

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

        // Primary shaders
        ShaderStage rgen =
            vkutil::loadStage(ctx.device, shaderDir, "RtScene.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR);

        ShaderStage rmiss =
            vkutil::loadStage(ctx.device, shaderDir, "RtScene.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR);

        ShaderStage rchit =
            vkutil::loadStage(ctx.device, shaderDir, "RtScene.rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

        // NEW: primary any-hit (alpha cutouts)
        ShaderStage rahit =
            vkutil::loadStage(ctx.device, shaderDir, "RtScene.rahit.spv", VK_SHADER_STAGE_ANY_HIT_BIT_KHR);

        // Shadow shaders
        ShaderStage smiss =
            vkutil::loadStage(ctx.device, shaderDir, "RtShadow.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR);

        ShaderStage schit =
            vkutil::loadStage(ctx.device, shaderDir, "RtShadow.rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

        // NEW: shadow any-hit (alpha cutouts)
        ShaderStage sahit =
            vkutil::loadStage(ctx.device, shaderDir, "RtShadow.rahit.spv", VK_SHADER_STAGE_ANY_HIT_BIT_KHR);

        if (!rgen.isValid() || !rmiss.isValid() || !rchit.isValid() ||
            !rahit.isValid() || !smiss.isValid() || !schit.isValid() || !sahit.isValid())
        {
            std::cerr << "RtPipeline: Failed to load RT scene/shadow shaders (rgen/rmiss/rchit/rahit/smiss/schit/sahit).\n";
            destroy();
            return false;
        }

        // ------------------------------------------------------------
        // Stage indices (MUST match group references below)
        //
        // 0 = rgen
        // 1 = primary miss
        // 2 = primary closest hit
        // 3 = primary any-hit
        // 4 = shadow miss
        // 5 = shadow closest hit
        // 6 = shadow any-hit
        // ------------------------------------------------------------
        VkPipelineShaderStageCreateInfo stages[7] = {
            rgen.stageInfo(),  // 0
            rmiss.stageInfo(), // 1
            rchit.stageInfo(), // 2
            rahit.stageInfo(), // 3
            smiss.stageInfo(), // 4
            schit.stageInfo(), // 5
            sahit.stageInfo(), // 6
        };

        VkRayTracingShaderGroupCreateInfoKHR groups[5]{};

        for (auto& g : groups)
        {
            g.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            g.generalShader      = VK_SHADER_UNUSED_KHR;
            g.closestHitShader   = VK_SHADER_UNUSED_KHR;
            g.anyHitShader       = VK_SHADER_UNUSED_KHR;
            g.intersectionShader = VK_SHADER_UNUSED_KHR;
        }

        // Group 0: Raygen
        groups[0].type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[0].generalShader = 0; // rgen

        // Group 1: Primary miss
        groups[1].type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[1].generalShader = 1; // rmiss

        // Group 2: Primary hit group (triangles)
        groups[2].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        groups[2].closestHitShader = 2; // rchit
        groups[2].anyHitShader     = 3; // rahit (NEW)

        // Group 3: Shadow miss
        groups[3].type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[3].generalShader = 4; // smiss

        // Group 4: Shadow hit group (triangles)
        groups[4].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        groups[4].closestHitShader = 5; // schit
        groups[4].anyHitShader     = 6; // sahit (NEW)

        VkRayTracingPipelineCreateInfoKHR ci{};
        ci.sType      = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        ci.stageCount = 7;
        ci.pStages    = stages;
        ci.groupCount = 5;
        ci.pGroups    = groups;

        // Recursion depth >= 2 (primary + shadow)
        ci.maxPipelineRayRecursionDepth = 2;
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
