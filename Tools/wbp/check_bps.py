# /Game/PG 아래 블루프린트(위젯 포함)를 전부 다시 컴파일해 오류를 모아 찍는다. (2026-10-04)
# C++ 클래스·enum 을 지우거나 이름을 바꾼 뒤, BP 쪽에 깨진 곳이 없는지 확인하는 용도.
# 결과 줄: "[CHECKBP] total=.. failed=.." + 실패한 에셋 이름.
import json
import unreal

registry = unreal.AssetRegistryHelpers.get_asset_registry()
assets = registry.get_assets_by_path("/Game/PG", recursive=True)
failed = []
total = 0
for data in assets:
    cls = str(data.asset_class_path.asset_name)
    if cls not in ("Blueprint", "WidgetBlueprint", "AnimBlueprint"):
        continue
    path = str(data.package_name)
    if "/LevelDesign/Tiles/" in path:
        continue  # 타일 BP 수백 개는 C++ 지운 것과 상관없음 — 시간만 걸린다
    bp = unreal.load_asset(path)
    if bp is None:
        failed.append(path + " (load)")
        continue
    total += 1
    result = unreal.MCPythonHelper.compile_blueprint(bp)
    try:
        info = json.loads(result)
    except Exception:
        info = {"raw": result}
    ok = info.get("success", False) and not info.get("errors") and info.get("status", "") not in ("Error", "BS_Error")
    if not ok:
        failed.append("%s %s" % (path, result.replace("\n", " ")[:300]))
lines = ["[CHECKBP] total=%d failed=%d" % (total, len(failed))] + ["[CHECKBP] FAIL " + f for f in failed]
for line in lines:
    unreal.log(line)
# 명령줄 실행에서는 로그 줄이 안 보일 때가 있어 파일에도 남긴다(Saved/CHECKBP.log, 매번 새로).
with open(unreal.Paths.project_saved_dir() + "CHECKBP.log", "w", encoding="utf-8") as out:
    out.write("\n".join(lines) + "\n")
