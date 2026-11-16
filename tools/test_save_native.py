"""Host execution of the actual native save validators, migrations and ABI."""
from pathlib import Path
import json
import shutil
import subprocess
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[1]


class NativeSaveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("gcc")
        if not compiler:
            raise unittest.SkipTest("gcc is required for native persistence tests")
        cls.work = ROOT / "release" / ("save-host-" + uuid.uuid4().hex)
        cls.work.mkdir(parents=True)
        cls.addClassCleanup(shutil.rmtree, cls.work)
        save = (ROOT / "main" / "save.c").read_text()
        companion = (ROOT / "main" / "companion.c").read_text()
        life = (ROOT / "main" / "life.c").read_text()
        # Execute production functions without linking ESP-IDF NVS/FreeRTOS.
        sections = [
            save[save.index("typedef struct {"):save.index("static bool s_nvs_ready;")],
            save[save.index("static void migrate_automation"):save.index("static void save_worker")],
            companion[companion.index("void companion_defaults"):companion.index("static void remember")],
            life[life.index("static bool s_testing"):],
            save[save.index("static bool raw_valid_state("):save.index("bool save_write_raw(")],
            save[save.index("bool save_format_test("):],
        ]
        source = r'''
typedef struct lv_draw_buf_t lv_draw_buf_t;
#include "game.h"
#include "game_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
static struct tm *host_gmtime_r(const time_t *clock, struct tm *result) {
    return gmtime_s(result, clock) ? NULL : result;
}
#define gmtime_r host_gmtime_r
#endif
#define SAVE_MAX_PAYLOAD 23308u
#define SAVE_MAX_RAW (SAVE_MAX_PAYLOAD + 12u)
#define AUDIO_VOLUME 70
#define AUDIO_MUSIC_TRACK_COUNT 10
#define THEME_COUNT 48
#define PREY_SPECIES_COUNT 8
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define heap_caps_malloc(size, caps) malloc(size)
#define heap_caps_free(ptr) free(ptr)
game_state_t g;
uint32_t lv_tick_get(void) { return 1000; }
bool save_request(const void *src, size_t size) { (void)src; (void)size; return true; }
bool save_request_tracked(const void *src, size_t size, uint32_t *ticket) {
    (void)src; (void)size; *ticket = 1; return true;
}
bool save_tracked_result(uint32_t ticket, bool *ok) { (void)ticket; (void)ok; return false; }
bool save_busy(void) { return false; }
bool net_busy(void) { return false; }
bool sd_busy(void) { return false; }
void decor_clear_slots(decor_item_t *items, int count) {
    memset(items, 0, count * sizeof(*items));
    for (int i = 0; i < count; i++) items[i].kind = 255;
}
'''
        source += "\n".join(sections)
        source += r'''
int main(int argc, char **argv) {
    if (argc == 2) {
        FILE *file = fopen(argv[1], "rb");
        assert(file);
        unsigned char bytes[SAVE_MAX_RAW];
        size_t size = fread(bytes, 1, sizeof bytes, file);
        fclose(file);
        return save_raw_valid(bytes, size) ? 0 : 1;
    }
    g.brightness = 50;
    g.backgrounds_owned = 1;
    g.border_id = 255;
    decor_clear_slots(g.decor, DECOR_SLOTS);
    companion_defaults(&g.companion);
    life_defaults(&g.life);
    assert(save_format_test());
    assert(life_test());
    for (unsigned hour = 0; hour < 24; hour++) {
        life_clock_t c = life_clock(1704067200u + hour * 3600u);
        assert(c.available && c.season == 0 && c.season_key == 8097);
        assert(c.period == (hour < 6 || hour >= 22 ? 3 : hour < 12 ? 0 : hour < 18 ? 1 : 2));
    }
    g.life.cooldown = 30;
    life_tick_active(1000, true);
    for (unsigned i = 1; i <= 20; i++) life_tick_active(1000 + i * 50, true);
    assert(g.life.cooldown == 29);
    life_tick_active(2100, false);
    assert(g.life.cooldown == 29);
    life_tick_active(500000, true);
    assert(g.life.cooldown == 29);
    life_defaults(&g.life);
    companion_state_t defaults = g.companion;
    static const unsigned sizes[] = {0,0,132,132,132,136,152,160,236,248,260,260,268,268,268,860,3096};
    for (unsigned version = 2; version <= 16; version++) {
        unsigned count = version >= 10 ? 1 : 0;
        game_state_t state = g;
        state.decor_extra_count = count;
        unsigned char bytes[12 + sizeof(game_state_t) + 6] = {0};
        save_header_t header = {SAVE_MAGIC, version, 0, sizes[version] + count * 6};
        memcpy(bytes, &header, 12);
        memcpy(bytes + 12, &state, sizes[version]);
        if (count) {
            decor_item_t tail = {1, 1, 123, 234};
            memcpy(bytes + 12 + sizes[version], &tail, 6);
        }
        assert(save_raw_valid(bytes, 12 + header.size));
        memset(&state, 0, sizeof state);
        size_t prefix = payload_prefix(version);
        memcpy(&state, bytes + 12, sizes[version] < prefix ? sizes[version] : prefix);
        migrate_automation(&state, version);
        assert(!memcmp(&state.companion, &defaults, sizeof defaults));
        assert(state.usb_sounds == (version < 15 ? 1 : g.usb_sounds));
        assert(life_valid(&state.life) && state.life.daynight == 1 &&
               state.life.retired_count == 0 && state.life.visitor_day == LIFE_NEVER &&
               state.life.light_gesture_disabled == 0);
        if (version == 16) {
            bytes[12 + 860 + 15] = 1;
            assert(!save_raw_valid(bytes, 12 + header.size));
            bytes[12 + 860 + 15] = 0;
        }
        if (count) {
            decor_item_t tail;
            memcpy(&tail, bytes + 12 + prefix, 6);
            assert(tail.x == 123 && tail.y == 234);
        }
    }
    puts("native save format, v2-v16 migration, historical tails and ABI: PASS");
    return 0;
}
'''
        cls.source = cls.work / "save_host.c"
        cls.source.write_text(source)
        cls.binary = cls.work / "save_host.exe"
        result = subprocess.run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-I", str(ROOT / "sim"), "-I", str(ROOT / "main"),
             str(cls.source), "-o", str(cls.binary)],
            capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_native_save_format_and_all_migrations(self):
        result = subprocess.run([str(self.binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_browser_companion_raw_fixture_and_corruptions(self):
        node = shutil.which("node") or str(ROOT / "site" / "node.exe")
        if not Path(node).is_file():
            self.skipTest("Node required for browser/native interoperability")
        script = """
import {readFileSync} from 'node:fs';
import {encodeSave,toHex} from './site/lib/save.js';
import {companionDefaults} from './site/lib/companion.js';
const schema=JSON.parse(readFileSync('./site/save-schema.json'));
const c=companionDefaults();
Object.assign(c,{initialized:1,clock_mode:2,request_day:123,seen_achievements:0xfffff,
  favorite_treat:11,favorite_gift:35,favorite_play:2,bond_level:4,last_stage:2});
c.requests=[{kind:1,target:255,progress:2,state:0},{kind:4,target:35,progress:1,state:1}];
c.treat_counts[11]=255;c.gift_counts[35]=9;c.play_counts[2]=8;
c.diary=[{day:123,kind:2,value:35,pinned:1,calendar:0}];c.diary_count=1;
console.log(toHex(encodeSave({companion:c,brightness:50,backgrounds_owned:1,
  background_id:0,border_id:255,decor_extra_count:1,
  decor:Array.from({length:13},(_,i)=>({kind:i,blocking:i%3,x:100+i,y:200+i}))},schema)));
"""
        result = subprocess.run([node, "--input-type=module", "-e", script],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        raw = bytes.fromhex(result.stdout.strip())
        fixture = self.work / "browser-save.bin"
        fixture.write_bytes(raw)
        self.assertEqual(subprocess.run([str(self.binary), str(fixture)]).returncode, 0)
        self.check_companion_fields(raw, fixture)

    def test_browser_life_archives_and_corruptions(self):
        node = shutil.which("node") or str(ROOT / "site" / "node.exe")
        script = """
import {readFileSync} from 'node:fs';
import {encodeSave,toHex} from './site/lib/save.js';
import {companionDefaults} from './site/lib/companion.js';
import {lifeDefaults,encodeLifePet} from './site/lib/life.js';
const schema=JSON.parse(readFileSync('./site/save-schema.json')),life=lifeDefaults();
life.progress=[5,3,1];life.cooldown=30;life.visitor_day=20000;life.season_highwater=8105;life.light_gesture_disabled=1;
const pet={hunger:80,mood:70,energy:60,hygiene:90,stage:2,temperament:1,bond:55,
 name:'Ancestor',colour:2,pupil_colour:3,leg_colour:4,fang_colour:5,
 spider_x:240,spider_y:200,companion:companionDefaults(),life};
life.retired=Array.from({length:3},()=>encodeLifePet(pet));life.retired_count=3;
console.log(toHex(encodeSave({life},schema)));
"""
        result = subprocess.run([node, "--input-type=module", "-e", script],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        raw = bytes.fromhex(result.stdout.strip())
        fixture = self.work / "browser-life.bin"
        fixture.write_bytes(raw)
        self.assertEqual(subprocess.run([str(self.binary), str(fixture)]).returncode, 0)
        self.assertEqual(int.from_bytes(raw[4:6], "little"),
                         json.loads((ROOT / "site" / "save-schema.json").read_text())["version"])
        legacy = bytearray(raw)
        legacy[4:6] = (16).to_bytes(2, "little")
        legacy[887] = 0
        fixture.write_bytes(legacy)
        self.assertEqual(subprocess.run([str(self.binary), str(fixture)]).returncode, 0)
        legacy[887] = 1
        fixture.write_bytes(legacy)
        self.assertEqual(subprocess.run([str(self.binary), str(fixture)]).returncode, 1)
        for offset, value in ((8,6),(11,31),(12,2),(13,2),(14,4),(15,2),
                              (16,101),(16+46,3),(16+96,4),(16+736,6),(16+739,1)):
            invalid = bytearray(raw)
            invalid[872+offset] = value
            fixture.write_bytes(invalid)
            with self.subTest(offset=offset):
                self.assertEqual(subprocess.run([str(self.binary), str(fixture)]).returncode, 1)

    def test_maximum_v14_enclosure_browser_native_interoperability(self):
        node = shutil.which("node") or str(ROOT / "site" / "node.exe")
        if not Path(node).is_file():
            self.skipTest("Node required for browser/native interoperability")
        script = """
import {readFileSync} from 'node:fs';
import {decodeSave,encodeSave,toHex} from './site/lib/save.js';
const schema=JSON.parse(readFileSync('./site/save-schema.json')),count=3368;
const bytes=new Uint8Array(12+268+count*6),v=new DataView(bytes.buffer);
v.setUint32(0,schema.magic,true);v.setUint16(4,14,true);v.setUint32(8,bytes.length-12,true);
v.setUint32(248,1,true);bytes[278]=255;bytes[140]=50;v.setUint32(268,count,true);
for(let i=0;i<12;i++)bytes[176+i*6]=255;
for(let i=0;i<count;i++){
  const o=280+i*6;bytes[o]=i%68;bytes[o+1]=i%3;
  v.setUint16(o+2,i%481,true);v.setUint16(o+4,(i*7)%481,true);
}
const old=decodeSave(bytes,schema),current=encodeSave(old.fields,schema,bytes);
if(JSON.stringify(decodeSave(current,schema).fields.decor)!==JSON.stringify(old.fields.decor))
  throw Error('lost decorations');
console.log(toHex(bytes));console.log(toHex(current));
"""
        result = subprocess.run([node, "--input-type=module", "-e", script],
                    cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        historical, current = [bytes.fromhex(line) for line in result.stdout.splitlines()]
        schema = json.loads((ROOT / "site" / "save-schema.json").read_text())
        tail = schema["decorTail"]["offset"]
        self.assertEqual(len(current), len(historical) + tail - 268)
        self.assertEqual(current[12 + tail:], historical[280:])
        fixture = self.work / "maximum-save.bin"
        for raw in (historical, current):
            fixture.write_bytes(raw)
            self.assertEqual(subprocess.run([str(self.binary), str(fixture)]).returncode, 0)
            oversized = bytearray(raw + bytes(6))
            oversized[8:12] = (len(oversized) - 12).to_bytes(4, "little")
            oversized[268:272] = (3369).to_bytes(4, "little")
            fixture.write_bytes(oversized)
            self.assertEqual(subprocess.run([str(self.binary), str(fixture)]).returncode, 1)

    def check_companion_fields(self, raw, fixture):
        for enabled in (0, 1, 2):
            flag = bytearray(raw)
            flag[279] = enabled
            fixture.write_bytes(flag)
            with self.subTest(usb_sounds=enabled):
                self.assertEqual(subprocess.run([str(self.binary), str(fixture)]).returncode,
                                 0 if enabled <= 1 else 1)
        for offset, value in ((7, 0x10), (8, 5), (9, 0), (10, 3), (11, 1),
                              (13, 36), (14, 2), (15, 3), (27, 2), (63, 2), (66, 2),
                              (67, 12), (68, 36), (69, 3),
                              (70, 65), (71, 0), (71, 3), (72, 0), (72, 2), (73, 5), (74, 3), (75, 2),
                              (84, 9), (85, 36), (86, 2), (87, 2)):
            invalid = bytearray(raw)
            invalid[280 + offset] = value
            fixture.write_bytes(invalid)
            with self.subTest(offset=offset):
                self.assertEqual(subprocess.run([str(self.binary), str(fixture)]).returncode, 1)
        for offset in (76, 77, 78, 79, 88, 591):
            ignored = bytearray(raw)
            ignored[280 + offset] = 255
            fixture.write_bytes(ignored)
            with self.subTest(ignored_offset=offset):
                self.assertEqual(subprocess.run([str(self.binary), str(fixture)]).returncode, 0)
        for kind, targets in ((1, [255]), (2, [0, 2, 255]), (3, [0, 7, 255]),
                              (4, [0, 35, 255]), (6, [255])):
            for target in targets:
                valid = bytearray(raw)
                valid[288:292] = bytes((kind, target, 0, 0))
                fixture.write_bytes(valid)
                with self.subTest(kind=kind, target=target):
                    self.assertEqual(subprocess.run([str(self.binary), str(fixture)]).returncode, 0)


if __name__ == "__main__":
    unittest.main()
