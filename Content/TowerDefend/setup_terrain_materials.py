"""
Post-restart setup script: Populate BP_TDHexTile.TerrainMaterials map.
Run this script in the UE5 Python console after editor restart to configure
the terrain type -> MaterialInstance mapping.

Usage in UE5 Python console:
  exec(open('/Users/kyriewei/Desktop/TowerDefend/Content/TowerDefend/setup_terrain_materials.py').read())
"""

import unreal

bp = unreal.load_asset("/Game/TowerDefend/Blueprints/HexGrid/BP_TDHexTile")
if bp is None:
    print("ERROR: BP_TDHexTile not found")
else:
    cdo = unreal.get_default_object(bp.generated_class())
    if cdo is None:
        print("ERROR: Could not get CDO")
    else:
        mappings = [
            (unreal.TDTerrainType.PLAIN,      "/Game/TowerDefend/Materials/MI_Terrain_Plain"),
            (unreal.TDTerrainType.HILL,        "/Game/TowerDefend/Materials/MI_Terrain_Hill"),
            (unreal.TDTerrainType.MOUNTAIN,    "/Game/TowerDefend/Materials/MI_Terrain_Mountain"),
            (unreal.TDTerrainType.FOREST,      "/Game/TowerDefend/Materials/MI_Terrain_Forest"),
            (unreal.TDTerrainType.RIVER,       "/Game/TowerDefend/Materials/MI_Terrain_River"),
            (unreal.TDTerrainType.SWAMP,       "/Game/TowerDefend/Materials/MI_Terrain_Swamp"),
            (unreal.TDTerrainType.DEEP_WATER,  "/Game/TowerDefend/Materials/MI_Terrain_DeepWater"),
        ]

        terrain_map = {}
        for terrain_type, mat_path in mappings:
            mat = unreal.load_asset(mat_path)
            if mat:
                terrain_map[terrain_type] = mat
                print("  " + str(terrain_type) + " -> " + mat.get_name())
            else:
                print("  WARNING: " + mat_path + " not found")

        cdo.set_editor_property("TerrainMaterials", terrain_map)

        result = cdo.get_editor_property("TerrainMaterials")
        print("\nTerrainMaterials set: " + str(len(result)) + " entries")

        unreal.EditorAssetLibrary.save_asset(
            "/Game/TowerDefend/Blueprints/HexGrid/BP_TDHexTile"
        )
        print("BP_TDHexTile saved.")
