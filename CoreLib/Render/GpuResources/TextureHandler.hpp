#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

#include "ImageHandler.hpp"
#include "VulkanContext.hpp"

// Opaque handle for GPU textures
using TextureId = int32_t;

constexpr TextureId kInvalidTextureId = -1;

enum class TextureUsage
{
    Color,
    Normal,
    Data
};

struct TextureDesc
{
    TextureUsage usage{TextureUsage::Color};
    bool         generateMipmaps{true};
    bool         srgb{true};
};

struct GpuTexture
{
    VkImage        image{VK_NULL_HANDLE};
    VkImageView    view{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkSampler      sampler{VK_NULL_HANDLE};

    int32_t  width{};
    int32_t  height{};
    uint32_t mipLevels{};
    VkFormat format{VK_FORMAT_UNDEFINED};

    ImageId sourceImage{kInvalidImageId};
};

class TextureHandler
{
public:
    TextureHandler(const VulkanContext& ctx, ImageHandler* imageHandler);
    ~TextureHandler();

    TextureHandler(const TextureHandler&)            = delete;
    TextureHandler& operator=(const TextureHandler&) = delete;
    TextureHandler(TextureHandler&&)                 = delete;
    TextureHandler& operator=(TextureHandler&&)      = delete;

    // Creates a texture directly from an ImageId (no caching).
    TextureId createTexture(ImageId            imageId,
                            const TextureDesc& desc,
                            const std::string& debugName);

    // Returns an existing texture for (imageId, desc) if present, otherwise creates one.
    TextureId ensureTexture(ImageId            imageId,
                            const TextureDesc& desc,
                            const std::string& debugName);

    const GpuTexture* get(TextureId id) const noexcept;

    void destroy(TextureId id) noexcept;
    void destroyAll() noexcept;

    size_t size() const noexcept
    {
        return m_textures.size();
    }

private:
    VulkanContext        m_ctx;
    ImageHandler*        m_imageHandler{};

    std::vector<GpuTexture> m_textures;

    struct CacheKey
    {
        ImageId      imageId;
        TextureUsage usage;
        bool         generateMipmaps;
        bool         srgb;

        bool operator==(const CacheKey&) const noexcept = default;
    };

    struct CacheKeyHash
    {
        std::size_t operator()(const CacheKey& k) const noexcept
        {
            std::size_t h = std::hash<int32_t>{}(k.imageId);
            h ^= (std::hash<int>{}(static_cast<int>(k.usage)) + 0x9e3779b9 + (h << 6) + (h >> 2));
            h ^= (std::hash<bool>{}(k.generateMipmaps) + 0x9e3779b9 + (h << 6) + (h >> 2));
            h ^= (std::hash<bool>{}(k.srgb) + 0x9e3779b9 + (h << 6) + (h >> 2));
            return h;
        }
    };

    std::unordered_map<CacheKey, TextureId, CacheKeyHash> m_cache;

    TextureId createTextureInternal(ImageId            imageId,
                                    const TextureDesc& desc,
                                    const std::string& debugName);

    void destroyTexture(GpuTexture& tex) noexcept;
};
