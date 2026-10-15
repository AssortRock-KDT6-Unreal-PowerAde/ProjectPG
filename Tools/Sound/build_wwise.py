# WAAPI 로 남은 소리를 Wwise 프로젝트에 만든다. 여러 번 돌려도 같은 결과가 되도록 merge/replace 사용.
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from waapi import call

C = r"D:\PG_Candidates"

def obj(path, props=None):
    try:
        r = call("ak.wwise.core.object.get", {"waql": f'$ "{path}"'}, {"return": ["id", "path"] + (props or [])})["return"]
    except RuntimeError as e:
        if "Object not found" in str(e):
            return None
        raise
    return r[0] if r else None

def ensure(parent, typ, name, props=None):
    p = obj(parent + "\\" + name)
    if p:
        return p["id"]
    args = {"parent": parent, "type": typ, "name": name, "onNameConflict": "merge"}
    args.update(props or {})
    return call("ak.wwise.core.object.create", args)["id"]

def setp(o, k, v):
    call("ak.wwise.core.object.setProperty", {"object": o, "property": k, "value": v})

def setref(o, k, v):
    call("ak.wwise.core.object.setReference", {"object": o, "reference": k, "value": v})

# ---- 거리 감쇠 (단위: 언리얼 cm) ----
def attenuation(name, radius):
    a = ensure("\\Attenuations\\Default Work Unit", "Attenuation", name, {"@RadiusMax": radius})
    setp(a, "RadiusMax", radius)
    call("ak.wwise.core.object.setAttenuationCurve", {
        "object": a, "curveType": "VolumeDryUsage", "use": "Custom",
        "points": [
            {"x": 0, "y": 0, "shape": "Linear"},
            {"x": radius * 0.15, "y": -6, "shape": "Linear"},
            {"x": radius * 0.5, "y": -20, "shape": "Linear"},
            {"x": radius, "y": -200, "shape": "Linear"},
        ]})
    return a

ATT = {
    "Weapon": attenuation("Att_Weapon", 15000),
    "Foley": attenuation("Att_Foley", 2500),
    "World": attenuation("Att_World", 2000),
}

def make_3d(o, att):
    setp(o, "OverridePositioning", True)
    setp(o, "3DSpatialization", 1)
    setp(o, "EnableAttenuation", True)
    setref(o, "Attenuation", ATT[att])

# ---- 소리 오브젝트 ----
for wu in ["Weapon", "Player", "World"]:
    ensure("\\Containers", "WorkUnit", wu)

def import_files(target_path, files):
    imports = [{"audioFile": os.path.join(C, f),
                "objectPath": target_path + "\\<Sound>" + os.path.splitext(os.path.basename(f))[0].replace(",", "_")[:60],
                "originalsSubFolder": "SFX"} for f in files]
    call("ak.wwise.core.audio.import", {"importOperation": "useExisting", "default": {"importLanguage": "SFX"}, "imports": imports})

def single(wu, name, f, att, volume=0):
    # 소리 하나짜리: Sound 이름을 직접 지정
    call("ak.wwise.core.audio.import", {"importOperation": "useExisting", "default": {"importLanguage": "SFX"},
         "imports": [{"audioFile": os.path.join(C, f), "objectPath": f"\\Containers\\{wu}\\<Sound>{name}", "originalsSubFolder": "SFX"}]})
    o = obj(f"\\Containers\\{wu}\\{name}")["id"]
    setp(o, "Volume", volume)
    make_3d(o, att)
    return o

def random(wu, name, files, att, volume=0, pitch=100):
    o = ensure(f"\\Containers\\{wu}", "RandomSequenceContainer", name)
    setp(o, "RandomOrSequence", 1)
    setp(o, "RandomAvoidRepeating", True)
    setp(o, "RandomAvoidRepeatingCount", 1)
    import_files(f"\\Containers\\{wu}\\{name}", files)
    setp(o, "Volume", volume)
    make_3d(o, att)
    call("ak.wwise.core.object.setRandomizer", {"object": o, "property": "Pitch", "enabled": True, "min": -pitch, "max": pitch})
    return o

grass = [f"Footstep_Grass\\K_footstep_grass_00{i}.wav" for i in range(5)]
land = [f"Land\\K_impactSoft_heavy_00{i}.wav" for i in range(5)]

targets = {
    "Weapon_Equip": single("Weapon", "Weapon_Equip", r"Container_Open\S_Latchlocker,cable,steel,wood,room,channel,slide,catch,alt2.wav", "Foley", -4),
    "Weapon_Reload_MagOut": single("Weapon", "Weapon_Reload_MagOut", r"Container_Open\K_metalLatch.wav", "Foley", -2),
    "Weapon_Reload_MagIn": single("Weapon", "Weapon_Reload_MagIn", r"Container_Open\S_Latchlocker,aluminum,link,jostle,slide,click,out,over.wav", "Foley", -2),
    "Player_Footstep": random("Player", "Player_Footstep", grass, "Foley", -6),
    "Player_Footstep_Crouch": random("Player", "Player_Footstep_Crouch", grass, "Foley", -14),
    "Player_Land": random("Player", "Player_Land", land, "Foley", -3),
    "World_Container_Open": single("World", "World_Container_Open", r"Container_Open\S_Latchlocker,design,FutureLoot,score,plunder.wav", "World", -3),
}

# 기존 총소리에도 거리 감쇠
for n in ["Weapon_Fire_Rifle", "Weapon_Fire_Pistol", "Weapon_Fire_Shotgun"]:
    make_3d(obj(f"\\Containers\\Weapon\\{n}")["id"], "Weapon")

# ---- 이벤트 ----
for n, o in targets.items():
    ev = "Play_" + n
    if not obj(f"\\Events\\Default Work Unit\\{ev}"):
        call("ak.wwise.core.object.create", {"parent": "\\Events\\Default Work Unit", "type": "Event", "name": ev,
             "children": [{"type": "Action", "name": "", "@ActionType": 1, "@Target": o}]})

# ---- 뱅크 ----
banks = {"Weapon": ["Weapon_Equip", "Weapon_Reload_MagOut", "Weapon_Reload_MagIn"],
         "Player": ["Player_Footstep", "Player_Footstep_Crouch", "Player_Land"],
         "World": ["World_Container_Open"]}
for b, names in banks.items():
    ensure("\\SoundBanks\\Default Work Unit", "SoundBank", b)
    call("ak.wwise.core.soundbank.setInclusions", {"soundbank": f"\\SoundBanks\\Default Work Unit\\{b}", "operation": "add",
         "inclusions": [{"object": f"\\Events\\Default Work Unit\\Play_{n}", "filter": ["events", "structures", "media"]} for n in names]})

call("ak.wwise.core.project.save", {})
r = call("ak.wwise.core.soundbank.generate", {"soundbanks": [{"name": b} for b in ["UI", "Weapon", "Player", "World"]], "platforms": ["Windows"], "writeToDisk": True})
print("generate:", {k: v for k, v in r.items() if k != "logs"})
for l in r.get("logs", []):
    if l.get("severity") in ("Error", "Fatal Error", "Warning"):
        print(l)
print("done")
