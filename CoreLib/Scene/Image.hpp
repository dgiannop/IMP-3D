#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "PathUtilities.hpp"

class Image
{
public:
    Image() = default;

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
        return m_width > 0 && m_height > 0 && !m_pixels.empty();
    }

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

    /// Sets the image source path (always normalized via PathUtil)
    const std::filesystem::path& path() const
    {
        return m_path;
    }

private:
    std::string                m_name;
    std::filesystem::path      m_path;
    int                        m_width = 0, m_height = 0, m_channels = 0;
    std::vector<unsigned char> m_pixels;
};
