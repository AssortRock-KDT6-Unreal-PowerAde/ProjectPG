# 로비·캐릭터(인벤토리) 화면을 기획서(시스템 초안 1.1·1.2.1) 모양으로 맞춘다. (2026-10-04)
# 형님 WBP 의 배치는 그대로 두고 더하기만 한다:
#   WBP_CharacterWidget : 파란 바탕, 창고 위 검색 칸(SearchBox)·종류 고르기(FilterCombo) — 창고를 70px 내림
#   WBP_Slot·WBP_EquipSlot : 칸 색(연한 파랑), 칸 사이 선(진한 파랑)
#   WBP_ItemContextWidget : 돌리기(RotateButton)·나누기(SplitButton) 버튼 — 취소는 맨 아래 유지
#   WBP_Lobby : 버튼을 흰 판·검은 글자로
#   WBP_ItemTooltip(새) : 설명 창 — NameText, DescriptionText, StatNameText, StatValueText
#   BP_GameInstance : 설명 창 등록(EUIType ItemTooltip)
# 동작은 C++ 가 이름으로 찾아 한다(UInventoryWindow, UItemContextWidget, UItemTooltipWidget).
import json
import os
import sys
import traceback

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import pgwbp as W

H = unreal.HorizontalAlignment
V = unreal.VerticalAlignment
UI = "/Game/PG/Blueprint/UI/"


def character_widget():
    b = W.Builder(UI + "WBP_CharacterWidget")
    root = b.root()  # MainCanvas
    # (10/6) 파란 바탕 대신 뒤의 실제 맵·캐릭터가 보이게 옅게 어둡게만 한다(글자·칸이 잘 읽히게).
    bg, bg_slot = b.panel("PlanBackground", root, W.srgb(0x08, 0x1C, 0x28, 0.35), unreal.Margin(0, 0, 0, 0), radius=0.0)
    bg.set_brush_color(W.srgb(0x08, 0x1C, 0x28, 0.35))
    b.place(bg_slot, (0, 0, 1, 1), (0, 0), (0, 0))
    bg_slot.set_auto_size(False)
    bg_slot.set_offsets(unreal.Margin(0, 0, 0, 0))
    bg_slot.set_z_order(-10)

    # 창고를 70px 내리고 그 위에 검색 줄.
    stash = b.find("MainInventoryOverlay")
    stash_slot = stash.get_editor_property("slot")
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


def equip_see_through():
    # 장비 칸 사이 캐릭터 자리(CharacterView, 형님 WBP_Equip 의 연한 파랑 판)를 투명하게 → 뒤의 실제 캐릭터가 보인다(10/6).
    b = W.Builder(UI + "WBP_Equip")
    view = b.find("CharacterView")
    if view is not None:
        view.set_color_and_opacity(unreal.LinearColor(1, 1, 1, 0))
    b.finish()


def slot_colors():
    for name, image in (("WBP_Slot", "BackGround"), ("WBP_EquipSlot", "BackGround")):
        b = W.Builder(UI + name)
        border = b.find("SlotBorder")
        border.set_brush_color(W.PLAN_BLUE_DEEP)
        border.set_padding(unreal.Margin(1, 1, 1, 1))
        img = b.find(image)
        img.set_color_and_opacity(W.PLAN_LIGHT)
        b.finish()
    # 칸 기본색(하이라이트가 끝나면 돌아갈 색)도 연한 파랑으로.
    slot_bp = unreal.EditorAssetLibrary.load_asset(UI + "WBP_Slot")
    unreal.get_default_object(slot_bp.generated_class()).set_editor_property("default_slot_color", W.PLAN_LIGHT)
    unreal.EditorAssetLibrary.save_loaded_asset(slot_bp)


def context_menu():
    b = W.Builder(UI + "WBP_ItemContextWidget")
    box = b.find("VerticalBox_0")
    sample_text = b.find("TextBlock_324")
    sample_font = sample_text.get_editor_property("font")
    for key, label in (("Rotate", "돌리기"), ("Split", "나누기")):
        size, _ = b.make(unreal.SizeBox, key + "Size", box)
        size.set_width_override(200)
        size.set_height_override(50)
        btn, _ = b.make(unreal.Button, key + "Button", size)
        text, text_slot = b.make(unreal.TextBlock, key + "Label", btn)
        text.set_text(unreal.Text(label))
        text.set_font(sample_font)
    # 취소(SizeBox_3)를 맨 아래로 다시 붙인다.
    cancel = b.find("SizeBox_3")
    if cancel is not None and box.get_child_index(cancel) != box.get_children_count() - 1:
        box.remove_child(cancel)
        box.add_child(cancel)
    b.finish()


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


def character_and_menu_buttons():
    # 캐릭터 화면 아래 탭·뒤로가기, 우클릭 메뉴 버튼도 기획서처럼 흰 판·검은 글자.
    white_buttons("WBP_CharacterWidget", (("InventoryBtn", "TextBlock_59"), ("QuestBtn", "TextBlock"), ("MailBtn", "TextBlock_1"),
                                          ("StatisticsBtn", "TextBlock_2"), ("BackBtn", "TextBlock_3")))
    white_buttons("WBP_ItemContextWidget", (("OpenButton", "TextBlock_324"), ("EquipButton", "TextBlock"), ("UnEquipButton", "TextBlock_262"),
                                            ("DropButton", "TextBlock_141"), ("UseButton", "TextBlock_202"), ("CancleButton", "TextBlock_70"),
                                            ("RotateButton", "RotateLabel"), ("SplitButton", "SplitLabel")))


def lobby_buttons():
    b = W.Builder(UI + "WBP_Lobby")
    for btn_name, text_name in (("CharacterBtn", "CharterText"), ("GameStartBtn", "GameStartText"),
                                ("OptionBtn", "OptionText"), ("ExitBtn", "ExitText")):
        b.find(btn_name).set_style(b.button_style(W.PLAN_WHITE, W.PLAN_WHITE_HOVER, W.PLAN_LIGHT, radius=0.0))
        b.find(text_name).set_color_and_opacity(W.slate_color(W.PLAN_INK))
    b.finish()


def tooltip():
    # 맨 바깥을 캔버스로 하면 화면에 띄울 때 크기가 0 이 되어 잘린다(10/4 첫 시도) → 판(Border)이 맨 바깥, 폭은 SizeBox 로.
    path = UI + "WBP_ItemTooltip"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    b = W.Builder(path, "/Script/ProjectPG.ItemTooltipWidget")
    panel = b.root("Panel", "Border")
    panel.set_brush(W.flat_brush(W.srgb(0x12, 0x96, 0xD6), 0.0))
    panel.set_padding(unreal.Margin(18, 14, 18, 14))
    width, _ = b.make(unreal.SizeBox, "Width", panel)
    width.set_width_override(320)
    stack, _ = b.make(unreal.VerticalBox, "Stack", width)
    name, name_slot = b.text("NameText", stack, "야구 모자", 24, "Bold", W.PLAN_INK)
    name.set_shadow_color_and_opacity(W.CLEAR)
    name_slot.set_padding(unreal.Margin(0, 0, 0, 8))
    desc, desc_slot = b.text("DescriptionText", stack, "모자에 대한 묘사와 서사.", 16, "Regular", W.PLAN_INK, wrap=True)
    desc.set_shadow_color_and_opacity(W.CLEAR)
    desc.set_editor_property("wrap_text_at", 300.0)
    desc_slot.set_padding(unreal.Margin(0, 0, 0, 14))
    row, _ = b.make(unreal.HorizontalBox, "StatRow", stack)
    stat, stat_slot = b.text("StatNameText", row, "머리 방어", 16, "SemiBold", W.PLAN_INK)
    stat.set_shadow_color_and_opacity(W.CLEAR)
    b.fill(stat_slot)
    value, _ = b.text("StatValueText", row, "+10", 16, "SemiBold", W.PLAN_INK)
    value.set_shadow_color_and_opacity(W.CLEAR)
    b.finish()


def register_tooltip():
    H2 = unreal.MCPythonHelper
    gi = unreal.load_asset("/Game/PG/Blueprint/BP_GameInstance")
    graph = "EventGraph"
    info = json.loads(H2.get_blueprint_graph_info(gi, graph))
    nodes = {n["node_name"]: n for n in info["nodes"]}

    def pin(node, name, key="default_value"):
        for p in node.get("pins", []):
            if p["pin_name"] == name:
                return p.get(key)
        return None

    reg = [n for n in info["nodes"] if n["node_class"] == "K2Node_CallFunction" and pin(n, "UIType") is not None]
    existing = [n for n in reg if pin(n, "UIType") == "ItemTooltip"]
    if existing:
        # WBP_ItemTooltip 을 지웠다 다시 만들면 노드의 클래스 칸이 비므로 매번 다시 넣는다.
        H2.set_blueprint_node_pin_default(gi, graph, existing[0]["node_name"], "WidgetClass", UI + "WBP_ItemTooltip.WBP_ItemTooltip_C")
        H2.compile_blueprint(gi)
        unreal.EditorAssetLibrary.save_loaded_asset(gi)
        W.log("ItemTooltip class re-set on %s" % existing[0]["node_name"])
        return
    last = [n for n in reg if not pin(n, "then", "linked_to")][0]
    self_src = pin(last, "self", "linked_to")[0]
    added = json.loads(H2.add_blueprint_node(gi, graph, json.dumps(
        {"type": "CallFunction", "function_name": "RegisterUIClass", "target": "UIManagerSubSystem",
         "pos_x": last["pos_x"] + 320, "pos_y": 80})))
    new = added["node_name"]
    H2.connect_blueprint_pins(gi, graph, last["node_name"], "then", new, "execute")
    H2.connect_blueprint_pins(gi, graph, self_src["node_name"], self_src["pin_name"], new, "self")
    H2.set_blueprint_node_pin_default(gi, graph, new, "UIType", "ItemTooltip")
    H2.set_blueprint_node_pin_default(gi, graph, new, "WidgetClass", UI + "WBP_ItemTooltip.WBP_ItemTooltip_C")
    H2.compile_blueprint(gi)
    unreal.EditorAssetLibrary.save_loaded_asset(gi)
    W.log("ItemTooltip registered as %s" % new)


for step in (character_widget, equip_see_through, slot_colors, context_menu, lobby_buttons, character_and_menu_buttons, tooltip, register_tooltip):
    try:
        step()
        W.log("done " + step.__name__)
    except Exception:
        W.log("FAILED %s\n%s" % (step.__name__, traceback.format_exc()))
