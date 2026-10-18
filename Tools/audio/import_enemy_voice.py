# 몬스터 목소리(외침·비명·죽는 소리)를 가져와 DA_EnemyAI_Rifle 의 Voice 칸에 넣는다. (2026-10-18)
# 무엇을: 폴더 안의 wav 를 /Game/PG/Sound/EnemyVoice 로 가져오고, 거리 감쇠 에셋(ATT_EnemyVoice)을 만들고,
#         파일 이름으로 종류를 나눠 데이터 에셋 목록에 채운다.
#   - SFX_EnemyVoice_Spotted_*.wav → TargetSpottedSounds (플레이어 발견 외침)
#   - SFX_EnemyVoice_Hurt_*.wav    → HurtSounds (맞을 때)
#   - SFX_EnemyVoice_Death_*.wav   → DeathSounds (죽을 때)
# 소리를 바꿀 땐: 같은 이름 규칙으로 wav 를 새로 만들어 다시 돌리면 덮어쓴다(목록도 폴더 내용대로 다시 채움).
# 지금 들어간 소리는 Sonniss GDC 무료 묶음에서 고른 임시본. 나중에 직접 녹음 + 목소리 바꾸기(RVC)로 교체 예정.
# 실행: UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<프로젝트>/Tools/audio/import_enemy_voice.py <wav 폴더>" -unattended -nosplash -nullrhi
import os
import sys
import unreal

DEST = "/Game/PG/Sound/EnemyVoice"
ATTENUATION = DEST + "/ATT_EnemyVoice"
AI_CONFIG = "/Game/PG/AI/DA_EnemyAI_Rifle"
# 거리 감쇠: 이 안(cm)에선 원래 크기, 그 뒤 FALLOFF(cm)에 걸쳐 0 까지 작아짐.
# 외침이 들리는 거리 ≈ 동료를 부르는 거리(AIConfig.CalloutRange 2500)보다 조금 넓게 → 플레이어도 "부르는 소리"를 듣는다.
INNER_RADIUS = 400.0
FALLOFF = 3600.0
KINDS = {
    "Spotted": "target_spotted_sounds",
    "Hurt": "hurt_sounds",
    "Death": "death_sounds",
}

EAL = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("[VOICE] " + msg)


def fail(msg):
    unreal.log_error("[VOICE] " + msg)
    unreal.SystemLibrary.quit_editor()
    raise SystemExit(1)


def import_wavs(folder):
    wavs = sorted(f for f in os.listdir(folder) if f.lower().endswith(".wav") and f.startswith("SFX_EnemyVoice_"))
    if not wavs:
        fail("가져올 wav 가 없음: " + folder)
    tasks = []
    for f in wavs:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(folder, f))
        task.set_editor_property("destination_path", DEST)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", True)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    names = [os.path.splitext(f)[0] for f in wavs]
    for n in names:
        if not EAL.does_asset_exist(DEST + "/" + n):
            fail("가져오기 실패: " + n)
    log("가져옴 %d개" % len(names))
    return names


def make_attenuation():
    if EAL.does_asset_exist(ATTENUATION):
        asset = unreal.load_asset(ATTENUATION)
    else:
        folder, name = ATTENUATION.rsplit("/", 1)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.SoundAttenuation, None)
    settings = asset.get_editor_property("attenuation")
    settings.set_editor_property("attenuation_shape", unreal.AttenuationShape.SPHERE)
    settings.set_editor_property("attenuation_shape_extents", unreal.Vector(INNER_RADIUS, 0, 0))
    settings.set_editor_property("falloff_distance", FALLOFF)
    asset.set_editor_property("attenuation", settings)
    EAL.save_loaded_asset(asset)
    log("거리 감쇠 준비: 안쪽 %.0f + 줄어드는 거리 %.0f" % (INNER_RADIUS, FALLOFF))
    return asset


def fill_config(names, attenuation):
    config = unreal.load_asset(AI_CONFIG)
    if not config:
        fail("AI 설정 에셋이 없음: " + AI_CONFIG)
    for kind, prop in KINDS.items():
        sounds = [unreal.load_asset(DEST + "/" + n) for n in names if n.startswith("SFX_EnemyVoice_%s_" % kind)]
        if not sounds:
            unreal.log_warning("[VOICE] %s 소리가 없음 → 그 목소리는 안 남" % kind)
        config.set_editor_property(prop, sounds)
        log("%s: %d개" % (kind, len(sounds)))
    config.set_editor_property("voice_attenuation", attenuation)
    EAL.save_loaded_asset(config)
    log("저장: " + AI_CONFIG)


if len(sys.argv) < 2 or not os.path.isdir(sys.argv[1]):
    fail("wav 폴더를 인자로 주세요")
fill_config(import_wavs(sys.argv[1]), make_attenuation())
unreal.SystemLibrary.quit_editor()
