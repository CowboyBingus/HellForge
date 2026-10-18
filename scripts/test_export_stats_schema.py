"""Synthetic fixtures for the stats exporter. Run: py -m unittest discover -s scripts -p test_export_stats_schema.py"""
import json
import tempfile
import struct
import unittest
from pathlib import Path
from ExportStatsSchema import export, read_literal, table_metadata, native_layouts


def literal(value):
    if isinstance(value, dict):
        return "{" + ",".join("[" + json.dumps(k) + "]=" + literal(v) for k, v in value.items()) + "}"
    if isinstance(value, list):
        return "{" + ",".join(map(literal, value)) + "}"
    return "nil" if value is None else json.dumps(value)


class ExportTests(unittest.TestCase):
    def test_literal_never_executes_and_preserves_comment_text(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "catalog.lua"
            path.write_text('-- generated\nreturn {["name"]="a--b",["list"]={1,true,nil}}')
            self.assertEqual(read_literal(path), {"name": "a--b", "list": [1, True, None]})
            path.write_text('return {}; os.execute("bad")')
            with self.assertRaises(ValueError):
                read_literal(path)

    def test_metadata_only_and_explicit_refusals(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "schemas").mkdir(); (root / "domains").mkdir()
            (root / "schemas/current.lua").write_text('return {["dll_sha"]="fixture"}')
            field = {"semanticFieldId": "weapon.fire_rate", "displayName": "RPM", "editable": True,
                     "type": "number", "currentDefault": 123456789, "backing": {"kind": "component",
                     "component": "Fixture", "recordIndex": 2, "offset": 4, "storage": "f32", "width": 4}}
            target = {"resources": ["0x1"], "slot": "primary", "fields": [field, {"semanticFieldId": "magazine.capacity", "editable": False}]}
            for name, collection in [("player_weapon", "weapons"), ("support_weapon", "weapons"), ("vehicle_weapon", "weapons"),
                                     ("throwable", "throwables"), ("stratagem", "stratagems")]:
                content = {collection: {"Fixture weapon": target} if name == "player_weapon" else {}}
                (root / "domains" / (name + "_authoring.lua")).write_text("return " + literal(content))
            (root / "domains/entity_authoring.lua").write_text('return {["backpacks"]={},["vehicles"]={}}')
            profile = {"gameDllSha256": "fixture", "components": {"Fixture": {"stride": 16}}}
            result = export(root, profile)
            entry = result["targets"]["weapons"]["Fixture weapon"]
            self.assertIn("weapon.fire_rate", entry["fields"])
            self.assertIn("magazine.capacity", entry["blocked"])
            self.assertEqual((entry["slot"], entry["fields"]["weapon.fire_rate"]["order"]), ("primary", 0))
            self.assertNotIn("123456789", json.dumps(result))
            self.assertEqual(result, export(root, profile))
            attachments = {"weapons": {"Fixture weapon": {"default": "fixture_mag"}}, "attachments": {"fixture_mag": {
                "resource": "0x123", "settingsIndex": 2, "fields": {"attachment.magazine_capacity": {
                    "editable": True, "component": 5, "componentOffset": 136, "dataOffset": 300,
                    "storage": "u32", "min": 1, "max": 200, "currentDefault": 123456789}}}}}
            (root / "domains/attachment_authoring.lua").write_text("return " + literal(attachments))
            promoted = export(root, profile)
            magazine = promoted["targets"]["weapons"]["Fixture weapon"]
            self.assertEqual(magazine["fields"]["magazine.capacity"]["kind"], "EntityDelta")
            self.assertEqual(magazine["fields"]["magazine.capacity"]["sharedConsumers"], ["Fixture weapon"])
            self.assertNotIn("magazine.capacity", magazine["blocked"])
            self.assertNotIn("123456789", json.dumps(promoted))
            profile["gameDllSha256"] = "other"
            with self.assertRaises(ValueError):
                export(root, profile)

    def test_settings_framing_becomes_numeric_metadata(self):
        header = struct.pack("<6I", 0x444C444C, 1, 123, 320, 1, 0).hex()
        schema = {"settings": {"explosion": {"size": 348, "stride": 152, "groups": [
            {"header": header, "offset": 4, "root": 28, "row_offset": 16, "count": 2}]}}}
        tables = table_metadata(schema)
        self.assertEqual(tables[0]["groups"][0]["type"], 123)
        self.assertNotIn(header, json.dumps(tables))
        schema["settings"]["explosion"]["groups"][0]["header"] = "00" * 24
        with self.assertRaises(ValueError):
            table_metadata(schema)

    def test_committed_native_layouts_match_embedded_schema(self):
        root = Path(__file__).resolve().parents[1]
        schema = json.loads((root / "src/HellForge.Core/stats-schema.json").read_text(encoding="utf-8"))
        self.assertEqual(native_layouts(schema), (root / "native/mod-loader/stats-layouts.h").read_text())


if __name__ == "__main__":
    unittest.main()
