"""Export reviewed authoring metadata, never game bytes, from HD2Runtime table literals.

Usage: py scripts/ExportStatsSchema.py <HD2Runtime> <output.json> [native-layouts.h]
The literal reader deliberately does not execute Lua code.
"""
import json
import re
import sys
import struct
from pathlib import Path


def read_literal(path):
    source = re.sub(r"\A(?:\s*--[^\n]*\n)*\s*", "", Path(path).read_text(encoding="utf-8")).strip()
    if not source.startswith("return "):
        raise ValueError("Expected a returned Lua literal: " + str(path))
    token = re.compile(r'\s*("(?:\\.|[^"\\])*"|-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?|true|false|nil|[{}\[\]=,])')
    position = len("return ")

    def take():
        nonlocal position
        match = token.match(source, position)
        if not match:
            raise ValueError(f"Invalid literal at {position} in {path}")
        position = match.end()
        return match[1]

    def value(t=None):
        t = take() if t is None else t
        if t != "{":
            return None if t == "nil" else json.loads(t)
        array, mapping = [], {}
        t = take()
        while t != "}":
            if t == "[":
                key = value()
                if take() != "]" or take() != "=":
                    raise ValueError("Invalid keyed table")
                mapping[key] = value()
            else:
                array.append(value(t))
            t = take()
            if t == ",":
                t = take()
            elif t != "}":
                raise ValueError("Missing separator")
        if array and mapping:
            raise ValueError("Mixed table")
        return mapping if mapping else array

    result = value()
    if source[position:].strip():
        raise ValueError("Trailing Lua code")
    return result


ALIASES = {"rpm": "weapon.fire_rate", "magazine": "magazine.capacity",
           "recoil": "weapon.recoil", "fuse": "throwable.explosion_delay",
           "cooldown": "stratagem.cooldown", "uses": "stratagem.max_uses"}
SETTINGS = {"projectile": "ProjectileSettings", "damage": "DamageInfo", "explosion_damage": "DamageInfo",
            "explosion": "ExplosionSettings", "arc": "ArcSettings", "beam": "BeamSettings", "status": "StatusEffectSettings"}
LAYOUTS = {"ProjectileSettings": (351, 272), "DamageInfo": (650, 76),
           "StratagemDefinition": (150, 400), "ExplosionSettings": (423, 152), "ArcSettings": (17, 104),
           "BeamSettings": (31, 112), "StatusEffectSettings": (72, 152)}


def consumer_names(consumers):
    """The catalogs name consumers directly or as {stratagem|backpack|vehicle|externalConsumer: name, ...}."""
    names = []
    for consumer in consumers or []:
        name = consumer if isinstance(consumer, str) else next((consumer[k] for k in ("stratagem", "backpack", "vehicle", "externalConsumer")
                                                                  if isinstance(consumer.get(k), str)), None)
        if name and name not in names:
            names.append(name)
    return names


# Field order for the editor: the catalogs' own order, and a fixed order for rows reached by type id.
DIRECT_ORDER = {name: i for i, name in enumerate(("velocity", "mass", "drag", "gravity", "pellet_count", "damage", "penetration_slowdown", "lifetime",
    "standard_damage", "durable_damage", "ap_direct", "ap_slight", "ap_large", "ap_extreme", "demolition", "stagger", "push_force", "strength"))}
DIRECT_MAGAZINE = {"attachment.magazine_capacity": "magazine.capacity", "attachment.spare_magazines": "magazine.spare_magazines",
                   "attachment.starting_magazines": "magazine.starting_magazines", "attachment.magazines_from_supply": "magazine.magazines_from_supply"}


def placement(source, section, target):
    """Where the editor lists a target: armory slot, weapon class, host item. Catalog names only."""
    if source == "player_weapon":
        return {k: v for k, v in (("slot", target.get("slot")), ("class", target.get("category")), ("host", target.get("subweaponOf"))) if v}
    if source == "support_weapon":
        return {"slot": "support"}
    if source == "vehicle_weapon":
        return {k: v for k, v in (("slot", "mounted"), ("host", target.get("stratagemHost") or target.get("vehicle")), ("mount", target.get("mount"))) if v}
    if source == "throwable":
        return {"slot": "throwable", "class": target.get("category")}
    if source == "stratagem":
        return {"family": target.get("family")}
    return {}


def table_metadata(schema):
    tables = []
    for source in ("explosion", "arc", "beam", "status"):
        if source not in schema.get("settings", {}):
            continue
        desc = schema["settings"][source]
        groups = []
        for group in desc["groups"]:
            words = struct.unpack("<6I", bytes.fromhex(group["header"]))
            if words[:2] != (0x444C444C, 1) or words[4:] != (1, 0):
                raise ValueError("Unreviewed settings framing")
            groups.append({"offset": group["offset"], "type": words[2], "extent": words[3],
                           "root": group.get("root", 0), "rowOffset": group.get("row_offset", 0), "count": group.get("count", 0)})
        tables.append({"kind": SETTINGS[source], "size": desc["size"], "stride": desc["stride"], "groups": groups})
    return tables


# HD2R Editor's Helldiver labels (catalog.lua HELLDIVER_LABELS, ZONE_LABELS)
HELLDIVER_LABELS = {"direction_factor": "Strafe and backpedal factor", "aim": "Aiming", "sprint_exhausted": "Sprint (exhausted)",
                    "crouch_aim": "Crouched, aiming", "crouch_walk": "Crouched, walking", "crouch_jog": "Crouched, jogging",
                    "crouch_sprint": "Crouched, sprinting", "prone": "Prone", "swim": "Swimming", "sprint_duration": "Sprint duration",
                    "recover_time_standing": "Recovery standing", "recover_time_crouching": "Recovery crouching",
                    "recover_time_prone": "Recovery prone", "recover_delay": "Recovery delay", "cost_jump": "Cost of a jump",
                    "cost_dodge": "Cost of a dive", "cost_climb": "Cost of a climb", "cost_slide": "Cost of a slide",
                    "explosive_damage_percentage": "Explosion damage share", "damage_multiplier": "Damage multiplier",
                    "damage_multiplier_dps": "Damage over time multiplier", "durable_resistance": "Durable damage share",
                    "affects_main_health": "Main health share", "health": "Health", "stims_from_supply": "Stims from supply"}
ZONE_LABELS = {"head": "Head", "body": "Body", "arm_left": "Left arm", "arm_right": "Right arm", "leg_left": "Left leg", "leg_right": "Right leg"}

REFERENCES = {"projectile_reference": "projectile", "function_projectile_reference": "projectile", "explosion_reference": "explosion",
              "status_reference": "status"}


def humanize(name):
    text = str(name).replace("_", " ").strip()
    return text[:1].upper() + text[1:]


def expand(f):
    """HD2R Editor's value kinds as plain fields, each one value HellForge's stats file and live edits hold: a flag
    (its u8 or u32), an enum or weapon function (a u32 with its named values: "options"), and the slot lists HD2Runtime
    writes slot by slot (player_weapon_writes.lua SLOT_SETS): a fire-rate set's three f32 rates (X, Y the default, Z),
    a fire-mode set's u32 FireMode slots, a trait set's five u32 trait tags (the armory's labels; the armor
    penetration label is one of them, so it has no field of its own). Other fields pass unchanged."""
    ty, b = f.get("type"), dict(f.get("backing") or {})
    if b.get("kind") != "component" and ty in ("boolean", "fire_rate_set", "fire_mode_set", "trait_set"):
        return [f]
    def slot(key, name, offset, storage, options=None, **extra):
        g = dict(f, semanticFieldId=key, displayName=name, backing=dict(b, offset=offset, storage=storage, width=1 if storage == "u8" else 4),
                 type="number" if storage == "f32" else "integer", _options=options, **extra)
        g["_slot"] = key != f["semanticFieldId"]  # one slot of a list: gives way to a named field on the same bytes
        return g
    if ty == "boolean" and b.get("storage") in ("u8", "u32"):
        return [slot(f["semanticFieldId"], f.get("displayName") or humanize(f["semanticFieldId"].split(".")[-1]), b["offset"], b["storage"],
                     [[0, "No"], [1, "Yes"]], min=0, max=1)]
    if ty == "enum" and isinstance(f.get("allowedValues"), list) and f.get("writeKind") != "reorder_native_mode_vector" and b.get("storage") in ("u32", "i32"):
        names = {v: humanize(k) for k, v in (f.get("enumValues") or {}).items()}
        options = [[v, names.get(v, str(v))] for v in f["allowedValues"] if isinstance(v, int) and not isinstance(v, bool)]
        options += [[v["value"], v.get("label") or humanize(v.get("name"))] for v in f["allowedValues"]  # {value, name, label}
                    if isinstance(v, dict) and isinstance(v.get("value"), int)]
        if options:
            return [slot(f["semanticFieldId"], f.get("displayName"), b["offset"], b["storage"], options)]
    if ty == "weapon_function" and isinstance(f.get("functionValues"), dict) and b.get("storage") == "u32":
        allowed = [n for n in (f.get("allowedValues") or f["functionValues"]) if n in f["functionValues"]]
        return [slot(f["semanticFieldId"], f.get("displayName"), b["offset"], "u32", [[f["functionValues"][n], humanize(n)] for n in allowed])]
    if ty == "fire_rate_set" and b.get("width") == 12:
        names = f.get("slotNames") or ["x", "y", "z"]
        default = f.get("defaultSlot") or "y"
        return [slot("%s.%s" % (f["semanticFieldId"], n), "Rate of fire %s%s" % (n.upper(), " (default)" if n == default else ""),
                     b["offset"] + 4 * i, "f32", None, min=0, max=f.get("max") or 3000, unit="rpm") for i, n in enumerate(names[:3])]
    if ty == "fire_mode_set" and isinstance(f.get("modeValues"), dict) and b.get("width") == 16:
        options = [[0, "None"]] + [[v, humanize(k)] for k, v in sorted(f["modeValues"].items(), key=lambda kv: kv[1])]
        count = max(1, min(4, f.get("maxModes") or 4))
        return [slot("%s.%d" % (f["semanticFieldId"], i + 1), "Fire mode %d%s" % (i + 1, " (default)" if i == 0 else ""),
                     b["offset"] + 4 * i, "u32", options) for i in range(count)]
    if ty == "trait_set" and isinstance(f.get("traitValues"), dict) and b.get("width") == 20:
        options = [[0, "None"]] + sorted(([v, humanize(k)] for k, v in f["traitValues"].items()), key=lambda o: o[1])
        return [slot("%s.%d" % (f["semanticFieldId"], i + 1), "Trait %d" % (i + 1), b["offset"] + 4 * i, "u32", options)
                for i in range(min(5, f.get("maxTraits") or 5))]
    return [f]


def export(runtime, profile):
    schema = read_literal(runtime / "schemas/current.lua")
    if schema["dll_sha"].upper() != profile["gameDllSha256"].upper():
        raise ValueError("HD2Runtime and HellForge game.dll fingerprints differ")
    targets = {}
    sources = [("player_weapon", "weapons", "weapons"), ("support_weapon", "weapons", "weapons"),
               ("throwable", "throwables", "throwables"), ("stratagem", "stratagems", "stratagems"),
               ("vehicle_weapon", "weapons", "weapons"), ("entity", "backpacks", "backpacks"),
               ("entity", "vehicles", "vehicles")]
    for source, collection, section in sources:
        catalog = read_literal(runtime / "domains" / (source + "_authoring.lua"))
        for name, target in (catalog[collection] or {}).items():
            fields = target.get("fields", [])
            if collection == "throwables":
                fields = [f for t in target["targets"].values() for f in t.get("fields", {}).values()]
            exported, blocked = {}, {}
            fields = [g for f in fields for g in expand(f)]
            slot_keys = {g["semanticFieldId"] for g in fields if g.get("_slot")}
            for order, f in enumerate(fields):
                key = f["semanticFieldId"]
                b = f.get("backing") or {}
                # a component named by the stratagem it belongs to (the Orbital Laser's OrbitalAbility phase fields): the
                # record of the stratagem's payload entity (its rootLink)
                link = target.get("rootLink") or {}
                if (not b.get("component") and str(b.get("kind", "")).endswith("ComponentData") and link.get("component") == b.get("kind") and
                        b.get("nativeIdentity") in ((target.get("root") or {}).get("id"), link.get("payload"))):
                    b = dict(b, component=b["kind"], kind="component", resource=link["payload"], recordIndex=link.get("recordIndex"))
                reason = (f.get("reason") or "Catalog does not approve this field for writes") if not f.get("acceptedForWrites", f.get("editable", False)) else None
                if target.get("ordinaryWritesBlocked"):
                    reason = target.get("blockReason") or "Target identity is ambiguous"
                kind = "component" if b.get("component") else SETTINGS.get(b.get("settings"), b.get("kind"))
                storage = b.get("storage")
                # a reference (a projectile, an explosion, a status) on a component: its u32 type id; the mappings' "ref"
                # (ImportSchemaMappings.py) gives it the loader's reference checks and the editor's picker
                reference = REFERENCES.get(f.get("type")) if kind == "component" else None
                if f.get("type") == "function_projectile_reference" and source != "support_weapon":
                    reference = None  # HD2R Editor offers it on support weapons; a primary's comes with its programmable ammo (Stats.cs)
                scalar_type = f.get("type") in ("number", "integer") or reference is not None or (
                    f.get("type") == "stratagem_uses" and kind == "StratagemDefinition" and b.get("offset") == 80)
                one_byte = storage == "u8" and b.get("width") == 1 and kind == "component" and f.get("_options") is not None
                if not reason and not one_byte and (storage not in ("f32", "u32", "i32") or b.get("width") != 4 or not scalar_type):
                    reason = "No reviewed scalar backing; structured/reference fields are not supported"
                resource = b.get("resource") or b.get("nativeIdentity") or target.get("resource")
                if kind == "component":
                    resources = target.get("resources", [])
                    resource = resource or (resources[0] if len(resources) == 1 else None)
                    # a support weapon made of several entities: its weapon components are its delivered weapon's (the
                    # loader still checks the resource owns the record before writing)
                    resource = resource or target.get("attackResource")
                    component = profile["components"].get(b.get("component"))
                    if not component or resource is None or b.get("recordIndex") is None:
                        reason = reason or "Component layout or unique resource is not in the reviewed profile"
                    elif b.get("offset", -1) < 0 or b["offset"] + (1 if one_byte else 4) > component["stride"]:
                        reason = reason or "Field is outside the reviewed component layout"
                elif kind not in LAYOUTS:
                    reason = reason or "No reviewed load-time table adapter for " + str(kind)
                if reason:
                    blocked[key] = reason
                    continue
                identity = b.get("nativeIdentity", b.get("recordType", 0)) if kind != "component" else 0
                if kind != "component" and (not isinstance(identity, int) or identity <= 0):
                    blocked[key] = "Missing reviewed settings identity"
                    continue
                if kind != "component" and (b["offset"] < 4 or b["offset"] + 4 > LAYOUTS[kind][1]):
                    blocked[key] = "Field is outside the reviewed settings layout"
                    continue
                bounds = f.get("range") or {}
                item = {"name": f.get("displayName", key), "kind": kind,
                        "component": b.get("component", ""), "resource": resource if kind == "component" else "0x0",
                        "record": b.get("recordIndex", -1) if kind == "component" else identity,
                        "offset": b["offset"], "storage": storage, "min": f.get("min", bounds.get("min")),
                        "max": f.get("max", bounds.get("max")), "unit": f.get("unit"),
                        "shared": bool(f.get("shared") or f.get("affectsMultipleWeapons") or b.get("ownerCount", 1) > 1 or kind != "component"),
                        "sharedConsumers": consumer_names(f.get("sharedConsumers", f.get("sharedWithWeapons", []))),
                        "unverified": not (isinstance(f.get("provenance"), dict) and f["provenance"].get("gameplay_proven", False)),
                        "order": order}
                if reference is not None:
                    item["ref"] = reference
                if f.get("_options") is not None:
                    item["options"] = f["_options"]  # its named values (a flag's No / Yes, an enum's, a slot's)
                # Repeated semantic names can address distinct attack branches. Never choose one silently.
                if key in exported and dict(exported[key], order=order) != item:
                    blocked[key] = "Ambiguous semantic field across attack branches"
                else:
                    exported[key] = item
                if kind in ("ProjectileSettings", "DamageInfo"):
                    direct = "projectiles" if kind == "ProjectileSettings" else "damage"
                    leaf = key.split(".")[-1]
                    direct_target = targets.setdefault(direct, {}).setdefault(str(identity), {"fields": {}, "blocked": {}, "aliases": {}})
                    prior = direct_target["fields"].get(leaf)
                    if prior and (prior["offset"], prior["storage"]) != (item["offset"], item["storage"]):
                        direct_target["blocked"][leaf] = "Conflicting reviewed field layouts"
                    else:
                        direct_target["fields"][leaf] = dict(item, order=DIRECT_ORDER.get(leaf, 100 + int(leaf.rsplit("_", 2)[-2]) if leaf.startswith("status_") else 200))
            for key in blocked:
                exported.pop(key, None)
            # a list's slot on bytes a named field shows already (fire_rate.modes.y is weapon.fire_rate): the named one
            named = {(v["kind"], v["component"], v["record"], v["offset"]) for k, v in exported.items() if k not in slot_keys}
            for key in [k for k in exported if k in slot_keys and (exported[k]["kind"], exported[k]["component"], exported[k]["record"], exported[k]["offset"]) in named]:
                exported.pop(key)
            aliases = {a: k for a, k in ALIASES.items() if k in exported or k in blocked}
            targets.setdefault(section, {})[name] = dict({"fields": exported, "blocked": blocked, "aliases": aliases}, **placement(source, section, target))
    # The Helldiver (HD2R Editor's Helldiver page; domains/helldiver_fields.lua): the avatar type's movement speeds and
    # stamina, its explosion damage share, the six body zones of its health record and the stims a supply gives. Type
    # records every Helldiver this machine simulates reads: shared, applied to Helldivers spawned after.
    hpath = runtime / "domains/helldiver_fields.lua"
    if hpath.exists():
        h = read_literal(hpath)
        records = {name: h.get(name) or {} for name in ("avatar", "health", "inventory")}
        fields = {}
        for order, f in enumerate(h.get("fields") or []):
            r = records.get(f.get("record"))
            if not r or r.get("component") not in profile["components"] or f.get("storage") not in ("f32", "u32", "i32"):
                continue
            key = (f["zone"] + ":" if f.get("zone") else "") + f["id"]
            leaf = f["id"].split(".")[-1]
            item = {"name": HELLDIVER_LABELS.get(leaf, humanize(leaf)), "kind": "component", "component": r["component"], "resource": r.get("resource") or h["resource"],
                    "record": r["recordIndex"], "offset": f["offset"], "storage": f["storage"], "min": f.get("min"), "max": f.get("max"),
                    "unit": f.get("unit"), "shared": True, "sharedConsumers": [], "unverified": True, "order": order}
            if f.get("type") == "enum" and isinstance((h.get("enums") or {}).get(f.get("enum")), dict):
                item["options"] = [[v, humanize(k)] for k, v in sorted(h["enums"][f["enum"]].items(), key=lambda kv: kv[1])]
            if f.get("zone"):
                item["section"] = ZONE_LABELS.get(f["zone"], humanize(f["zone"]))
            fields[key] = item
        if fields:
            targets.setdefault("helldiver", {})["Helldiver"] = {"fields": fields, "blocked": {}, "aliases": {}}
    # A stratagem's drop pod (HD2R Editor's Hellpod rows; pod_payload_authoring, pod_payload_writes.lua): how many of
    # its rack's items the pod spawns, HellpodRackComponentData +556 of the rack entity, 1 to the authored slots.
    pods = runtime / "domains/pod_payload_authoring.lua"
    if pods.exists() and "HellpodRackComponentData" in profile["components"]:
        racks = read_literal(pods)
        for rack in (racks.get("racks") or {}).values():
            if not isinstance(rack.get("spawnCount"), int) or rack.get("writable") is False or rack.get("recordIndex") is None:
                continue
            for consumer in rack.get("consumers") or []:
                target = targets.get("stratagems", {}).get(consumer.get("name"))
                if target is None or "payload.spawn_count" in target["fields"]:
                    continue
                target["fields"]["payload.spawn_count"] = {"name": "Items spawned", "kind": "component", "component": "HellpodRackComponentData",
                    "resource": rack["resource"], "record": rack["recordIndex"], "offset": 556, "storage": "u32", "min": 1,
                    "max": racks.get("authoredSlots") or 4, "unit": "items", "shared": bool(rack.get("shared")),
                    "sharedConsumers": [c.get("name") for c in rack.get("consumers") or []], "unverified": True, "order": 500}
    # The attachment catalog supersedes the older player-weapon capacity refusal: it proves the native default
    # and its complete entity-delta chain. Only reviewed magazine scalars are promoted here.
    attachment_path = runtime / "domains/attachment_authoring.lua"
    if attachment_path.exists():
        attachments = read_literal(attachment_path)
        for name, relationship in attachments["weapons"].items():
            # An empty Lua table reads as an empty list: treat it as an empty mapping here.
            attachment = (attachments["attachments"] or {}).get(relationship.get("default"))
            target = targets.get("weapons", {}).get(name)
            if not attachment or not target:
                continue
            for source, dest in DIRECT_MAGAZINE.items():
                field = (attachment["fields"] or {}).get(source)
                if not field or field.get("editable") is False:
                    continue
                target["fields"][dest] = {"name": dest, "kind": "EntityDelta", "component": str(field["component"]) + ":" + str(field["componentOffset"]),
                    "resource": attachment["resource"], "record": attachment["settingsIndex"], "offset": field["dataOffset"],
                    "storage": field["storage"], "min": field.get("min"), "max": field.get("max"), "unit": "rounds" if dest == "magazine.capacity" else "magazines",
                    "shared": True, "sharedConsumers": [w for w, rel in attachments["weapons"].items() if rel.get("default") == relationship["default"]], "unverified": True,
                    "order": -4 + list(DIRECT_MAGAZINE.values()).index(dest)}
                target["blocked"].pop(dest, None)
                if dest == "magazine.capacity":
                    target["aliases"]["magazine"] = dest
    # A typed row layout applies to every vanilla id, not only rows reached by a player-weapon catalog.
    templates = {
        "projectiles": ("ProjectileSettings", {"velocity": (32, "f32"), "mass": (36, "f32"), "drag": (40, "f32"),
            "gravity": (44, "f32"), "pellet_count": (28, "u32"), "damage": (60, "u32")}),
        "damage": ("DamageInfo", {"standard_damage": (4, "i32"), "durable_damage": (8, "i32"), "ap_direct": (12, "u32"),
            "ap_slight": (16, "u32"), "ap_large": (20, "u32"), "ap_extreme": (24, "u32"), "demolition": (28, "u32"),
            "stagger": (32, "u32"), "push_force": (36, "u32")})}
    for section, (kind, members) in templates.items():
        for identity in range(1, LAYOUTS[kind][0]):
            target = targets.setdefault(section, {}).setdefault(str(identity), {"fields": {}, "blocked": {}, "aliases": {}})
            for name, (offset, storage) in members.items():
                target["fields"].setdefault(name, {"name": name, "kind": kind, "component": "", "resource": "0x0", "record": identity,
                    "offset": offset, "storage": storage, "min": 1 if name == "damage" else None, "max": 649 if name == "damage" else None,
                    "unit": None, "shared": True, "sharedConsumers": [], "unverified": True, "order": DIRECT_ORDER[name]})
    for section, entries in targets.items():
        for target in entries.values():
            for key in target["blocked"]:
                target["fields"].pop(key, None)
            if section == "projectiles":
                target["aliases"].update({"speed": "velocity"})
            elif section == "damage":
                target["aliases"].update({"standard": "standard_damage", "durable": "durable_damage"})
    charges = runtime / "research/stratagem-charges-F5FEE03DCFDB.json"
    aliases = {}
    if charges.exists():
        for row in json.loads(charges.read_text(encoding="utf-8"))["rows"]:
            if row.get("catalogued") in targets.get("stratagems", {}) and row.get("nativeTypeName"):
                aliases[row["nativeTypeName"]] = row["catalogued"]
    delta = schema.get("entity_deltas")
    if delta:
        delta = {k: v for k, v in delta.items() if k != "header"}
        delta["type"] = 0x683E604F
    return {"format": 1, "build": "25480438", "gameDllSha256": profile["gameDllSha256"], "targets": targets,
            "tables": table_metadata(schema), "entityDeltas": delta, "targetAliases": {"stratagems": aliases}}


def native_layouts(result):
    lines = ["/* Generated by ExportStatsSchema.py. Layout metadata only; no game bytes. */", "static const StatsLayout kStatsLayouts[] = {"]
    for table in result["tables"]:
        groups = ",".join("{%du,%du,%du,%du,%du,%du}" % tuple(g[k] for k in ("offset", "type", "extent", "root", "rowOffset", "count")) for g in table["groups"])
        lines.append('{"%s",%du,%du,%du,{%s}},' % (table["kind"], table["size"], table["stride"], len(table["groups"]), groups))
    lines.append("};")
    if result["entityDeltas"]:
        d = result["entityDeltas"]
        lines.append("static const StatsDeltaLayout kStatsDelta = {%du,%du,%du,{%s},{%s}};" % (
            d["size"], d["type"], d["header_offset"], ",".join(str(d[n + "_offset"]) + "u" for n in ("hashmap", "settings", "component", "delta", "data")),
            ",".join(str(d[n + "_count"]) + "u" for n in ("hashmap", "settings", "component", "delta", "data"))))
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    runtime, output = map(Path, sys.argv[1:3])
    profile = json.loads((Path(__file__).resolve().parents[1] / "src/HellForge.Core/entity-profile.json").read_text())
    result = export(runtime, profile)
    output.write_text(json.dumps(result, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")
    if len(sys.argv) > 3:
        Path(sys.argv[3]).write_text(native_layouts(result), encoding="utf-8")
    print("Exported", sum(len(t["fields"]) for c in result["targets"].values() for t in c.values()), "scalar fields")
