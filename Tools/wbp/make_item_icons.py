# 아이템 표(ItemTable)의 아이콘을 바닥 메시(WorldMesh)로 찍어 채운다. (2026-10-04)
# 메시가 있는 줄 → /Game/PG/UI/ItemIcons/T_Icon_<번호> 를 만들어 Icon 칸에 넣는다.
# 메시가 없는 줄 → 공용 상자 아이콘 T_Icon_Unknown(엔진 큐브).
# 그림을 그려야 한다. 명령줄 모드(-run=pythonscript)에서는 -AllowCommandletRendering 을 붙여도 썸네일이 새까맣게 나와서(10/4),
# 에디터 자체를 화면 밖으로 띄워 돌리고 끝나면 스스로 닫는다:
#   UnrealEditor.exe <uproject> -ExecutePythonScript="<프로젝트>/Tools/wbp/make_item_icons.py" -RenderOffScreen -unattended -nosplash
import csv
import io
import unreal

TABLE = "/Game/PG/Table/ItemTable"
FOLDER = "/Game/PG/UI/ItemIcons"
SIZE = 128


def log(msg):
    unreal.log("[ICON] " + msg)


def texture_ref(path):
    name = path.rsplit("/", 1)[1]
    return "/Script/Engine.Texture2D'%s.%s'" % (path, name)


# 다시 돌릴 때: 이전에 만든 아이콘을 지우고 새로 만든다(덮어쓰면 "일부만 로드된 에셋" 이라 저장이 안 되고 에디터가 멈춘다, 10/4).
if unreal.EditorAssetLibrary.does_directory_exist(FOLDER):
    unreal.EditorAssetLibrary.delete_directory(FOLDER)

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
