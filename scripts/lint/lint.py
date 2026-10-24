"""HellForge's structure lint: Bingus Shared Runtime's code-structure rules (R10-R15) for C#, C and C++.

  python scripts/lint/lint.py                         # unit x rule table
  python scripts/lint/lint.py scan --list             # every violation, with its detail
  python scripts/lint/lint.py scan --json             # machine-readable output
  python scripts/lint/lint.py check                   # exit 1 when anything is new or worse than the baseline
  python scripts/lint/lint.py update                  # rewrite the baseline after fixes; refuses increases
  python scripts/lint/lint.py update --allow-increase # accept increases (reviewed ones)

Rules (structure.py measures them): R10 cognitive complexity, R11 function size, R12 parameters, R13 stack size (C
and C++), R14 duplicated code, R15 file size. Suppress one on its reported line with `// lint-ok: R<n> <reason>`.
The baseline holds the measured value per (unit, file, rule, line text), so a known function that gets worse fails
`check`. Exit codes: 0 OK; 1 new violations or a refused update; 2 a configuration or parse error.
"""
from __future__ import annotations

import argparse
import fnmatch
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import structure  # noqa: E402

DEFAULT_CONFIG = HERE / 'lint-config.json'
DEFAULT_BASELINE = HERE / 'lint-baseline.json'
RULES = {
    'R10': 'cognitive-complexity',
    'R11': 'function-size',
    'R12': 'parameters',
    'R13': 'stack-size',
    'R14': 'duplicated-code',
    'R15': 'module-size',
}
SUPPRESS_RE = re.compile(r'(?://|/\*|\*)\s*lint-ok:\s*((?:R\d+\s*[, ]\s*)*R\d+)\s+\S')
FAMILIES = {'cs': 'cs', 'c': 'c', 'cpp': 'c'}        # R14 compares within a family: C# alone, C with C++


class LintError(Exception):
    pass


@dataclass
class Unit:
    name: str
    path: Path
    sources: list[str]


@dataclass
class Violation:
    unit: str
    rule: str
    file: str
    line: int
    text: str
    detail: str
    value: int

    def key(self) -> tuple[str, str, str, str]:
        return (self.unit, self.file, self.rule, normalise(self.text))

    def as_json(self) -> dict:
        return {'unit': self.unit, 'rule': self.rule, 'name': RULES[self.rule], 'file': self.file, 'line': self.line,
                'text': self.text, 'detail': self.detail, 'value': self.value}


@dataclass
class Scan:
    units: list[Unit]
    violations: list[Violation] = field(default_factory=list)
    suppressed: list[Violation] = field(default_factory=list)
    files: int = 0
    functions: int = 0


@dataclass
class Parsed:
    unit: Unit
    rel: str
    lines: list[str]
    model: structure.Model
    language: str


def normalise(text: str) -> str:
    return ' '.join(text.split())


def suppressed_rules(line: str) -> set[str]:
    match = SUPPRESS_RE.search(line)
    return set(re.findall(r'R\d+', match.group(1))) if match else set()


# ----------------------------------------------------------------------------- rules

def structure_findings(model: structure.Model, language: str, limits: dict) -> list[tuple]:
    """R10-R13 and R15 for one file: (rule, line, detail, measured value)."""
    found = []
    for function in model.functions:
        where = f'{function.name}: '
        if function.cognitive > limits['cognitive']:
            found.append(('R10', function.line, f"{where}cognitive complexity {function.cognitive} (limit "
                          f"{limits['cognitive']}), nesting depth {function.nesting}", function.cognitive))
        size = len(function.lines)
        if size > limits['function_lines']:
            found.append(('R11', function.line, f"{where}{size} code lines (limit {limits['function_lines']})", size))
        if function.parameters > limits['parameters']:
            found.append(('R12', function.line, f"{where}{function.parameters} parameters (limit "
                          f"{limits['parameters']})", function.parameters))
        if language != 'cs' and function.stack_bytes > limits['stack_bytes']:
            found.append(('R13', function.line, f"{where}{function.stack_bytes} bytes of local arrays on the stack "
                          f"(limit {limits['stack_bytes']}); make them static or allocate them once",
                          function.stack_bytes))
    if model.code_lines > limits['module_lines']:
        found.append(('R15', 1, f"{model.code_lines} code lines in one file (limit {limits['module_lines']})",
                      model.code_lines))
    return found


def violations(unit: Unit, rel: str, lines: list[str], findings: list[tuple]) -> tuple[list[Violation], list[Violation]]:
    """One violation per rule and line (details joined, values added); those marked lint-ok are suppressed."""
    merged: dict[tuple[str, int], tuple[list[str], int]] = {}
    for rule, line, detail, value in findings:
        details, total = merged.get((rule, line), ([], 0))
        if detail not in details:
            details.append(detail)
        merged[(rule, line)] = (details, total + value)
    kept, dropped = [], []
    for (rule, line), (details, value) in sorted(merged.items(), key=lambda item: (item[0][1], item[0][0])):
        text = lines[line - 1] if 0 < line <= len(lines) else ''
        violation = Violation(unit.name, rule, rel, line, text.strip(), '; '.join(details), value)
        (dropped if rule in suppressed_rules(text) else kept).append(violation)
    return kept, dropped


def duplicated_code(parsed: list[Parsed], length: int, report: set[str]) -> tuple[list[Violation], list[Violation]]:
    """R14: regions of at least `length` tokens that occur again in a file of the same language family."""
    kept, dropped = [], []
    for family in sorted(set(FAMILIES.values())):
        group = [p for p in parsed if FAMILIES[p.language] == family]
        files = [p.model.tokens for p in group]
        findings: dict[int, list[tuple]] = {}
        for clone in structure.duplicates(files, length):
            here = group[clone.file]
            if here.unit.name not in report:
                continue
            other = group[clone.other_file]
            other_line = files[clone.other_file][clone.other_start].line
            where = other.rel if (other.unit.name, other.rel) == (here.unit.name, here.rel) else \
                f'{other.unit.name} {other.rel}'
            tokens = clone.end - clone.start
            findings.setdefault(clone.file, []).append((
                'R14', files[clone.file][clone.start].line,
                f'{tokens} tokens also at {where}:{other_line}; share one copy', tokens))
        for index, found in findings.items():
            k, d = violations(group[index].unit, group[index].rel, group[index].lines, found)
            kept += k
            dropped += d
    return kept, dropped


# ----------------------------------------------------------------------------- scanning

def unit_files(unit: Unit, excludes: list[str], root: Path) -> list[Path]:
    if not unit.path.is_dir():
        raise LintError(f'{unit.name}: folder not found: {unit.path}')
    found: set[Path] = set()
    for pattern in unit.sources:
        found.update(p for p in unit.path.glob(pattern) if p.is_file() and p.suffix.lower() in structure.LANGUAGES)
    kept = []
    for path in sorted(found):
        rel = path.resolve().relative_to(root).as_posix() if path.resolve().is_relative_to(root) else path.as_posix()
        if not any(fnmatch.fnmatch(rel, pattern) for pattern in excludes):
            kept.append(path)
    return kept


def parse(unit: Unit, path: Path) -> Parsed:
    text = path.read_text(encoding='utf-8-sig', errors='replace')
    language = structure.LANGUAGES[path.suffix.lower()]
    rel = path.relative_to(unit.path).as_posix()
    try:
        model = structure.analyse(text, language)
    except structure.ParseError as problem:
        raise LintError(f'{unit.name}/{rel}: {problem}') from None
    return Parsed(unit, rel, text.splitlines(), model, language)


def scan(units: list[Unit], corpus: list[Unit], limits: dict, excludes: list[str], root: Path) -> Scan:
    """Scans units; R14 compares them with every unit of the corpus, whatever --only selected."""
    result = Scan(units)
    selected = {unit.name for unit in units}
    parsed: list[Parsed] = []
    for unit in corpus:
        for path in unit_files(unit, excludes, root):
            item = parse(unit, path)
            parsed.append(item)
            if unit.name not in selected:
                continue
            kept, dropped = violations(unit, item.rel, item.lines,
                                       structure_findings(item.model, item.language, limits))
            result.violations += kept
            result.suppressed += dropped
            result.files += 1
            result.functions += len(item.model.functions)
    kept, dropped = duplicated_code(parsed, limits['clone_tokens'], selected)
    result.violations += kept
    result.suppressed += dropped
    result.violations.sort(key=lambda v: (v.unit, v.file, v.line, v.rule))
    return result


# ----------------------------------------------------------------------------- configuration

def load_config(path: Path, root: Path, overrides: list[str], only: list[str]) -> tuple[list[Unit], list[Unit], dict, list[str]]:
    """(selected units, every unit, limits, exclude patterns). A unit is each folder under a `discover` folder, or
    one named in `units`."""
    try:
        config = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError) as problem:
        raise LintError(f'cannot read config {path}: {problem}') from None
    unknown = set(config.get('limits', {})) - set(structure.LIMITS)
    if unknown:
        raise LintError(f'unknown limits in {path.name}: {", ".join(sorted(unknown))}')
    limits = dict(structure.LIMITS, **config.get('limits', {}))
    sources = list(config.get('default_sources', ['**/*']))
    units: dict[str, Unit] = {}
    for folder in config.get('discover', []):
        base = root / folder
        if not base.is_dir():
            raise LintError(f'discover folder not found: {base}')
        for child in sorted(p for p in base.iterdir() if p.is_dir()):
            units[child.name] = Unit(child.name, child, sources)
    for entry in config.get('units', []):
        unit_path = (root / entry['path']) if 'path' in entry else units[entry['name']].path
        units[entry['name']] = Unit(entry['name'], unit_path, list(entry.get('sources', sources)))
    for override in overrides:
        name, sep, folder = override.partition('=')
        if not sep or not name or not folder:
            raise LintError(f'--unit expects NAME=PATH, got {override!r}')
        units[name] = Unit(name, Path(folder).resolve(), units[name].sources if name in units else sources)
    corpus = list(units.values())
    selected = corpus
    if only:
        wanted = {name for item in only for name in item.split(',') if name}
        missing = wanted - set(units)
        if missing:
            raise LintError(f'--only names unknown units: {", ".join(sorted(missing))}')
        selected = [unit for unit in corpus if unit.name in wanted]
    return selected, corpus, limits, list(config.get('exclude', []))


# ----------------------------------------------------------------------------- baseline

Key = tuple[str, str, str, str]  # unit, file, rule, normalised line text


def counts(found: list[Violation]) -> dict[Key, int]:
    """Per identity: the measured values added up."""
    result: dict[Key, int] = {}
    for violation in found:
        result[violation.key()] = result.get(violation.key(), 0) + violation.value
    return result


def read_baseline(path: Path) -> dict[Key, int]:
    if not path.exists():
        return {}
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
        return {(e['unit'], e['file'], e['rule'], e['text']): int(e['value']) for e in data['entries']}
    except (OSError, ValueError, KeyError, TypeError) as problem:
        raise LintError(f'cannot read baseline {path}: {problem}') from None


def write_baseline(path: Path, entries: dict[Key, int]) -> None:
    rows = [json.dumps({'unit': k[0], 'file': k[1], 'rule': k[2], 'text': k[3], 'value': n}, ensure_ascii=False)
            for k, n in sorted(entries.items()) if n > 0]
    body = ('{\n  "format": 1,\n'
            '  "note": "Known structure violations per (unit, file, rule, normalised line text) with the measured value. '
            '`python scripts/lint/lint.py check` fails when one is new or worse; `update` rewrites this file.",\n'
            '  "entries": [' + ('\n    ' + ',\n    '.join(rows) + '\n  ' if rows else '') + ']\n}\n')
    path.write_text(body, encoding='utf-8', newline='\n')


def in_scope(key: Key, result: Scan) -> bool:
    return key[0] in {unit.name for unit in result.units}


def compare(baseline: dict[Key, int], current: dict[Key, int], result: Scan):
    increases, decreases = [], []
    for key in sorted(set(baseline) | set(current)):
        if not in_scope(key, result):
            continue
        old, new = baseline.get(key, 0), current.get(key, 0)
        if new > old:
            increases.append((key, old, new))
        elif new < old:
            decreases.append((key, old, new))
    return increases, decreases


# ----------------------------------------------------------------------------- output

def summary_table(result: Scan) -> str:
    rules = list(RULES)
    table = {unit.name: {rule: 0 for rule in rules} for unit in result.units}
    for violation in result.violations:
        table[violation.unit][violation.rule] += 1
    width = max([len('unit')] + [len(name) for name in table]) + 2
    lines = ['unit'.ljust(width) + ''.join(r.rjust(5) for r in rules) + 'total'.rjust(7)]
    totals = {rule: 0 for rule in rules}
    for name, row in table.items():
        lines.append(name.ljust(width) + ''.join(str(row[r]).rjust(5) for r in rules) + str(sum(row.values())).rjust(7))
        for rule in rules:
            totals[rule] += row[rule]
    lines.append('total'.ljust(width) + ''.join(str(totals[r]).rjust(5) for r in rules) + str(sum(totals.values())).rjust(7))
    return '\n'.join(lines)


def describe(violation: Violation) -> str:
    return (f'{violation.rule} {RULES[violation.rule]}  {violation.unit} {violation.file}:{violation.line}\n'
            f'    {violation.text}\n    -> {violation.detail}')


def print_identities(title: str, changes, result: Scan) -> None:
    by_key: dict[Key, list[Violation]] = {}
    for violation in result.violations:
        by_key.setdefault(violation.key(), []).append(violation)
    for key, old, new in changes:
        unit, file, rule, text = key
        lines = ','.join(str(v.line) for v in by_key.get(key, []))
        print(f'{title} {rule} {RULES[rule]}  {unit} {file}{":" + lines if lines else ""}  (baseline {old}, now {new})')
        print(f'    {text}')
        for violation in by_key.get(key, [])[:1]:
            print(f'    -> {violation.detail}')


# ----------------------------------------------------------------------------- commands

def command_scan(result: Scan, args) -> int:
    if args.json:
        summary: dict[str, dict[str, int]] = {unit.name: {} for unit in result.units}
        for violation in result.violations:
            summary[violation.unit][violation.rule] = summary[violation.unit].get(violation.rule, 0) + 1
        print(json.dumps({'rules': RULES, 'files': result.files, 'functions': result.functions, 'summary': summary,
                          'violations': [v.as_json() for v in result.violations],
                          'suppressed': [v.as_json() for v in result.suppressed]}, indent=2, ensure_ascii=False))
        return 0
    if args.list:
        for violation in result.violations:
            print(describe(violation))
        print()
    print(summary_table(result))
    print(f'\n{len(result.violations)} violation(s) in {result.files} file(s), {result.functions} function(s); '
          f'{len(result.suppressed)} suppressed by lint-ok')
    return 0


def command_check(result: Scan, args) -> int:
    baseline = read_baseline(args.baseline)
    current = counts(result.violations)
    increases, decreases = compare(baseline, current, result)
    print_identities('NEW', increases, result)
    print_identities('fixed - run update:', decreases, result)
    known = sum(1 for key, n in current.items() if 0 < n <= baseline.get(key, 0))
    print(f'lint check: {len(increases)} new, {len(decreases)} fixed, {known} known in {len(result.units)} unit(s); '
          f'baseline {args.baseline.name}')
    return 1 if increases else 0


def command_update(result: Scan, args) -> int:
    baseline = read_baseline(args.baseline)
    current = counts(result.violations)
    increases, decreases = compare(baseline, current, result)
    if increases and not args.allow_increase:
        print_identities('INCREASE', increases, result)
        print(f'lint update refused: {len(increases)} identity value(s) would rise; fix them, add a '
              f'`// lint-ok: R<n> <reason>` comment, or rerun with --allow-increase')
        return 1
    entries = {key: n for key, n in baseline.items() if not in_scope(key, result)}
    entries.update(current)
    write_baseline(args.baseline, entries)
    print(f'baseline updated: {sum(1 for n in entries.values() if n > 0)} identities; {len(decreases)} lower or '
          f'gone, {len(increases)} higher or new than before')
    return 0


def main(argv: list[str] | None = None) -> int:
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(errors='replace')
    parser = argparse.ArgumentParser(prog='lint.py', description=__doc__.split('\n\n')[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', nargs='?', default='scan', choices=('scan', 'check', 'update'))
    parser.add_argument('--unit', action='append', default=[], metavar='NAME=PATH', help='lint this folder as NAME')
    parser.add_argument('--only', action='append', default=[], metavar='NAME', help='limit to these units')
    parser.add_argument('--json', action='store_true', help='scan: machine-readable output')
    parser.add_argument('--list', action='store_true', help='scan: print every violation')
    parser.add_argument('--allow-increase', action='store_true', help='update: accept values that rose')
    parser.add_argument('--config', type=Path, default=DEFAULT_CONFIG)
    parser.add_argument('--baseline', type=Path, default=DEFAULT_BASELINE)
    parser.add_argument('--root', type=Path, default=HERE.parent.parent,
                        help='the folder config paths start from (default: the repository)')
    args = parser.parse_args(argv)
    try:
        root = args.root.resolve()
        units, corpus, limits, excludes = load_config(args.config, root, args.unit, args.only)
        result = scan(units, corpus, limits, excludes, root)
        return {'scan': command_scan, 'check': command_check, 'update': command_update}[args.command](result, args)
    except LintError as problem:
        print(f'lint.py: {problem}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
