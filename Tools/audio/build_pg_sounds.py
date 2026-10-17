# 게임 소리 에셋을 한 번에 만든다(가져오기 → 감쇠 → 물리 재질 → 메타사운드 → 소리 표).
# 설명: Source/ProjectPG/Public/Audio/README.md
#
# 돌리는 법(에디터 없이):
#   UnrealEditor-Cmd.exe ProjectPG.uproject -run=pythonscript -script="Tools/audio/build_pg_sounds.py" -EnablePlugins=PythonScriptPlugin -unattended -nullrhi
#   소리 원본 폴더는 환경 변수 PG_SOUND_SRC (기본 E:\SoundSources\PG_Processed).
# 다시 돌려도 됨: 있는 에셋은 덮어쓰고, 소리 표는 매번 새로 채운다(손으로 바꾼 값은 사라짐 → 손으로 고칠 거면 이 스크립트를 고칠 것).
import os
import unreal

SRC = os.environ.get("PG_SOUND_SRC", r"E:\SoundSources\PG_Processed")
ROOT = "/Game/PG/Audio"
WAVES = ROOT + "/Waves"
MS = ROOT + "/MetaSounds"
ATT = ROOT + "/Attenuation"
PM = ROOT + "/PhysMats"

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
log = unreal.log
errors = []


def fail(msg):
    errors.append(msg)
    unreal.log_error("[PGSound] " + msg)


# ---------- 1) wav 가져오기: 원본 하위 폴더 이름 그대로 Waves/<폴더> ----------
def import_folder(folder):
    src_dir = os.path.join(SRC, folder)
    if not os.path.isdir(src_dir):
        fail("원본 폴더 없음: " + src_dir)
        return []
    tasks = []
    for name in sorted(os.listdir(src_dir)):
        if not name.lower().endswith(".wav"):
            continue
        # 이미 가져온 녹음은 건너뜀(다시 가져오면 내용이 같아도 파일이 새로 저장돼 바뀐 것으로 잡힘).
        # 원본 녹음을 고쳤으면 그 에셋을 지우고 다시 돌릴 것.
        if assets.does_asset_exist(WAVES + "/" + folder + "/" + os.path.splitext(name)[0]):
            continue
        t = unreal.AssetImportTask()
        t.filename = os.path.join(src_dir, name)
        t.destination_path = WAVES + "/" + folder
        t.automated = True
        t.replace_existing = True
        t.save = True
        tasks.append(t)
    tools.import_asset_tasks(tasks)
    return load_waves(folder)


def load_waves(folder, contains=None, exclude=None):
    out = []
    for path in sorted(assets.list_assets(WAVES + "/" + folder, recursive=False)):
        a = assets.load_asset(path.split(".")[0])
        if not isinstance(a, unreal.SoundWave):
            continue
        n = a.get_name()
        if contains and not any(c in n for c in contains):
            continue
        if exclude and any(x in n for x in exclude):
            continue
        out.append(a)
    if not out:
        fail("녹음이 하나도 없음: %s %s" % (folder, contains or ""))
    return out


# ---------- 2) 감쇠: 거리 → 크기 + 멀수록 고음 깎기 ----------
def make_attenuation(name, inner, falloff, lpf_far_hz):
    path = ATT + "/" + name
    a = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(name, ATT, unreal.SoundAttenuation, None)
    s = a.get_editor_property("attenuation")
    s.set_editor_property("attenuate", True)
    s.set_editor_property("spatialize", True)
    s.set_editor_property("distance_algorithm", unreal.AttenuationDistanceModel.NATURAL_SOUND)
    s.set_editor_property("attenuation_shape", unreal.AttenuationShape.SPHERE)
    s.set_editor_property("attenuation_shape_extents", unreal.Vector(inner, 0, 0))
    s.set_editor_property("falloff_distance", falloff)
    s.set_editor_property("attenuate_with_lpf", True)
    s.set_editor_property("lpf_radius_min", inner)
    s.set_editor_property("lpf_radius_max", inner + falloff)
    s.set_editor_property("lpf_frequency_at_min", 20000.0)
    s.set_editor_property("lpf_frequency_at_max", lpf_far_hz)
    a.set_editor_property("attenuation", s)
    assets.save_loaded_asset(a)
    return a


# ---------- 3) 물리 재질 ----------
def make_physmat(name, surface):
    path = PM + "/" + name
    a = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(name, PM, unreal.PhysicalMaterial, unreal.PhysicalMaterialFactoryNew())
    a.set_editor_property("surface_type", surface)
    assets.save_loaded_asset(a)


# ---------- 4) 메타사운드 ----------
bs = unreal.get_engine_subsystem(unreal.MetaSoundBuilderSubsystem)
es = unreal.get_editor_subsystem(unreal.MetaSoundEditorSubsystem)


def cls(ns, name, var=""):
    c = unreal.MetasoundFrontendClassName()
    c.set_editor_property("namespace", ns)
    c.set_editor_property("name", name)
    c.set_editor_property("variant", var)
    return c


class Graph:
    def __init__(self, name, one_shot=True):
        r = bs.create_source_builder(name, unreal.MetaSoundOutputAudioFormat.MONO, one_shot)
        self.b, self.on_play, self.on_finished, self.outs = r[0], r[1], r[2], list(r[3])
        self.name = name

    def ok(self, r, what):
        res = r[-1] if isinstance(r, tuple) else r
        if "SUCCEEDED" not in str(res):
            fail("%s: %s 실패 (%s)" % (self.name, what, res))
        return r[0] if isinstance(r, tuple) else r

    def node(self, ns, name, var=""):
        return self.ok(self.b.add_node_by_class_name(cls(ns, name, var)), "노드 %s" % name)

    def pin_in(self, node, pin):
        return self.ok(self.b.find_node_input_by_name(node, pin), "입력칸 " + pin)

    def pin_out(self, node, pin):
        return self.ok(self.b.find_node_output_by_name(node, pin), "출력칸 " + pin)

    def link(self, out_handle, node, pin):
        self.ok(self.b.connect_nodes(out_handle, self.pin_in(node, pin)), "연결 → " + pin)

    def set(self, node, pin, value):
        lit = bs.create_float_meta_sound_literal(float(value))
        lit = lit[0] if isinstance(lit, tuple) else lit
        self.ok(self.b.set_node_input_default(self.pin_in(node, pin), lit), "값 " + pin)

    def input(self, name, data_type, default):
        if data_type == "Float":
            lit = bs.create_float_meta_sound_literal(float(default))
        else:
            lit = bs.create_object_meta_sound_literal(default)
        lit = lit[0] if isinstance(lit, tuple) else lit
        return self.ok(self.b.add_graph_input_node(name, data_type, lit), "그래프 입력 " + name)

    def interface_input(self, name):
        r = self.b.find_graph_input_node(name)
        node = r[0]
        return self.pin_out(node, name)

    def save(self):
        # 이미 있으면 덮어쓰기가 안 돼서(build_to_asset 실패) 지우고 새로 만든다. 소리 표는 아래에서 다시 채워 연결이 복구됨.
        if assets.does_asset_exist(MS + "/" + self.name):
            assets.delete_asset(MS + "/" + self.name)
        r = es.build_to_asset(self.b, "ProjectPG", self.name, MS)
        self.ok(r, "에셋 저장")
        a = assets.load_asset(MS + "/" + self.name)
        assets.save_loaded_asset(a)
        return a


def build_one_shot(preview):
    # 녹음 하나 틀고 끝. 어떤 녹음인지는 C++ 이 "Wave" 입력으로 넘김.
    g = Graph("MS_OneShot")
    wave = g.input("Wave", "WaveAsset", preview)  # 기본값 = 에디터 미리듣기용(게임에선 C++ 이 매번 덮어씀)
    wp = g.node("UE", "Wave Player", "Mono")
    g.link(g.on_play, wp, "Play")
    g.link(wave, wp, "Wave Asset")
    g.ok(g.b.connect_nodes(g.pin_out(wp, "Out Mono"), g.outs[0]), "출력 연결")
    g.ok(g.b.connect_nodes(g.pin_out(wp, "On Finished"), g.on_finished), "끝 연결")
    return g.save()


def build_gunshot(preview_near, preview_far):
    # 총소리 한 발 = 가까운 녹음 + 먼 녹음을 듣는 사람과의 거리로 섞음. 합성 소리는 넣지 않는다(녹음만 씀).
    # 거리는 감쇠 인터페이스가 듣는 사람마다 따로 넣어 줌 → 같은 총도 사람마다 다르게 들림.
    g = Graph("MS_Gunshot")
    g.ok(g.b.add_interface("UE.Attenuation"), "감쇠 인터페이스")
    dist = g.interface_input("UE.Attenuation.Distance")
    wave = g.input("Wave", "WaveAsset", preview_near)  # 기본값 = 에디터 미리듣기용
    far_wave = g.input("FarWave", "WaveAsset", preview_far)
    near_d = g.input("NearDistance", "Float", 1500.0)   # 이 거리까진 가까운 녹음만
    far_d = g.input("FarDistance", "Float", 8000.0)     # 이 거리부터 먼 녹음만

    near = g.node("UE", "Wave Player", "Mono")
    far = g.node("UE", "Wave Player", "Mono")
    for wp, w in ((near, wave), (far, far_wave)):
        g.link(g.on_play, wp, "Play")
        g.link(w, wp, "Wave Asset")

    # t = clamp((거리 - Near) / (Far - Near), 0, 1) → 먼 녹음 크기, 1 - t → 가까운 녹음 크기
    sub = g.node("UE", "Subtract", "Float"); g.link(dist, sub, "PrimaryOperand"); g.link(near_d, sub, "AdditionalOperands")
    span = g.node("UE", "Subtract", "Float"); g.link(far_d, span, "PrimaryOperand"); g.link(near_d, span, "AdditionalOperands")
    div = g.node("UE", "Divide", "Float"); g.link(g.pin_out(sub, "Out"), div, "PrimaryOperand"); g.link(g.pin_out(span, "Out"), div, "AdditionalOperands")
    clamp = g.node("Clamp", "Clamp", "Float"); g.link(g.pin_out(div, "Out"), clamp, "In"); g.set(clamp, "Min", 0); g.set(clamp, "Max", 1)
    t = g.pin_out(clamp, "Value")
    inv = g.node("UE", "Subtract", "Float"); g.set(inv, "PrimaryOperand", 1); g.link(t, inv, "AdditionalOperands")
    near_gain = g.pin_out(inv, "Out")

    layers = g.node("AudioMixer", "Audio Mixer (Mono, 2)")
    g.link(g.pin_out(near, "Out Mono"), layers, "In 0"); g.link(near_gain, layers, "Gain 0")
    g.link(g.pin_out(far, "Out Mono"), layers, "In 1"); g.link(t, layers, "Gain 1")

    g.ok(g.b.connect_nodes(g.pin_out(layers, "Out"), g.outs[0]), "출력 연결")

    # 녹음 둘 중 긴 쪽이 끝날 때까지 들리게 3초 뒤에 끝냄(녹음은 3초 이하로 잘라 둠).
    life = g.node("AD Envelope", "AD Envelope", "Audio"); g.link(g.on_play, life, "Trigger"); g.set(life, "Attack Time", 0); g.set(life, "Decay Time", 3.0)
    g.ok(g.b.connect_nodes(g.pin_out(life, "On Done"), g.on_finished), "끝 연결")
    return g.save()


# ---------- 5) 소리 표 ----------
def cue(sound, waves, att, volume=1.0, jitter=0.05, far=None, two_d=False, indoor=False):
    c = unreal.PGSoundCue()
    c.set_editor_property("sound", sound)
    c.set_editor_property("waves", waves)
    if far:
        c.set_editor_property("far_waves", far)
    if att:
        c.set_editor_property("attenuation", att)
    c.set_editor_property("volume", volume)
    c.set_editor_property("pitch_jitter", jitter)
    c.set_editor_property("b2d", two_d)
    c.set_editor_property("check_indoor", indoor)
    return c


def montage(path):
    return assets.load_asset(path) if assets.does_asset_exist(path) else None


def main():
    folders = ["Weapon_Fire", "Debris_Impact", "Footstep_Concrete", "Footstep_Grass", "Footstep_Metal", "Footstep_Soft",
               "Footstep_Wood", "Land", "UI_Announce", "Container_Open", "Door_Open", "Door_Close", "Explosion_Barrel",
               "Ground_Collapse", "Monster_Attack", "Monster_Death", "WigBeam_Fire", "WigBeam_Impact",
               "Finale_Arrival", "Finale_Dragon", "Finale_Hover", "Finale_Launch", "Finale_Victory", "Finale_Warning",
               "Impact_Concrete", "Impact_Dirt", "Impact_Metal", "Impact_Wood", "Impact_Flesh", "Hit_React", "Body_Fall",
               "UI_Click", "UI_Hover", "UI_Open", "UI_Close", "UI_Error", "Reload",
               "Gun_Rifle", "Gun_RifleFar", "Gun_Pistol", "Gun_PistolFar", "Gun_Shotgun", "Gun_ShotgunFar", "Gun_Reload", "Step_Dirt", "Step_Grass"]
    w = {f: import_folder(f) for f in folders}

    att_gun = make_attenuation("ATT_Gunshot", 600, 15000, 2500)
    att_impact = make_attenuation("ATT_Impact", 200, 4000, 3000)
    att_step = make_attenuation("ATT_Footstep", 150, 2500, 4000)
    att_body = make_attenuation("ATT_Body", 200, 2000, 4000)
    att_big = make_attenuation("ATT_Big", 1000, 30000, 1500)

    S = unreal.PhysicalSurface
    for name, s in (("PM_Concrete", S.SURFACE_TYPE1), ("PM_Metal", S.SURFACE_TYPE2), ("PM_Wood", S.SURFACE_TYPE3),
                    ("PM_Dirt", S.SURFACE_TYPE4), ("PM_Grass", S.SURFACE_TYPE5), ("PM_Flesh", S.SURFACE_TYPE6)):
        make_physmat(name, s)

    # 게임 총은 실총(AK·AR·권총·산탄총)이라 실제 녹음만 쓴다(미래형·기관포 녹음은 안 씀).
    fire = "Weapon_Fire"
    # 총 하나엔 녹음 한 계열만(여러 총을 섞으면 쏠 때마다 다른 총처럼 들림).
    # 베레타 근거리 녹음은 평균 -40dB 로 거의 무음이라 안 씀.
    rifle = load_waves("Gun_Rifle", contains=["M4_Shots"])
    rifle_far = w["Gun_RifleFar"]
    pistol = load_waves("Gun_Pistol", contains=["38_Plus_Power"])
    pistol_far = w["Gun_PistolFar"]
    shotgun = load_waves(fire, contains=["Saiga"], exclude=["TRANSIENT"])
    shotgun_far = w["Gun_ShotgunFar"]
    one = build_one_shot(w["UI_Click"][0])
    gun = build_gunshot(rifle[0], rifle_far[0])

    da_path = ROOT + "/DA_PGSounds"
    da = assets.load_asset(da_path) if assets.does_asset_exist(da_path) else tools.create_asset("DA_PGSounds", ROOT, unreal.PGSoundSet, unreal.DataAssetFactory())
    W = unreal.WeaponType
    da.set_editor_property("gunshots", {
        W.RIFLE: cue(gun, rifle, att_gun, 1.0, 0.03, rifle_far),
        W.SHOTGUN: cue(gun, shotgun, att_gun, 1.0, 0.03, shotgun_far),
        W.PISTOL: cue(gun, pistol, att_gun, 0.8, 0.03, pistol_far),
    })
    da.set_editor_property("bullet_impacts", {
        S.SURFACE_TYPE_DEFAULT: cue(one, w["Impact_Concrete"], att_impact, 0.7, 0.08),
        S.SURFACE_TYPE1: cue(one, w["Impact_Concrete"], att_impact, 0.7, 0.08),
        S.SURFACE_TYPE2: cue(one, w["Impact_Metal"], att_impact, 0.7, 0.08),
        S.SURFACE_TYPE3: cue(one, w["Impact_Wood"], att_impact, 0.7, 0.08),
        S.SURFACE_TYPE4: cue(one, w["Impact_Dirt"], att_impact, 0.7, 0.08),
        S.SURFACE_TYPE5: cue(one, w["Impact_Dirt"], att_impact, 0.5, 0.08),
        S.SURFACE_TYPE6: cue(one, w["Impact_Flesh"], att_impact, 0.8, 0.08),
    })
    da.set_editor_property("footsteps", {
        S.SURFACE_TYPE_DEFAULT: cue(one, w["Footstep_Concrete"], att_step, 0.5, 0.06),
        S.SURFACE_TYPE1: cue(one, w["Footstep_Concrete"], att_step, 0.5, 0.06),
        S.SURFACE_TYPE2: cue(one, w["Footstep_Metal"], att_step, 0.5, 0.06),
        S.SURFACE_TYPE3: cue(one, w["Footstep_Wood"], att_step, 0.5, 0.06),
        S.SURFACE_TYPE4: cue(one, w["Step_Dirt"], att_step, 0.5, 0.06),   # 흙·자갈(PMSFX)
        S.SURFACE_TYPE5: cue(one, w["Step_Grass"], att_step, 0.45, 0.06),  # 마른 풀(PMSFX)
    })
    da.set_editor_property("landing", cue(one, w["Land"], att_step, 0.7, 0.05))

    # 물리 재질이 없는 맵 메시(서드파티 팩)의 재질을 이름으로 짐작. 위에서부터 처음 맞는 것.
    rules = []
    for words, s in ((["grass", "shrub", "bush", "foliage", "leaf", "leaves", "hedge"], S.SURFACE_TYPE5),
                     (["dirt", "mud", "sand", "soil", "gravel", "ground", "terrain", "earth"], S.SURFACE_TYPE4),
                     (["metal", "steel", "iron", "alumin", "container", "pipe", "rail", "tank", "vehicle", "truck", "car_",
                       "sherman", "scaffold", "barrel", "locker", "vent", "duct", "fence_metal", "chain"], S.SURFACE_TYPE2),
                     (["wood", "plank", "crate", "pallet", "cabin", "log", "timber", "board", "door", "table", "chair", "shelf"], S.SURFACE_TYPE3),
                     (["concrete", "brick", "stone", "rock", "asphalt", "road", "cement", "tile", "wall", "floor", "pavement", "plaster", "cinder"], S.SURFACE_TYPE1)):
        for word in words:
            r = unreal.PGSurfaceKeyword()
            r.set_editor_property("keyword", word)
            r.set_editor_property("surface", s)
            rules.append(r)
    da.set_editor_property("surface_keywords", rules)
    da.set_editor_property("landscape_surface", S.SURFACE_TYPE4)

    reload_w = w["Gun_Reload"]  # 실제 장전 손잡이·탄창 소리
    cues = {
        "UI.Click": cue(one, w["UI_Click"], None, 0.5, 0.03, two_d=True),
        "UI.Hover": cue(one, w["UI_Hover"], None, 0.25, 0.03, two_d=True),
        "UI.Open": cue(one, w["UI_Open"], None, 0.5, 0.02, two_d=True),
        "UI.Close": cue(one, w["UI_Close"], None, 0.5, 0.02, two_d=True),
        "UI.Error": cue(one, w["UI_Error"], None, 0.5, 0.0, two_d=True),
        "UI.Announce": cue(one, w["UI_Announce"], None, 0.7, 0.0, two_d=True),
        "Reload": cue(one, reload_w, att_body, 0.6, 0.05),
        "Hit.React": cue(one, w["Hit_React"], att_body, 0.8, 0.06),
        "Death.Body": cue(one, w["Body_Fall"], att_body, 0.9, 0.05),
        # 아래는 아직 게임에 기능이 없어서 붙는 곳이 없다. 기능이 생기면 BP 에서 PlayPGSound(이름) 로 부른다.
        "Container.Open": cue(one, w["Container_Open"], att_body, 0.7),
        "Door.Open": cue(one, w["Door_Open"], att_body, 0.8),
        "Door.Close": cue(one, w["Door_Close"], att_body, 0.8),
        "Explosion.Barrel": cue(one, w["Explosion_Barrel"], att_big, 1.0, 0.04),
        "Ground.Collapse": cue(one, w["Ground_Collapse"], att_big, 1.0, 0.03),
        "Monster.Attack": cue(one, w["Monster_Attack"], att_body, 0.9, 0.06),
        "Monster.Death": cue(one, w["Monster_Death"], att_body, 1.0, 0.04),
        "WigBeam.Fire": cue(one, w["WigBeam_Fire"], att_gun, 0.9, 0.04),
        "WigBeam.Impact": cue(one, w["WigBeam_Impact"], att_impact, 0.8, 0.05),
        "Finale.Arrival": cue(one, w["Finale_Arrival"], att_big, 1.0, 0.0),
        "Finale.Dragon": cue(one, w["Finale_Dragon"], att_big, 1.0, 0.03),
        "Finale.Hover": cue(one, w["Finale_Hover"], att_big, 0.8, 0.0),
        "Finale.Launch": cue(one, w["Finale_Launch"], att_big, 1.0, 0.0),
        "Finale.Victory": cue(one, w["Finale_Victory"], None, 0.8, 0.0, two_d=True),
        "Finale.Warning": cue(one, w["Finale_Warning"], None, 0.7, 0.0, two_d=True),
    }
    da.set_editor_property("cues", cues)

    mont = []
    for base in ("/Game/PG/Animations/Montages", "/Game/PG/Animations/Meta/Montages"):
        for name, c, delay in [("AM_Reload_Pistol", "Reload", 0.1), ("AM_Reload_Rifle_Hip", "Reload", 0.1),
                               ("AM_Reload_Rifle_Ironsights", "Reload", 0.1), ("AM_Reload_Shotgun_Hip", "Reload", 0.1),
                               ("AM_Reload_Shotgun_Ironsights", "Reload", 0.1),
                               ("AM_Hit_React_1", "Hit.React", 0), ("AM_Hit_React_2", "Hit.React", 0),
                               ("AM_Hit_React_3", "Hit.React", 0), ("AM_Hit_React_4", "Hit.React", 0),
                               ("AM_Death_Hip", "Death.Body", 0.6), ("AM_Death_Ironsights", "Death.Body", 0.6)]:
            m = montage(base + "/" + name)
            if not m:
                continue
            e = unreal.PGMontageSound()
            e.set_editor_property("montage", m)
            e.set_editor_property("cue", c)
            e.set_editor_property("delay", delay)
            mont.append(e)
    if not mont:
        fail("몽타주를 하나도 못 찾음 → 장전·피격·죽음 소리가 안 남")
    da.set_editor_property("montage_sounds", mont)
    assets.save_loaded_asset(da)

    log("[PGSound] 끝. 몽타주 %d개 연결, 오류 %d개" % (len(mont), len(errors)))
    for e in errors:
        unreal.log_error("[PGSound] 오류: " + e)


main()
