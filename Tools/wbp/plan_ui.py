# 기획서 화면을 형님 main 위에 붙이는 에셋 작업 (2026-10-08, 브랜치 기획서-화면-붙이기).
# 형님 WBP 는 배치를 그대로 두고 "더하기·색 바꾸기·부모를 우리 자식 클래스로" 만 한다.
# 어느 단계를 돌릴지는 환경 변수 PG_PLAN_STEPS(쉼표로 구분, 비우면 전부):
#   tooltip   : WBP_FitIconItemWidget 에 설명 창(WBP_ItemTooltip)·1초 연결, WBP_InventoryGrid 의 아이템 칸을 WBP_FitIconItemWidget 로
#   option    : WBP_Lobby 부모를 ULobbyMenuWidget 로, 옵션 화면(WBP_Option) 연결
#   inventory : WBP_CharacterWidget·WBP_ItemContextWidget 부모를 우리 자식으로, 검색 칸·종류 고르기·돌리기 버튼·기획서 색
#   title     : 로비 버튼 흰 판·검은 글자, L_Title 게임모드 확인
#   matching  : WBP_Lobby 에 매칭 화면(WBP_Matching) 연결
# 실행(에디터 끈 상태):
#   UnrealEditor-Cmd.exe <프로젝트>/ProjectPG.uproject -run=pythonscript -script="<프로젝트>/Tools/wbp/plan_ui.py" -unattended -nosplash -nullrhi
# 같은 단계를 다시 돌려도 이미 있는 것은 다시 만들지 않는다(이름으로 찾음).
import os
import sys
import traceback

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import pgwbp as W

UI = "/Game/PG/Blueprint/UI/"
EAL = unreal.EditorAssetLibrary


def cls_path(name):
    return UI + name + "." + name + "_C"


def set_default(asset, prop, value):
    bp = unreal.load_asset(UI + asset)
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property(prop, value)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    EAL.save_loaded_asset(bp, only_if_is_dirty=False)
    W.log("%s.%s = %s" % (asset, prop, cdo.get_editor_property(prop)))


def reparent(asset, parent_path):
    bp = unreal.load_asset(UI + asset)
    parent = unreal.load_class(None, parent_path)
    if parent is None:
        raise RuntimeError("no class " + parent_path)
    unreal.BlueprintEditorLibrary.reparent_blueprint(bp, parent)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    EAL.save_loaded_asset(bp, only_if_is_dirty=False)
    W.log("%s parent -> %s" % (asset, parent_path))


def white_buttons(asset, pairs):
    b = W.Builder(UI + asset)
    for btn_name, text_name in pairs:
        btn = b.find(btn_name)
        if btn is None:
            continue
        btn.set_style(b.button_style(W.PLAN_WHITE, W.PLAN_WHITE_HOVER, W.PLAN_LIGHT, radius=0.0))
        text = b.find(text_name)
        if text is not None:
            text.set_color_and_opacity(W.slate_color(W.PLAN_INK))
    b.finish()


# ---------------- 1) 아이템 설명 창 ----------------
def tooltip():
    load = unreal.load_class(None, cls_path("WBP_ItemTooltip"))
    set_default("WBP_FitIconItemWidget", "tooltip_class", load)
    set_default("WBP_FitIconItemWidget", "tooltip_delay_seconds", 1.0)
    # 형님 격자가 아이템 칸을 우리 자식 칸(비율 맞춘 아이콘 + 설명 창)으로 만들게 고른다.
    set_default("WBP_InventoryGrid", "item_widget_class", unreal.load_class(None, cls_path("WBP_FitIconItemWidget")))


# ---------------- 2) 옵션 화면 ----------------
def option():
    # 형님 로비 메뉴 WBP 의 부모를 우리 자식(ULobbyMenuWidget: 옵션·종료 동작)으로. 배치·버튼은 그대로.
    reparent("WBP_Lobby", "/Script/ProjectPG.LobbyMenuWidget")
    set_default("WBP_Lobby", "option_screen_class", unreal.load_class(None, cls_path("WBP_Option")))


# ---------------- 3) 캐릭터·인벤토리 창 모양 ----------------
def inventory():
    # 부모를 우리 자식(검색·종류 고르기 / 돌리기 버튼)으로. 형님 동작은 부모가 그대로 한다.
    reparent("WBP_CharacterWidget", "/Script/ProjectPG.PlanInventoryWindow")
    reparent("WBP_ItemContextWidget", "/Script/ProjectPG.PlanItemContextWidget")

    # 캐릭터 창: 뒤의 실제 장면(타이틀이면 마을·캐릭터)이 비치게 옅게 어둡게, 창고를 70px 내리고 그 위에 검색 줄.
    b = W.Builder(UI + "WBP_CharacterWidget")
    root = b.root()  # MainCanvas
    bg, bg_slot = b.panel("PlanBackground", root, W.srgb(0x08, 0x1C, 0x28, 0.35), unreal.Margin(0, 0, 0, 0), radius=0.0)
    bg.set_brush_color(W.srgb(0x08, 0x1C, 0x28, 0.35))
    bg.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
    b.place(bg_slot, (0, 0, 1, 1), (0, 0), (0, 0))
    bg_slot.set_auto_size(False)
    bg_slot.set_offsets(unreal.Margin(0, 0, 0, 0))
    bg_slot.set_z_order(-10)
    stash_slot = b.find("MainInventoryOverlay").get_editor_property("slot")
    off = stash_slot.get_offsets()
    if off.top < 70:
        stash_slot.set_offsets(unreal.Margin(off.left, 70, off.right, off.bottom))
    search, search_slot = b.make(unreal.EditableTextBox, "SearchBox", root)
    search.set_hint_text(unreal.Text("아이템 이름 검색"))
    b.place(search_slot, (0.6, 0, 0.6, 0), (0, 0), (0, 14), (560, 46))
    combo, combo_slot = b.make(unreal.ComboBoxString, "FilterCombo", root)
    combo.set_editor_property("font", W.font(16, "Regular"))
    b.place(combo_slot, (0.6, 0, 0.6, 0), (0, 0), (576, 14), (188, 46))
    b.finish()

    # 장비 칸 사이 캐릭터 자리(CharacterView, 연한 판)를 투명하게 -> 뒤의 실제 캐릭터가 보인다.
    b = W.Builder(UI + "WBP_Equip")
    view = b.find("CharacterView")
    if view is not None:
        view.set_color_and_opacity(unreal.LinearColor(1, 1, 1, 0))
    b.finish()

    # 칸 색(기획서 연한 파랑), 칸 사이 선(진한 파랑).
    for name in ("WBP_Slot", "WBP_EquipSlot"):
        b = W.Builder(UI + name)
        border = b.find("SlotBorder")
        border.set_brush_color(W.PLAN_BLUE_DEEP)
        border.set_padding(unreal.Margin(1, 1, 1, 1))
        b.find("BackGround").set_color_and_opacity(W.PLAN_LIGHT)
        b.finish()
    set_default("WBP_Slot", "default_slot_color", W.PLAN_LIGHT)

    # 우클릭 메뉴: "돌리기" 버튼(취소 바로 위). "나누기"는 형님 쪽 쪼개기 요청이 없어 넣지 않는다.
    b = W.Builder(UI + "WBP_ItemContextWidget")
    box = b.find("VerticalBox_0")
    sample_font = b.find("TextBlock_324").get_editor_property("font")
    size, _ = b.make(unreal.SizeBox, "RotateSize", box)
    size.set_width_override(200)
    size.set_height_override(50)
    btn, _ = b.make(unreal.Button, "RotateButton", size)
    text, _ = b.make(unreal.TextBlock, "RotateLabel", btn)
    text.set_text(unreal.Text("돌리기"))
    text.set_font(sample_font)
    cancel = b.find("SizeBox_3")
    if cancel is not None and box.get_child_index(cancel) != box.get_children_count() - 1:
        box.remove_child(cancel)
        box.add_child(cancel)
    b.finish()

    # 아래 탭·뒤로가기, 우클릭 메뉴 버튼을 기획서처럼 흰 판·검은 글자로.
    white_buttons("WBP_CharacterWidget", (("InventoryBtn", "TextBlock_59"), ("QuestBtn", "TextBlock"), ("MailBtn", "TextBlock_1"),
                                          ("StatisticsBtn", "TextBlock_2"), ("BackBtn", "TextBlock_3")))
    white_buttons("WBP_ItemContextWidget", (("OpenButton", "TextBlock_324"), ("EquipButton", "TextBlock"), ("UnEquipButton", "TextBlock_262"),
                                            ("DropButton", "TextBlock_141"), ("UseButton", "TextBlock_202"), ("CancleButton", "TextBlock_70"),
                                            ("RotateButton", "RotateLabel")))


STEPS = {"tooltip": tooltip, "option": option, "inventory": inventory}

wanted = [s.strip() for s in os.environ.get("PG_PLAN_STEPS", "").split(",") if s.strip()] or list(STEPS.keys())
for name in wanted:
    try:
        STEPS[name]()
        W.log("done " + name)
    except Exception:
        W.log("FAILED %s\n%s" % (name, traceback.format_exc()))
