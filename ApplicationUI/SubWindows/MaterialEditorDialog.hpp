#pragma once

#include <QListWidgetItem>
#include <cstdint>

#include "SubWindowBase.hpp"

class QComboBox;

namespace Ui
{
    class MaterialEditorDialog;
} // namespace Ui

class Material;
using ImageId = int32_t;

class MaterialEditorDialog final : public SubWindowBase
{
    Q_OBJECT

public:
    explicit MaterialEditorDialog(QWidget* parent = nullptr);
    ~MaterialEditorDialog() noexcept override;

    void idleEvent(class Core* core) override;

private slots:
    void onToggleLeft();
    void onMaterialSelectionChanged(QListWidgetItem* current, QListWidgetItem* previous);
    void onAssignClicked();
    void onNameEdited();
    void onMetallicChanged(int v);
    void onRoughnessChanged(int v);
    void onIorChanged(int v);
    void onOpacityChanged(int v);
    void onPickBaseColor();
    void onPickEmissive();

    void onBaseMapChanged(int);
    void onNormalMapChanged(int);
    void onMraoMapChanged(int);
    void onEmissiveMapChanged(int);

private:
    void applyCollapsedState(bool collapsed, bool force = false);

    void      refreshMaterialList();
    void      loadMaterialToUi(int32_t id);
    void      setUiEnabled(bool enabled);
    int32_t   currentMaterialId() const noexcept;
    Material* currentMaterialMutable() noexcept;

    // Texture combo helpers (UI-side only)
    void    initMapCombos();
    void    rebuildMapCombosIfNeeded();
    ImageId comboImageId(QComboBox* cb) const noexcept;
    void    setComboToImageId(QComboBox* cb, ImageId imageId) noexcept;

private:
    Ui::MaterialEditorDialog* ui = nullptr;

    class Core* m_core = nullptr;

    bool  m_leftCollapsed    = false;
    QSize m_lastExpandedSize = QSize(760, 540);

    int m_leftIndex  = 0;
    int m_rightIndex = 1;

    int m_rightPanelMinW = 0;
    int m_rightPanelMaxW = 0;

    QSize m_expandedMinSize;
    QSize m_expandedMaxSize;

    uint64_t m_lastLibraryCounter  = 0;
    uint64_t m_lastMaterialCounter = 0;
    uint64_t m_lastImagesCounter   = 0;
};
