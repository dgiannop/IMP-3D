#include "ShaderStage.hpp"

#include <cstdio>
#include <fstream>
#include <vector>

static std::vector<char> loadSpirvFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        std::fprintf(stderr, "ShaderStage: failed to open %s\n", path.string().c_str());
        return {};
    }

    const std::streamsize size = file.tellg();
    std::vector<char>     code(static_cast<size_t>(size));

    file.seekg(0, std::ios::beg);
    file.read(code.data(), size);

    return code;
}

ShaderStage::ShaderStage(VkDevice              device,
                         VkShaderModule        module,
                         VkShaderStageFlagBits stage,
                         std::string           entryPoint) : m_device(device),
                                                   m_module(module),
                                                   m_stage(stage),
                                                   m_entryPoint(std::move(entryPoint))
{
}

ShaderStage::~ShaderStage()
{
    destroy();
}

void ShaderStage::destroy()
{
    if (m_module != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(m_device, m_module, nullptr);
        m_module = VK_NULL_HANDLE;
    }
}

ShaderStage ShaderStage::fromSpirvFile(VkDevice                     device,
                                       const std::filesystem::path& path,
                                       VkShaderStageFlagBits        stage,
                                       const char*                  entryPoint)
{
    auto code = loadSpirvFile(path);
    if (code.empty())
        return {}; // invalid, caller checks isValid()

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS)
    {
        std::fprintf(stderr, "ShaderStage: vkCreateShaderModule failed for %s\n", path.string().c_str());
        return {};
    }

    return ShaderStage(device, module, stage, entryPoint);
}

ShaderStage::ShaderStage(ShaderStage&& other) noexcept
{
    m_device     = other.m_device;
    m_module     = other.m_module;
    m_stage      = other.m_stage;
    m_entryPoint = std::move(other.m_entryPoint);

    other.m_device = VK_NULL_HANDLE;
    other.m_module = VK_NULL_HANDLE;
}

ShaderStage& ShaderStage::operator=(ShaderStage&& other) noexcept
{
    if (this != &other)
    {
        destroy();

        m_device     = other.m_device;
        m_module     = other.m_module;
        m_stage      = other.m_stage;
        m_entryPoint = std::move(other.m_entryPoint);

        other.m_device = VK_NULL_HANDLE;
        other.m_module = VK_NULL_HANDLE;
    }
    return *this;
}
