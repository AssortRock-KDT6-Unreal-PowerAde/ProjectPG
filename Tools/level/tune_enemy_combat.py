# 몬스터 전투 숫자 맞추기 (2026-10-19 사용자 시험: "영문도 모른 채 맞고 죽었다, 외침도 못 들었다")
# 무엇을:
#   - EnemyTable 의 Enemy_Rifle 줄: 감지 거리 40m, 사거리 25m, 명중률 0.45
#   - ATT_EnemyVoice(몬스터 목소리 거리 감쇠): 5m 까지 그대로, 50m 에서 0 → 감지 거리(40m)에서 외쳐도 들림
# 실행(에디터 끈 상태, 메인 폴더):
#   UnrealEditor-Cmd.exe <프로젝트>/ProjectPG.uproject -run=pythonscript -script="<프로젝트>/Tools/level/tune_enemy_combat.py" -unattended -nosplash -nullrhi
# 결과: 로그의 [EnemyTune] 줄
import json
import unreal

TABLE = "/Game/PG/Table/EnemyTable"
ROW = "Enemy_Rifle"
ROW_VALUES = {"DetectionRange": 4000, "AttackRange": 2500, "Accuracy": 0.45}
ATTENUATION = "/Game/PG/Sound/EnemyVoice/ATT_EnemyVoice"
VOICE_INNER_RADIUS_CM = 500.0
VOICE_FALLOFF_CM = 4500.0


def log(msg):
    unreal.log("[EnemyTune] " + msg)


def tune_table():
    table = unreal.load_asset(TABLE)
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    for row in rows:
        if row.get("Name") == ROW:
            log("before " + json.dumps(row))
            row.update(ROW_VALUES)
            log("after  " + json.dumps(row))
    ok = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows))
    saved = unreal.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False)
    log("table fill=%s saved=%s" % (ok, saved))


def tune_attenuation():
    att = unreal.load_asset(ATTENUATION)
    settings = att.get_editor_property("attenuation")
    log("voice before: shape_extents=%s falloff=%s" % (settings.get_editor_property("attenuation_shape_extents"), settings.get_editor_property("falloff_distance")))
    settings.set_editor_property("attenuation_shape", unreal.AttenuationShape.SPHERE)
    settings.set_editor_property("attenuation_shape_extents", unreal.Vector(VOICE_INNER_RADIUS_CM, 0.0, 0.0))
    settings.set_editor_property("falloff_distance", VOICE_FALLOFF_CM)
    att.set_editor_property("attenuation", settings)
    saved = unreal.EditorAssetLibrary.save_loaded_asset(att, only_if_is_dirty=False)
    log("voice after: inner=%.0f falloff=%.0f saved=%s" % (VOICE_INNER_RADIUS_CM, VOICE_FALLOFF_CM, saved))


tune_table()
tune_attenuation()
