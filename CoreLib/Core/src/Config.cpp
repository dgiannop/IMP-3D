#include "Config.hpp"

#include "BevelTool.hpp"
#include "BoxTool.hpp"
#include "CmdCenter.hpp"
#include "CmdCreatePoly.hpp"
#include "CmdDelete.hpp"
#include "CmdDissolveEdge.hpp"
#include "CmdDivide.hpp"
#include "CmdDuplicatePolys.hpp"
#include "CmdFitView.hpp"
#include "CmdFlattenNormals.hpp"
#include "CmdFlipNormals.hpp"
#include "CmdFreeze.hpp"
#include "CmdMergeByDistance.hpp"
#include "CmdRestOnGround.hpp"
#include "CmdReverseWinding.hpp"
#include "CmdSelect.hpp"
#include "CmdSelectConnected.hpp"
#include "CmdSmoothNormals.hpp"
#include "CmdTriangulate.hpp"
#include "CylinderTool.hpp"
#include "ExtrudeTool.hpp"
#include "Formats/ImpSceneFormat.hpp"
#include "Formats/ObjSceneFormat.hpp"
#include "InsetTool.hpp"
#include "MockTool.hpp"
#include "MoveTool.hpp"
#include "SelectTool.hpp"
#include "SphereTool.hpp"

namespace config
{

    void registerTools(ItemFactory<Tool>& factory)
    {
        factory.registerItem("SelectTool", factory.createItemType<SelectTool>);
        factory.registerItem("MoveTool", factory.createItemType<MoveTool>);
        factory.registerItem("BoxTool", factory.createItemType<BoxTool>);
        factory.registerItem("SphereTool", factory.createItemType<SphereTool>);
        factory.registerItem("CylinderTool", factory.createItemType<CylinderTool>);

        factory.registerItem("ExtrudeTool", factory.createItemType<ExtrudeTool>);
        factory.registerItem("InsetTool", factory.createItemType<InsetTool>);
        factory.registerItem("BevelTool", factory.createItemType<BevelTool>);
        factory.registerItem("MockTool", factory.createItemType<MockTool>);
    }

    void registerCommands(ItemFactory<Command>& factory)
    {
        factory.registerItem("SelectAll", factory.createItemType<CmdSelectAll>);
        factory.registerItem("SelectNone", factory.createItemType<CmdSelectNone>);
        factory.registerItem("Delete", factory.createItemType<CmdDelete>);
        factory.registerItem("EdgeLoop", factory.createItemType<CmdEdgeLoop>);
        factory.registerItem("EdgeRing", factory.createItemType<CmdEdgeRing>);
        factory.registerItem("Dissolve", factory.createItemType<CmdDissolveEdge>);
        factory.registerItem("Divide", factory.createItemType<CmdDivide>);
        factory.registerItem("Triangulate", factory.createItemType<CmdTriangulate>);
        factory.registerItem("Freeze", factory.createItemType<CmdFreeze>);
        factory.registerItem("MergeByDistance", factory.createItemType<CmdMergeByDistance>);
        factory.registerItem("FlipNormals", factory.createItemType<CmdFlipNormals>);
        factory.registerItem("ReverseWinding", factory.createItemType<CmdReverseWinding>);
        factory.registerItem("SmoothNormals", factory.createItemType<CmdSmoothNormals>);
        factory.registerItem("FlattenNormals", factory.createItemType<CmdFlattenNormals>);
        factory.registerItem("DuplicatePolys", factory.createItemType<CmdDuplicatePolys>);
        factory.registerItem("RestOnGround", factory.createItemType<CmdRestOnGround>);
        factory.registerItem("Center", factory.createItemType<CmdCenter>);
        factory.registerItem("FitToView", factory.createItemType<CmdFitView>);
        factory.registerItem("CreatePoly", factory.createItemType<CmdCreatePoly>);
        factory.registerItem("SelectConnected", factory.createItemType<CmdSelectConnected>);
    }

    void registerSceneFormats(ItemFactory<SceneFormat>& factory)
    {
        factory.registerItem(".imp", factory.createItemType<ImpSceneFormat>);
        factory.registerItem(".obj", factory.createItemType<ObjSceneFormat>);
    }

} // namespace config
