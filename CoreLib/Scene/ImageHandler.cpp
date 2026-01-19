#include "ImageHandler.hpp"

#include <iostream>

#include "PathUtilities.hpp"

// ---------------------------------------------------------
// Helpers
// ---------------------------------------------------------

namespace
{
    [[nodiscard]] bool isValidId(ImageId id, std::size_t size) noexcept
    {
        return id >= 0 && static_cast<std::size_t>(id) < size;
    }
} // namespace

// ---------------------------------------------------------
// Public API
// ---------------------------------------------------------
ImageHandler::ImageHandler() : m_changeCounter{std::make_shared<SysCounter>()}
{
}

ImageId ImageHandler::loadFromFile(const std::filesystem::path& path,
                                   bool                         flipY)
{
    // Normalize the path (so the same file isn't loaded twice)
    const std::string normalized = PathUtil::normalizedPath(path);

    // Reuse existing image if already loaded
    if (auto it = m_pathToId.find(normalized); it != m_pathToId.end())
    {
        return it->second;
    }

    Image img;
    if (!img.loadFromFile(path, flipY))
    {
        // std::cerr << "ImageHandler::loadFromFile: failed to load image: " << normalized << "\n";
        return kInvalidImageId;
    }

    // Ensure the Image knows its source path (if Image supports this)
    img.setPath(normalized);

    const ImageId id = static_cast<ImageId>(m_images.size());
    m_images.push_back(std::move(img));
    m_pathToId.emplace(normalized, id);

    return id;
}

ImageId ImageHandler::loadFromEncodedMemory(std::span<const unsigned char> encodedData,
                                            const std::string&             nameHint,
                                            bool                           flipY)
{
    if (encodedData.empty())
        return kInvalidImageId;

    Image img;
    if (!img.loadFromEncodedMemory(encodedData.data(),
                                   static_cast<int>(encodedData.size()),
                                   flipY))
    {
        std::cerr << "ImageHandler::loadFromEncodedMemory: failed to decode image: "
                  << nameHint << "\n";
        return kInvalidImageId;
    }

    // For embedded images there is no filesystem path; I can
    // optionally store the nameHint somewhere inside Image if I add it.
    // e.g. img.setName(nameHint);

    const ImageId id = static_cast<ImageId>(m_images.size());
    m_images.push_back(std::move(img));

    return id;
}

ImageId ImageHandler::createFromRaw(const unsigned char* pixels,
                                    int                  width,
                                    int                  height,
                                    int                  channels,
                                    const std::string&   nameHint,
                                    bool                 flipY)
{
    if (!pixels || width <= 0 || height <= 0 || channels <= 0)
        return kInvalidImageId;

    Image img;
    if (!img.loadFromMemory(pixels, width, height, channels, flipY))
    {
        std::cerr << "ImageHandler::createFromRaw: failed to create image from raw data: " << nameHint << "\n";
        return kInvalidImageId;
    }

    // If Image later has a name field, set it here.
    // img.setName(nameHint);

    const ImageId id = static_cast<ImageId>(m_images.size());
    m_images.push_back(std::move(img));

    return id;
}

const Image* ImageHandler::get(ImageId id) const noexcept
{
    if (!isValidId(id, m_images.size()))
        return nullptr;

    return &m_images[static_cast<std::size_t>(id)];
}

Image* ImageHandler::get(ImageId id) noexcept
{
    if (!isValidId(id, m_images.size()))
        return nullptr;

    return &m_images[static_cast<std::size_t>(id)];
}

void ImageHandler::clear() noexcept
{
    m_images.clear();
    m_pathToId.clear();
}

SysCounterPtr ImageHandler::changeCounter() const noexcept
{
    return m_changeCounter;
}
