#include "CoreDocument.hpp"

#include <algorithm>
#include <cctype>

#include "Formats/SceneIOUtils.hpp"
#include "Scene.hpp"

namespace
{
    static SceneIOReport* reportOrLocal(SceneIOReport* out, SceneIOReport& local) noexcept
    {
        return out ? out : &local;
    }

    static std::string toLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static std::filesystem::path forceImpExtension(std::filesystem::path p)
    {
        if (toLower(p.extension().string()) != ".imp")
            p.replace_extension(".imp");
        return p;
    }
} // namespace

CoreDocument::CoreDocument(Scene* owner) noexcept
    : m_scene(owner)
{
    resetSaveState();
}

ItemFactory<SceneFormat>& CoreDocument::formatFactory() noexcept
{
    return m_formatFactory;
}

bool CoreDocument::hasFilePath() const noexcept
{
    return m_path.has_value();
}

const std::filesystem::path& CoreDocument::filePath() const noexcept
{
    static const std::filesystem::path kEmpty = {};
    return m_path ? *m_path : kEmpty;
}

void CoreDocument::clearFilePath() noexcept
{
    m_path.reset();
}

bool CoreDocument::requestNew() const noexcept
{
    return !hasUnsavedChanges();
}

bool CoreDocument::requestExit() const noexcept
{
    return !hasUnsavedChanges();
}

uint64_t CoreDocument::currentCounter() const noexcept
{
    if (!m_scene)
        return 0;

    const SysCounterPtr c = m_scene->contentChangeCounter();
    if (!c)
        return 0;

    return c->value();
}

void CoreDocument::markDirtyFallback() const noexcept
{
    m_dirtyFallback = true;
}

bool CoreDocument::hasUnsavedChanges() const noexcept
{
    const uint64_t now = currentCounter();

    // If the counter is available (or we have a saved snapshot), prefer it.
    if (now != 0 || m_savedCounter != 0)
        return now != m_savedCounter;

    return m_dirtyFallback;
}

void CoreDocument::resetSaveState() noexcept
{
    m_savedCounter  = currentCounter();
    m_dirtyFallback = false;
}

std::string CoreDocument::extensionLower_(const std::filesystem::path& path)
{
    return toLower(path.extension().string());
}

bool CoreDocument::isNativeImp_(const std::filesystem::path& path) noexcept
{
    return extensionLower_(path) == ".imp";
}

std::unique_ptr<SceneFormat> CoreDocument::createFormatForPath_(const std::filesystem::path& path) const
{
    const std::string ext = extensionLower_(path);
    if (ext.empty())
        return nullptr;

    return m_formatFactory.createItem(ext);
}

bool CoreDocument::newFile() noexcept
{
    if (!m_scene)
        return false;

    m_scene->clear();

    clearFilePath();
    resetSaveState();
    return true;
}

bool CoreDocument::openFile(const std::filesystem::path& path, const LoadOptions& options, SceneIOReport* report)
{
    SceneIOReport  local = {};
    SceneIOReport* rep   = reportOrLocal(report, local);

    if (!m_scene)
    {
        rep->status = SceneIOStatus::InvalidScene;
        rep->error("CoreDocument::openFile: scene is null");
        dumpSceneIOReport(*rep);
        return false;
    }

    auto fmt = createFormatForPath_(path);
    if (!fmt)
    {
        rep->status = SceneIOStatus::UnsupportedFormat;
        rep->error("CoreDocument::openFile: unsupported extension: " + path.extension().string());
        dumpSceneIOReport(*rep);
        return false;
    }

    if (!fmt->load(m_scene, path, options, *rep))
    {
        dumpSceneIOReport(*rep);
        return false;
    }

#ifndef NDEBUG
    dumpSceneIOReport(*rep); // ← this is GOLD while debugging glTF
#endif

    // Opening a file updates document path to what was opened.
    m_path = path;
    resetSaveState();
    return true;
}

bool CoreDocument::importFile(const std::filesystem::path& path, const LoadOptions& options, SceneIOReport* report)
{
    SceneIOReport  local = {};
    SceneIOReport* rep   = reportOrLocal(report, local);

    if (!m_scene)
    {
        rep->status = SceneIOStatus::InvalidScene;
        rep->error("CoreDocument::importFile: scene is null");
        return false;
    }

    auto fmt = createFormatForPath_(path);
    if (!fmt)
    {
        rep->status = SceneIOStatus::UnsupportedFormat;
        rep->error("CoreDocument::importFile: unsupported extension: " + path.extension().string());
        return false;
    }

    LoadOptions opt       = options;
    opt.mergeIntoExisting = true;

    if (!fmt->load(m_scene, path, opt, *rep))
        return false;

    // Import does not change document path; it makes the doc dirty.
    markDirtyFallback();
    return true;
}

bool CoreDocument::save(const SaveOptions& options, SceneIOReport* report)
{
    SceneIOReport  local = {};
    SceneIOReport* rep   = reportOrLocal(report, local);

    if (!m_scene)
    {
        rep->status = SceneIOStatus::InvalidScene;
        rep->error("CoreDocument::save: scene is null");
        return false;
    }

    if (!m_path.has_value())
    {
        rep->status = SceneIOStatus::Cancelled;
        rep->warning("CoreDocument::save: no file path set (use Save As)");
        return false;
    }

    if (!isNativeImp_(*m_path))
    {
        rep->status = SceneIOStatus::UnsupportedFormat;
        rep->error("CoreDocument::save: current document path is not native .imp");
        return false;
    }

    auto fmt = createFormatForPath_(*m_path);
    if (!fmt || !fmt->supportsSave())
    {
        rep->status = SceneIOStatus::UnsupportedFormat;
        rep->error("CoreDocument::save: save not supported for extension: " + m_path->extension().string());
        return false;
    }

    if (!fmt->save(m_scene, *m_path, options, *rep))
        return false;

    resetSaveState();
    return true;
}

bool CoreDocument::saveAs(const std::filesystem::path& path, const SaveOptions& options, SceneIOReport* report)
{
    SceneIOReport  local = {};
    SceneIOReport* rep   = reportOrLocal(report, local);

    if (!m_scene)
    {
        rep->status = SceneIOStatus::InvalidScene;
        rep->error("CoreDocument::saveAs: scene is null");
        return false;
    }

    const std::filesystem::path nativePath = forceImpExtension(path);

    auto fmt = m_formatFactory.createItem(".imp");
    if (!fmt || !fmt->supportsSave())
    {
        rep->status = SceneIOStatus::UnsupportedFormat;
        rep->error("CoreDocument::saveAs: native .imp format not registered");
        return false;
    }

    if (!fmt->save(m_scene, nativePath, options, *rep))
        return false;

    m_path = nativePath;
    resetSaveState();
    return true;
}

bool CoreDocument::exportFile(const std::filesystem::path& path, const SaveOptions& options, SceneIOReport* report) const
{
    SceneIOReport  local = {};
    SceneIOReport* rep   = reportOrLocal(report, local);

    if (!m_scene)
    {
        rep->status = SceneIOStatus::InvalidScene;
        rep->error("CoreDocument::exportFile: scene is null");
        return false;
    }

    auto fmt = createFormatForPath_(path);
    if (!fmt || !fmt->supportsSave())
    {
        rep->status = SceneIOStatus::UnsupportedFormat;
        rep->error("CoreDocument::exportFile: export not supported for extension: " + path.extension().string());
        return false;
    }

    // Export does NOT touch document path or save snapshot.
    return fmt->save(m_scene, path, options, *rep);
}
