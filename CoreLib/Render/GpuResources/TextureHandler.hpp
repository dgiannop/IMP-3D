//============================================================
// TextureHandler.hpp
//============================================================
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

/**
 * @brief GPU texture manager with caching (ImageId + TextureDesc -> TextureId).
 *
 * Responsibilities:
 *  - Create GPU textures from ImageHandler images (raw pixels or KTX path).
 *  - Cache textures for repeated (imageId, desc) requests.
 *  - Provide a valid fallback texture to be used when a material references
 *    an unset texture slot (required for descriptor table updates).
 *
 * Notes:
 *  - TextureId values are stable and not reused in this implementation.
 *  - destroy(TextureId) frees GPU resources but keeps the slot.
 *  - fallbackTexture() returns a 1x1 RGBA texture with a valid view+sampler.
 */
class TextureHandler
{
public:
    /**
     * @brief Construct a texture handler.
     *
     * Creates a fallback 1x1 RGBA texture with a valid view and sampler.
     * The fallback is required to safely populate unused entries in the
     * renderer's combined image sampler descriptor table.
     */
    TextureHandler(const VulkanContext& ctx, ImageHandler* imageHandler);

    /**
     * @brief Destroy all textures and the fallback texture.
     */
    ~TextureHandler();

    TextureHandler(const TextureHandler&)            = delete;
    TextureHandler& operator=(const TextureHandler&) = delete;
    TextureHandler(TextureHandler&&)                 = delete;
    TextureHandler& operator=(TextureHandler&&)      = delete;

public:
    /**
     * @brief Creates a texture directly from an ImageId (no caching).
     */
    TextureId createTexture(ImageId            imageId,
                            const TextureDesc& desc,
                            const std::string& debugName);

    /**
     * @brief Returns an existing texture for (imageId, desc) if present, otherwise creates one.
     */
    TextureId ensureTexture(ImageId            imageId,
                            const TextureDesc& desc,
                            const std::string& debugName);

    /**
     * @brief Retrieve a GPU texture by TextureId.
     */
    [[nodiscard]] const GpuTexture* get(TextureId id) const noexcept;

    /**
     * @brief Destroy a GPU texture (keeps the slot; IDs are not reused).
     */
    void destroy(TextureId id) noexcept;

    /**
     * @brief Destroy all cached/created textures (does not destroy fallback).
     */
    void destroyAll() noexcept;

    /**
     * @brief Returns the number of allocated texture slots (including destroyed slots).
     */
    [[nodiscard]] size_t size() const noexcept
    {
        return m_textures.size();
    }

    /**
     * @brief Return the fallback texture (never null after successful construction).
     *
     * The fallback texture is used to fill unused combined image sampler table entries
     * so descriptor writes never contain VK_NULL_HANDLE samplers or views.
     */
    [[nodiscard]] const GpuTexture* fallbackTexture() const noexcept
    {
        return m_hasFallback ? &m_fallback : nullptr;
    }

private:
    VulkanContext m_ctx          = {};
    ImageHandler* m_imageHandler = nullptr;

    std::vector<GpuTexture> m_textures = {};

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

    std::unordered_map<CacheKey, TextureId, CacheKeyHash> m_cache = {};

private:
    TextureId createTextureInternal(ImageId            imageId,
                                    const TextureDesc& desc,
                                    const std::string& debugName);

    void destroyTexture(GpuTexture& tex) noexcept;

private:
    bool createFallbackTexture() noexcept;

private:
    GpuTexture m_fallback    = {};
    bool       m_hasFallback = false;
};
