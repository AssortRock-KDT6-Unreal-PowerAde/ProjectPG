# 재장전·총 꺼내기 몽타주, 발소리·착지 애니메이션에 AnimNotify_AkEvent 를 꽂는다.
# 이미 같은 이벤트 노티파이가 있으면 건너뛴다. DRY = True 면 계산만 출력.
import unreal

DRY = True
EV = "/Game/WwiseAudio/Events/Default_Work_Unit/"
A = "/Game/PG/Animations/"
TRACK = "Sound"

notify_class = unreal.load_asset("/Wwise/AnimNotify_AkEvent").generated_class()

def ev(name):
    e = unreal.load_asset(EV + name)
    if not e:
        raise RuntimeError("이벤트 없음: " + name)
    return e

def has_event(anim, event):
    for n in unreal.AnimationLibrary.get_animation_notify_events(anim):
        if n.notify and n.notify.get_class() == notify_class and n.notify.get_editor_property("event") == event:
            return True
    return False

def add(anim, event, times):
    if has_event(anim, event):
        unreal.log(f"[소리] 이미 있음: {anim.get_path_name()} {event.get_name()}")
        return False
    unreal.log(f"[소리] {anim.get_path_name()} {event.get_name()} @ {[round(t, 3) for t in times]}")
    if DRY:
        return False
    if TRACK not in [str(t) for t in unreal.AnimationLibrary.get_animation_notify_track_names(anim)]:
        unreal.AnimationLibrary.add_animation_notify_track(anim, TRACK, unreal.LinearColor(0.2, 0.6, 1.0, 1.0))
    for t in times:
        n = unreal.AnimationLibrary.add_animation_notify_event(anim, TRACK, t, notify_class)
        n.set_editor_property("event", event)
        n.set_editor_property("follow", False)
    return True

def length(anim):
    return anim.get_play_length()

def foot_contacts(anim):
    """발 뼈 높이가 가장 낮아지는 순간(발이 땅에 닿는 순간)을 찾는다."""
    L = length(anim)
    opts = unreal.AnimPoseEvaluationOptions()
    step = 1.0 / 60.0
    times = [i * step for i in range(int(L / step) + 1)]
    result = []
    for bone in ("foot_l", "foot_r"):
        zs = []
        for t in times:
            pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts)
            zs.append(unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD).translation.z)
        lo, hi = min(zs), max(zs)
        th = lo + (hi - lo) * 0.25
        n = len(zs)
        # 한 바퀴 도는 애니메이션이라 앞뒤를 이어서 본다. 땅 근처로 내려오는 순간(내려가다 멈추는 첫 프레임)
        for i in range(n):
            prev, cur = zs[i - 1], zs[i]
            if cur <= th and prev > th:
                result.append(times[i])
    result = sorted(result)
    out = []
    for t in result:
        if not out or t - out[-1] > 0.15:
            out.append(min(t, L - 0.01))
    return out

def load(p):
    a = unreal.load_asset(p)
    if not a:
        raise RuntimeError("애니 없음: " + p)
    return a

changed = []
def mark(a, did):
    if did:
        changed.append(a)

# 1) 총 꺼내기 / 재장전 몽타주
for folder in ("Montages/", "Meta/Montages/"):
    for w in ("Pistol", "Rifle", "Shotgun"):
        a = load(A + folder + "AM_Equip_" + w)
        mark(a, add(a, ev("Play_Weapon_Equip"), [length(a) * 0.3]))
    for w in ("Pistol", "Rifle_Hip", "Rifle_Ironsights", "Shotgun_Hip", "Shotgun_Ironsights"):
        a = load(A + folder + "AM_Reload_" + w)
        L = length(a)
        mark(a, add(a, ev("Play_Weapon_Reload_MagOut"), [L * 0.25]))
        mark(a, add(a, ev("Play_Weapon_Reload_MagIn"), [L * 0.6]))

# 2) 발소리
S = A + "Sequences/"
walk = [S + "Rifle/AS_Jog_%s_Rifle" % d for d in ("Fwd", "Bwd", "Lt", "Rt")] \
     + [S + "Rifle/AS_Walk_%s_Rifle_Ironsights" % d for d in ("Fwd", "Bwd", "Lt", "Rt")] \
     + [S + "Rifle/AS_Sprint_Fwd_Rifle", S + "Pistol/AS_Sprint_Fwd_Pistol"] \
     + [S + "Pistol/AS_Walk_%s_Pistol" % d for d in ("Fwd", "Bwd", "Lt", "Rt")] \
     + [S + "Pistol/AS_Jog_%s_Pistol" % d for d in ("Fwd", "Bwd", "Lt", "Rt")]
crouch = [S + "Rifle/AS_Crouch_Walk_%s_Rifle_%s" % (d, s) for d in ("Fwd", "Bwd", "Lt", "Rt") for s in ("Hip", "Ironsights")] \
       + [S + "Pistol/AS_Crouch_Walk_%s_Pistol" % d for d in ("Fwd", "Bwd", "Lt", "Rt")]
for p in walk:
    a = load(p)
    mark(a, add(a, ev("Play_Player_Footstep"), foot_contacts(a)))
for p in crouch:
    a = load(p)
    mark(a, add(a, ev("Play_Player_Footstep_Crouch"), foot_contacts(a)))

# 3) 착지
for p in (S + "AS_Jump_From_Jog_Land", S + "AS_Jump_From_Stand_Land", S + "AS_Jump_From_Stand_Ironsights_Land",
          S + "Pistol/AS_Jump_Land_Jog_Pistol", S + "Pistol/AS_Jump_Land_Stand_Pistol"):
    a = load(p)
    mark(a, add(a, ev("Play_Player_Land"), [0.02]))

for a in changed:
    unreal.EditorAssetLibrary.save_loaded_asset(a, False)
unreal.log(f"[소리] 저장 {len(changed)}개 (DRY={DRY})")
