# 죽은 뒤 뜨는 통계표 WBP(WBP_DeathStats)를 처음 한 번 만든다 (2026-10-19).
# 흐름 설명: Source/ProjectPG/Private/GameMode/README.md "죽으면 통계표 → [확인] → 그 사람만 로비로"
#
# 무엇을: C++ 부모(UPGDeathStatsWidget)가 숫자를 넣을 글칸과 [확인] 버튼을 이름 맞춰 깐다.
#         글칸 이름: SurvivalTimeText, KillsText, ShotsFiredText, HitsText, DamageDealtText, DamageTakenText, KilledByText, 버튼 ConfirmButton
# 다시 돌려도: 같은 이름 위젯은 새로 만들지 않고 글꼴·색만 다시 맞춘다. 에디터에서 옮긴 자리·더한 연출은 그대로.
# 실행(에디터 끈 상태, C++ 빌드 뒤 — 부모 클래스가 있어야 함):
#   UnrealEditor-Cmd.exe <프로젝트>/ProjectPG.uproject -run=pythonscript -script="<프로젝트>/Tools/wbp/build_death_stats.py" -unattended -nosplash -nullrhi
# 결과: Saved/PGWBP.log 에 "[PGWBP] ... saved=True"
import os
import sys
import traceback

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import pgwbp as W

ASSET = "/Game/PG/Blueprint/UI/DeathStats/WBP_DeathStats"
PARENT = "/Script/ProjectPG.PGDeathStatsWidget"
DEATH_RED = W.srgb(0xE0, 0x4A, 0x3F)
BACKDROP = W.srgb(0x00, 0x00, 0x00, 0.72)

# (왼쪽 글, 오른쪽 숫자 칸 이름, 처음 보이는 글) — 처음 글은 에디터 미리보기용, 게임에서는 C++ 이 덮어쓴다.
ROWS = [
    ("생존 시간", "SurvivalTimeText", "00:00"),
    ("처치", "KillsText", "0"),
    ("발사", "ShotsFiredText", "0"),
    ("명중", "HitsText", "0"),
    ("가한 피해", "DamageDealtText", "0"),
    ("받은 피해", "DamageTakenText", "0"),
    ("사망 원인", "KilledByText", "몬스터"),
]


def build():
    b = W.Builder(ASSET, PARENT)
    root = b.root("Root", "CanvasPanel")

    # 화면 전체를 어둡게 덮는 판(뒤 게임 화면은 흐리게 보임).
    backdrop, slot = b.make(unreal.Border, "Backdrop", root)
    backdrop.set_brush(W.flat_brush(BACKDROP))
    W.Builder.place(slot, (0, 0, 1, 1), (0, 0), (0, 0))
    slot.set_offsets(unreal.Margin(0, 0, 0, 0))

    # 가운데 카드: 너비 560 고정, 높이는 내용만큼.
    card_size, slot = b.make(unreal.SizeBox, "CardSize", root)
    card_size.set_width_override(560.0)
    W.Builder.place(slot, (0.5, 0.5, 0.5, 0.5), (0.5, 0.5), (0, 0))
    card, _ = b.panel("Card", card_size, W.PANEL_SOLID, unreal.Margin(40, 32, 40, 32), radius=8.0)
    column, _ = b.make(unreal.VerticalBox, "CardColumn", card)

    title, slot = b.text("TitleText", column, "사망", 44, "Bold", DEATH_RED)
    slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
    sub, slot = b.text("SubtitleText", column, "이번 판 기록", 18, "Regular", W.TEXT_DIM)
    slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
    slot.set_padding(unreal.Margin(0, 4, 0, 20))

    for index, (label, value_name, preview) in enumerate(ROWS):
        row, row_slot = b.make(unreal.HorizontalBox, "Row" + value_name, column)
        row_slot.set_padding(unreal.Margin(0, 6, 0, 6))
        label_tb, label_slot = b.text("Label" + value_name, row, label, 20, "Regular", W.TEXT_DIM)
        W.Builder.fill(label_slot)
        label_slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
        value_tb, value_slot = b.text(value_name, row, preview, 22, "SemiBold", W.TEXT)
        value_slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
        value_tb.set_editor_property("justification", unreal.TextJustify.RIGHT)

    button, slot = b.button("ConfirmButton", column, "확인", "primary", 22, unreal.Margin(24, 12, 24, 12))
    slot.set_padding(unreal.Margin(0, 28, 0, 0))
    slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    hint, slot = b.text("HintText", column, "Enter 키로도 확인 · 로비로 돌아갑니다", 14, "Regular", W.TEXT_FAINT)
    slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
    slot.set_padding(unreal.Margin(0, 10, 0, 0))

    b.finish()


try:
    build()
except Exception:
    W.log("FAILED: " + traceback.format_exc())
    raise
