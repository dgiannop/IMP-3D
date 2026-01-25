#include "Image.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <ktx.h>
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{
    static std::string toLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static bool isKtxExtension(const std::filesystem::path& p)
    {
        const std::string ext = toLower(p.extension().string());
        return ext == ".ktx" || ext == ".ktx2";
    }

    static const char* ktxErrStr(KTX_error_code ec) noexcept
    {
        const char* s = ::ktxErrorString(ec);
        return s ? s : "KTX_UNKNOWN_ERROR";
    }

    // For now: hardcode a good desktop target.
    // Later: choose based on Vulkan device format support.
    static ktx_transcode_fmt_e defaultTranscodeTarget() noexcept
    {
        return KTX_TTF_BC7_RGBA;
    }

    static bool transcodeIfNeeded(ktxTexture* baseTex, VkFormat& outVkFormat) noexcept
    {
        if (!baseTex)
            return false;

        // Only KTX2 can need transcoding (BasisU / UASTC).
        if (baseTex->classId != ktxTexture2_c)
        {
            // KTX1: no vkFormat field in this libktx build.
            // You can add a GL-internal-format -> VkFormat mapping later if you need KTX1.
            outVkFormat = VK_FORMAT_UNDEFINED;
            return true;
        }

        ktxTexture2* tex2 = reinterpret_cast<ktxTexture2*>(baseTex);

        const bool needs = (ktxTexture2_NeedsTranscoding(tex2) != KTX_FALSE);
        if (!needs)
        {
            outVkFormat = static_cast<VkFormat>(tex2->vkFormat);
            return true;
        }

        const ktx_transcode_fmt_e target = defaultTranscodeTarget();

        // IMPORTANT: Must transcode before asking for mip offsets / using pData for BasisU payloads.
        const KTX_error_code ec = ktxTexture2_TranscodeBasis(tex2, target, 0);
        if (ec != KTX_SUCCESS)
            return false;

        // After transcode, vkFormat should reflect the transcoded block format.
        outVkFormat = static_cast<VkFormat>(tex2->vkFormat);
        return true;
    }

    static bool buildMipTable(ktxTexture*                      tex,
                              std::vector<Image::KtxMipLevel>& outMips) noexcept
    {
        outMips.clear();
        if (!tex)
            return false;

        const uint32_t levels = tex->numLevels;
        if (levels == 0)
            return false;

        outMips.reserve(levels);

        // We store only layer 0, faceSlice 0 (matches your current usage).
        const uint32_t layer     = 0;
        const uint32_t faceSlice = 0;

        for (uint32_t level = 0; level < levels; ++level)
        {
            ktx_size_t           offset = 0;
            const KTX_error_code ecOff  = ktxTexture_GetImageOffset(
                ktxTexture(tex),
                /*level*/ level,
                /*layer*/ 0,
                /*faceSlice*/ 0,
                &offset);

            if (ecOff != KTX_SUCCESS)
                return false;

            const ktx_size_t levelSize = ktxTexture_GetImageSize(tex, level);

            Image::KtxMipLevel m = {};
            m.level              = level;
            m.width              = std::max(1u, tex->baseWidth >> level);
            m.height             = std::max(1u, tex->baseHeight >> level);
            m.depth              = std::max(1u, tex->baseDepth >> level);
            m.offset             = static_cast<VkDeviceSize>(offset);
            m.size               = static_cast<VkDeviceSize>(levelSize);

            outMips.push_back(m);
        }

        return true;
    }

} // namespace

void Image::clear() noexcept
{
    m_width    = 0;
    m_height   = 0;
    m_channels = 0;
    m_pixels.clear();

    m_isKtx               = false;
    m_ktxVkFormat         = VK_FORMAT_UNDEFINED;
    m_ktxNeedsTranscoding = false;
    m_ktxLevels           = 0;
    m_ktxLayers           = 0;
    m_ktxFaces            = 0;
    m_ktxData.clear();
    m_ktxMips.clear();
}

bool Image::loadFromFile(const std::filesystem::path& path, bool flipY)
{
    clear();

    m_path = PathUtil::normalizedPath(path); // store normalized!

    // KTX/KTX2 route (ignore flipY here; KTX payload should be treated as authoritative)
    if (isKtxExtension(m_path))
        return loadKtxFromFile_(m_path);

    // stb route
    stbi_set_flip_vertically_on_load(flipY);
    unsigned char* raw = stbi_load(m_path.string().c_str(), &m_width, &m_height, &m_channels, 0);

    if (!raw)
    {
        std::cerr << "Failed to load image: " << m_path
                  << " reason: " << stbi_failure_reason() << "\n";
        return false;
    }

    m_pixels.assign(raw, raw + (m_width * m_height * m_channels));
    stbi_image_free(raw);
    return true;
}

bool Image::loadFromMemory(const unsigned char* pixels,
                           int                  width,
                           int                  height,
                           int                  channels,
                           bool                 flipY)
{
    clear();

    if (!pixels || width <= 0 || height <= 0 || channels <= 0)
        return false;

    m_width    = width;
    m_height   = height;
    m_channels = channels;
    m_pixels.resize(size_t(width) * size_t(height) * size_t(channels));

    const size_t rowBytes = size_t(width) * size_t(channels);
    if (!flipY)
    {
        std::memcpy(m_pixels.data(), pixels, rowBytes * size_t(height));
    }
    else
    {
        for (int y = 0; y < height; ++y)
        {
            const unsigned char* src = pixels + size_t(y) * rowBytes;
            unsigned char*       dst = m_pixels.data() + size_t(height - 1 - y) * rowBytes;
            std::memcpy(dst, src, rowBytes);
        }
    }

    m_path.clear();
    return true;
}

bool Image::loadFromEncodedMemory(const unsigned char* data,
                                  int                  sizeInBytes,
                                  bool                 flipY)
{
    clear();

    if (!data || sizeInBytes <= 0)
        return false;

    // Try KTX magic first.
    if (sizeInBytes >= 12)
    {
        const unsigned char ktx1Magic[12] = {
            0xAB,
            0x4B,
            0x54,
            0x58,
            0x20,
            0x31,
            0x31,
            0xBB,
            0x0D,
            0x0A,
            0x1A,
            0x0A};
        const unsigned char ktx2Magic[12] = {
            0xAB,
            0x4B,
            0x54,
            0x58,
            0x20,
            0x32,
            0x30,
            0xBB,
            0x0D,
            0x0A,
            0x1A,
            0x0A};

        if (std::memcmp(data, ktx1Magic, 12) == 0 || std::memcmp(data, ktx2Magic, 12) == 0)
            return loadKtxFromMemory_(data, sizeInBytes);
    }

    // stb route
    stbi_set_flip_vertically_on_load(flipY);
    unsigned char* raw = stbi_load_from_memory(
        data,
        sizeInBytes,
        &m_width,
        &m_height,
        &m_channels,
        0);

    if (!raw)
    {
        std::cerr << "Failed to load image from memory: "
                  << stbi_failure_reason() << "\n";
        return false;
    }

    m_pixels.assign(raw, raw + (m_width * m_height * m_channels));
    stbi_image_free(raw);

    m_path.clear();
    return true;
}

bool Image::loadKtxFromFile_(const std::filesystem::path& path)
{
    // Generic KTX loader: supports both KTX1 and KTX2
    ktxTexture* tex = nullptr;

    const KTX_error_code ec = ktxTexture_CreateFromNamedFile(
        path.string().c_str(),
        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
        &tex);

    if (ec != KTX_SUCCESS || !tex)
    {
        std::cerr << "Failed to load KTX: " << path
                  << " reason: " << ktxErrStr(ec) << "\n";
        return false;
    }

    // Fill common fields
    m_isKtx = true;

    // Dimensions (base level)
    m_width    = static_cast<int>(tex->baseWidth);
    m_height   = static_cast<int>(tex->baseHeight);
    m_channels = 0;

    // Transcode if needed (KTX2 BasisU/UASTC)
    m_ktxNeedsTranscoding = false;
    if (tex->classId == ktxTexture2_c)
    {
        ktxTexture2* tex2     = reinterpret_cast<ktxTexture2*>(tex);
        m_ktxNeedsTranscoding = (ktxTexture2_NeedsTranscoding(tex2) != KTX_FALSE);
    }

    VkFormat vkFmt = VK_FORMAT_UNDEFINED;
    if (!transcodeIfNeeded(tex, vkFmt))
    {
        std::cerr << "Failed to transcode KTX2 Basis payload: " << path << "\n";
        ktxTexture_Destroy(tex);
        clear();
        return false;
    }
    m_ktxVkFormat = vkFmt;

    m_ktxLevels = tex->numLevels;
    m_ktxLayers = tex->numLayers;
    m_ktxFaces  = tex->numFaces;

    // Copy payload bytes (after transcode if any)
    const ktx_uint8_t* srcData = ktxTexture_GetData(tex);
    const ktx_size_t   srcSize = ktxTexture_GetDataSize(tex);

    if (!srcData || srcSize == 0)
    {
        std::cerr << "KTX has no image payload: " << path << "\n";
        ktxTexture_Destroy(tex);
        clear();
        return false;
    }

    m_ktxData.resize(static_cast<size_t>(srcSize));
    std::memcpy(m_ktxData.data(), srcData, static_cast<size_t>(srcSize));

    // Build mip table offsets (layer 0, face 0)
    if (!buildMipTable(tex, m_ktxMips))
    {
        std::cerr << "KTX failed to build mip table: " << path << "\n";
        ktxTexture_Destroy(tex);
        clear();
        return false;
    }

    ktxTexture_Destroy(tex);
    return true;
}

bool Image::loadKtxFromMemory_(const unsigned char* data, int sizeInBytes)
{
    ktxTexture* tex = nullptr;

    const KTX_error_code ec = ktxTexture_CreateFromMemory(
        reinterpret_cast<const ktx_uint8_t*>(data),
        static_cast<ktx_size_t>(sizeInBytes),
        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
        &tex);

    if (ec != KTX_SUCCESS || !tex)
    {
        std::cerr << "Failed to load KTX from memory, reason: "
                  << ktxErrStr(ec) << "\n";
        return false;
    }

    m_isKtx = true;

    m_width    = static_cast<int>(tex->baseWidth);
    m_height   = static_cast<int>(tex->baseHeight);
    m_channels = 0;

    m_ktxNeedsTranscoding = false;
    if (tex->classId == ktxTexture2_c)
    {
        ktxTexture2* tex2     = reinterpret_cast<ktxTexture2*>(tex);
        m_ktxNeedsTranscoding = (ktxTexture2_NeedsTranscoding(tex2) != KTX_FALSE);
    }

    VkFormat vkFmt = VK_FORMAT_UNDEFINED;
    if (!transcodeIfNeeded(tex, vkFmt))
    {
        std::cerr << "Failed to transcode KTX2 Basis payload from memory.\n";
        ktxTexture_Destroy(tex);
        clear();
        return false;
    }
    m_ktxVkFormat = vkFmt;

    m_ktxLevels = tex->numLevels;
    m_ktxLayers = tex->numLayers;
    m_ktxFaces  = tex->numFaces;

    const ktx_uint8_t* srcData = ktxTexture_GetData(tex);
    const ktx_size_t   srcSize = ktxTexture_GetDataSize(tex);

    if (!srcData || srcSize == 0)
    {
        std::cerr << "KTX from memory has no image payload.\n";
        ktxTexture_Destroy(tex);
        clear();
        return false;
    }

    m_ktxData.resize(static_cast<size_t>(srcSize));
    std::memcpy(m_ktxData.data(), srcData, static_cast<size_t>(srcSize));

    if (!buildMipTable(tex, m_ktxMips))
    {
        std::cerr << "KTX(mem) failed to build mip table.\n";
        ktxTexture_Destroy(tex);
        clear();
        return false;
    }

    ktxTexture_Destroy(tex);
    m_path.clear();
    return true;
}
