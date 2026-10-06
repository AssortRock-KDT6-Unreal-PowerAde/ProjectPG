# 타이틀(로비) 레벨 L_Title 을 만든다. (2026-10-04)
# 게임에서: 게임을 켜면 처음 보이는 화면. 호숫가 마을(시설 레벨 LD_Facility_RuralDiorama_2x2)을 배경으로
#           팀 캐릭터가 흙길에 서 있고, 오른쪽 아래에 로비 메뉴(캐릭터·게임 시작·옵션·종료, WBP_Lobby)가 뜬다.
# 레벨이 하는 일은 보이는 것뿐이다: 배경·조명·카메라·캐릭터. 메뉴 흐름은 게임모드 GM_InLobby(→ 로비 흐름 담당)가 한다.
# 다시 돌리면 L_Title 을 지우지 않고, 이 스크립트가 놓은 액터(이름표 Title_*)만 지우고 다시 놓는다.
# 실행: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<프로젝트>/Tools/level/build_title_level.py" -unattended -nosplash
import unreal

LEVEL = "/Game/PG/Level/L_Title"
BACKDROP = "/Game/PG/LevelDesign/Facilities/LD_Facility_RuralDiorama_2x2"
GAME_MODE = "/Game/PG/Blueprint/GM_InLobby.GM_InLobby_C"
CHARACTER = "/Game/PG/Blueprint/Characters/BP_CustomPlayerCharacter.BP_CustomPlayerCharacter_C"

# 자리(cm). 흙길(x≈50) 위에 캐릭터, 그 앞(+x)에 카메라.
# 화면 왼쪽/오른쪽: 카메라 방향(yaw)보다 각도가 작은 쪽이 화면 왼쪽. 캐릭터가 카메라 방향에서 10도쯤 왼쪽에 오게 yaw 를 정한다
# (오른쪽 아래에 로비 메뉴가 뜨므로). 10/4 첫 시도는 이걸 거꾸로 계산해 캐릭터가 화면 밖(35도 왼쪽)에 있었다.
CHARACTER_AT = unreal.Vector(60, 40, 160)
CHARACTER_YAW = -20.0          # 카메라 쪽(+x)을 조금 비껴 본다
CAMERA_AT = unreal.Vector(430, -150, 150)
CAMERA_ROT = unreal.Rotator(roll=0, pitch=-6, yaw=163)
CAMERA_FOV = 60.0
# 캐릭터 화면용 카메라: 캐릭터 정면 3.3m 에서, 캐릭터가 화면 왼쪽 장비 칸 가운데(가로 1/6쯤 = 카메라 방향에서 21도 왼쪽)에 오게.
# 캐릭터 방향(-20도) 앞으로 3.3m → (370,-72). 거기서 캐릭터를 보는 방향은 160도 → 21도 왼쪽에 두려면 카메라 방향 181도.
CHARACTER_CAMERA_AT = unreal.Vector(370, -72, 140)
CHARACTER_CAMERA_ROT = unreal.Rotator(roll=0, pitch=-4, yaw=181)
# 카메라 이름표: 로비 흐름(ULobbyUIFlowController::FocusCamera)이 이 이름으로 찾아 바꾼다.
MENU_CAMERA_TAG = "LobbyCamera_Menu"
CHARACTER_CAMERA_TAG = "LobbyCamera_Character"

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EAL = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("[TITLE] " + msg)


if EAL.does_asset_exist(LEVEL):
    les.load_level(LEVEL)
else:
    les.new_level(LEVEL)
world = unreal.EditorLevelLibrary.get_editor_world()

# 지난번에 놓은 것 지우기
for actor in eas.get_all_level_actors():
    if actor.get_actor_label().startswith("Title_"):
        eas.destroy_actor(actor)
# 다시 돌릴 때도 마을 레벨이 바뀌지 않게, 저장 직전에 마을 레벨이 더러워졌는지 본다(아래 끝).

# 배경: 호숫가 마을을 항상 불러오는 하위 레벨로 붙인다(복사하지 않으니 마을을 고치면 타이틀도 같이 바뀐다).
streaming = [lv.get_outer().get_path_name() for lv in unreal.EditorLevelUtils.get_levels(world)]
if not any(BACKDROP in s for s in streaming):
    unreal.EditorLevelUtils.add_level_to_world(world, BACKDROP, unreal.LevelStreamingAlwaysLoaded)
    log("backdrop added")
# 주의: 하위 레벨을 붙이면 "지금 고치는 레벨" 이 그 하위 레벨(호숫가 마을)로 바뀐다.
#       그대로 액터를 놓고 저장하면 마을 레벨이 바뀐다(10/4 실제로 한 번 겪음) → 타이틀 레벨로 되돌린다.
les.set_current_level_by_name("L_Title")
current = unreal.EditorLevelUtils.get_levels(world)
log("current level set back to L_Title")


def spawn(cls, label, location, rotation=unreal.Rotator()):
    actor = eas.spawn_actor_from_class(cls, location, rotation)
    actor.set_actor_label(label)
    return actor


# 조명: 해(약간 낮은 오후 해), 하늘빛, 대기, 안개.
sun = spawn(unreal.DirectionalLight, "Title_Sun", unreal.Vector(0, 0, 800), unreal.Rotator(roll=0, pitch=-28, yaw=215))
sun_comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
sun_comp.set_editor_property("intensity", 8.0)
sun_comp.set_editor_property("atmosphere_sun_light", True)
sky = spawn(unreal.SkyLight, "Title_SkyLight", unreal.Vector(0, 0, 600))
sky_comp = sky.get_component_by_class(unreal.SkyLightComponent)
sky_comp.set_editor_property("real_time_capture", True)
sky_comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
spawn(unreal.SkyAtmosphere, "Title_Atmosphere", unreal.Vector(0, 0, 0))
fog = spawn(unreal.ExponentialHeightFog, "Title_Fog", unreal.Vector(0, 0, 100))
fog.get_component_by_class(unreal.ExponentialHeightFogComponent).set_editor_property("fog_density", 0.02)
spawn(unreal.VolumetricCloud, "Title_Clouds", unreal.Vector(0, 0, 0))

# 카메라: 플레이어 0 이 처음부터 이 카메라로 본다.
camera = spawn(unreal.CameraActor, "Title_Camera", CAMERA_AT, CAMERA_ROT)
camera.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
camera.get_component_by_class(unreal.CameraComponent).set_editor_property("field_of_view", CAMERA_FOV)
camera.set_editor_property("tags", [MENU_CAMERA_TAG])
# 캐릭터 화면 카메라: 처음엔 안 쓰고, 캐릭터 화면이 열릴 때 로비 흐름이 이쪽으로 옮겨 간다.
character_camera = spawn(unreal.CameraActor, "Title_CharacterCamera", CHARACTER_CAMERA_AT, CHARACTER_CAMERA_ROT)
character_camera.get_component_by_class(unreal.CameraComponent).set_editor_property("field_of_view", CAMERA_FOV)
character_camera.set_editor_property("tags", [CHARACTER_CAMERA_TAG])

# 캐릭터: 팀 캐릭터 BP 를 그대로 세운다(모습·서 있는 동작은 캐릭터 BP·애니 BP 그대로).
character_class = unreal.load_class(None, CHARACTER)
spawn(character_class, "Title_Character", CHARACTER_AT, unreal.Rotator(roll=0, pitch=0, yaw=CHARACTER_YAW))

# 플레이어 시작 자리: 카메라 뒤(화면 밖).
spawn(unreal.PlayerStart, "Title_PlayerStart", unreal.Vector(900, -150, 200))

# 게임모드: 로비 게임모드(로비 흐름 → 시작 짐 → 로비 메뉴).
settings = world.get_world_settings()
settings.set_editor_property("default_game_mode", unreal.load_class(None, GAME_MODE))

# 놓은 액터가 모두 타이틀 레벨(L_Title)에 들어갔는지 확인하고 그 레벨만 저장한다.
for actor in eas.get_all_level_actors():
    if actor.get_actor_label().startswith("Title_"):
        outer = actor.get_outer().get_outer().get_path_name()
        if "L_Title" not in outer:
            raise RuntimeError("%s went into %s - not saving" % (actor.get_actor_label(), outer))
les.save_current_level()
log("saved %s (actors=%d)" % (LEVEL, len(eas.get_all_level_actors())))
