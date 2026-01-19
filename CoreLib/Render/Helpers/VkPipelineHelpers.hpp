#pragma once

#include <filesystem>
#include <vulkan/vulkan.h>

#include "VkUtilities.hpp"

class VulkanContext;
class ShaderStage;

namespace vkutil
{
    // ---------------------------------------------------------
    // Shader loading
    // ---------------------------------------------------------
    ShaderStage loadStage(VkDevice                     device,
                          const std::filesystem::path& dir,
                          const char*                  filename,
                          VkShaderStageFlagBits        stage);

    // ---------------------------------------------------------
    // Vertex input presets
    // ---------------------------------------------------------
    void makeSolidVertexInput(VkPipelineVertexInputStateCreateInfo& vi,
                              VkVertexInputBindingDescription (&bindings)[4],
                              VkVertexInputAttributeDescription (&attrs)[4]);

    void makeLineVertexInput(VkPipelineVertexInputStateCreateInfo& vi,
                             VkVertexInputBindingDescription&      binding,
                             VkVertexInputAttributeDescription&    attr);

    // ---------------------------------------------------------
    // Pipeline layout helper
    // ---------------------------------------------------------
    VkPipelineLayout createPipelineLayout(VkDevice                     device,
                                          const VkDescriptorSetLayout* setLayouts,
                                          uint32_t                     setLayoutCount,
                                          const VkPushConstantRange*   pushConstants     = nullptr,
                                          uint32_t                     pushConstantCount = 0);

    // ---------------------------------------------------------
    // Mesh pipeline preset + creator
    // ---------------------------------------------------------

    struct MeshPipelinePreset
    {
        VkPrimitiveTopology topology;
        VkPolygonMode       polygonMode;
        VkCullModeFlags     cullMode;
        VkFrontFace         frontFace;

        bool depthTest = true;
        bool depthWrite;

        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS; // default

        bool enableBlend;

        bool enableDepthBias = false;

        // NEW: allow depth-only pipelines (disable color writes)
        bool colorWrite = true;

        bool  sampleShadingEnable = false; // per-sample shading
        float minSampleShading    = 1.0f;  // fraction of samples shaded individually

        bool alphaToCoverageEnable{};
    };

    VkPipeline createMeshPipeline(const VulkanContext&                        ctx,
                                  VkRenderPass                                rp,
                                  VkPipelineLayout                            layout,
                                  const VkPipelineShaderStageCreateInfo*      stages,
                                  uint32_t                                    stageCount,
                                  const VkPipelineVertexInputStateCreateInfo* vertexInput,
                                  const MeshPipelinePreset&                   preset);

} // namespace vkutil
