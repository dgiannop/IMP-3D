#include "TextureHandler.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

#include "VkTextureUtilities.hpp" // vkutil::GpuImage & helpers
#include "VkUtilities.hpp"

// ---------------------------------------------------------
// Local helpers
// ---------------------------------------------------------

namespace
{
    [[nodiscard]] bool isValidTextureId(TextureId id, std::size_t size) noexcept
    {
        return id >= 0 && static_cast<std::size_t>(id) < size;
    }

    // Simple staging buffer helper – host-visible, host-coherent
    bool createStagingBuffer(const VulkanContext& ctx,
                             VkDeviceSize         size,
                             VkBuffer&            buffer,
                             VkDeviceMemory&      memory)
    {
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;

        VkBufferCreateInfo info{};
        info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size        = size;
        info.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(ctx.device, &info, nullptr, &buffer) != VK_SUCCESS)
        {
            std::cerr << "TextureHandler: vkCreateBuffer (staging) failed.\n";
            return false;
        }

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(ctx.device, buffer, &memReq);

        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &memProps);

        uint32_t memoryTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            const bool typeSupported = (memReq.memoryTypeBits & (1u << i)) != 0;
            const bool hasFlags      = (memProps.memoryTypes[i].propertyFlags &
                                   (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                                  (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (typeSupported && hasFlags)
            {
                memoryTypeIndex = i;
                break;
            }
        }

        if (memoryTypeIndex == UINT32_MAX)
        {
            std::cerr << "TextureHandler: failed to find HOST_VISIBLE | HOST_COHERENT memory type.\n";
            vkDestroyBuffer(ctx.device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryAllocateInfo alloc{};
        alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize  = memReq.size;
        alloc.memoryTypeIndex = memoryTypeIndex;

        if (vkAllocateMemory(ctx.device, &alloc, nullptr, &memory) != VK_SUCCESS)
        {
            std::cerr << "TextureHandler: vkAllocateMemory (staging) failed.\n";
            vkDestroyBuffer(ctx.device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            return false;
        }

        vkBindBufferMemory(ctx.device, buffer, memory, 0);
        return true;
    }

} // namespace

// ---------------------------------------------------------
// TextureHandler
// ---------------------------------------------------------

TextureHandler::TextureHandler(const VulkanContext& ctx, ImageHandler* imageHandler) :
    m_ctx(ctx),
    m_imageHandler(imageHandler)
{
    // You can change to Q_ASSERT if you prefer Qt style
    assert(m_imageHandler && "TextureHandler requires a valid ImageHandler pointer.");
}

TextureHandler::~TextureHandler()
{
    destroyAll();
}

TextureId TextureHandler::createTexture(ImageId            imageId,
                                        const TextureDesc& desc,
                                        const std::string& debugName)
{
    return createTextureInternal(imageId, desc, debugName);
}

TextureId TextureHandler::ensureTexture(ImageId            imageId,
                                        const TextureDesc& desc,
                                        const std::string& debugName)
{
    if (imageId == kInvalidImageId)
        return kInvalidTextureId;

    CacheKey key{imageId, desc.usage, desc.generateMipmaps, desc.srgb};

    if (auto it = m_cache.find(key); it != m_cache.end())
        return it->second;

    TextureId id = createTextureInternal(imageId, desc, debugName);
    if (id != kInvalidTextureId)
    {
        m_cache.emplace(key, id);
    }
    return id;
}

const GpuTexture* TextureHandler::get(TextureId id) const noexcept
{
    if (!isValidTextureId(id, m_textures.size()))
        return nullptr;

    return &m_textures[static_cast<std::size_t>(id)];
}

void TextureHandler::destroy(TextureId id) noexcept
{
    if (!isValidTextureId(id, m_textures.size()))
        return;

    destroyTexture(m_textures[static_cast<std::size_t>(id)]);
    // We keep the slot; TextureId values are not reused in this simple version.
}

void TextureHandler::destroyAll() noexcept
{
    for (auto& tex : m_textures)
        destroyTexture(tex);

    m_textures.clear();
    m_cache.clear();
}

// ---------------------------------------------------------
// Internal
// ---------------------------------------------------------

TextureId TextureHandler::createTextureInternal(ImageId            imageId,
                                                const TextureDesc& desc,
                                                const std::string& debugName)
{
    if (!m_imageHandler)
        return kInvalidTextureId;

    const Image* img = m_imageHandler->get(imageId);
    if (!img || !img->valid())
    {
        std::cerr << "TextureHandler::createTextureInternal: invalid ImageId for '"
                  << debugName << "'\n";
        return kInvalidTextureId;
    }

    const int      width    = img->width();
    const int      height   = img->height();
    // const
    int            channels = img->channels();
    const uint8_t* pixels   = img->data();

    if (width <= 0 || height <= 0 || !pixels)
    {
        std::cerr << "TextureHandler::createTextureInternal: image has no data for '"
                  << debugName << "'\n";
        return kInvalidTextureId;
    }

    std::vector<uint8_t> rgba;

    // Choose Vulkan format from channel count + sRGB flag
    VkFormat format = VK_FORMAT_UNDEFINED;
    switch (channels)
    {
        case 4:
            format = desc.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            break;
        case 3: {
            // format = desc.srgb ? VK_FORMAT_R8G8B8_SRGB : VK_FORMAT_R8G8B8_UNORM;

            // Expand RGB → RGBA
            rgba.resize(static_cast<std::size_t>(width) *
                        static_cast<std::size_t>(height) * 4u);

            const uint8_t* src = pixels;
            uint8_t*       dst = rgba.data();

            const std::size_t count = static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height);

            for (std::size_t i = 0; i < count; ++i)
            {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 255; // opaque
                src += 3;
                dst += 4;
            }

            pixels   = rgba.data();
            channels = 4;
            format   = desc.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

            break;
        }
        case 2: {
            rgba.resize(static_cast<std::size_t>(width) *
                        static_cast<std::size_t>(height) * 4u);

            const uint8_t* src = pixels;
            uint8_t*       dst = rgba.data();

            const std::size_t count = static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height);

            if (desc.srgb)
            {
                // Assume Luminance + Alpha (common for grayscale PNGs w/ alpha)
                for (std::size_t i = 0; i < count; ++i)
                {
                    const uint8_t l = src[0];
                    const uint8_t a = src[1];
                    dst[0]          = l;
                    dst[1]          = l;
                    dst[2]          = l;
                    dst[3]          = a;
                    src += 2;
                    dst += 4;
                }
            }
            else
            {
                // Assume RG packed (normals XY, packed params, etc.)
                for (std::size_t i = 0; i < count; ++i)
                {
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = 0;
                    dst[3] = 255;
                    src += 2;
                    dst += 4;
                }
            }

            pixels   = rgba.data();
            channels = 4;
            format   = desc.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            break;
        }
        case 1:
            format = VK_FORMAT_R8_UNORM;
            break;
        default:
            std::cerr << "TextureHandler: unsupported channel count (" << channels
                      << ") for '" << debugName << "'\n";
            return kInvalidTextureId;
    }

    // Compute mip levels
    uint32_t mipLevels = 1;
    if (desc.generateMipmaps)
    {
        const int maxDim = std::max(width, height);
        mipLevels        = static_cast<uint32_t>(std::floor(std::log2(maxDim))) + 1u;
    }

    // Create staging buffer and copy pixels into it
    const VkDeviceSize imageSize =
        static_cast<VkDeviceSize>(static_cast<std::size_t>(width) *
                                  static_cast<std::size_t>(height) *
                                  static_cast<std::size_t>(channels));

    VkBuffer       stagingBuffer{VK_NULL_HANDLE};
    VkDeviceMemory stagingMemory{VK_NULL_HANDLE};

    if (!createStagingBuffer(m_ctx, imageSize, stagingBuffer, stagingMemory))
    {
        std::cerr << "TextureHandler: failed to create staging buffer for '"
                  << debugName << "'\n";
        return kInvalidTextureId;
    }

    void* mapped = nullptr;
    if (vkMapMemory(m_ctx.device, stagingMemory, 0, imageSize, 0, &mapped) != VK_SUCCESS)
    {
        std::cerr << "TextureHandler: vkMapMemory(staging) failed for '"
                  << debugName << "'\n";
        vkDestroyBuffer(m_ctx.device, stagingBuffer, nullptr);
        vkFreeMemory(m_ctx.device, stagingMemory, nullptr);
        return kInvalidTextureId;
    }
    std::memcpy(mapped, pixels, static_cast<std::size_t>(imageSize));
    vkUnmapMemory(m_ctx.device, stagingMemory);

    // Create device-local VkImage via vkutil
    const VkImageUsageFlags usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        (desc.generateMipmaps ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0u) |
        VK_IMAGE_USAGE_SAMPLED_BIT;

    vkutil::GpuImage gpuImg =
        vkutil::createDeviceLocalImage2D(m_ctx, width, height, mipLevels, format, usage);

    if (!gpuImg.valid())
    {
        std::cerr << "TextureHandler: createDeviceLocalImage2D failed for '"
                  << debugName << "'\n";
        vkDestroyBuffer(m_ctx.device, stagingBuffer, nullptr);
        vkFreeMemory(m_ctx.device, stagingMemory, nullptr);
        return kInvalidTextureId;
    }

    // Layout transition: UNDEFINED -> TRANSFER_DST_OPTIMAL
    vkutil::transitionImageLayout(m_ctx,
                                  gpuImg.image,
                                  gpuImg.format,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  gpuImg.mipLevels);

    // Copy staging buffer into image base level
    vkutil::copyBufferToImage(m_ctx,
                              stagingBuffer,
                              gpuImg.image,
                              width,
                              height);

    // Mipmaps or direct-to-shader layout
    if (desc.generateMipmaps)
    {
        vkutil::generateMipmaps(m_ctx,
                                gpuImg.image,
                                width,
                                height,
                                gpuImg.mipLevels);
    }
    else
    {
        vkutil::transitionImageLayout(m_ctx,
                                      gpuImg.image,
                                      gpuImg.format,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      gpuImg.mipLevels);
    }

    // Staging buffer is no longer needed
    vkDestroyBuffer(m_ctx.device, stagingBuffer, nullptr);
    vkFreeMemory(m_ctx.device, stagingMemory, nullptr);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = gpuImg.image;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = gpuImg.format;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = gpuImg.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    VkImageView view{VK_NULL_HANDLE};
    if (vkCreateImageView(m_ctx.device, &viewInfo, nullptr, &view) != VK_SUCCESS)
    {
        std::cerr << "TextureHandler: vkCreateImageView failed for '"
                  << debugName << "'\n";
        vkDestroyImage(m_ctx.device, gpuImg.image, nullptr);
        vkFreeMemory(m_ctx.device, gpuImg.memory, nullptr);
        return kInvalidTextureId;
    }

    // Create sampler (you can parameterize this later)
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter               = VK_FILTER_LINEAR;
    samplerInfo.minFilter               = VK_FILTER_LINEAR;
    samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable        = VK_TRUE;
    samplerInfo.maxAnisotropy           = std::min(16.0f, m_ctx.deviceProps.limits.maxSamplerAnisotropy);
    samplerInfo.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable           = VK_FALSE;
    samplerInfo.compareOp               = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias              = 0.0f;
    samplerInfo.minLod                  = 0.0f;
    samplerInfo.maxLod                  = static_cast<float>(gpuImg.mipLevels);

    VkSampler sampler{VK_NULL_HANDLE};
    if (vkCreateSampler(m_ctx.device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
    {
        std::cerr << "TextureHandler: vkCreateSampler failed for '"
                  << debugName << "'\n";
        vkDestroyImageView(m_ctx.device, view, nullptr);
        vkDestroyImage(m_ctx.device, gpuImg.image, nullptr);
        vkFreeMemory(m_ctx.device, gpuImg.memory, nullptr);
        return kInvalidTextureId;
    }

    GpuTexture tex{};
    tex.image       = gpuImg.image;
    tex.view        = view;
    tex.memory      = gpuImg.memory;
    tex.sampler     = sampler;
    tex.width       = gpuImg.width;
    tex.height      = gpuImg.height;
    tex.mipLevels   = gpuImg.mipLevels;
    tex.format      = gpuImg.format;
    tex.sourceImage = imageId;

    const TextureId id = static_cast<TextureId>(m_textures.size());
    m_textures.push_back(tex);

    // Optional: hook in debug utils label here if you have them.

    return id;
}

void TextureHandler::destroyTexture(GpuTexture& tex) noexcept
{
    if (!m_ctx.device)
        return;

    if (tex.sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(m_ctx.device, tex.sampler, nullptr);
        tex.sampler = VK_NULL_HANDLE;
    }

    if (tex.view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(m_ctx.device, tex.view, nullptr);
        tex.view = VK_NULL_HANDLE;
    }

    if (tex.image != VK_NULL_HANDLE)
    {
        vkDestroyImage(m_ctx.device, tex.image, nullptr);
        tex.image = VK_NULL_HANDLE;
    }

    if (tex.memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(m_ctx.device, tex.memory, nullptr);
        tex.memory = VK_NULL_HANDLE;
    }
}
