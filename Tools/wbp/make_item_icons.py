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
import unreal

TABLE = "/Game/PG/Table/ItemTable"
FOLDER = "/Game/PG/UI/ItemIcons"
CUSTOM_FOLDER = "/Game/PG/UI/ItemIcons_Custom"
CUSTOM_SOURCE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "icons_custom")
SIZE = 128


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
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png)
    task.set_editor_property("destination_path", CUSTOM_FOLDER)
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


unknown = FOLDER + "/T_Icon_Unknown"
if unreal.ItemIconTools.render_mesh_icon(unreal.load_asset("/Engine/BasicShapes/Cube"), unknown, SIZE) is None:
    log("render failed")

table = unreal.load_asset(TABLE)
text = unreal.DataTableFunctionLibrary.export_data_table_to_csv_string(table)
rows = list(csv.reader(io.StringIO(text)))
header = rows[0]
icon_col = header.index("Icon")
mesh_col = header.index("WorldMesh")
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
        if mesh is not None and unreal.ItemIconTools.render_mesh_icon(mesh, target, SIZE) is not None:
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
