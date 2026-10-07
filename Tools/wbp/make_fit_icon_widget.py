# 아이콘 비율 지키는 아이템 칸 WBP 만들기 (2026-10-07)
#   형님 WBP_ItemWidget 을 복사해 WBP_FitIconItemWidget 을 만들고, 부모를 UFitIconItemWidget(자식 C++)로 바꾼 뒤
#   ItemIcon 을 IconCanvas(캔버스) > IconScale(ScaleBox, 맞춰 줄이기) > ItemIcon 으로 감싼다.
#   원본 WBP_ItemWidget 은 건드리지 않는다. 쓰려면 WBP_InventoryGrid 의 Item Widget Class 를 이것으로 고른다.
# 두 번 돌려도 같다(이미 있으면 건너뜀).
# 실행(에디터 끈 상태):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<프로젝트>/Tools/wbp/make_fit_icon_widget.py" -unattended -nosplash -nullrhi
# 필요: Plugins/UnrealMCPython (UMG 위젯 감싸기 도우미)
import json

import unreal

SOURCE = "/Game/PG/Blueprint/UI/WBP_ItemWidget"
TARGET = "/Game/PG/Blueprint/UI/WBP_FitIconItemWidget"
PARENT = "/Script/ProjectPG.FitIconItemWidget"


def log(msg):
    unreal.log("[FITICON] " + msg)


def wrap(bp, widget, wrapper_type, wrapper_name):
    if unreal.MCPythonHelper.umg_find_widget(bp, wrapper_name) is not None:
        return
    result = json.loads(unreal.MCPythonHelper.umg_wrap_widget(bp, widget, wrapper_type, wrapper_name))
    if not result.get("success"):
        raise RuntimeError("wrap %s: %s" % (widget, result))
    log("wrapped %s in %s %s" % (widget, wrapper_type, wrapper_name))


if not unreal.EditorAssetLibrary.does_asset_exist(TARGET):
    if unreal.EditorAssetLibrary.duplicate_asset(SOURCE, TARGET) is None:
        raise RuntimeError("duplicate failed")
    log("duplicated %s -> %s" % (SOURCE, TARGET))

bp = unreal.EditorAssetLibrary.load_asset(TARGET)
parent = unreal.load_class(None, PARENT)
if parent is None:
    raise RuntimeError("C++ class not found (build first): " + PARENT)
unreal.BlueprintEditorLibrary.reparent_blueprint(bp, parent)  # 이미 같은 부모면 그대로
log("parent = " + PARENT)

wrap(bp, "ItemIcon", "ScaleBox", "IconScale")
wrap(bp, "IconScale", "CanvasPanel", "IconCanvas")

box = unreal.MCPythonHelper.umg_find_widget(bp, "IconScale")
box.set_editor_property("stretch", unreal.Stretch.SCALE_TO_FIT)
box.set_editor_property("stretch_direction", unreal.StretchDirection.BOTH)
box.set_editor_property("visibility", unreal.SlateVisibility.HIT_TEST_INVISIBLE)
# 기본 자리 = 칸 전체(여백 3). 실제 자리·돌리기는 코드(RefreshWidget)가 정한다.
icon_slot = box.get_editor_property("slot")
icon_slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(0, 0), maximum=unreal.Vector2D(1, 1)))
icon_slot.set_alignment(unreal.Vector2D(0, 0))
icon_slot.set_offsets(unreal.Margin(3, 3, 3, 3))

canvas = unreal.MCPythonHelper.umg_find_widget(bp, "IconCanvas")
canvas_slot = canvas.get_editor_property("slot")
canvas_slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
canvas_slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
# 캔버스가 칸 전체에서 마우스를 받는다 → 끌기·우클릭이 칸 어디서나 된다(마우스 통과로 두면 끌기가 안 됐다, 10/7).
canvas.set_editor_property("visibility", unreal.SlateVisibility.VISIBLE)

fixed = unreal.MCPythonHelper.umg_ensure_widget_guids(bp)
if fixed:
    log("filled %d widget GUIDs" % fixed)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
log("saved=%s" % unreal.EditorAssetLibrary.save_asset(TARGET, only_if_is_dirty=False))
