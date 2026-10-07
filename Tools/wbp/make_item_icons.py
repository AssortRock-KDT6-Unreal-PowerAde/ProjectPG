# 아이템 표(ItemTable)의 아이콘을 바닥 메시(WorldMesh)로 찍어 채운다. (2026-10-04)
# 메시가 있는 줄 → /Game/PG/UI/ItemIcons/T_Icon_<번호> 를 만들어 Icon 칸에 넣는다.
# 메시가 없는 줄 → 공용 상자 아이콘 T_Icon_Unknown(엔진 큐브).
# 손으로 만든 아이콘이 먼저: Tools/icons_custom/T_Icon_<번호>.png 가 있으면 그 그림을 /Game/PG/UI/ItemIcons_Custom 으로 가져와 쓴다.
#   (예: 방탄조끼 2003 — 바닥 메시가 구겨진 모양이라 찍어도 알아보기 어려워 그림으로 대신함, 10/6)
#   팀원도 PNG 만 넣고 이 스크립트를 다시 돌리면 아이콘이 바뀐다. 찍은 아이콘 폴더(ItemIcons)는 돌릴 때마다 새로 만들고, 손 아이콘 폴더는 안 지운다.
# 그림을 그려야 한다. 명령줄 모드(-run=pythonscript)에서는 -AllowCommandletRendering 을 붙여도 썸네일이 새까맣게 나와서(10/4),
# 에디터 자체를 화면 밖으로 띄워 돌리고 끝나면 스스로 닫는다:
#   UnrealEditor.exe <uproject> -ExecutePythonScript="<프로젝트>/Tools/wbp/make_item_icons.py" -RenderOffScreen -unattended -nosplash
import csv
import io
import os
import re
import shutil
import unreal

TABLE = "/Game/PG/Table/ItemTable"
FOLDER = "/Game/PG/UI/ItemIcons"
CUSTOM_FOLDER = "/Game/PG/UI/ItemIcons_Custom"
CUSTOM_SOURCE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "icons_custom")
PIXELS_PER_CELL = 64   # 칸 하나당 픽셀. 4×2 칸 아이템 → 256×128 그림
# 아이템별 손질(찍어 보고 방향만 틀렸을 때): flip_x 좌우, flip_y 위아래 뒤집기,
# axis 1/2/3 = 메시 X/Y/Z 의 + 쪽에서 보기, -1/-2/-3 = 반대쪽에서 보기(0 자동)
OVERRIDES = {
    # 총구를 오른쪽으로 통일(AK·AR70 과 같게)
    "1001": {"flip_x": True}, "1002": {"flip_x": True}, "1003": {"flip_x": True},
    "1004": {"flip_x": True}, "1005": {"flip_x": True}, "1006": {"flip_x": True},
    "1007": {"flip_x": True}, "1008": {"flip_x": True}, "1009": {"flip_x": True},
    # 가방: 자동으로는 등판 쪽(은색 패드)이 찍혀서 앞(주머니 쪽)에서 찍는다
    "5001": {"axis": -2},
}


def log(msg):
    unreal.log("[ICON] " + msg)


def texture_ref(path):
    name = path.rsplit("/", 1)[1]
    return "/Script/Engine.Texture2D'%s.%s'" % (path, name)


# 다시 돌릴 때: 이전에 만든 아이콘을 지우고 새로 만든다(덮어쓰면 "일부만 로드된 에셋" 이라 저장이 안 되고 에디터가 멈춘다, 10/4).
if unreal.EditorAssetLibrary.does_directory_exist(FOLDER):
    unreal.EditorAssetLibrary.delete_directory(FOLDER)

def import_custom(item_id):
    """Tools/icons_custom/T_Icon_<번호>.png 가 있으면 UI 텍스처로 가져와 그 경로를, 없으면 None."""
    png = os.path.join(CUSTOM_SOURCE, "T_Icon_%s.png" % item_id)
    if not os.path.exists(png):
        return None
    # 파일 이름 끝의 1001~1999 는 언리얼이 UDIM(타일 텍스처) 번호로 읽어 "T_Icon" 이라는 다른 에셋을 만든다(10/7 활 1016).
    # → 숫자 뒤에 글자를 붙인 임시 사본으로 가져오고, 에셋 이름은 destination_name 으로 정한다.
    staged = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_intermediate_dir()), "IconImport_%s_src.png" % item_id)
    shutil.copyfile(png, staged)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", staged)
    task.set_editor_property("destination_path", CUSTOM_FOLDER)
    task.set_editor_property("destination_name", "T_Icon_%s" % item_id)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    path = "%s/T_Icon_%s" % (CUSTOM_FOLDER, item_id)
    tex = unreal.load_asset(path)
    if tex is None:
        log("custom import failed %s" % png)
        return None
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    log("custom icon %s" % path)
    return path


# 10/7 UDIM 으로 잘못 가져와 생긴 에셋 정리
if unreal.EditorAssetLibrary.does_asset_exist(CUSTOM_FOLDER + "/T_Icon"):
    unreal.EditorAssetLibrary.delete_asset(CUSTOM_FOLDER + "/T_Icon")

unknown = FOLDER + "/T_Icon_Unknown"
if unreal.ItemIconTools.render_item_icon(unreal.load_asset("/Engine/BasicShapes/Cube"), unknown, unreal.IntPoint(1, 1), PIXELS_PER_CELL) is None:
    log("render failed")

table = unreal.load_asset(TABLE)
text = unreal.DataTableFunctionLibrary.export_data_table_to_csv_string(table)
rows = list(csv.reader(io.StringIO(text)))
header = rows[0]
icon_col = header.index("Icon")
mesh_col = header.index("WorldMesh")
grid_col = header.index("GridSize")
made = 0
for row in rows[1:]:
    if not row:
        continue
    item_id = row[0]
    mesh_ref = row[mesh_col]
    icon_path = unknown
    custom = import_custom(item_id)
    if custom:
        row[icon_col] = texture_ref(custom)
        continue
    if mesh_ref and mesh_ref != "None":
        mesh_path = mesh_ref.split("'")[1] if "'" in mesh_ref else mesh_ref
        mesh = unreal.load_asset(mesh_path)
        target = "%s/T_Icon_%s" % (FOLDER, item_id)
        nums = [int(n) for n in re.findall(r"\d+", row[grid_col])] or [1, 1]
        o = OVERRIDES.get(item_id, {})
        if mesh is not None and unreal.ItemIconTools.render_item_icon(
                mesh, target, unreal.IntPoint(nums[0], nums[1]), PIXELS_PER_CELL, 0.08,
                o.get("flip_x", False), o.get("flip_y", False), o.get("axis", 0)) is not None:
            icon_path = target
            made += 1
        else:
            log("no icon for %s (%s)" % (item_id, mesh_path))
    row[icon_col] = texture_ref(icon_path)

out = io.StringIO()
writer = csv.writer(out, lineterminator="\n")
for row in rows:
    writer.writerow(row)
ok = unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(table, out.getvalue())
unreal.EditorAssetLibrary.save_loaded_asset(table)
log("icons made=%d rows=%d fill=%s" % (made, len(rows) - 1, ok))

unreal.SystemLibrary.quit_editor()
