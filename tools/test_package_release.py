"""Release range/hash tests with isolated temporary firmware images."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from package_release import REGIONS, copy_licenses, image_manifest, validate_save_schema


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.build = Path(self.temp.name)
        (self.build / "project_description.json").write_text(json.dumps({
            "project_name": "spidarium", "project_version": "1.0.0", "target": "esp32s3"}))
        self.flash = {"flash_files": {}}
        for name, (start, _) in REGIONS.items():
            data = bytearray(256)
            if name == "spidarium.bin":
                data[0] = 0xE9
                struct.pack_into("<I", data, 32, 0xABCD5432)
                data[48:53] = b"1.0.0"
                data[80:89] = b"spidarium"
            (self.build / name).write_bytes(data)
            self.flash["flash_files"][hex(start)] = name
        self.write_config()

    def write_config(self):
        (self.build / "flasher_args.json").write_text(json.dumps(self.flash))

    def test_hashes_and_nvs_gap(self):
        result = image_manifest(self.build, "1.0.0")
        self.assertEqual(len(result["files"]), 5)
        for item in result["files"]:
            data = (self.build / item["path"]).read_bytes()
            self.assertEqual(item["sha256"], hashlib.sha256(data).hexdigest())
            self.assertFalse(item["offset"] < 0x10000 and item["offset"] + len(data) > 0x9000)

    def test_nvs_image_rejected(self):
        self.flash["flash_files"]["0x9000"] = "nvs.bin"
        (self.build / "nvs.bin").write_bytes(b"private")
        self.write_config()
        with self.assertRaisesRegex(ValueError, "Unexpected"):
            image_manifest(self.build, "1.0.0")

    def test_partition_overflow(self):
        (self.build / "partition-table.bin").write_bytes(bytes(4097))
        with self.assertRaisesRegex(ValueError, "overflow"):
            image_manifest(self.build, "1.0.0")

    def test_wrong_version(self):
        with self.assertRaisesRegex(ValueError, "version"):
            image_manifest(self.build, "2.0.0")

    def test_binary_version_checked(self):
        data = bytearray((self.build / "spidarium.bin").read_bytes())
        data[48:53] = b"0.0.0"
        (self.build / "spidarium.bin").write_bytes(data)
        with self.assertRaisesRegex(ValueError, "wrong version"):
            image_manifest(self.build, "1.0.0")

    def schema_fixture(self, version=8):
        (self.build / "site").mkdir(exist_ok=True)
        (self.build / "main").mkdir(exist_ok=True)
        (self.build / "main" / "game_config.h").write_text("#define SAVE_VERSION 8\n")
        schema = {"version": version, "headerSize": 12, "payloadSizes": [120, 236],
                  "fields": [{"name": "decor", "type": "decor[12]", "length": 72, "offset": 164}]}
        path = self.build / "site" / "save-schema.json"
        path.write_text(json.dumps(schema))
        return path, schema

    def test_current_schema_is_accepted(self):
        path, _ = self.schema_fixture()
        self.assertEqual(validate_save_schema(self.build), path)

    def test_stale_schema_is_rejected_before_publication(self):
        self.schema_fixture(5)
        with self.assertRaisesRegex(ValueError, "Stale save schema"):
            validate_save_schema(self.build)

    def test_schema_fields_must_cover_but_not_overflow_payload(self):
        path, schema = self.schema_fixture()
        for width in (71, 73):
            schema["fields"][0]["length"] = width
            path.write_text(json.dumps(schema))
            with self.subTest(width=width), self.assertRaisesRegex(ValueError, "schema"):
                validate_save_schema(self.build)

    def test_nested_licenses_and_spdx_texts_included(self):
        source = self.build / "vendor"
        (source / "nested" / "LICENSES").mkdir(parents=True)
        (source / "nested" / "LICENSES" / "MIT.txt").write_text("SPDX text")
        (source / "nested" / "COPYING").write_text("license")
        (source / "nested" / "implementation.c").write_text("not a notice")
        destination = self.build / "notices"
        copy_licenses(source, destination)
        self.assertEqual((destination / "nested" / "LICENSES" / "MIT.txt").read_text(), "SPDX text")
        self.assertEqual((destination / "nested" / "COPYING").read_text(), "license")
        self.assertFalse((destination / "nested" / "implementation.c").exists())
        with self.assertRaisesRegex(ValueError, "Missing license directory"):
            copy_licenses(source / "missing", destination)

    def test_v9_tail_padding_matches_native_struct_alignment(self):
        path, schema = self.schema_fixture()
        (self.build / "main" / "game_config.h").write_text("#define SAVE_VERSION 9\n")
        schema.update(version=9, payloadSizes=[120, 248])
        schema["fields"].extend([
            {"name": "backgrounds_owned", "type": "u32", "length": 0, "offset": 236},
            {"name": "rtc_wake", "type": "u8", "length": 0, "offset": 244},
        ])
        path.write_text(json.dumps(schema))
        self.assertEqual(validate_save_schema(self.build), path)
        for size in (245, 246, 252):
            schema["payloadSizes"][1] = size
            path.write_text(json.dumps(schema))
            with self.subTest(size=size), self.assertRaisesRegex(ValueError, "complete current payload"):
                validate_save_schema(self.build)

    def test_v11_music_preferences_are_required(self):
        path, schema = self.schema_fixture()
        (self.build / "main" / "game_config.h").write_text("#define SAVE_VERSION 11\n")
        schema.update(version=11, payloadSizes=[120, 260], maxPayload=20480,
                      decorTail={"offset": 260, "stride": 6, "countField": "decor_extra_count"})
        schema["fields"].extend([
            {"name": "decor_extra_count", "type": "u32", "length": 0, "offset": 256},
            {"name": "music_volume", "type": "u8", "length": 0, "offset": 254, "minVersion": 11},
            {"name": "music_track", "type": "u8", "length": 0, "offset": 255, "minVersion": 11},
        ])
        path.write_text(json.dumps(schema))
        self.assertEqual(validate_save_schema(self.build), path)
        schema["fields"].pop()
        path.write_text(json.dumps(schema))
        with self.assertRaisesRegex(ValueError, "music preferences"):
            validate_save_schema(self.build)

    def test_v10_variable_tail_metadata_is_required(self):
        path, schema = self.schema_fixture()
        (self.build / "main" / "game_config.h").write_text("#define SAVE_VERSION 10\n")
        tail = {"offset": 260, "stride": 6, "countField": "decor_extra_count"}
        schema.update(version=10, payloadSizes=[120, 260], maxPayload=20480, decorTail=tail)
        schema["fields"].append({"name": "decor_extra_count", "type": "u32",
                                 "length": 0, "offset": 256})
        path.write_text(json.dumps(schema))
        self.assertEqual(validate_save_schema(self.build), path)
        for invalid in (None, {**tail, "offset": 248}, {**tail, "stride": 8},
                        {**tail, "countField": "decor_owned"}):
            schema["decorTail"] = invalid
            path.write_text(json.dumps(schema))
            with self.subTest(tail=invalid), self.assertRaisesRegex(ValueError, "variable decoration"):
                validate_save_schema(self.build)


if __name__ == "__main__":
    unittest.main()
