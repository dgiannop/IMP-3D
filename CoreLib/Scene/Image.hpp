#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "PathUtilities.hpp"

class Image
{
public:
    struct KtxMipLevel
    {
        uint32_t     level  = 0;
        uint32_t     width  = 0;
        uint32_t     height = 0;
        uint32_t     depth  = 1;
        VkDeviceSize offset = 0; // into m_ktxData
        VkDeviceSize size   = 0; // bytes for this level (all layers/faces packed as in KTX)
    };

public:
    Image() = default;

    ~Image() = default;

    // No copies
    Image(const Image&)            = delete;
    Image& operator=(const Image&) = delete;

    // Moves are fine
    Image(Image&&) noexcept            = default;
    Image& operator=(Image&&) noexcept = default;

    bool loadFromFile(const std::filesystem::path& path, bool flipY = true);

    bool loadFromMemory(const unsigned char* pixels,
                        int                  width,
                        int                  height,
                        int                  channels,
                        bool                 flipY = true);

    bool loadFromEncodedMemory(const unsigned char* data,
                               int                  sizeInBytes,
                               bool                 flipY);

    bool valid() const noexcept
    {
        if (m_width > 0 && m_height > 0 && !m_pixels.empty())
            return true;

        if (m_isKtx && m_width > 0 && m_height > 0 && !m_ktxData.empty())
            return true;

        return false;
    }

    // -------------------------------------------------------------------------
    // Classic pixel image access (stb, or loadFromMemory)
    // -------------------------------------------------------------------------
    int width() const
    {
        return m_width;
    }

    int height() const
    {
        return m_height;
    }

    int channels() const
    {
        return m_channels;
    }

    const unsigned char* data() const
    {
        return m_pixels.data();
    }

    unsigned char* data() noexcept
    {
        return m_pixels.data();
    }

    // -------------------------------------------------------------------------
    // KTX/KTX2 access (for Vulkan upload)
    // -------------------------------------------------------------------------
    bool isKtx() const noexcept
    {
        return m_isKtx;
    }

    VkFormat ktxVkFormat() const noexcept
    {
        return m_ktxVkFormat;
    }

    uint32_t ktxMipLevels() const noexcept
    {
        return m_ktxLevels;
    }

    uint32_t ktxLayers() const noexcept
    {
        return m_ktxLayers;
    }

    uint32_t ktxFaces() const noexcept
    {
        return m_ktxFaces;
    }

    bool ktxNeedsTranscoding() const noexcept
    {
        return m_ktxNeedsTranscoding;
    }

    const std::vector<std::uint8_t>& ktxData() const noexcept
    {
        return m_ktxData;
    }

    const std::vector<KtxMipLevel>& ktxMips() const noexcept
    {
        return m_ktxMips;
    }

    // Convenience: return pointer/size for a mip level (layer0/face0 offset is included in mips[i].offset)
    const std::uint8_t* ktxMipData(uint32_t level, VkDeviceSize* outBytes = nullptr) const noexcept
    {
        for (const KtxMipLevel& m : m_ktxMips)
        {
            if (m.level == level)
            {
                if (outBytes)
                    *outBytes = m.size;
                return m_ktxData.data() + static_cast<size_t>(m.offset);
            }
        }
        if (outBytes)
            *outBytes = 0;
        return nullptr;
    }

    // -------------------------------------------------------------------------
    // Name/path
    // -------------------------------------------------------------------------
    void setName(const std::string& name)
    {
        m_name = name;
    }

    const std::string& name() const noexcept
    {
        return m_name;
    }

    /// Sets the image source path (absolute, normalized)
    void setPath(const std::filesystem::path& p)
    {
        m_path = PathUtil::normalizedPath(p);
    }

    /// Gets the image source path (always normalized via PathUtil)
    const std::filesystem::path& path() const
    {
        return m_path;
    }

private:
    void clear() noexcept;

    bool loadKtxFromFile_(const std::filesystem::path& path);
    bool loadKtxFromMemory_(const unsigned char* data, int sizeInBytes);

private:
    std::string           m_name;
    std::filesystem::path m_path;

    // Classic pixel image
    int                        m_width    = 0;
    int                        m_height   = 0;
    int                        m_channels = 0;
    std::vector<unsigned char> m_pixels;

    // KTX/KTX2 payload
    bool                      m_isKtx               = false;
    VkFormat                  m_ktxVkFormat         = VK_FORMAT_UNDEFINED;
    bool                      m_ktxNeedsTranscoding = false;
    uint32_t                  m_ktxLevels           = 0;
    uint32_t                  m_ktxLayers           = 0;
    uint32_t                  m_ktxFaces            = 0;
    std::vector<std::uint8_t> m_ktxData;
    std::vector<KtxMipLevel>  m_ktxMips;
};
