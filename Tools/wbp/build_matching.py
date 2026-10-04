# WBP_Matching(매칭 화면, 기획서 1.3.1)을 만든다. (2026-10-04)
# 파란 바탕 가운데 "매칭중...", 아래 흰 "취소" 버튼, 맨 아래 진행 막대.
# 동작(글자 바꾸기·취소·막대 채우기)은 C++ UMatchingWidget 이 이름으로 찾아 한다: StatusText, CancelButton, MatchProgress.
import os
import sys
import traceback

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import pgwbp as W

ASSET = "/Game/PG/Blueprint/UI/WBP_Matching"
PARENT = "/Script/ProjectPG.MatchingWidget"
H = unreal.HorizontalAlignment


def build():
    b = W.Builder(ASSET, PARENT)
    root = b.root()

    # 화면 전체 파란 바탕(뒤의 로비가 안 눌리게 마우스도 막는다).
    bg, bg_slot = b.panel("Background", root, W.PLAN_BLUE, unreal.Margin(0, 0, 0, 0), radius=0.0)
    b.place(bg_slot, (0, 0, 1, 1), (0, 0), (0, 0))
    bg_slot.set_auto_size(False)
    bg_slot.set_offsets(unreal.Margin(0, 0, 0, 0))

    status, status_slot = b.text("StatusText", root, "매칭중...", 44, "SemiBold", W.PLAN_WHITE)
    status.set_shadow_color_and_opacity(W.CLEAR)
    b.place(status_slot, (0.5, 0.42, 0.5, 0.42), (0.5, 0.5), (0, 0))

    cancel, cancel_slot = b.button("CancelButton", root, "취소", "plan", 22, unreal.Margin(0, 8, 0, 8))
    b.place(cancel_slot, (0.5, 0.62, 0.5, 0.62), (0.5, 0.5), (0, 0), (240, 56))

    bar, bar_slot = b.make(unreal.ProgressBar, "MatchProgress", root)
    style = unreal.ProgressBarStyle()
    style.set_editor_property("background_image", W.flat_brush(W.PLAN_LIGHT, 0.0))
    style.set_editor_property("fill_image", W.flat_brush(W.PLAN_WHITE, 0.0))
    bar.set_editor_property("widget_style", style)
    bar.set_fill_color_and_opacity(W.PLAN_WHITE)
    bar.set_percent(0.5)
    b.place(bar_slot, (0.5, 0.82, 0.5, 0.82), (0.5, 0.5), (0, 0), (860, 44))

    b.finish()


try:
    build()
except Exception:
    W.log("FAILED\n" + traceback.format_exc())
    raise
