# 워존 몬스터 BP 를 DA_MapAssets 의 "몬스터" 칸(EnemyClasses)에 넣기 (2026-10-19).
#
# 왜 있나:
#   맵 빌더의 몬스터 담당(UMapEnemySpawner)은 몬스터 자리(AISpawn 지점)에 DA_MapAssets 의 EnemyClasses 목록에서 몬스터를 골라 놓는다.
#   경로를 C++ 에 글자로 박지 않고 데이터 에셋 칸에 저장해야, 패키징(쿠킹)이 연결을 보고 몬스터 BP 를 패키지에 담는다.
#   목록이 비어 있으면 몬스터가 하나도 안 나온다(서버 로그 Error "no monster BP in DA_MapAssets EnemyClasses").
#
# 실행은 에디터를 끈 상태에서(새 C++ 를 빌드한 뒤):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<프로젝트>/Tools/level/setup_enemy_spawns.py" -unattended -nosplash
# 다시 돌려도 된다: 목록을 아래 값으로 맞추기만 한다. 숫자 칸(최대 수·거리 등)은 건드리지 않는다(에디터에서 DA_MapAssets 를 열어 바꾼다).
import unreal

DATA_ASSET = "/Game/PG/LevelDesign/DA_MapAssets"
ENEMY_BPS = ["/Game/PG/Blueprint/Characters/CBP_CustomEnemyCharacter"]


def log(msg):
    unreal.log("[EnemySpawnSetup] " + msg)
    print("[EnemySpawnSetup] " + msg)


def main():
    data = unreal.EditorAssetLibrary.load_asset(DATA_ASSET)
    if not data:
        log("MISSING data asset: " + DATA_ASSET)
        return
    classes = []
    for path in ENEMY_BPS:
        cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
        if not cls:
            log("MISSING monster BP: " + path)
            continue
        classes.append(cls)
    if not classes:
        log("no monster BP found - nothing changed")
        return
    data.set_editor_property("enemy_classes", classes)
    saved = unreal.EditorAssetLibrary.save_loaded_asset(data, only_if_is_dirty=False)
    now = data.get_editor_property("enemy_classes")
    log("EnemyClasses = %s saved=%s" % ([str(c) for c in now], saved))


main()
