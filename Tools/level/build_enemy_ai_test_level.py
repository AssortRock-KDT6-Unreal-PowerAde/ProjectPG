# 몬스터 AI 시험 맵 L_EnemyAITest 를 만든다. (2026-10-17)
# 무엇을 보려고: 접속한 플레이어 2명이 시작 자리에 가만히 서 있어도 몬스터가 보고 → 다가가고 → 엄폐 → 쏘는지,
#               플레이어가 죽으면 → 포기 → 수색 → 순찰로 돌아가는지. 서버 로그 [EnemyAI] 줄로 확인한다.
# 생김새: 평평한 바닥, 시작 자리 2개(원점 근처), 20m 앞에 몬스터 1마리, 그 사이에 엄폐용 벽 몇 개.
#        그 뒤에 등 돌린 몬스터 1마리 더(동료) → 앞 몬스터가 외치면 이 동료가 듣고 플레이어 쪽으로 오는지 본다.
#        서버 로그: 앞 몬스터 "발견, 외침" → 동료 "동료 외침 듣고 ... 쪽으로 감".
# 길(네비)은 서버가 켜질 때 만든다(RecastNavMesh 를 "실행 중 생성"으로 둠) → 에디터에서 길 굽기 안 해도 됨.
# 다시 돌리면 이 스크립트가 놓은 액터(이름이 AITest_*)만 지우고 다시 놓는다.
# 실행: UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<프로젝트>/Tools/level/build_enemy_ai_test_level.py" -unattended -nosplash -RenderOffscreen
#       (-run=pythonscript 로 돌리면 new_level 에서 에디터가 죽는다(5.6). 창 없이 띄우고 끝에서 스스로 닫는다.)
#       (-nullrhi 로 돌리면 이미 있는 맵을 열고 액터를 놓을 때 0 으로 나누기 오류로 죽는다(5.6, 2026-10-18) → -RenderOffscreen.)
# 시험: 서버  UnrealEditor-Cmd.exe <uproject> /Game/PG/AI/Test/L_EnemyAITest -server -log
#       클라  UnrealEditor-Cmd.exe <uproject> 127.0.0.1 -game -log   (2개)
import unreal

LEVEL = "/Game/PG/AI/Test/L_EnemyAITest"
ENEMY_BP = "/Game/PG/Blueprint/Characters/CBP_CustomEnemyCharacter.CBP_CustomEnemyCharacter_C"
# 시험 전용 게임모드 BP. 플레이어가 PlayerStart 에서 생기게 하려고 따로 둔다.
# 왜: GameModePG 계열(InGame·Mock)은 레벨에 MapBuilder 를 만들어 플레이어를 지형의 시작 구역에 세운다 → 몬스터와 멀어짐.
#     부모 ACustomGameMode 는 MapBuilder 를 안 만든다. 캐릭터·컨트롤러·플레이어 상태는 InGame 과 같게.
GAME_MODE_BP = "/Game/PG/AI/Test/BP_EnemyAITestGameMode"
GAME_MODE_PARENT = "/Script/ProjectPG.CustomGameMode"
PLAYER_PAWN = "/Game/PG/Blueprint/Characters/CBP_CustomPlayerCharacter_Modular.CBP_CustomPlayerCharacter_Modular_C"
PLAYER_CONTROLLER = "/Script/ProjectPG.PlayerController_InGame"
PLAYER_STATE = "/Script/ProjectPG.CustomPlayerState"
CUBE = "/Engine/BasicShapes/Cube.Cube"

PLAYER_STARTS = [unreal.Vector(0, -150, 120), unreal.Vector(0, 150, 120)]
ENEMY_AT = unreal.Vector(2000, 0, 120)          # 감지 2500 안, 사거리 1200 밖 → 먼저 다가가야 함
# 동료: 앞 몬스터에서 약 13m(외침 거리 2500 안), 플레이어에서 약 29m(감지 거리 밖), 플레이어 반대쪽을 봄 → 눈으론 못 봄.
BUDDY_AT = unreal.Vector(2600, 1200, 120)
# 엄폐 벽: (위치, 크기 배율). 몬스터와 플레이어 사이 여기저기.
WALLS = [
    (unreal.Vector(1300, -400, 100), unreal.Vector(0.4, 3.0, 2.0)),
    (unreal.Vector(1100, 500, 100), unreal.Vector(0.4, 3.0, 2.0)),
    (unreal.Vector(800, 0, 100), unreal.Vector(0.4, 2.0, 2.0)),
]

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EAL = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("[AITEST] " + msg)


def spawn(cls, name, location, rotation=unreal.Rotator(0, 0, 0)):
    log("놓기: " + name)
    actor = eas.spawn_actor_from_class(cls, location, rotation)
    actor.set_actor_label("AITest_" + name)
    return actor


def spawn_cube(name, location, scale):
    actor = spawn(unreal.StaticMeshActor, name, location)
    actor.static_mesh_component.set_static_mesh(unreal.load_asset(CUBE))
    actor.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)  # 엄폐로 치려면 고정이어야 함
    actor.set_actor_scale3d(scale)
    return actor


def make_game_mode():
    if EAL.does_asset_exist(GAME_MODE_BP):
        bp = unreal.load_asset(GAME_MODE_BP)
    else:
        folder, name = GAME_MODE_BP.rsplit("/", 1)
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.load_class(None, GAME_MODE_PARENT))
        bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Blueprint, factory)
    gm = unreal.get_default_object(unreal.load_class(None, GAME_MODE_BP + "." + GAME_MODE_BP.rsplit("/", 1)[1] + "_C"))
    gm.set_editor_property("default_pawn_class", unreal.load_class(None, PLAYER_PAWN))
    gm.set_editor_property("player_controller_class", unreal.load_class(None, PLAYER_CONTROLLER))
    gm.set_editor_property("player_state_class", unreal.load_class(None, PLAYER_STATE))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    EAL.save_loaded_asset(bp)
    log("게임모드 BP 준비: " + GAME_MODE_BP)
    return bp.generated_class()


game_mode_class = make_game_mode()

if EAL.does_asset_exist(LEVEL):
    les.load_level(LEVEL)
else:
    les.new_level(LEVEL)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
log("레벨 열림")

for actor in eas.get_all_level_actors():
    if actor.get_actor_label().startswith("AITest_"):
        eas.destroy_actor(actor)

spawn_cube("Floor", unreal.Vector(0, 0, -50), unreal.Vector(80, 80, 1))
for i, (at, scale) in enumerate(WALLS):
    spawn_cube("Wall%d" % i, at, scale)

for i, at in enumerate(PLAYER_STARTS):
    spawn(unreal.PlayerStart, "PlayerStart%d" % i, at)

enemy_class = unreal.load_object(None, ENEMY_BP)
spawn(enemy_class, "Enemy", ENEMY_AT, unreal.Rotator(0, 0, 180))
spawn(enemy_class, "EnemyBuddy", BUDDY_AT, unreal.Rotator(0, 0, 0))

spawn(unreal.DirectionalLight, "Sun", unreal.Vector(0, 0, 1000), unreal.Rotator(0, -50, 30))
spawn(unreal.SkyLight, "Sky", unreal.Vector(0, 0, 800))

bounds = spawn(unreal.NavMeshBoundsVolume, "NavBounds", unreal.Vector(0, 0, 0))
bounds.set_actor_scale3d(unreal.Vector(40, 40, 5))
navmesh = spawn(unreal.RecastNavMesh, "NavMesh", unreal.Vector(0, 0, 0))
navmesh.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)

settings = world.get_world_settings()
settings.set_editor_property("default_game_mode", game_mode_class)

les.save_current_level()
log("저장: " + LEVEL)
unreal.SystemLibrary.quit_editor()
