//============================================================
// ImageHandler.cpp  (FULL REPLACEMENT)
//============================================================
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

    [[nodiscard]] bool nameExists(const std::vector<Image>& images, const std::string& name)
    {
        if (name.empty())
            return false;

        for (const Image& img : images)
        {
            if (img.name() == name)
                return true;
        }
        return false;
    }

    [[nodiscard]] std::string makeUniqueName(const std::vector<Image>& images, const std::string& base)
    {
        if (base.empty())
            return base;

        if (!nameExists(images, base))
            return base;

        for (int n = 2; n < 1000000; ++n)
        {
            std::string candidate = base;
            candidate += " (";
            candidate += std::to_string(n);
            candidate += ")";

            if (!nameExists(images, candidate))
                return candidate;
        }

        return base;
    }

    [[nodiscard]] std::string fallbackEmbeddedName(ImageId id)
    {
        return std::string("EmbeddedImage_") + std::to_string(id);
    }

    [[nodiscard]] std::string fallbackRawName(ImageId id)
    {
        return std::string("Image_") + std::to_string(id);
    }

    [[nodiscard]] std::string stemNameOrEmpty(const std::filesystem::path& p)
    {
        // stem().string() can be empty for odd paths; keep it safe.
        return p.stem().string();
    }
} // namespace

// ---------------------------------------------------------
// Public API
// ---------------------------------------------------------

ImageHandler::ImageHandler() : m_changeCounter{std::make_shared<SysCounter>()}
{
}

ImageId ImageHandler::loadFromFile(const std::filesystem::path& path, bool flipY)
{
    // Normalize the path (so the same file isn't loaded twice).
    const std::string normalized = PathUtil::normalizedPath(path);

    // Reuse existing image if already loaded.
    if (auto it = m_pathToId.find(normalized); it != m_pathToId.end())
    {
        return it->second;
    }

    Image img;
    if (!img.loadFromFile(path, flipY))
    {
        return kInvalidImageId;
    }

    // Allocate ID before pushing so fallback naming can include the final ID.
    const ImageId id = static_cast<ImageId>(m_images.size());

    // Ensure the Image knows its source path.
    img.setPath(normalized);

    // Enforce non-empty name.
    std::string baseName = img.name();
    if (baseName.empty())
    {
        baseName = stemNameOrEmpty(path);
        if (baseName.empty())
            baseName = fallbackRawName(id);
    }

    img.setName(makeUniqueName(m_images, baseName));

    m_images.push_back(std::move(img));
    m_pathToId.emplace(normalized, id);

    m_changeCounter->change();
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

    const ImageId id = static_cast<ImageId>(m_images.size());

    // Enforce non-empty name (prefer hint; otherwise deterministic fallback).
    std::string baseName = nameHint;
    if (baseName.empty())
        baseName = fallbackEmbeddedName(id);

    img.setName(makeUniqueName(m_images, baseName));

    // No filesystem path for embedded images.

    m_images.push_back(std::move(img));

    m_changeCounter->change();
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
        std::cerr << "ImageHandler::createFromRaw: failed to create image from raw data: "
                  << nameHint << "\n";
        return kInvalidImageId;
    }

    const ImageId id = static_cast<ImageId>(m_images.size());

    // Enforce non-empty name.
    std::string baseName = nameHint;
    if (baseName.empty())
        baseName = fallbackRawName(id);

    img.setName(makeUniqueName(m_images, baseName));

    // No filesystem path for raw images by default.

    m_images.push_back(std::move(img));

    m_changeCounter->change();
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
    if (m_images.empty() && m_pathToId.empty())
        return;

    m_images.clear();
    m_pathToId.clear();

    m_changeCounter->change();
}

SysCounterPtr ImageHandler::changeCounter() const noexcept
{
    return m_changeCounter;
}
