"""Export the weapons' attachments (HD2R Editor's Magazines, Muzzles, Optics and Underbarrels groups) from HD2Runtime's
attachment authoring table. Metadata only: names, field ids, entity delta addresses, ranges and defaults as labels.

Usage: py scripts/ExportAttachments.py <HD2Runtime> mappings/attachments.json

An attachment's stats are entity deltas (EntityDelta: component:member, the delta's data offset, the attachment's
settings index and resource), shared by every weapon that equips it; they apply when a weapon is built from the next
boot (the stats pass writes the delta table once). Per weapon: the attachments each slot offers (its magazines, and
its muzzles, optics and underbarrels), each with its fields.
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from ExportReferences import read_table  # noqa: E402

# HD2R Editor's labels (catalog.lua MODIFIER_LABELS; attachment.ergonomics_modifier 'Ergonomics')
MODIFIER_LABELS = {'sway': 'Sway multiplier', 'recoil_horizontal': 'Horizontal recoil multiplier', 'recoil_vertical': 'Vertical recoil multiplier',
                   'climb_horizontal': 'Horizontal climb multiplier', 'climb_vertical': 'Vertical climb multiplier',
                   'spread_horizontal': 'Horizontal spread multiplier', 'spread_vertical': 'Vertical spread multiplier'}
GROUPS = [('magazine', 'Magazines'), ('muzzle', 'Muzzles'), ('optics', 'Optics'), ('underbarrel', 'Underbarrels')]


def humanize(text):
    words = str(text).replace('/', ' ').replace('.', ' ').replace('_', ' ').split()
    out = ' '.join(words)
    return out[:1].upper() + out[1:] if out else str(text)


def label_of(fid, f):
    if f.get('displayName'):
        return f['displayName']
    if fid.startswith('attachment.modifier.') and fid[len('attachment.modifier.'):] in MODIFIER_LABELS:
        return MODIFIER_LABELS[fid[len('attachment.modifier.'):]]
    if fid == 'attachment.ergonomics_modifier':
        return 'Ergonomics'
    return humanize(fid.split('.', 1)[1] if fid.startswith('attachment.') else fid)


def main(runtime, out):
    a = read_table(Path(runtime) / 'domains' / 'attachment_authoring.lua')
    attachments = {}
    for aid, x in sorted(a['attachments'].items()):
        fields = []
        for fid in sorted(x.get('fields') or {}):
            f = x['fields'][fid]
            if not isinstance(f.get('currentDefault'), (int, float)) or isinstance(f.get('currentDefault'), bool) or f.get('editable') is False:
                continue
            if f.get('component') is None or f.get('componentOffset') is None or f.get('dataOffset') is None:
                continue
            fields.append({'id': fid, 'label': label_of(fid, f), 'component': f'{f["component"]}:{f["componentOffset"]}', 'offset': f['dataOffset'],
                           'storage': f.get('storage') or 'f32', 'min': f.get('min'), 'max': f.get('max'),
                           'unit': f.get('unit') or ('seconds' if fid == 'attachment.reload_duration' else None), 'base': f.get('currentDefault')})
        attachments[aid] = {'name': x.get('name') or humanize(aid.split('/')[-2]), 'slot': x.get('slot') or 'magazine', 'kind': x.get('kind'),
                            'resource': x['resource'], 'record': x['settingsIndex'], 'fields': fields}
    weapons = {}

    def add(weapon, slot, listing):
        if not isinstance(listing, dict):
            return
        options = [o for o in listing.get('options') or [] if isinstance(o, dict)]
        ids = [o.get('attachment') for o in options]
        names = {o['attachment']: o['name'] for o in options if o.get('attachment') and o.get('name')}
        if isinstance(listing.get('default'), str):
            ids.append(listing['default'])
        for aid in ids:
            if aid in attachments:
                group = weapons.setdefault(weapon, {}).setdefault(slot, {'default': listing.get('default'), 'attachments': [], 'names': {}})
                if aid not in group['attachments']:
                    group['attachments'].append(aid)
                if aid in names:  # the name the weapon's armory shows ("Extended Magazine")
                    group['names'][aid] = names[aid]

    for weapon, listing in sorted(a['weapons'].items()):
        add(weapon, 'magazine', listing)
    for weapon, slots in sorted(a['slots'].items()):
        for slot, listing in sorted(slots.items()):
            add(weapon, slot, listing)
    for w in weapons.values():
        for g in w.values():
            g['attachments'].sort(key=lambda aid: (g['names'].get(aid) or attachments[aid]['name']).lower())
    used = {aid for w in weapons.values() for g in w.values() for aid in g['attachments']}
    data = {'format': 1, 'build': '25480438', 'groups': [{'slot': s, 'label': l} for s, l in GROUPS],
            'note': 'Weapon attachments as HD2R Editor groups them (HD2Runtime 0.40.1 domains/attachment_authoring; scripts/ExportAttachments.py).',
            'attachments': {k: v for k, v in attachments.items() if k in used}, 'weapons': weapons}
    Path(out).write_text(json.dumps(data, indent=1, ensure_ascii=False) + '\n', encoding='utf-8', newline='\n')
    print(f'{len(weapons)} weapons, {len(used)} attachments, {sum(len(attachments[k]["fields"]) for k in used)} fields')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(*sys.argv[1:])
