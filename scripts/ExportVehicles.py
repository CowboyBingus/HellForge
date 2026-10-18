"""Export the vehicles' pages (HD2R Editor's vehicle pages) from HD2Runtime's entity and vehicle weapon tables.
Metadata only: names, sections, field ids, record ids, offsets, ranges and defaults as labels; no game bytes.

Usage: py scripts/ExportVehicles.py <HD2Runtime> mappings/vehicles.json

Per vehicle: its fields HD2R Editor shows that the stats schema lacks, keyed for the stats file (entity and rotation
fields by their field id; a damage zone's as "<zone>.<field>": "zone_3.armor"), each in HD2R Editor's section (Health
& Armor, or the zone's name: its native name when the game has one, "Zone N" otherwise); the mounts that are shields
(the EXO-55's left arm); and its mount slots (MountComponentData slot k at +24k: the mounted entity's resource) with the
weapons HD2Runtime allows there.
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from ExportReferences import read_table  # noqa: E402

# HD2R Editor's mount labels (catalog.lua MOUNT_LABELS); the others are their humanized mount node
MOUNT_LABELS = {
    'TD-110 Maelstrom': {'attach_tank_gun': 'Main cannon', 'slot_0': 'Main cannon', 'slot_2': 'Driver gun',
                         'slot_3': 'Missile pods (slot 3)', 'slot_4': 'Missile pods (slot 4)'},
    'TD-220 Bastion MK XVI': {'attach_tank_gun': 'Cannon', 'attach_tank_gun_mg': 'Coaxial MG', 'slot_0': 'Cannon', 'slot_1': 'Coaxial MG'},
}


# HD2R Editor's labels for vehicle fields (its string tables; HD2Runtime's entity fields carry none)
LABELS = {'entity.health': 'Health', 'entity.armor': 'Armor', 'zone.armor': 'Armor', 'zone.health': 'Health',
          'zone.affects_main_health': 'Affects main health', 'rotation.turn_speed': 'Turn speed', 'rotation.acceleration': 'Acceleration',
          'rotation.deceleration': 'Deceleration', 'vehicle.steering_response_speed': 'Steering response speed'}


def label_of(f):
    sid = f['semanticFieldId']
    return f.get('displayName') or LABELS.get(sid) or humanize(sid.split('.')[-1])


def humanize(text):
    words = str(text).replace('/', ' ').replace('.', ' ').replace('_', ' ').split()
    out = ' '.join(words)
    return out[:1].upper() + out[1:] if out else str(text)


def mount_label(vehicle, mount, slot):
    labels = MOUNT_LABELS.get(vehicle, {})
    return labels.get(mount or '') or (labels.get(f'slot_{slot}') if slot is not None else None) or humanize(mount or slot or '')


def zone_namer(zones):
    names, count = {}, {}
    for key, z in (zones or {}).items():
        if isinstance(z, dict) and isinstance(z.get('name'), str) and z['name']:
            label = humanize(z['name'])
            names[z.get('zoneId') or key] = label
            count[label] = count.get(label, 0) + 1

    def name(zone):
        label = names.get(zone)
        if label and count[label] > 1:
            label += f' ({humanize(zone)})'
        return label or humanize(zone)
    return name


def field_entry(f, label, section, resource, key):
    b = f['backing']
    return {'key': key, 'label': label, 'section': section, 'component': b['component'], 'resource': b.get('resource') or resource,
            'record': b['recordIndex'], 'offset': b['offset'], 'storage': b['storage'], 'min': f.get('min'), 'max': f.get('max'),
            'unit': f.get('unit'), 'base': f.get('currentDefault'), 'hd2r': f.get('instanceKey'),
            'note': next((n['text'] for n in f.get('notices') or [] if n.get('kind') == 'lifecycle'), None)}


def main(runtime, out):
    domains = Path(runtime) / 'domains'
    entities = read_table(domains / 'entity_authoring.lua')
    weapons = read_table(domains / 'vehicle_weapon_authoring.lua')['weapons']
    vehicles = {}
    for name, v in sorted(entities['vehicles'].items()):
        zone_name = zone_namer(v.get('zones'))
        fields, mounts = [], []
        for f in v['fields']:
            t = f.get('target') or {}
            if not f.get('editable'):
                continue
            if t.get('path') == 'mount' and f.get('type') == 'mounted_weapon_reference':
                slot = t.get('mount')
                info = (v.get('mounts') or {}).get(slot) or {}
                b = f['backing']
                mounts.append({'slot': info.get('slot'), 'key': slot, 'label': mount_label(name, info.get('role') or slot, info.get('slot')),
                               'component': b['component'], 'record': b['recordIndex'], 'offset': b['offset'],
                               'resource': b.get('resource') or v['resource'], 'default': f['currentDefault'],
                               'allowed': f.get('allowedValues') or [], 'hd2r': f.get('instanceKey')})
            elif isinstance(f.get('currentDefault'), (int, float)) and not isinstance(f.get('currentDefault'), bool) and t.get('path') in ('entity', 'damage_zone'):
                leaf = f['semanticFieldId']
                if t['path'] == 'entity':
                    fields.append(field_entry(f, label_of(f), 'Health & Armor', v['resource'], leaf))
                else:
                    fields.append(field_entry(f, label_of(f), zone_name(t['zone']), v['resource'], f'{t["zone"]}.{leaf.split(".", 1)[1]}'))
        # a mount that is a shield (the EXO-55's left arm): its page part is titled "Shield", its fields are the mount's own
        shields = sorted(wkey for wkey, w in weapons.items() if w.get('vehicle') == name and w.get('mountedShield'))
        keys = [f['key'] for f in fields]
        if len(set(keys)) != len(keys):
            raise ValueError(f'{name}: a key twice')
        vehicles[name] = {'resource': v['resource'], 'fields': fields, 'mounts': mounts, 'shields': shields}
    mounted = {mid: {'name': humanize(m.get('displayName') or mid), 'resource': m.get('resource'), 'family': m.get('attackFamily')}
               for mid, m in sorted(entities.get('mountedWeapons', {}).items())}
    data = {'format': 1, 'build': '25480438',
            'note': 'Vehicle pages as HD2R Editor lays them out (HD2Runtime 0.40.1 domains/entity_authoring, vehicle_weapon_authoring; '
                    'scripts/ExportVehicles.py).',
            'vehicles': vehicles, 'mountedWeapons': mounted}
    Path(out).write_text(json.dumps(data, indent=1, ensure_ascii=False) + '\n', encoding='utf-8', newline='\n')
    print(f'{len(vehicles)} vehicles, {sum(len(v["fields"]) for v in vehicles.values())} fields, '
          f'{sum(len(v["mounts"]) for v in vehicles.values())} mount slots, {len(mounted)} mounted weapons')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(*sys.argv[1:])
