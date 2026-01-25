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

    static VkCommandBuffer beginOneShotCmd(const VulkanContext& ctx, VkCommandPool& outPool)
    {
        outPool = VK_NULL_HANDLE;

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = ctx.graphicsQueueFamilyIndex;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

        if (vkCreateCommandPool(ctx.device, &poolInfo, nullptr, &outPool) != VK_SUCCESS)
            return VK_NULL_HANDLE;

        VkCommandBufferAllocateInfo alloc{};
        alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool        = outPool;
        alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(ctx.device, &alloc, &cmd) != VK_SUCCESS)
        {
            vkDestroyCommandPool(ctx.device, outPool, nullptr);
            outPool = VK_NULL_HANDLE;
            return VK_NULL_HANDLE;
        }

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS)
        {
            vkDestroyCommandPool(ctx.device, outPool, nullptr);
            outPool = VK_NULL_HANDLE;
            return VK_NULL_HANDLE;
        }

        return cmd;
    }

    static bool endOneShotCmd(const VulkanContext& ctx, VkCommandBuffer cmd, VkCommandPool pool)
    {
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
            return false;

        VkSubmitInfo submit{};
        submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &cmd;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(ctx.device, &fenceInfo, nullptr, &fence) != VK_SUCCESS)
            return false;

        const VkResult qres = vkQueueSubmit(ctx.graphicsQueue, 1, &submit, fence);
        if (qres != VK_SUCCESS)
        {
            vkDestroyFence(ctx.device, fence, nullptr);
            return false;
        }

        vkWaitForFences(ctx.device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(ctx.device, fence, nullptr);

        vkDestroyCommandPool(ctx.device, pool, nullptr);
        return true;
    }

    static void imageBarrier(VkCommandBuffer cmd,
                             VkImage         image,
                             VkImageLayout   oldLayout,
                             VkImageLayout   newLayout,
                             uint32_t        mipLevels)
    {
        VkImageMemoryBarrier b{};
        b.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout                       = oldLayout;
        b.newLayout                       = newLayout;
        b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.image                           = image;
        b.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.baseMipLevel   = 0;
        b.subresourceRange.levelCount     = mipLevels;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount     = 1;

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage        = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage        = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage        = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage        = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // or ALL_GRAPHICS if you prefer
        }

        vkCmdPipelineBarrier(cmd,
                             srcStage,
                             dstStage,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &b);
    }

    static VkFormat srgbVariantIfNeeded(VkFormat fmt, bool srgb) noexcept
    {
        if (!srgb)
            return fmt;

        // If your KTX transcode target is linear BC7, creating the image as SRGB is fine (data unchanged).
        // Expand mapping later as needed.
        if (fmt == VK_FORMAT_BC7_UNORM_BLOCK)
            return VK_FORMAT_BC7_SRGB_BLOCK;
        if (fmt == VK_FORMAT_BC3_UNORM_BLOCK)
            return VK_FORMAT_BC3_SRGB_BLOCK;
        if (fmt == VK_FORMAT_BC1_RGBA_UNORM_BLOCK)
            return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;

        return fmt;
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

    // ---------------------------------------------------------
    // KTX path (compressed + mip chain provided by Image)
    // ---------------------------------------------------------
    if (img->isKtx())
    {
        const auto& data = img->ktxData();
        const auto& mips = img->ktxMips();

        if (data.empty() || mips.empty())
        {
            std::cerr << "TextureHandler::createTextureInternal: KTX image missing payload/mips for '"
                      << debugName << "'\n";
            return kInvalidTextureId;
        }

        const int width  = img->width();
        const int height = img->height();
        if (width <= 0 || height <= 0)
        {
            std::cerr << "TextureHandler::createTextureInternal: invalid KTX dimensions for '"
                      << debugName << "'\n";
            return kInvalidTextureId;
        }

        VkFormat format = img->ktxVkFormat();
        format          = srgbVariantIfNeeded(format, desc.srgb);

        if (format == VK_FORMAT_UNDEFINED)
        {
            std::cerr << "TextureHandler::createTextureInternal: KTX has VK_FORMAT_UNDEFINED for '"
                      << debugName << "' (KTX1 mapping not implemented?)\n";
            return kInvalidTextureId;
        }

        const uint32_t mipLevels = static_cast<uint32_t>(mips.size());

        // -----------------------------------------------------
        // Stage the full KTX payload (already transcoded if needed)
        // -----------------------------------------------------
        const VkDeviceSize uploadSize = static_cast<VkDeviceSize>(data.size());

        VkBuffer       stagingBuffer{VK_NULL_HANDLE};
        VkDeviceMemory stagingMemory{VK_NULL_HANDLE};

        if (!createStagingBuffer(m_ctx, uploadSize, stagingBuffer, stagingMemory))
        {
            std::cerr << "TextureHandler: failed to create KTX staging buffer for '"
                      << debugName << "'\n";
            return kInvalidTextureId;
        }

        void* mapped = nullptr;
        if (vkMapMemory(m_ctx.device, stagingMemory, 0, uploadSize, 0, &mapped) != VK_SUCCESS)
        {
            std::cerr << "TextureHandler: vkMapMemory(KTX staging) failed for '"
                      << debugName << "'\n";
            vkDestroyBuffer(m_ctx.device, stagingBuffer, nullptr);
            vkFreeMemory(m_ctx.device, stagingMemory, nullptr);
            return kInvalidTextureId;
        }

        std::memcpy(mapped, data.data(), static_cast<std::size_t>(uploadSize));
        vkUnmapMemory(m_ctx.device, stagingMemory);

        // -----------------------------------------------------
        // Create device-local image (NO mip-gen here; KTX already has mips)
        // -----------------------------------------------------
        const VkImageUsageFlags usage =
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT;

        vkutil::GpuImage gpuImg =
            vkutil::createDeviceLocalImage2D(m_ctx, width, height, mipLevels, format, usage);

        if (!gpuImg.valid())
        {
            std::cerr << "TextureHandler: createDeviceLocalImage2D(KTX) failed for '"
                      << debugName << "'\n";
            vkDestroyBuffer(m_ctx.device, stagingBuffer, nullptr);
            vkFreeMemory(m_ctx.device, stagingMemory, nullptr);
            return kInvalidTextureId;
        }

        // -----------------------------------------------------
        // Record copy of each mip level using offsets/sizes
        // -----------------------------------------------------
        VkCommandPool   pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd  = beginOneShotCmd(m_ctx, pool);
        if (cmd == VK_NULL_HANDLE)
        {
            std::cerr << "TextureHandler: beginOneShotCmd failed for KTX '" << debugName << "'\n";
            vkDestroyImage(m_ctx.device, gpuImg.image, nullptr);
            vkFreeMemory(m_ctx.device, gpuImg.memory, nullptr);
            vkDestroyBuffer(m_ctx.device, stagingBuffer, nullptr);
            vkFreeMemory(m_ctx.device, stagingMemory, nullptr);
            return kInvalidTextureId;
        }

        imageBarrier(cmd,
                     gpuImg.image,
                     VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     mipLevels);

        std::vector<VkBufferImageCopy> regions;
        regions.reserve(mipLevels);

        for (uint32_t level = 0; level < mipLevels; ++level)
        {
            const Image::KtxMipLevel& ml = mips[level];

            VkBufferImageCopy r{};
            r.bufferOffset                    = static_cast<VkDeviceSize>(ml.offset);
            r.bufferRowLength                 = 0; // tightly packed
            r.bufferImageHeight               = 0;
            r.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            r.imageSubresource.mipLevel       = level;
            r.imageSubresource.baseArrayLayer = 0;
            r.imageSubresource.layerCount     = 1;
            r.imageOffset                     = {0, 0, 0};
            r.imageExtent                     = {
                static_cast<uint32_t>(std::max(1u, ml.width)),
                static_cast<uint32_t>(std::max(1u, ml.height)),
                1u};

            regions.push_back(r);
        }

        vkCmdCopyBufferToImage(cmd,
                               stagingBuffer,
                               gpuImg.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(regions.size()),
                               regions.data());

        imageBarrier(cmd,
                     gpuImg.image,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     mipLevels);

        if (!endOneShotCmd(m_ctx, cmd, pool))
        {
            std::cerr << "TextureHandler: endOneShotCmd failed for KTX '" << debugName << "'\n";
            vkDestroyImage(m_ctx.device, gpuImg.image, nullptr);
            vkFreeMemory(m_ctx.device, gpuImg.memory, nullptr);
            vkDestroyBuffer(m_ctx.device, stagingBuffer, nullptr);
            vkFreeMemory(m_ctx.device, stagingMemory, nullptr);
            return kInvalidTextureId;
        }

        // Staging no longer needed
        vkDestroyBuffer(m_ctx.device, stagingBuffer, nullptr);
        vkFreeMemory(m_ctx.device, stagingMemory, nullptr);

        // -----------------------------------------------------
        // Create view + sampler
        // -----------------------------------------------------
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
            std::cerr << "TextureHandler: vkCreateImageView(KTX) failed for '"
                      << debugName << "'\n";
            vkDestroyImage(m_ctx.device, gpuImg.image, nullptr);
            vkFreeMemory(m_ctx.device, gpuImg.memory, nullptr);
            return kInvalidTextureId;
        }

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
            std::cerr << "TextureHandler: vkCreateSampler(KTX) failed for '"
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
        return id;
    }

    // ---------------------------------------------------------
    // stb / raw pixels path (your original implementation)
    // ---------------------------------------------------------
    const int      width    = img->width();
    const int      height   = img->height();
    int            channels = img->channels();
    const uint8_t* pixels   = img->data();

    if (width <= 0 || height <= 0 || !pixels)
    {
        std::cerr << "TextureHandler::createTextureInternal: image has no data for '"
                  << debugName << "'\n";
        return kInvalidTextureId;
    }

    std::vector<uint8_t> rgba;

    VkFormat format = VK_FORMAT_UNDEFINED;
    switch (channels)
    {
        case 4:
            format = desc.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            break;
        case 3: {
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
                dst[3] = 255;
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

    uint32_t mipLevels = 1;
    if (desc.generateMipmaps)
    {
        const int maxDim = std::max(width, height);
        mipLevels        = static_cast<uint32_t>(std::floor(std::log2(maxDim))) + 1u;
    }

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

    vkutil::transitionImageLayout(m_ctx,
                                  gpuImg.image,
                                  gpuImg.format,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  gpuImg.mipLevels);

    vkutil::copyBufferToImage(m_ctx,
                              stagingBuffer,
                              gpuImg.image,
                              width,
                              height);

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

    vkDestroyBuffer(m_ctx.device, stagingBuffer, nullptr);
    vkFreeMemory(m_ctx.device, stagingMemory, nullptr);

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
