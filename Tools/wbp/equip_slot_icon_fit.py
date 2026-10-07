# 장비 칸(WBP_EquipSlot) 아이콘을 늘이지 않고 비율대로 칸에 맞춘다. (2026-10-08)
#   Icon 을 IconScale(ScaleBox, 맞춰 줄이기)로 감싼다. 코드는 그대로(형님 EquipSlot 은 이미 그림 원래 크기로 넣는다).
#   여백은 IconScale 슬롯의 Padding(기본 6).
# 두 번 돌려도 같다(이미 있으면 건너뜀).
# 실행(에디터 끈 상태):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<프로젝트>/Tools/wbp/equip_slot_icon_fit.py" -unattended -nosplash -nullrhi
import json

import unreal

TARGET = "/Game/PG/Blueprint/UI/WBP_EquipSlot"
PADDING = 6.0


def log(msg):
    unreal.log("[EQUIPFIT] " + msg)


bp = unreal.EditorAssetLibrary.load_asset(TARGET)
if unreal.MCPythonHelper.umg_find_widget(bp, "IconScale") is None:
    result = json.loads(unreal.MCPythonHelper.umg_wrap_widget(bp, "Icon", "ScaleBox", "IconScale"))
    if not result.get("success"):
        raise RuntimeError("wrap failed: %s" % result)
    log("wrapped Icon in ScaleBox IconScale")

box = unreal.MCPythonHelper.umg_find_widget(bp, "IconScale")
box.set_editor_property("stretch", unreal.Stretch.SCALE_TO_FIT)
box.set_editor_property("stretch_direction", unreal.StretchDirection.BOTH)
# 마우스는 칸 바탕(BackGround)이 받는다. 틀은 마우스를 통과시켜 끌기·놓기를 막지 않게.
box.set_editor_property("visibility", unreal.SlateVisibility.HIT_TEST_INVISIBLE)
slot = box.get_editor_property("slot")
slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
slot.set_padding(unreal.Margin(PADDING, PADDING, PADDING, PADDING))

fixed = unreal.MCPythonHelper.umg_ensure_widget_guids(bp)
if fixed:
    log("filled %d widget GUIDs" % fixed)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
log("saved=%s" % unreal.EditorAssetLibrary.save_asset(TARGET, only_if_is_dirty=False))
