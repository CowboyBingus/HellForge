"""Export HD2R Editor's preset keys (its row keys) with the native bytes each edits, for HellForge's preset import and
export. Metadata only: keys, field ids, value kinds and addresses (record ids and offsets); no game bytes.

Usage: py scripts/ExportHd2rKeys.py <HD2Runtime> mappings/hd2r-keys.json

The HD2R Editor (1.0.0, for HD2Runtime 0.40) keys a row '<family>|<object>|[<part>|]<row id>' (editor/catalog.lua);
this rebuilds those keys from HD2Runtime's own generated domain tables (domains/*.lua, read as literals, never
executed) and gives each its field's native address as HellForge's stats schema names it:
  "component|<component>|<record>|<offset>", "EntityDelta|<component:member>|<data offset>",
  "<settings kind>|<row id>|<offset>" (ProjectileSettings, DamageInfo, ExplosionSettings, StatusEffectSettings,
  StratagemDefinition, BoosterSettings, PassiveModifier, PassiveStat...).
HellForge's editor catalogue (EditorCatalog.cs) gives each of its rows the keys on its bytes. A key's value kind says
how the preset stores the value: number, uses ("unlimited"), status (a status id), projectile ({ref:"output",id}),
explosion ({ref:"explosion",id} / {ref:"none"}). Rows HellForge has no field for (enemies, missions, vehicle mounts,
attachments' other fields...) keep no address and are skipped on import.
"""
import collections
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent

# ------------------------------------------------------------------------------------------- Lua literals --
_TOK = re.compile(r'\s*(?:--[^\n]*\n\s*)*("(?:\\.|[^"\\])*"|-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?|0x[0-9A-Fa-f]+|true|false|nil|'
                  r'[A-Za-z_][A-Za-z_0-9]*|[{}\[\]=,;])')
_DEC = re.compile(r'\\(\d{1,3})')


def read_literal(path):
    """A generated 'return {...}' Lua table as Python data ([k]=v, bare ident=v, arrays). Never executes anything."""
    src = re.sub(r"\A(?:\s*--[^\n]*\n)*\s*", "", Path(path).read_text(encoding="utf-8")).strip()
    if not src.startswith("return"):
        raise ValueError("not a returned literal: %s" % path)
    pos = len("return")

    def take():
        nonlocal pos
        m = _TOK.match(src, pos)
        if not m:
            raise ValueError("bad literal at %d in %s" % (pos, path))
        pos = m.end()
        return m.group(1)

    def peek():
        m = _TOK.match(src, pos)
        return m.group(1) if m else None

    def atom(t):
        if t in ("nil", "true", "false"):
            return {"nil": None, "true": True, "false": False}[t]
        if t.startswith('"'):
            return json.loads(_DEC.sub(lambda m: "\\u%04x" % int(m.group(1)), t))
        if t.lower().startswith("0x"):
            return int(t, 16)
        return json.loads(t)

    def value(t=None):
        t = take() if t is None else t
        if t != "{":
            return atom(t)
        arr, mp = [], {}
        t = take()
        while t != "}":
            if t == "[":
                k = value()
                if take() != "]" or take() != "=":
                    raise ValueError("bad keyed entry in %s" % path)
                mp[k] = value()
            elif re.match(r"[A-Za-z_]", t) and t not in ("true", "false", "nil") and peek() == "=":
                take()
                mp[t] = value()
            else:
                arr.append(value(t))
            t = take()
            if t in (",", ";"):
                t = take()
            elif t != "}":
                raise ValueError("missing separator at %d in %s" % (pos, path))
        if arr and mp:
            for i, v in enumerate(arr, 1):
                mp.setdefault(i, v)
            return mp
        return mp if mp else arr

    return value()


DOMAINS = ("player_weapon_authoring", "support_weapon_authoring", "stratagem_authoring", "entity_authoring",
           "vehicle_weapon_authoring", "throwable_authoring", "booster_authoring", "status_effect_authoring",
           "explosion_catalogue", "attack_outputs", "attachment_authoring", "melee_authoring", "pod_payload_authoring",
           "helldiver_fields")


def load_domains(runtime):
    return {n: read_literal(Path(runtime) / "domains" / (n + ".lua")) for n in DOMAINS}


def dct(x):
    """An empty Lua table reads as []: as a mapping."""
    return x if isinstance(x, dict) else {}


def lst(x):
    if isinstance(x, list):
        return x
    if isinstance(x, dict):
        return [x[k] for k in sorted(k for k in x if isinstance(k, int))]
    return []


# ---------------------------------------------------------------------------------------- native addresses --
SETTINGS = {"projectile": "ProjectileSettings", "damage": "DamageInfo", "explosion_damage": "DamageInfo",
            "explosion": "ExplosionSettings", "arc": "ArcSettings", "beam": "BeamSettings", "status": "StatusEffectSettings"}
TABLE_KINDS = {"ProjectileSettings", "DamageInfo", "ExplosionSettings", "ArcSettings", "BeamSettings",
               "StatusEffectSettings", "StratagemDefinition"}


def address(b, ctx=None):
    """The native bytes a HD2Runtime backing names, in HellForge's terms: ('component', name, record, offset),
    ('EntityDelta', 'component:componentOffset', dataOffset) or (<settings kind>, row type, offset). None: none known."""
    ctx = ctx or {}
    if not isinstance(b, dict):
        return None
    off = b.get("offset")
    kind = b.get("kind")
    if kind == "entity_delta":
        return ("EntityDelta", ctx.get("delta_component"), ctx.get("delta_offset")) if ctx.get("delta_component") else None
    if kind == "native" and b.get("component") == "MeleeHitVolume" and b.get("recordIndex") is not None:
        return ("MeleeHitVolume", b["recordIndex"], off)  # a hit volume by its RVA (ExportMelee.py)
    if isinstance(b.get("component"), str):
        if b.get("recordIndex") is None or off is None:
            return None
        return ("component", b["component"], b["recordIndex"], off)
    link = ctx.get("rootLink") or {}
    if str(kind).endswith("ComponentData") and link.get("component") == kind and b.get("nativeIdentity") in (ctx.get("rootId"), link.get("payload")):
        return ("component", kind, link.get("recordIndex"), off)  # the stratagem payload's record (ExportStatsSchema.py)
    if b.get("settings") in SETTINGS:
        return (SETTINGS[b["settings"]], b.get("recordType") or b.get("nativeIdentity"), off)
    if kind in TABLE_KINDS:
        return (kind, b.get("nativeIdentity", b.get("recordType")), off)
    if kind == "booster_table":
        return ("BoosterSettings", b.get("row"), off)
    if kind == "explosion" and ctx.get("explosionType"):
        return ("ExplosionSettings", ctx["explosionType"], off)
    if kind in ("explosion_damage", "status_damage") and ctx.get("damageType"):
        return ("DamageInfo", ctx["damageType"], off)
    if kind == "status_multiplier" and ctx.get("statusType") and isinstance(b.get("index"), int):
        return ("StatusMultiplier", ctx["statusType"], b["index"] * 8 + 4)
    if kind == "status_row" and ctx.get("statusType"):
        return ("StatusEffectSettings", ctx["statusType"], off)
    if kind == "stratagem_row" and ctx.get("stratagemRecord"):
        return ("StratagemDefinition", ctx["stratagemRecord"], off)
    return None


def hf_address(f):
    if f["kind"] == "component":
        return ("component", f["component"], f["record"], f["offset"])
    if f["kind"] == "EntityDelta":
        return ("EntityDelta", f["component"], f["offset"])
    return (f["kind"], f["record"], f["offset"])


# ------------------------------------------------------------------------------------------------- HD2R rows --
class Row:
    """One HD2R Editor row: its preset key, family, the field id it edits (sid), its value kind ('number', 'uses',
    'status', 'projectile', 'explosion', 'traits', ...), the native address and the HellForge targets it belongs to."""
    __slots__ = ("key", "family", "sid", "kind", "addr", "home", "note", "field")

    def __init__(self, key, family, sid, kind, addr, home, note=None, field=None):
        self.key, self.family, self.sid, self.kind, self.addr, self.home, self.note = key, family, sid, kind, addr, home, note
        self.field = field  # the HD2Runtime field of a value kind HellForge holds as slots (STRUCTURED)


NUMERIC = ("number", "integer")
# Row kinds whose values HellForge's stats file cannot hold (it takes one number per field).
# Field ids of rows with no native address that HellForge's editor holds another way (keys kept as "unaddressed")
LINKED = {"payload.entity", "mount.weapon", "charge.overcharge_explosion", "attachment.reload_duration", "stratagem.calldown_code"}
NOT_SCALAR = {"traits", "fire_modes", "fire_rates", "ap_label", "boolean", "enum", "code", "mount", "beam", "pickup", "overcharge"}
VALUE_KINDS = {"boolean": "boolean", "status_reference": "status", "fire_mode_set": "fire_modes", "fire_rate_set": "fire_rates",
               "trait_set": "traits", "armor_penetration_label": "ap_label", "enum": "enum",
               "projectile_reference": "projectile", "explosion_reference": "explosion"}


def numeric(f):
    return f.get("type") in NUMERIC and isinstance(f.get("currentDefault"), (int, float)) and not isinstance(f.get("currentDefault"), bool)


def kind_of(f):
    if numeric(f):
        return "number"
    return VALUE_KINDS.get(f.get("type"), f.get("type") or "?")


def shown(f, entry):
    """HD2R Editor shows a field it may edit (a weapon whose identity blocks ordinary writes shows them locked)."""
    if f.get("editable") is not False:
        return True
    return bool(entry.get("ordinaryWritesBlocked")) and f.get("reason") is not None and f.get("reason") == entry.get("blockReason")


def build_rows(D):
    """Every row key the HD2R Editor 1.0.0 builds for the families HellForge could take (catalog.lua; line numbers in
    the comments), with its address. Families with nothing in HellForge (enemies, missions, armor, helldiver...) are
    classified from the key alone (classify())."""
    rows = {}
    PW, SW, ST = D["player_weapon_authoring"]["weapons"], D["support_weapon_authoring"]["weapons"], D["stratagem_authoring"]["stratagems"]
    EN, VW, TH = D["entity_authoring"], D["vehicle_weapon_authoring"]["weapons"], D["throwable_authoring"]["throwables"]
    BO, SE, EX = D["booster_authoring"]["boosters"], D["status_effect_authoring"]["statuses"], D["explosion_catalogue"]
    AO, AT = D["attack_outputs"], D["attachment_authoring"]
    explosion_damage = {e.get("type"): dct(e.get("damage")).get("type") for e in dct(EX["explosions"]).values()}

    def add(key, family, sid, kind, addr, home, note=None, field=None):
        rows.setdefault(key, Row(key, family, sid, kind, addr, home, note, field))

    # player weapons (player_rows, catalog.lua 772-974; part prefixes ub:/mag: 2272-2289)
    def player(prefix, name, entry, home):
        for f in lst(entry.get("fields")):
            sid = f.get("semanticFieldId")
            if not f.get("preferred") or f.get("deprecated") or f.get("derivedReadOnly") or not shown(f, entry):
                continue
            k = kind_of(f)
            if k == "?" or (k not in ("number",) and f.get("type") not in VALUE_KINDS and f.get("type") not in ("projectile_reference", "explosion_reference")):
                continue
            add(prefix + sid, "pw", sid, k, address(f.get("backing")), home, field=f)
        if not entry.get("subweaponOf") and dct(AO.get("ammunition")).get(name):
            add(prefix + "ammunition.projectile", "pw", "ammunition.projectile", "projectile", None, home, "ammunition")
        for f in lst(entry.get("fields")):
            if f.get("type") == "beam_reference" and not entry.get("subweaponOf"):
                add(prefix + "attack.beam", "pw", "attack.beam", "beam", None, home)

    for name, entry in PW.items():
        if entry.get("slot") not in ("primary", "secondary"):
            continue
        base = "pw|%s|" % name
        player(base, name, entry, [("weapons", name)])
        for sub in lst(entry.get("subweapons")):
            sub_entry = PW.get(sub.get("name"))
            if sub_entry:
                player(base + "ub:%s|" % sub["name"], sub["name"], sub_entry, [("weapons", sub["name"])])
        # magazine / muzzle / optic / underbarrel attachments (weapon_attachments 2171-2202, attachment_rows 1960-1981)
        ids = set()
        rel = dct(AT.get("weapons")).get(name)
        lists = [rel] + list(dct(dct(AT.get("slots")).get(name)).values())
        for l in lists:
            if isinstance(l, dict):
                ids.update(o.get("attachment") or o.get("semanticId") or o.get("id") for o in lst(l.get("options")) if isinstance(o, dict))
                if isinstance(l.get("default"), str):
                    ids.add(l["default"])
            for o in lst(l):
                ids.add(o if isinstance(o, str) else (o or {}).get("attachment") if isinstance(o, dict) else None)
        for aid in filter(None, ids):
            a = dct(AT["attachments"]).get(aid)
            if not a:
                continue
            for fid, f in dct(a.get("fields")).items():
                if isinstance(f.get("currentDefault"), (int, float)) and f.get("editable") is not False:
                    addr = ("EntityDelta", "%s:%s" % (f.get("component"), f.get("componentOffset")), f.get("dataOffset"))
                    add(base + "mag:%s|%s" % (aid, fid), "pw", fid, "number", addr, [("weapons", name)], "attachment")
        # (catalog.lua player_rows 757-768: only where its reload timing names a default magazine with a reload duration)
        default = next((f["reloadTiming"]["defaultAttachment"] for f in lst(entry.get("fields")) if isinstance(f.get("reloadTiming"), dict)
                        and isinstance(f["reloadTiming"].get("defaultAttachment"), str)), None)
        if default and "attachment.reload_duration" in dct(dct(dct(AT["attachments"]).get(default)).get("fields")):
            add(base + "reload.default_magazine", "pw", "attachment.reload_duration", "number", None, [("weapons", name)], "attachment")

    # catalogue explosions (catalogue_explosion_rows 1032-1073; explosion_field_row 1013): '<eid>|<field id>'
    def explosion_rows(prefix, family, owner, roles, home):
        out, picked = [], []
        alle = dct(EX["explosions"])
        own = set()
        for eid in sorted(alle):
            for o in lst(alle[eid].get("owners")):
                if isinstance(o, dict) and o.get("name") == owner and o.get("category") != "explosion":
                    own.add(eid)
                    if o.get("role") in roles and eid not in picked:
                        picked.append(eid)
        grew = True
        while grew:
            grew = False
            for eid in sorted(alle):
                if eid in picked:
                    continue
                if any(isinstance(o, dict) and o.get("category") == "explosion" and (o.get("name") in own or o.get("name") in picked)
                       for o in lst(alle[eid].get("owners"))):
                    picked.append(eid)
                    grew = True
        for eid in picked:
            explosion_field_rows(prefix + eid + "|", family, eid, home)

    def explosion_field_rows(prefix, family, eid, home):
        e = dct(EX["explosions"]).get(eid) or {}
        for fd in lst(EX.get("fields")):
            if fd.get("backing") == "explosion":
                addr = ("ExplosionSettings", e.get("type"), fd.get("offset"))
            elif fd.get("backing") == "damage" and dct(e.get("damage")).get("type"):
                addr = ("DamageInfo", e["damage"]["type"], fd.get("offset"))
            else:
                addr = None
            add(prefix + fd["id"], family, fd["id"], "number" if fd.get("type") in NUMERIC else fd.get("type"), addr, home, "catalogue explosion")

    for eid in dct(EX["explosions"]):
        explosion_field_rows("ex|%s|" % eid, "ex", eid, [])

    # support weapons (support_rows 1201-1383; CATEGORY.support_weapons 2359-2396)
    def support(prefix, name, entry, home):
        for f in lst(entry.get("fields")):
            t, sid = dct(f.get("target")), f.get("semanticFieldId")
            if f.get("deprecated") or f.get("derivedReadOnly") or f.get("preferred") is False or not shown(f, entry):
                continue
            ty = f.get("type")
            if numeric(f) or ty in ("boolean", "status_reference", "fire_mode_set", "fire_rate_set", "trait_set", "armor_penetration_label"):
                add(prefix + sid + ("@" + t["attack"] if t.get("attack") else ""), "sp", sid, kind_of(f), address(f.get("backing")), home, field=f)
            elif ty == "projectile_reference" and t.get("path") == "attack":
                add(prefix + "attack.projectile@" + (t.get("attack") or "primary"), "sp", sid, "projectile", address(f.get("backing")), home)
            elif ty == "beam_reference":
                add(prefix + "attack.beam", "sp", sid, "beam", None, home)
            elif ty == "overcharge_explosion_reference":
                add(prefix + sid, "sp", sid, "overcharge", None, home)
            elif ty == "function_projectile_reference":
                add(prefix + "function_ammo.projectile", "sp", sid, "projectile", address(f.get("backing")), home)
            elif ty == "explosion_reference" and t.get("path") == "terminal_action":
                add(prefix + sid + "@" + (t.get("attack") or "primary"), "sp", sid, "explosion", address(f.get("backing")), home)
        explosion_rows(prefix, "sp", name, {"function_impact"}, home)

    # stratagems (stratagem_rows 1448-1526; sentry_swap_rows 2303-2337)
    def stratagem(prefix, name, entry, home):
        ctx = {"rootLink": entry.get("rootLink"), "rootId": dct(entry.get("root")).get("id")}
        for f in lst(entry.get("fields")):
            t, sid = dct(f.get("target")), f.get("semanticFieldId")
            rid = f.get("instanceKey")
            ty = f.get("type")
            if not shown(f, entry):
                continue
            if ty == "calldown_code":
                add(prefix + (rid or "stratagem.calldown_code"), "st", sid, "code", None, home)
            elif ty == "stratagem_uses":
                add(prefix + (rid or "stratagem.max_uses"), "st", sid, "uses", address(f.get("backing"), ctx), home)
            elif ty == "enum" and f.get("allowedValues"):
                add(prefix + (rid or "%s@%s" % (sid, t.get("path"))), "st", sid, "enum", address(f.get("backing"), ctx), home, field=f)
            elif numeric(f):
                add(prefix + (rid or "%s@%s" % (sid, t.get("path"))), "st", sid, "number", address(f.get("backing"), ctx), home)

    def sentry(prefix, name, home):
        host = VW.get(name + " / weapon")
        if not host or host.get("stratagemHost") != name:
            return
        for f in lst(host.get("fields")):
            ty, t = f.get("type"), dct(f.get("target"))
            if ty == "projectile_reference":
                add(prefix + "attack.projectile@stratagem", "st", f.get("semanticFieldId"), "projectile", address(f.get("backing")), home + [("weapons", name + " / weapon")])
            elif ty == "beam_reference":
                add(prefix + "attack.beam@stratagem", "st", f.get("semanticFieldId"), "beam", None, home)
            elif numeric(f) and t.get("path") == "weapon" and not f.get("derivedReadOnly"):
                add(prefix + f["semanticFieldId"] + "@stratagem", "st", f["semanticFieldId"], "number", address(f.get("backing")),
                    home + [("weapons", name + " / weapon")])

    # backpacks (backpack_rows 1642-1730) and vehicles (vehicle_rows 1579-1629)
    def entity(prefix, family, entry, home):
        for f in lst(entry.get("fields")):
            t, sid = dct(f.get("target")), f.get("semanticFieldId")
            if t.get("path") == "mount" and f.get("type") == "mounted_weapon_reference":
                add(prefix + (f.get("instanceKey") or "mount.weapon@%s" % t.get("mount")), family, sid, "mount", None, home)
            elif numeric(f) and t.get("path") in ("backpack", "linked", "damage_zone", "entity"):
                rid = f.get("instanceKey") or ("%s@%s" % (sid, t.get("zone") if family == "vh" else t.get("path")))
                add(prefix + rid, family, sid, "number", address(f.get("backing")), home)

    # vehicle and drone weapons (vehicle_weapon_rows 1733-1844)
    def mount(prefix, key, entry, home):
        for f in lst(entry.get("fields")):
            t, sid = dct(f.get("target")), f.get("semanticFieldId")
            status = f.get("type") == "status_reference" and f.get("allowedValues")
            if f.get("derivedReadOnly") or not (numeric(f) or status):
                continue
            if t.get("path") not in ("weapon", "projectile_reference", "explosion", "attack"):
                continue
            role = t.get("attack")
            add(prefix + sid + ("@" + role if role else ""), "vw", sid, "status" if status else "number", address(f.get("backing")), home)
        if entry.get("mountedShield"):
            return
        for f in lst(entry.get("fields")):
            if f.get("type") == "beam_reference":
                add(prefix + "attack.beam@mount", "vw", "attack.beam", "beam", None, home)
                break
        proj = next((f for f in lst(entry.get("fields")) if f.get("type") == "projectile_reference" and dct(f.get("target")).get("path") == "attack"), None)
        if proj:
            add(prefix + "attack.projectile@mount", "vw", proj.get("semanticFieldId"), "projectile", address(proj.get("backing")), home)

    # a stratagem's drop pod (pod_rows 2217-2250): its rack's spawn count (HellpodRackComponentData +556) and slots
    PODS = D["pod_payload_authoring"]

    def pod(prefix, name, home):
        rack = dct(PODS.get("racks")).get(dct(PODS.get("byStratagem")).get(name))
        if not rack:
            return
        if isinstance(rack.get("spawnCount"), int) and rack.get("writable") is not False and rack.get("recordIndex") is not None:
            add(prefix + "payload.spawn_count", "pod", "payload.spawn_count", "number",
                ("component", "HellpodRackComponentData", rack["recordIndex"], 556), home)
        for i in range(1, 5):
            add(prefix + "payload.slot_%d" % i, "pod", "payload.entity", "pickup", None, home)

    for name, weapon in SW.items():
        base = "sp|%s|" % name
        home = [("weapons", name), ("stratagems", name)]
        if name in ST:
            stratagem(base + "st|", name, ST[name], [("stratagems", name)])
        support(base + "sw|", name, weapon, [("weapons", name)])
        bp = dct(EN.get("backpacks")).get(name + " Backpack")
        if bp:
            entity(base + "bp|", "bp", bp, [("backpacks", name + " Backpack"), ("backpacks", name)])
        pod(base + "pod|", name, [("stratagems", name)])
    for name, entry in ST.items():
        fam = entry.get("family")
        if fam in ("orbital", "eagle", "sentry", "emplacement", "mine"):
            stratagem("st|%s|" % name, name, entry, [("stratagems", name)])
            sentry("st|%s|" % name, name, [("stratagems", name)])
        elif fam == "backpack":
            base = "sb|%s|" % name
            stratagem(base + "st|", name, entry, [("stratagems", name)])
            bp = dct(EN.get("backpacks")).get(name)
            if bp:
                entity(base + "bp|", "bp", bp, [("backpacks", name)])
            for key in sorted(VW):
                w = VW[key]
                if w.get("vehicle") == name:
                    mount(base + "vw:%s|" % (w.get("mount") or "gun"), key, w, [("weapons", key)])
            explosion_rows(base + "ex|", "sb", name, {"behavior"}, [("backpacks", name)])
            pod(base + "pod|", name, [("stratagems", name)])
        elif fam == "mission":
            stratagem("rs|%s|st|" % name, name, entry, [("stratagems", name)])
            pod("rs|%s|pod|" % name, name, [("stratagems", name)])
    for name, vehicle in dct(EN.get("vehicles")).items():
        base = "ve|%s|" % name
        if name in ST:
            stratagem(base + "st|", name, ST[name], [("stratagems", name)])
        entity(base + "vh|", "vh", vehicle, [("vehicles", name)])
        for key in sorted(VW):
            w = VW[key]
            if w.get("vehicle") == name and w.get("carrier") != "backpack_drone":
                mount(base + "vw:%s|" % (w.get("mount") or key), key, w, [("weapons", key)])
    explosion_field_rows  # (the NUX-223 Hellbomb page: 'ex|NUX-223 Hellbomb|<eid>|<field>', detonation_rows 2158)
    explosion_rows("ex|NUX-223 Hellbomb|", "ex", "NUX-223 Hellbomb", {"behavior"}, [])

    # throwables (throwable_rows 1858-1930): instanceKey, else '<target key>:<field id>'
    for name, entry in TH.items():
        for tkey, t in dct(entry.get("targets")).items():
            for fid, f in dct(t.get("fields")).items():
                ty = f.get("type")
                if not shown(f, entry):
                    continue
                k = "explosion" if ty in ("explosion_reference", "explosion_payload") else kind_of(f)
                add("th|%s|%s" % (name, f.get("instanceKey") or "%s:%s" % (tkey, fid)), "th", f.get("semanticFieldId") or fid, k,
                    address(f.get("backing")), [("throwables", name)])

    # boosters (booster_rows 1933-1951): instanceKey, else '<path>:<field id>'; the path's record from its target
    for name, entry in BO.items():
        for path, t in dct(entry.get("targets")).items():
            ctx = {"explosionType": t.get("explosionType"), "damageType": t.get("damageType") or explosion_damage.get(t.get("explosionType")),
                   "statusType": t.get("statusType"), "stratagemRecord": dct(t.get("record")).get("id")}
            for fid, f in dct(t.get("fields")).items():
                b = dct(f.get("backing"))
                note = {"explosion": "catalogue explosion", "status_multiplier": "status multiplier",
                        "stratagem_row": "granted stratagem"}.get(b.get("kind"))
                add("bo|%s|%s" % (name, f.get("instanceKey") or "%s:%s" % (path, fid)), "bo", f.get("semanticFieldId") or fid, kind_of(f),
                    address(b, ctx), [("boosters", name)], note)

    # status effects (CATEGORY.status_effects 2543-2576): 'se|<status id>|<path>:<semanticFieldId>'
    for sid_, entry in SE.items():
        for path in ("status", "damage"):
            for fid, f in dct(dct(dct(entry.get("targets")).get(path)).get("fields")).items():
                add("se|%s|%s:%s" % (sid_, path, f.get("semanticFieldId") or fid), "se", f.get("semanticFieldId") or fid, kind_of(f),
                    address(f.get("backing")), [("statuses", sid_)])

    # the Helldiver (helldiver_rows): 'hd|Helldiver|[<zone>:]<field id>', a field of its avatar, health or inventory record
    H = D["helldiver_fields"]
    for f in lst(H.get("fields")):
        r = dct(H.get(f.get("record")))
        if not r.get("component") or r.get("recordIndex") is None:
            continue
        addr = ("component", r["component"], r["recordIndex"], f.get("offset"))
        key = "hd|Helldiver|%s%s" % (f["zone"] + ":" if f.get("zone") else "", f["id"])
        enum = dct(H.get("enums")).get(f.get("enum")) if f.get("type") == "enum" else None
        add(key, "hd", f["id"], "enum" if enum else "number", addr, [("helldiver", "Helldiver")],
            field={"enumValues": enum} if enum else None)

    # melee (melee_rows 2508-2540): 'me|<weapon>|<target key>:<field id>'
    for name, e in dct(D["melee_authoring"].get("entries")).items():
        for tkey, t in dct(e.get("targets")).items():
            for fid, f in dct(t.get("fields")).items():
                add("me|%s|%s:%s" % (name, tkey, fid), "me", fid, kind_of(f) if f.get("type") else "number", address(f.get("backing")), [])
    return rows


# ---------------------------------------------------------------------------------------- the translation --

def structured(row):
    """A value kind HellForge holds as one or more plain fields (ExportStatsSchema.py expand()): the key's kind as the
    editor reads a preset value ("bool": true/false; "reticle": true/false over a reticle style; "enum": a number or a name; "slots": a list, one entry per slot;
    "ap": an armor penetration label, the trait slot that holds it), the slots' addresses and the names of values.
    None: not one."""
    f = row.field
    if f is None or row.addr is None or any(a is None for a in row.addr) or row.addr[0] != "component" and row.kind in ("fire_rates", "fire_modes", "traits", "ap_label"):
        return None
    head, off = row.addr[:-1], row.addr[-1]
    at = lambda i, size=4: key_address(head + (off + i * size,))
    if row.kind == "boolean" and row.sid == "weapon.third_person_reticle":  # a style number, its flag a view (ExportStatsSchema.py)
        return {"kind": "reticle", "addresses": [at(0)]}
    if row.kind == "boolean":
        return {"kind": "bool", "addresses": [at(0)]}
    if row.kind == "enum":
        names = {k: v for k, v in (f.get("enumValues") or {}).items()}
        names.update({v["name"]: v["value"] for v in f.get("allowedValues") or [] if isinstance(v, dict) and v.get("name")})
        return {"kind": "enum", "addresses": [at(0)], "names": names}
    if row.kind == "fire_rates":
        return {"kind": "slots", "addresses": [at(i) for i in range(3)]}
    if row.kind == "fire_modes" and isinstance(f.get("modeValues"), dict):
        return {"kind": "slots", "addresses": [at(i) for i in range(max(1, min(4, f.get("maxModes") or 4)))], "names": f["modeValues"]}
    if row.kind == "traits" and isinstance(f.get("traitValues"), dict):
        return {"kind": "slots", "addresses": [at(i) for i in range(min(5, f.get("maxTraits") or 5))], "names": f["traitValues"]}
    if row.kind == "ap_label" and isinstance(f.get("penetrationValues"), dict) and isinstance(f.get("penetrationSlot"), int):
        return {"kind": "ap", "addresses": [at(f["penetrationSlot"])], "names": dict(f["penetrationValues"], none=0)}
    return None


def key_address(addr):
    if addr is None or any(a is None for a in addr):
        return None
    return "|".join(str(a) for a in addr)


def main(runtime, out):
    D = load_domains(runtime)
    rows = build_rows(D)
    refs = json.loads((REPO / "mappings" / "references.json").read_text(encoding="utf-8"))
    ammo = {a["weapon"]: a for a in refs["ammunition"]}
    keys, slots, unaddressed = {}, {}, {}
    for key, row in sorted(rows.items()):
        addr = row.addr
        if row.note == "ammunition":  # the default ammunition's entity delta (references.json)
            weapon = key.split("|")[1]
            a = ammo.get(weapon)
            addr = ("EntityDelta", "%s:0" % a["component"], a["offset"]) if a else None
        a = key_address(addr)
        s_ = structured(row)
        if s_ is not None:
            slots[key] = dict(s_, field=row.sid)
            continue
        if a is None or row.kind in NOT_SCALAR:
            # rows HellForge holds another way (pod payloads, arm swaps, the overcharge explosion, the default magazine's
            # reload, call-in codes): their keys, kind and field id, for EditorCatalog to put on those rows
            if row.sid in LINKED:
                unaddressed[key] = [row.kind, row.sid]
            continue
        keys[key] = [a, row.kind, row.sid]
    # armor perk values and the passive choices (mappings/armor-perks.json; the editor's settings)
    perks = json.loads((REPO / "mappings" / "armor-perks.json").read_text(encoding="utf-8"))
    for p in perks["perks"]:
        for fid, f in p["fields"].items():
            keys[f["hd2r"]] = ["%s|%s|%s" % (f["kind"], f["record"], f["offset"]), "number", fid]
    keys["hd|Armor perks|armor"] = ["passives|armor", "passive", "armor"]
    keys["hd|Armor perks|second"] = ["passives|second", "passive", "second"]
    outputs = {}
    for oid, o in sorted(dct(D["attack_outputs"].get("outputs")).items()):
        if o.get("family") == "projectile":
            t = dct(o.get("referenceSettings")).get("recordType") or o.get("currentDefault")
            if isinstance(t, int):
                outputs[oid] = t
    explosions = {eid: e.get("type") for eid, e in sorted(dct(D["explosion_catalogue"]["explosions"]).items()) if isinstance(e.get("type"), int)}
    for alias, eid in sorted(dct(D["explosion_catalogue"].get("aliases")).items()):
        if eid in explosions:
            explosions.setdefault(alias, explosions[eid])
    data = {"format": 1, "editor": "1.0.0", "runtime": "0.40.0",
            "note": "HD2R Editor preset keys and the bytes they edit (scripts/ExportHd2rKeys.py from HD2Runtime 0.40.1).",
            "keys": keys, "slots": slots, "unaddressed": unaddressed, "outputs": outputs, "explosions": explosions}
    Path(out).write_text(json.dumps(data, indent=0, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    kinds = collections.Counter(v[1] for v in keys.values())
    print("%d keys (%s), %d slot keys (%s), %d outputs, %d explosions" % (len(keys), ", ".join("%s %d" % kv for kv in sorted(kinds.items())), len(slots),
          ", ".join("%s %d" % kv for kv in sorted(collections.Counter(v["kind"] for v in slots.values()).items())), len(outputs), len(explosions)))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(*sys.argv[1:])
