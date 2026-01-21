#pragma once

/// Abstract GPU representation of a SceneObject.
///
/// Backends (VK / GL / etc.) provide concrete implementations that
/// own GPU buffers and know how to sync them from the CPU mesh.
class GpuResources
{
public:
    virtual ~GpuResources() noexcept = default;

    /// Ensure GPU buffers are up-to-date with the CPU mesh.
    ///
    /// Backends typically:
    ///  - query the owning SceneMesh / SysMesh
    ///  - compare change counters
    ///  - (re)create or update GPU buffers as needed
    virtual void update()
    {
    } // = 0;
};
