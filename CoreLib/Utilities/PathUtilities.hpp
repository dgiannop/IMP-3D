#pragma once

#include <filesystem>
#include <string>
#include <system_error>

/**
 * @brief Cross-platform utility functions for robust asset/file path handling.
 *
 * Use for any resource: textures, meshes, scenes, audio, project files, etc.
 * Ensures all paths are stored, compared, and displayed in a normalized,
 * canonical, and platform-agnostic way. All returned strings use forward slashes.
 */
namespace PathUtil
{

    // ============================================================================
    // Normalization (absolute, normalized, forward-slash string)
    // ============================================================================

    /**
     * @brief Returns a normalized, absolute, forward-slash-separated path for storage or comparison.
     * @param input Path to normalize (may be relative or absolute).
     * @return std::string Normalized, absolute path with forward slashes.
     * @see normalizedPath(const std::string&)
     */
    inline std::string normalizedPath(const std::filesystem::path& input)
    {
        return std::filesystem::absolute(input).lexically_normal().generic_string();
    }

    /**
     * @brief Overload: Normalize from a string path.
     * @param input Path to normalize as a string.
     * @return std::string Normalized, absolute path with forward slashes.
     * @see normalizedPath(const std::filesystem::path&)
     */
    inline std::string normalizedPath(const std::string& input)
    {
        return normalizedPath(std::filesystem::path(input));
    }

    // ============================================================================
    // Relative path conversion (for export, portable saves, etc.)
    // ============================================================================

    /**
     * @brief Converts a normalized absolute path to a path relative to a given base directory.
     * @param absPath Normalized absolute path as a string (from PathUtil::normalizedPath).
     * @param relBase Base directory to relativize against.
     * @return std::string Relative path with forward slashes (or normalized if not possible).
     * @see toRelative(const std::filesystem::path&, const std::filesystem::path&)
     */
    inline std::string toRelative(const std::string& absPath, const std::filesystem::path& relBase)
    {
        std::filesystem::path abs(absPath);
        std::error_code       ec;
        auto                  rel = std::filesystem::relative(abs, relBase, ec);
        if (ec)
            return abs.generic_string();
        return rel.generic_string();
    }

    /**
     * @brief Overload: Converts a normalized absolute path to relative, both as paths.
     * @param absPath Normalized absolute path as std::filesystem::path.
     * @param relBase Base directory.
     * @return std::string Relative path with forward slashes (or normalized if not possible).
     * @see toRelative(const std::string&, const std::filesystem::path&)
     */
    inline std::string toRelative(const std::filesystem::path& absPath, const std::filesystem::path& relBase)
    {
        std::error_code ec;
        auto            rel = std::filesystem::relative(absPath, relBase, ec);
        if (ec)
            return absPath.generic_string();
        return rel.generic_string();
    }

    // ============================================================================
    // Filename extraction (for display/UI, etc.)
    // ============================================================================

    /**
     * @brief Extracts the filename (with extension) from a path string.
     * @param path Path as a string (absolute, relative, or normalized).
     * @return std::string Filename with extension (e.g., "diffuse.png").
     * @see filename(const std::filesystem::path&)
     */
    inline std::string filename(const std::string& path)
    {
        return std::filesystem::path(path).filename().string();
    }

    /**
     * @brief Overload: Extracts the filename (with extension) from a std::filesystem::path.
     * @param path Path as a std::filesystem::path.
     * @return std::string Filename with extension (e.g., "my_asset.obj").
     * @see filename(const std::string&)
     */
    inline std::string filename(const std::filesystem::path& path)
    {
        return path.filename().string();
    }

    // ============================================================================
    // Parent directory extraction (useful for UI and project references)
    // ============================================================================

    /**
     * @brief Gets the parent directory of a path (as a normalized string).
     * @param path Path as a string.
     * @return std::string Normalized, absolute path of the parent directory (forward slashes).
     */
    inline std::string parent(const std::string& path)
    {
        return normalizedPath(std::filesystem::path(path).parent_path());
    }

    /**
     * @brief Overload: Gets the parent directory from a std::filesystem::path.
     * @param path Path as a std::filesystem::path.
     * @return std::string Normalized, absolute path of the parent directory (forward slashes).
     */
    inline std::string parent(const std::filesystem::path& path)
    {
        return normalizedPath(path.parent_path());
    }

    // ============================================================================
    // Extension extraction (for filtering assets or showing in UI)
    // ============================================================================

    /**
     * @brief Gets the file extension (including the dot, e.g. ".png") from a path string.
     * @param path Path as a string.
     * @return std::string File extension (lowercase, with dot), or empty string.
     */
    inline std::string extension(const std::string& path)
    {
        std::string ext = std::filesystem::path(path).extension().string();
        for (char& c : ext)
            c = std::tolower(static_cast<unsigned char>(c));
        return ext;
    }

    /**
     * @brief Overload: Gets the file extension from a std::filesystem::path.
     * @param path Path as a std::filesystem::path.
     * @return std::string File extension (lowercase, with dot), or empty string.
     */
    inline std::string extension(const std::filesystem::path& path)
    {
        std::string ext = path.extension().string();
        for (char& c : ext)
            c = std::tolower(static_cast<unsigned char>(c));
        return ext;
    }

    // ============================================================================
    // Existence check (for robust import/workflows)
    // ============================================================================

    /**
     * @brief Checks if a file exists at the given path.
     * @param path Path as a string.
     * @return true if the file exists.
     */
    inline bool exists(const std::string& path)
    {
        return std::filesystem::exists(std::filesystem::path(path));
    }

    /**
     * @brief Overload: Checks if a file exists at the given path.
     * @param path Path as a std::filesystem::path.
     * @return true if the file exists.
     */
    inline bool exists(const std::filesystem::path& path)
    {
        return std::filesystem::exists(path);
    }

    // ============================================================================
    // Quick asset type checkers (optional/advanced, for filtering in UI)
    // ============================================================================

    /**
     * @brief Checks if a path is a texture/image file by extension.
     * Common image extensions: .png, .jpg, .jpeg, .bmp, .tga, .exr, .dds, .hdr
     * @param path Path as string or std::filesystem::path.
     * @return true if the extension matches a known image type.
     */
    inline bool isImage(const std::string& path)
    {
        static const char* exts[] = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".exr", ".dds", ".hdr"};
        std::string        ext    = extension(path);
        for (const char* e : exts)
            if (ext == e)
                return true;
        return false;
    }
    inline bool isImage(const std::filesystem::path& path)
    {
        return isImage(path.string());
    }

    /**
     * @brief Checks if a path is a mesh file by extension.
     * Common mesh extensions: .obj, .fbx, .gltf, .glb, .ply, .stl
     * @param path Path as string or std::filesystem::path.
     * @return true if the extension matches a known mesh type.
     */
    inline bool isMesh(const std::string& path)
    {
        static const char* exts[] = {".obj", ".fbx", ".gltf", ".glb", ".ply", ".stl"};
        std::string        ext    = extension(path);
        for (const char* e : exts)
            if (ext == e)
                return true;
        return false;
    }
    inline bool isMesh(const std::filesystem::path& path)
    {
        return isMesh(path.string());
    }

    // ============================================================================
    // Relative path + export name sanitization (for file format writing)
    // ============================================================================

    /**
     * @brief Converts a normalized asset path to a relative, export-sanitized path for file formats.
     *      * Combines relative path conversion and export sanitization (removes/escapes illegal characters,
     * replaces spaces with underscores, allows only a-z, A-Z, 0-9, '_', '-', '.' and '/').
     * Useful for OBJ/MTL export, etc.
     *      * @param normalizedPath Normalized (absolute, forward-slash) asset path.
     * @param exportBase    Base directory to relativize against (e.g., the .mtl or .obj parent).
     * @return std::string  A relative, export-sanitized string safe for file formats.
     *      * @see toRelative
     */
    inline std::string relativeSanitized(const std::string& normalizedPath, const std::filesystem::path& exportBase)
    {
        // Convert to relative path
        std::filesystem::path abs(normalizedPath);
        std::error_code       ec;
        std::filesystem::path rel     = std::filesystem::relative(abs, exportBase, ec);
        std::string           relPath = ec ? abs.generic_string() : rel.generic_string();

        // Sanitize (replace spaces with '_', strip special chars, keep a-zA-Z0-9_.- and '/')
        std::string sanitized;
        sanitized.reserve(relPath.size());
        for (char c : relPath)
        {
            if (c == ' ')
                sanitized += '_';
            else if (
                std::isalnum(static_cast<unsigned char>(c)) ||
                c == '_' || c == '-' || c == '.' || c == '/')
                sanitized += c;
            // skip all other characters
        }
        if (sanitized.empty())
            return "unnamed";
        return sanitized;
    }

} // namespace PathUtil
