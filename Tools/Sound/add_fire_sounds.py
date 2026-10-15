# 사격 몽타주 10개에 AnimNotify_AkEvent(총소리)를 꽂는 스크립트.
# 언리얼 에디터 Output Log 아래 입력칸을 Python으로 바꾸고:  py "이 파일 경로"
import unreal

EVENT_DIR = "/Game/WwiseAudio/Events/Default_Work_Unit/"
MONTAGES = {
    "AM_Fire_Pistol": "Play_Weapon_Fire_Pistol",
    "AM_Fire_Rifle_Hip": "Play_Weapon_Fire_Rifle",
    "AM_Fire_Rifle_Ironsights": "Play_Weapon_Fire_Rifle",
    "AM_Fire_Shotgun_Hip": "Play_Weapon_Fire_Shotgun",
    "AM_Fire_Shotgun_Ironsights": "Play_Weapon_Fire_Shotgun",
}
FOLDERS = ["/Game/PG/Animations/Montages/", "/Game/PG/Animations/Meta/Montages/"]
TRACK = "Sound"

_bp = unreal.load_asset("/Wwise/AnimNotify_AkEvent")
notify_class = _bp.generated_class() if _bp else None
if not notify_class:
    raise RuntimeError("AnimNotify_AkEvent 를 못 찾음 (Wwise 플러그인 확인)")

for folder in FOLDERS:
    for name, event_name in MONTAGES.items():
        path = folder + name
        montage = unreal.load_asset(path)
        event = unreal.load_asset(EVENT_DIR + event_name)
        if not montage or not event:
            unreal.log_warning(f"[총소리] 건너뜀: {path} / {event_name}")
            continue

        # 이미 꽂혀 있으면 다시 안 꽂는다
        already = [e for e in unreal.AnimationLibrary.get_animation_notify_events(montage)
                   if e.notify and e.notify.get_class() == notify_class]
        if already:
            unreal.log(f"[총소리] 이미 있음: {path}")
            continue

        if TRACK not in [str(t) for t in unreal.AnimationLibrary.get_animation_notify_track_names(montage)]:
            unreal.AnimationLibrary.add_animation_notify_track(montage, TRACK, unreal.LinearColor(0.2, 0.6, 1.0, 1.0))

        notify = unreal.AnimationLibrary.add_animation_notify_event(montage, TRACK, 0.0, notify_class)
        notify.set_editor_property("event", event)
        notify.set_editor_property("follow", False)  # 켜 두면 노란 경고 글자가 뜸

        unreal.EditorAssetLibrary.save_loaded_asset(montage, False)
        unreal.log(f"[총소리] 추가: {path} <- {event_name}")

# 확인용: 결과 출력
for folder in FOLDERS:
    for name in MONTAGES:
        m = unreal.load_asset(folder + name)
        for e in unreal.AnimationLibrary.get_animation_notify_events(m):
            if e.notify and e.notify.get_class() == notify_class:
                ev = e.notify.get_editor_property("event")
                unreal.log(f"[총소리확인] {folder+name} event={ev.get_name() if ev else None} follow={e.notify.get_editor_property('follow')}")
