# 아이콘이 칸에 늘어나 붙지 않게(비율 지키기) WBP 두 개를 고친다. (2026-10-07)
#   WBP_ItemWidget : ItemIcon → IconCanvas(캔버스) > IconScale(ScaleBox, 맞춰 줄이기) > ItemIcon
#                    자리 크기·돌리기(90°)는 코드(UItemWidget::RefreshWidget)가 IconScale 이름으로 찾아 정한다.
#                    여백은 WBP_ItemWidget 의 Icon Padding 칸.
#   WBP_EquipSlot  : Icon → IconScale(ScaleBox) > Icon. 여백은 IconScale 슬롯의 Padding.
# 두 번 돌려도 같다(이미 있으면 건너뜀).
# 실행: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<이 파일>" -unattended -nosplash -nullrhi (에디터 끈 상태)
import json
import os
import sys

import unreal

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import pgwbp as W

UI = "/Game/PG/Blueprint/UI/"
EQUIP_ICON_PADDING = 6.0


def wrap(b, widget, wrapper_type, wrapper_name):
    if b.find(wrapper_name) is not None:
        return False
    result = json.loads(unreal.MCPythonHelper.umg_wrap_widget(b.bp, widget, wrapper_type, wrapper_name))
    if not result.get("success"):
        raise RuntimeError("wrap %s: %s" % (widget, result))
    b.made.append(wrapper_name)
    return True


def fill_overlay_slot(widget, padding=0.0):
    slot = widget.get_editor_property("slot")
    slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
    slot.set_padding(unreal.Margin(padding, padding, padding, padding))


def fit_scale_box(box):
    box.set_editor_property("stretch", unreal.Stretch.SCALE_TO_FIT)
    box.set_editor_property("stretch_direction", unreal.StretchDirection.BOTH)
    box.set_editor_property("visibility", unreal.SlateVisibility.HIT_TEST_INVISIBLE)


def item_widget():
    b = W.Builder(UI + "WBP_ItemWidget")
    wrap(b, "ItemIcon", "ScaleBox", "IconScale")
    wrap(b, "IconScale", "CanvasPanel", "IconCanvas")
    fit_scale_box(b.find("IconScale"))
    canvas = b.find("IconCanvas")
    fill_overlay_slot(canvas)
    canvas.set_editor_property("visibility", unreal.SlateVisibility.HIT_TEST_INVISIBLE)
    # 디자이너에서 보기 좋게 기본 자리(실제 크기는 코드가 정함).
    W.Builder.place(b.find("IconScale").get_editor_property("slot"), (0.5, 0.5, 0.5, 0.5), (0.5, 0.5), (0, 0), (58, 58))
    b.finish()


def equip_slot():
    b = W.Builder(UI + "WBP_EquipSlot")
    wrap(b, "Icon", "ScaleBox", "IconScale")
    box = b.find("IconScale")
    fit_scale_box(box)
    fill_overlay_slot(box, EQUIP_ICON_PADDING)
    b.finish()


item_widget()
equip_slot()
