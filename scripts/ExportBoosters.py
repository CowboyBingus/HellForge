"""Export the boosters' pages (HD2R Editor's Boosters category) from HD2Runtime's booster authoring table.
Metadata only: names, sections, field ids, record ids, offsets, ranges and defaults as labels; no game bytes.

Usage: py scripts/ExportBoosters.py <HD2Runtime> mappings/boosters.json

Each booster is a stats target (HellForge's "boosters" category) named as the game names it; its fields are keyed by
HD2Runtime's semanticFieldId (the HD2R Editor's preset keys end in it), grouped in HD2R Editor's sections. Backings:
  booster_table    BoosterSettings, the booster's row (its value) +8
  explosion        ExplosionSettings, the explosion type
  explosion_damage DamageInfo, the explosion's damage type
  status_damage    DamageInfo, the status's damage type
  status_row       StatusEffectSettings, the status type
  stratagem_row    StratagemDefinition, the granted stratagem's id
  component        an entity's component record (resource, record index)
  status_multiplier StatusMultiplier: a stat of the status's multiplier array (StatusEffectSettings +104 points to it);
                   record the status type, component the stat id, offset index * 8 + 4 (the loader checks the stat)
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from ExportReferences import read_table  # noqa: E402

SECTIONS = {'tuning': 'Tuning', 'explosion': 'Explosion', 'status_effect': 'Status Effect', 'status_damage': 'Status Damage',
            'granted_stratagem': 'Granted Stratagem', 'deployed_entity': 'Deployed Entity'}


def main(runtime, out):
    boosters = read_table(Path(runtime) / 'domains' / 'booster_authoring.lua')['boosters']
    result = {}
    for name, b in sorted(boosters.items()):
        fields, unsupported = {}, []
        for target_key in sorted(b.get('targets') or {}):
            t = b['targets'][target_key]
            for fid in sorted(t['fields']):
                f = t['fields'][fid]
                back = f['backing']
                kind = back['kind']
                entry = {'label': f['displayName'], 'section': SECTIONS.get(target_key, target_key), 'offset': back.get('offset'),
                         'storage': back['storage'], 'unit': f.get('unit'), 'base': f.get('currentDefault'),
                         'shared': bool(f.get('shared')), 'hd2r': f['instanceKey']}
                rng = f.get('range') or {}
                entry['min'], entry['max'] = rng.get('min'), rng.get('max')
                if kind == 'booster_table':
                    entry.update(kind='BoosterSettings', record=back['row'])
                elif kind == 'explosion':
                    entry.update(kind='ExplosionSettings', record=t['explosionType'])
                elif kind in ('explosion_damage', 'status_damage'):
                    entry.update(kind='DamageInfo', record=t['damageType'])
                elif kind == 'status_row':
                    entry.update(kind='StatusEffectSettings', record=t['statusType'])
                elif kind == 'stratagem_row':
                    entry.update(kind='StratagemDefinition', record=t['record']['id'], stratagemType=t['stratagemType'])
                elif kind == 'status_multiplier':
                    entry.update(kind='StatusMultiplier', record=t['statusType'], component=str(back['stat']), offset=back['index'] * 8 + 4)
                elif kind == 'component':
                    entry.update(kind='component', component=back['component'], resource=t['resource'], record=back['recordIndex'])
                else:
                    unsupported.append({'key': fid, 'label': f['displayName'], 'backing': kind, 'hd2r': f['instanceKey']})
                    continue
                if fid in fields:
                    raise ValueError(f'{name}: {fid} twice')
                fields[fid] = entry
        result[name] = {'value': b['enumValue'], 'native': b['nativeName'], 'fields': fields}
        if unsupported:
            result[name]['unsupported'] = unsupported
    data = {
        'format': 2, 'build': '25480438',
        'note': 'Every booster as HD2R Editor lists it (HD2Runtime 0.40.1 domains/booster_authoring; written by '
                'scripts/ExportBoosters.py). No booster edit is gameplay-tested in HD2Runtime.',
        'boosters': result,
    }
    Path(out).write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8', newline='\n')
    print(f'{len(result)} boosters, {sum(len(b["fields"]) for b in result.values())} fields, '
          f'{sum(len(b.get("unsupported", [])) for b in result.values())} unsupported')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(*sys.argv[1:])
