# SoundTable 에 코드에서 부르는 소리 줄을 추가한다.
import unreal, json

ROWS = {"World_Container_Open": "Play_World_Container_Open"}
EV = "/Game/WwiseAudio/Events/Default_Work_Unit/"

table = unreal.load_asset("/Game/PG/Table/SoundTable")
data = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
names = {r["Name"] for r in data}
unreal.log(f"[소리표] 기존 줄 예시: {data[0]}")
for name, ev in ROWS.items():
    if name in names:
        unreal.log(f"[소리표] 이미 있음: {name}")
        continue
    obj = unreal.load_asset(EV + ev)
    data.append({"Name": name, "Event": obj.get_path_name()})
    unreal.log(f"[소리표] 추가: {name} -> {obj.get_path_name()}")
unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(data))
unreal.EditorAssetLibrary.save_loaded_asset(table, False)
for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)):
    unreal.log(f"[소리표] {r['Name']} = {r['Event']}")
