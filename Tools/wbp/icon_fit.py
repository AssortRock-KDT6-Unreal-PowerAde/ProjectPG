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
    # 캔버스가 칸 전체에서 마우스를 받는다 → 아이템 잡기(끌기)·우클릭·설명 창이 칸 어디서나 된다.
    # (10/7 첫 버전은 여기를 "마우스 통과" 로 해서, 마우스 받는 위젯이 하나도 없어 끌기가 안 됐다.)
    canvas.set_editor_property("visibility", unreal.SlateVisibility.VISIBLE)
    # 기본 자리 = 아이템 칸 전체(여백 3). 코드가 없어도(빌드 전) 아이콘이 칸에 꽉 맞는다.
    # (10/7 첫 버전은 가운데 58×58 이라 빌드 안 한 PC 에서 4칸짜리 총도 칸 하나 크기로 작게 보였다.)
    icon_slot = b.find("IconScale").get_editor_property("slot")
    icon_slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(0, 0), maximum=unreal.Vector2D(1, 1)))
    icon_slot.set_alignment(unreal.Vector2D(0, 0))
    icon_slot.set_offsets(unreal.Margin(3, 3, 3, 3))
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
