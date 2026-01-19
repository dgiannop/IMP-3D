#include "Image.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstring>
#include <iostream>

bool Image::loadFromFile(const std::filesystem::path& path, bool flipY)
{
    m_path = PathUtil::normalizedPath(path); // store normalized!
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
    if (!pixels || width <= 0 || height <= 0 || channels <= 0)
        return false;

    m_width    = width;
    m_height   = height;
    m_channels = channels;
    m_pixels.resize(size_t(width) * size_t(height) * size_t(channels));

    const size_t rowBytes = size_t(width) * size_t(channels);
    if (!flipY)
    {
        // straight copy
        std::memcpy(m_pixels.data(), pixels, rowBytes * size_t(height));
    }
    else
    {
        // vertical flip copy
        for (int y = 0; y < height; ++y)
        {
            const unsigned char* src = pixels + size_t(y) * rowBytes;
            unsigned char*       dst = m_pixels.data() + size_t(height - 1 - y) * rowBytes;
            std::memcpy(dst, src, rowBytes);
        }
    }
    return true;
}

bool Image::loadFromEncodedMemory(const unsigned char* data,
                                  int                  sizeInBytes,
                                  bool                 flipY)
{
    if (!data || sizeInBytes <= 0)
        return false;

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
    m_path.clear(); // not from a file
    return true;
}
