# 로비 흐름 에셋 정리 (2026-10-04, 웹 서버 제거 → 리슨 서버).
# 1) 시작 짐 표 StarterInventoryTable 을 만들고 TableLoader 에 등록한다.
# 2) BP_GameInstance 에서 로그인 화면 3개(LoginWindow·Login·CreateUser) 등록 노드를 지우고 줄을 잇는다.
# 3) 같은 줄 끝에 매칭(WBP_Matching)·옵션(WBP_Option) 화면 등록 노드를 붙인다.
# 실행: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<프로젝트>/Tools/wbp/setup_lobby_flow.py" -unattended -nosplash
import json
import unreal

EAL = unreal.EditorAssetLibrary
H = unreal.MCPythonHelper


def log(msg):
    unreal.log("[SETUP] " + msg)


# ---------- 1) 시작 짐 표 ----------
# 장착(Equip) 줄은 아이템 표의 장비 칸으로 바로 들어간다. 기획서 인벤토리 그림처럼 장비 칸이 다 차 보이게.
STARTER_CSV = """---,ItemID,Count,Container
Equip_Backpack,"5001",1,Equip
Equip_Helmet,"2001",1,Equip
Equip_Shirt,"2004",1,Equip
Equip_Pants,"2005",1,Equip
Equip_Shoes,"2006",1,Equip
Equip_Main,"1010",1,Equip
Equip_Sub,"1017",1,Equip
Equip_Pouch,"2007",1,Equip
Equip_Holster,"2008",1,Equip
Stash_Shotgun,"1007",1,Stash
Stash_Pistol,"1001",1,Stash
Stash_Vest,"2003",1,Stash
Stash_RifleAmmo,"3002",60,Stash
Stash_PistolAmmo,"3001",60,Stash
Stash_Medkit,"3011",1,Stash
Stash_Bandage,"3007",5,Stash
Stash_Painkiller,"3008",4,Stash
Stash_Water,"3005",1,Stash
Stash_Key,"4002",1,Stash
Stash_Scrap,"6002",10,Stash
Pocket_Money,"6001",12345,Pocket
Pocket_Grenade,"3012",1,Pocket
"""
path = "/Game/PG/Table/StarterInventoryTable"
if EAL.does_asset_exist(path):
    table = unreal.load_asset(path)
else:
    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", unreal.load_object(None, "/Script/ProjectPG.StarterInventoryRow"))
    table = unreal.AssetToolsHelpers.get_asset_tools().create_asset("StarterInventoryTable", "/Game/PG/Table", unreal.DataTable, factory)
ok = unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(table, STARTER_CSV)
EAL.save_loaded_asset(table)
log("StarterInventoryTable fill=%s rows=%d" % (ok, len(unreal.DataTableFunctionLibrary.get_data_table_row_names(table))))

loader = unreal.load_asset("/Game/PG/Table/TableLoader")
names = [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(loader)]
if "StarterInventoryTable" not in names:
    csv = unreal.DataTableFunctionLibrary.export_data_table_to_csv_string(loader)
    csv = csv.rstrip("\n") + '\nStarterInventoryTable,"/Game/PG/Table/StarterInventoryTable","True"\n'
    ok = unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(loader, csv)
    EAL.save_loaded_asset(loader)
    log("TableLoader add StarterInventoryTable fill=%s rows=%s" % (ok, [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(loader)]))

# ---------- 2·3) BP_GameInstance ----------
gi = unreal.load_asset("/Game/PG/Blueprint/BP_GameInstance")
graph = "EventGraph"
info = json.loads(H.get_blueprint_graph_info(gi, graph))
nodes = {n["node_name"]: n for n in info["nodes"]}


def pin_default(node, pin):
    for p in node.get("pins", []):
        if p["pin_name"] == pin:
            return p.get("default_value")
    return None


register = [n for n in info["nodes"] if n["node_class"] == "K2Node_CallFunction" and pin_default(n, "UIType") is not None]
by_type = {pin_default(n, "UIType"): n["node_name"] for n in register}
log("registered before: %s" % sorted(by_type.keys()))


def exec_target(node_name):
    for p in nodes[node_name]["pins"]:
        if p["pin_name"] == "then" and p.get("linked_to"):
            return p["linked_to"][0]["node_name"]
    return None


def exec_source(node_name):
    for p in nodes[node_name]["pins"]:
        if p["pin_name"] == "execute" and p.get("linked_to"):
            return p["linked_to"][0]
    return None


# 로그인 화면 등록 노드 지우기: 앞 노드의 then 을 뒤 노드의 execute 에 다시 잇는다.
for ui_type in ("LoginWindow", "Login", "CreateUser"):
    name = by_type.get(ui_type)
    if not name:
        continue
    before = exec_source(name)
    after = exec_target(name)
    log("remove %s (%s): %s" % (ui_type, name, H.remove_blueprint_node(gi, graph, name)))
    if before and after:
        log("  relink %s.%s -> %s: %s" % (before["node_name"], before["pin_name"], after,
                                          H.connect_blueprint_pins(gi, graph, before["node_name"], before["pin_name"], after, "execute")))
    info = json.loads(H.get_blueprint_graph_info(gi, graph))
    nodes = {n["node_name"]: n for n in info["nodes"]}

# 새 화면 등록 노드 붙이기(줄 끝 = then 이 비어 있는 등록 노드 뒤).
register = [n for n in info["nodes"] if n["node_class"] == "K2Node_CallFunction" and pin_default(n, "UIType") is not None]
by_type = {pin_default(n, "UIType"): n["node_name"] for n in register}
last = [n["node_name"] for n in register if not exec_target(n["node_name"])][0]
self_source = None
for p in nodes[last]["pins"]:
    if p["pin_name"] == "self" and p.get("linked_to"):
        self_source = p["linked_to"][0]
x = nodes[last]["pos_x"]
for ui_type, widget in (("Matching", "/Game/PG/Blueprint/UI/WBP_Matching.WBP_Matching_C"),
                        ("Option", "/Game/PG/Blueprint/UI/WBP_Option.WBP_Option_C")):
    if ui_type in by_type:
        last = by_type[ui_type]
        continue
    x += 320
    added = json.loads(H.add_blueprint_node(gi, graph, json.dumps(
        {"type": "CallFunction", "function_name": "RegisterUIClass", "target": "UIManagerSubSystem", "pos_x": x, "pos_y": 80})))
    new = added["node_name"]
    log("add %s -> %s" % (ui_type, new))
    log("  exec %s" % H.connect_blueprint_pins(gi, graph, last, "then", new, "execute"))
    if self_source:
        log("  self %s" % H.connect_blueprint_pins(gi, graph, self_source["node_name"], self_source["pin_name"], new, "self"))
    log("  type %s" % H.set_blueprint_node_pin_default(gi, graph, new, "UIType", ui_type))
    log("  class %s" % H.set_blueprint_node_pin_default(gi, graph, new, "WidgetClass", widget))
    last = new

log("compile %s" % H.compile_blueprint(gi))
EAL.save_loaded_asset(gi)
info = json.loads(H.get_blueprint_graph_info(gi, graph))
order = []
for n in info["nodes"]:
    t = pin_default(n, "UIType")
    if t is not None:
        cls = [p.get("default_object") for p in n["pins"] if p["pin_name"] == "WidgetClass"]
        order.append("%s=%s" % (t, cls[0] if cls else None))
log("registered after: %s" % order)
