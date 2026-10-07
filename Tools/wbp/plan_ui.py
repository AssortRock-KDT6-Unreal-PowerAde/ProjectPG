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


STEPS = {"tooltip": tooltip, "option": option}

wanted = [s.strip() for s in os.environ.get("PG_PLAN_STEPS", "").split(",") if s.strip()] or list(STEPS.keys())
for name in wanted:
    try:
        STEPS[name]()
        W.log("done " + name)
    except Exception:
        W.log("FAILED %s\n%s" % (name, traceback.format_exc()))
