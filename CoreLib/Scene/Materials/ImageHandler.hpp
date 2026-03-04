#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "Image.hpp"
#include "SysCounter.hpp"

using ImageId                     = int32_t;
constexpr ImageId kInvalidImageId = -1;

class ImageHandler
{
public:
    ImageHandler();
    ~ImageHandler() = default;

    // Load from file (PNG/JPG). Reuses existing if same normalized path.
    [[nodiscard]] ImageId loadFromFile(const std::filesystem::path& path,
                                       bool                         flipY = true);

    // Encoded data in memory (e.g. glTF bufferView containing PNG/JPEG)
    [[nodiscard]] ImageId loadFromEncodedMemory(std::span<const unsigned char> encodedData,
                                                const std::string&             nameHint,
                                                bool                           flipY = true);

    // Already-decoded pixels
    [[nodiscard]] ImageId createFromRaw(const unsigned char* pixels,
                                        int                  width,
                                        int                  height,
                                        int                  channels,
                                        const std::string&   nameHint,
                                        bool                 flipY = false);

    // Access
    [[nodiscard]] const Image* get(ImageId id) const noexcept;
    [[nodiscard]] Image*       get(ImageId id) noexcept;

    // For UI: enumerate all images
    [[nodiscard]] const std::vector<Image>& images() const noexcept
    {
        return m_images;
    }

    // Simple lifetime management
    void clear() noexcept;

    [[nodiscard]] SysCounterPtr changeCounter() const noexcept;

private:
    std::vector<Image>                       m_images;
    std::unordered_map<std::string, ImageId> m_pathToId;

    SysCounterPtr m_changeCounter;
};
