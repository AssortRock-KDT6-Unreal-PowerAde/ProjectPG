# 맵에 스폰할 상자 BP 만들기 (2026-10-14).
#
# 왜 있나:
#   맵 빌더는 상자 자리(Loot 지점)마다 DA_MapAssets 의 상자 BP 목록에서 하나를 골라 놓는다.
#   상자로 쓸 팩 BP(탄약·의료 상자)는 부모가 StaticMeshActor 라서 열 수 없고, 팩 폴더는 깃에 안 올라간다.
#   그래서 팩 BP 를 PG 폴더로 복제한 뒤 부모를 InteractActor 로 바꾸고, 목록에 넣는다.
#   원본 팩 BP 는 건드리지 않는다(팩 폴더가 다른 프로젝트와 연결돼 있어서).
#
# 부모를 바꾸면 몸통이 빠진다: 몸통은 StaticMeshActor 가 준 부품에 있었는데, 그 부품이 사라지고
#   InteractActor 의 빈 MeshComp 가 생긴다. 그래서 MeshComp 에 몸통 메시를 다시 넣는다.
#
# 실행은 에디터를 끈 상태에서(새 C++ 를 빌드한 뒤):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<프로젝트>/Tools/level/setup_loot_containers.py" -unattended -nosplash
# 다시 돌려도 된다: 이미 있는 복제본은 그대로 쓰고, 부모·메시·목록만 다시 맞춘다.
import unreal

DEST_FOLDER = "/Game/PG/Blueprint/Interact"
DATA_ASSET = "/Game/PG/LevelDesign/DA_MapAssets"

# (원본 팩 BP, 복제본 이름, 몸통 메시)
CONTAINERS = [
    ("/Game/AE_BR_Props/Blueprints/Ammunition_Box_01_Blueprint", "BP_AmmoBox_01", "/Game/AE_BR_Props/Models/Ammunition_Box_01_Sm"),
    ("/Game/AE_BR_Props/Blueprints/Ammunition_Box_02_Blueprint", "BP_AmmoBox_02", "/Game/AE_BR_Props/Models/Ammunition_Box_02_SM"),
    ("/Game/AE_BR_Props/Blueprints/Ammunition_Box_03_Blueprint", "BP_AmmoBox_03", "/Game/AE_BR_Props/Models/Ammunition_Box_03_SM"),
    ("/Game/AE_BR_Props/Blueprints/Ammunition_Box_04_Blueprint", "BP_AmmoBox_04", "/Game/AE_BR_Props/Models/Ammunition_Box_04_SM"),
    ("/Game/AE_BR_Props/Blueprints/Medical_Box_Blueprint", "BP_MedicalBox_01", "/Game/AE_BR_Props/Models/Medical_Box_SM"),
    ("/Game/AE_BR_Props/Blueprints/Medical_Box_02_Blueprint", "BP_MedicalBox_02", "/Game/AE_BR_Props/Models/Medical_Box_02_SM"),
]

# 원래 쓰던 나무 상자도 목록에 남긴다.
KEEP_IN_LIST = ["/Game/PG/Blueprint/Item/BP_LootCrate"]


def log(msg):
    unreal.log("[LootSetup] " + msg)
    print("[LootSetup] " + msg)
    try:
        with open(unreal.Paths.project_saved_dir() + "LootSetup.log", "a", encoding="utf-8") as f:
            f.write("[LootSetup] " + msg + "\n")
    except Exception:
        pass


def make_container(src_path, name, mesh_path):
    dst_path = DEST_FOLDER + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(dst_path):
        log("already there, reusing: " + dst_path)
    else:
        if not unreal.EditorAssetLibrary.does_asset_exist(src_path):
            log("MISSING source BP: " + src_path)
            return None
        if not unreal.EditorAssetLibrary.duplicate_asset(src_path, dst_path):
            log("FAILED to duplicate " + src_path)
            return None
        log("duplicated " + src_path + " -> " + dst_path)

    bp = unreal.EditorAssetLibrary.load_asset(dst_path)
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if not bp or not mesh:
        log("FAILED to load bp=%s mesh=%s" % (bool(bp), bool(mesh)))
        return None

    if not isinstance(unreal.get_default_object(bp.generated_class()), unreal.InteractActor):
        unreal.BlueprintEditorLibrary.reparent_blueprint(bp, unreal.InteractActor)
        log("reparented to InteractActor: " + name)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)

    # 몸통: InteractActor 의 MeshComp(코드가 만든 부품)에 넣는다. 게임 중에 바닥 높이로 옮기므로 무버블.
    cdo = unreal.get_default_object(bp.generated_class())
    body = cdo.get_editor_property("mesh_comp")
    body.set_editor_property("static_mesh", mesh)
    body.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)

    # BP 가 단 부품(Pivot·뚜껑)도 무버블로. 팩에서 스태틱으로 저장돼 있어서, 그대로 두면
    # 무버블 몸통에 못 붙고 떨어져 나간다(몸통만 바닥으로 옮겨지고 뚜껑은 공중에 남는다).
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    for handle in subsystem.k2_gather_subobject_data_for_blueprint(bp):
        data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
        template = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
        if isinstance(template, unreal.SceneComponent) and template.get_editor_property("mobility") != unreal.ComponentMobility.MOVABLE:
            template.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
            log("%s: %s -> Movable" % (name, template.get_name()))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_asset(bp, False)

    check_cdo = unreal.get_default_object(bp.generated_class())
    check = check_cdo.get_editor_property("mesh_comp").get_editor_property("static_mesh")
    log("%s: is_interact_actor=%s body=%s" % (name, isinstance(check_cdo, unreal.InteractActor), check.get_name() if check else "NONE"))
    return bp.generated_class()


def main():
    log("---- start ----")
    if not unreal.EditorAssetLibrary.does_directory_exist(DEST_FOLDER):
        unreal.EditorAssetLibrary.make_directory(DEST_FOLDER)

    classes = []
    for path in KEEP_IN_LIST:
        bp = unreal.EditorAssetLibrary.load_asset(path)
        if bp:
            classes.append(bp.generated_class())
        else:
            log("MISSING keep-in-list BP: " + path)
    for src, name, mesh in CONTAINERS:
        cls = make_container(src, name, mesh)
        if cls:
            classes.append(cls)

    data = unreal.EditorAssetLibrary.load_asset(DATA_ASSET)
    if not data:
        log("MISSING " + DATA_ASSET)
        return
    data.set_editor_property("loot_container_classes", classes)
    unreal.EditorAssetLibrary.save_loaded_asset(data, False)
    saved = data.get_editor_property("loot_container_classes")
    log("DA_MapAssets loot_container_classes = %d: %s" % (len(saved), ", ".join(str(c.get_name()) if c else "None" for c in saved)))
    log("---- done ----")


main()
