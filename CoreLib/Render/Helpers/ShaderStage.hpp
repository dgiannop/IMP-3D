#pragma once
#include <filesystem>
#include <string>
#include <vulkan/vulkan.h>

class ShaderStage
{
public:
    ShaderStage() = default;
    ~ShaderStage();

    // Factory: load SPIR-V + create module
    static ShaderStage fromSpirvFile(VkDevice                     device,
                                     const std::filesystem::path& path,
                                     VkShaderStageFlagBits        stage,
                                     const char*                  entryPoint = "main");

    // non-copyable, move-only
    ShaderStage(const ShaderStage&)            = delete;
    ShaderStage& operator=(const ShaderStage&) noexcept = delete;

    ShaderStage(ShaderStage&& other) noexcept;
    ShaderStage& operator=(ShaderStage&& other) noexcept;

    bool isValid() const
    {
        return m_module != VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo stageInfo() const
    {
        VkPipelineShaderStageCreateInfo info{};
        info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage  = m_stage;
        info.module = m_module;
        info.pName  = m_entryPoint.c_str();
        return info;
    }

    VkShaderModule handle() const
    {
        return m_module;
    }

private:
    ShaderStage(VkDevice              device,
                VkShaderModule        module,
                VkShaderStageFlagBits stage,
                std::string           entryPoint);

    void destroy();

    VkDevice              m_device     = VK_NULL_HANDLE;
    VkShaderModule        m_module     = VK_NULL_HANDLE;
    VkShaderStageFlagBits m_stage      = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
    std::string           m_entryPoint = "main";
};
