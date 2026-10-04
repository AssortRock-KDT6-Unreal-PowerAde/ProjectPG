# WBP_Option(옵션 화면)을 만든다. (2026-10-04)
# 기획서 색(파란 판, 흰 버튼). 줄마다 "이름 | 고르기" — 화면 모드·해상도·그래픽 품질·프레임 제한·수직 동기화.
# 동작(목록 채우기·적용·저장)은 C++ UOptionWidget 이 이름으로 찾아 한다:
#   WindowModeCombo, ResolutionCombo, QualityCombo, FrameLimitCombo, VSyncCheck, ApplyButton, BackButton.
import os
import sys
import traceback

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import pgwbp as W

ASSET = "/Game/PG/Blueprint/UI/WBP_Option"
PARENT = "/Script/ProjectPG.OptionWidget"
H = unreal.HorizontalAlignment
V = unreal.VerticalAlignment


def build():
    b = W.Builder(ASSET, PARENT)
    root = b.root()

    # 뒤를 살짝 어둡게(로비가 비쳐 보이되 눌리지 않게).
    dim, dim_slot = b.panel("Dim", root, unreal.LinearColor(0, 0, 0, 0.45), unreal.Margin(0, 0, 0, 0), radius=0.0)
    b.place(dim_slot, (0, 0, 1, 1), (0, 0), (0, 0))
    dim_slot.set_auto_size(False)
    dim_slot.set_offsets(unreal.Margin(0, 0, 0, 0))

    panel, panel_slot = b.panel("Panel", root, W.PLAN_BLUE, unreal.Margin(48, 36, 48, 36), radius=0.0)
    b.place(panel_slot, (0.5, 0.5, 0.5, 0.5), (0.5, 0.5), (0, 0), (760, 560))

    stack, _ = b.make(unreal.VerticalBox, "Stack", panel)

    _, title_slot = b.text("Title", stack, "옵션", 34, "Bold", W.PLAN_WHITE)
    title_slot.set_padding(unreal.Margin(0, 0, 0, 24))

    rows = (
        ("WindowMode", "화면 모드", "combo"),
        ("Resolution", "해상도", "combo"),
        ("Quality", "그래픽 품질", "combo"),
        ("FrameLimit", "프레임 제한", "combo"),
        ("VSync", "수직 동기화", "check"),
    )
    for key, label, kind in rows:
        row, row_slot = b.make(unreal.HorizontalBox, key + "Row", stack)
        row_slot.set_padding(unreal.Margin(0, 6, 0, 6))
        _, label_slot = b.text(key + "Label", row, label, 20, "SemiBold", W.PLAN_WHITE)
        label_slot.set_vertical_alignment(V.V_ALIGN_CENTER)
        b.fill(label_slot)
        size, size_slot = b.make(unreal.SizeBox, key + "Size", row)
        size.set_width_override(320)
        size.set_height_override(40)
        size_slot.set_vertical_alignment(V.V_ALIGN_CENTER)
        if kind == "combo":
            combo, _ = b.make(unreal.ComboBoxString, key + "Combo", size)
            combo.set_editor_property("font", W.font(16, "Regular"))
        else:
            b.make(unreal.CheckBox, key + "Check", size)

    spacer, spacer_slot = b.make(unreal.Spacer, "BottomSpace", stack)
    spacer.set_size(unreal.Vector2D(1, 24))

    buttons, buttons_slot = b.make(unreal.HorizontalBox, "Buttons", stack)
    buttons_slot.set_horizontal_alignment(H.H_ALIGN_RIGHT)
    for name, label in (("ApplyButton", "적용"), ("BackButton", "뒤로가기")):
        _, slot = b.button(name, buttons, label, "plan", 20, unreal.Margin(36, 10, 36, 10))
        slot.set_padding(unreal.Margin(12, 0, 0, 0))

    b.finish()


try:
    build()
except Exception:
    W.log("FAILED\n" + traceback.format_exc())
    raise
