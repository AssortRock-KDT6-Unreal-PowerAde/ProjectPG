# WBP 를 파이썬으로 채우는 공용 도우미 (2026-09-23 블루프린트 분리).
#
# 왜 있나:
#   화면(UI)을 C++ 로 그리던 것을 WBP 로 옮기는데, 손으로 수십 개를 끌어다 놓으면 색·여백이 조금씩 어긋난다.
#   C++ 의 모양 규칙(PGFlowStyle.h: 색, 둥근 판, 버튼 세 종류, Pretendard 글꼴)을 여기 한 번 옮겨 두고,
#   화면마다 build_*.py 가 이 함수들로 같은 모양을 WBP 안에 깐다. 그 뒤로는 에디터에서 손으로 다듬는다.
#
# 쓰는 곳: Tools/wbp/build_*.py. 실행은 에디터를 끈 상태에서
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<프로젝트>/Tools/wbp/build_matching.py" -unattended -nosplash
# 팀 프로젝트용(10/4): 개인 프로젝트(ProjectTest2)의 같은 파일을 옮기고, 기획서 색(파란 바탕·흰 버튼)을 더했다.
import unreal

FONT_PATHS = {
    "Regular": "/Game/PG/UI/Fonts/Pretendard-Regular_Font",
    "SemiBold": "/Game/PG/UI/Fonts/Pretendard-SemiBold_Font",
    "Bold": "/Game/PG/UI/Fonts/Pretendard-Bold_Font",
}


def log(msg):
    unreal.log("[PGWBP] " + msg)
    print("[PGWBP] " + msg)
    # 명령줄 실행에서는 로그 줄이 안 보일 때가 있어 파일에도 남긴다(Saved/PGWBP.log).
    try:
        with open(unreal.Paths.project_saved_dir() + "PGWBP.log", "a", encoding="utf-8") as f:
            f.write("[PGWBP] " + msg + "\n")
    except Exception:
        pass


# ---- 색: PGFlowStyle.h 의 Srgb 와 같은 변환(sRGB 바이트 → 선형) ----
def srgb(r, g, b, a=1.0):
    def lin(c):
        c = c / 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return unreal.LinearColor(lin(r), lin(g), lin(b), a)


ACCENT = srgb(0xE8, 0xC5, 0x47)
ACCENT_HOVER = srgb(0xF2, 0xD4, 0x62)
ACCENT_PRESSED = srgb(0xC9, 0xA8, 0x34)
INK = srgb(0x14, 0x14, 0x14)
TEXT = srgb(0xF2, 0xF2, 0xF2)
TEXT_DIM = srgb(0xA8, 0xA8, 0xA8)
TEXT_FAINT = srgb(0x78, 0x78, 0x78)
PANEL = srgb(0x0E, 0x0F, 0x12, 0.80)
PANEL_SOLID = srgb(0x0E, 0x0F, 0x12, 0.94)
BAR = srgb(0x08, 0x09, 0x0B, 0.78)
BUTTON = srgb(0x1C, 0x1D, 0x22, 0.88)
BUTTON_HOVER = srgb(0x2C, 0x2E, 0x35, 0.95)
ROW_TINT = srgb(0xFF, 0xFF, 0xFF, 0.04)
DIVIDER = srgb(0xFF, 0xFF, 0xFF, 0.10)
DISABLED = srgb(0x30, 0x30, 0x30, 0.6)
CLEAR = unreal.LinearColor(0, 0, 0, 0)

# ---- 기획서(시스템 초안) UI 색: 파란 바탕, 흰 버튼·검은 글자, 연한 파란 격자 ----
PLAN_BLUE = srgb(0x17, 0x5E, 0x80)
PLAN_BLUE_DEEP = srgb(0x0F, 0x46, 0x62)
PLAN_LIGHT = srgb(0xA3, 0xBF, 0xCC)
PLAN_WHITE = srgb(0xFF, 0xFF, 0xFF)
PLAN_WHITE_HOVER = srgb(0xE6, 0xEE, 0xF2)
PLAN_INK = srgb(0x10, 0x10, 0x10)


def slate_color(color):
    sc = unreal.SlateColor()
    sc.set_editor_property("specified_color", color)
    return sc


def flat_brush(color, radius=0.0):
    """PGFlowStyle::FlatBrush 와 같은 둥근 모서리 단색 브러시."""
    brush = unreal.SlateBrush()
    brush.set_editor_property("tint_color", slate_color(color))
    if radius > 0.0:
        brush.set_editor_property("draw_as", unreal.SlateBrushDrawType.ROUNDED_BOX)
        outline = unreal.SlateBrushOutlineSettings()
        outline.set_editor_property("corner_radii", unreal.Vector4(radius, radius, radius, radius))
        outline.set_editor_property("rounding_type", unreal.SlateBrushRoundingType.FIXED_RADIUS)
        outline.set_editor_property("color", slate_color(CLEAR))
        outline.set_editor_property("width", 0.0)
        brush.set_editor_property("outline_settings", outline)
    else:
        brush.set_editor_property("draw_as", unreal.SlateBrushDrawType.BOX)
    return brush


def font(size, weight="Regular"):
    info = unreal.SlateFontInfo()
    info.set_editor_property("font_object", unreal.EditorAssetLibrary.load_asset(FONT_PATHS[weight]))
    info.set_editor_property("size", float(size))
    return info


def engine_font(size, typeface="Bold"):
    """엔진 기본 글꼴(Roboto). 로딩·안내 문구가 원래 Slate 기본 글꼴(FCoreStyle::GetDefaultFontStyle)을 썼다 — 모양을 그대로 두려고."""
    info = unreal.SlateFontInfo()
    info.set_editor_property("font_object", unreal.load_object(None, "/Engine/EngineFonts/Roboto.Roboto"))
    info.set_editor_property("typeface_font_name", typeface)
    info.set_editor_property("size", float(size))
    return info


def ensure_wbp(asset_path, parent_class_path):
    """WBP 가 없으면 C++ 부모(예: /Script/ProjectPGTest2.PGScoreboardScreenWidget)로 새로 만든다."""
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return
    parent = unreal.load_class(None, parent_class_path)
    if parent is None:
        raise RuntimeError("parent class not found: " + parent_class_path)
    factory = unreal.WidgetBlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    folder, name = asset_path.rsplit("/", 1)
    created = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.WidgetBlueprint, factory)
    if created is None:
        raise RuntimeError("create failed: " + asset_path)
    log("created %s (parent %s)" % (asset_path, parent_class_path))


class Builder:
    """WBP 하나를 열어 위젯을 만들고, 끝나면 컴파일·저장한다."""

    def __init__(self, asset_path, parent_class_path=None):
        if parent_class_path:
            ensure_wbp(asset_path, parent_class_path)
        self.asset_path = asset_path
        self.bp = unreal.EditorAssetLibrary.load_asset(asset_path)
        if self.bp is None:
            raise RuntimeError("WBP not found: " + asset_path)
        self.tree = None
        self.made = []

    # ---- 찾기·만들기 ----
    def find(self, name):
        return unreal.MCPythonHelper.umg_find_widget(self.bp, name)

    def root(self, name="Root", widget_type="CanvasPanel"):
        """루트 위젯(기본 캔버스). 없으면 만든다(빈 WBP)."""
        found = self.find(name)
        if found is not None:
            self.tree = found.get_outer()
            return found
        import json
        info = json.loads(unreal.MCPythonHelper.umg_get_widget_info(self.bp))
        if info.get("root_widget"):
            canvas = self.find(info["root_widget"])
            self.tree = canvas.get_outer()
            return canvas
        result = json.loads(unreal.MCPythonHelper.umg_add_widget(self.bp, widget_type, name, ""))
        if not result.get("success"):
            raise RuntimeError(result)
        canvas = self.find(name)
        self.tree = canvas.get_outer()
        return canvas

    def make(self, cls, name, parent):
        """위젯을 만들어 parent(패널)에 붙이고 (위젯, 슬롯) 을 돌려준다. 같은 이름이 있으면 그걸 쓴다."""
        widget = self.find(name)
        if widget is None:
            # 플러그인의 정식 추가 함수로 만든다(변수 GUID 등록까지 해 준다). new_object 로 직접 만들면 GUID 가 빠져
            # 컴파일 때 위젯마다 ensure 가 뜬다(9/23 첫 시도).
            import json
            result = json.loads(unreal.MCPythonHelper.umg_add_widget(self.bp, cls.__name__, name, parent.get_name()))
            if not result.get("success"):
                raise RuntimeError("add %s %s: %s" % (cls.__name__, name, result))
            widget = self.find(name)
            self.made.append(name)
        slot = widget.get_editor_property("slot")
        return widget, slot

    # ---- 모양 도우미(PGFlowScreenWidget 의 Make* 와 같은 모양) ----
    def text(self, name, parent, value, size, weight="Regular", color=TEXT, wrap=False):
        tb, slot = self.make(unreal.TextBlock, name, parent)
        tb.set_text(unreal.Text(value))
        tb.set_font(font(size, weight))
        tb.set_color_and_opacity(slate_color(color))
        tb.set_shadow_offset(unreal.Vector2D(0.0, 1.5))
        tb.set_shadow_color_and_opacity(unreal.LinearColor(0.0, 0.0, 0.0, 0.55))
        if wrap:
            tb.set_auto_wrap_text(True)
        return tb, slot

    def panel(self, name, parent, color, padding, radius=6.0):
        border, slot = self.make(unreal.Border, name, parent)
        border.set_brush(flat_brush(color, radius))
        border.set_padding(padding)
        return border, slot

    def button_style(self, normal, hover, pressed, radius=4.0):
        style = unreal.ButtonStyle()
        style.set_editor_property("normal", flat_brush(normal, radius))
        style.set_editor_property("hovered", flat_brush(hover, radius))
        style.set_editor_property("pressed", flat_brush(pressed, radius))
        style.set_editor_property("disabled", flat_brush(DISABLED, radius))
        style.set_editor_property("normal_padding", unreal.Margin(0, 0, 0, 0))
        style.set_editor_property("pressed_padding", unreal.Margin(0, 0, 0, 0))
        return style

    def button(self, name, parent, label, kind, font_size, padding):
        """kind: primary(노란) / secondary(어두운) / ghost(바탕 없음). 글자는 <name>Label."""
        btn, slot = self.make(unreal.Button, name, parent)
        if kind == "plan":
            # 기획서 버튼: 흰 판, 검은 글자, 모서리 각짐
            btn.set_style(self.button_style(PLAN_WHITE, PLAN_WHITE_HOVER, PLAN_LIGHT, radius=0.0))
            color, weight = PLAN_INK, "SemiBold"
        elif kind == "primary":
            btn.set_style(self.button_style(ACCENT, ACCENT_HOVER, ACCENT_PRESSED))
            color, weight = INK, "Bold"
        elif kind == "ghost":
            btn.set_style(self.button_style(CLEAR, srgb(0xFF, 0xFF, 0xFF, 0.06), srgb(0xFF, 0xFF, 0xFF, 0.10)))
            color, weight = TEXT_DIM, "SemiBold"
        else:
            btn.set_style(self.button_style(BUTTON, BUTTON_HOVER, BUTTON))
            color, weight = TEXT, "SemiBold"
        label_tb, label_slot = self.text(name + "Label", btn, label, font_size, weight, color)
        if kind in ("primary", "plan"):
            label_tb.set_shadow_color_and_opacity(CLEAR)
        label_slot.set_padding(padding)
        label_slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
        label_slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
        return btn, slot

    @staticmethod
    def place(slot, anchors, alignment, position, size=None):
        """캔버스 슬롯 자리. anchors = (minX, minY, maxX, maxY)."""
        slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(anchors[0], anchors[1]),
                                        maximum=unreal.Vector2D(anchors[2], anchors[3])))
        slot.set_alignment(unreal.Vector2D(*alignment))
        slot.set_position(unreal.Vector2D(*position))
        if size is None:
            slot.set_auto_size(True)
        else:
            slot.set_auto_size(False)
            slot.set_size(unreal.Vector2D(*size))

    @staticmethod
    def fill(slot):
        slot.set_size(unreal.SlateChildSize(1.0, unreal.SlateSizeRule.FILL))

    def finish(self):
        # 예전에 GUID 없이 들어간 위젯이 있으면 채운다(9/23 첫 시도에서 저장된 것 복구 겸).
        fixed = unreal.MCPythonHelper.umg_ensure_widget_guids(self.bp)
        if fixed:
            log("filled %d missing widget GUIDs" % fixed)
        unreal.BlueprintEditorLibrary.compile_blueprint(self.bp)
        saved = unreal.EditorAssetLibrary.save_asset(self.asset_path, only_if_is_dirty=False)
        log("%s: made %d widgets %s, saved=%s" % (self.asset_path, len(self.made), self.made, saved))
