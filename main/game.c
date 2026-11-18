/*
 * Spidarium — game orchestrator.
 *
 * Owns the authoritative state and the two timers. All actual work lives in
 * the engine modules: terrarium (background), spider (rendering), behaviour
 * (movement/FSM), prey (hunting), needs (care/life cycle), ui_hud
 * (screens), audio (SFX), save (persistence).
 */
#include "game.h"
#include "achievements.h"
#include "help_content.h"

#include "album.h"
#include "ambient.h"
#include "assets.h"
#include "audio.h"
#include "behaviour.h"
#include "buttons.h"
#include "cspider.h"
#include "decor.h"
#include "game_config.h"
#include "gift.h"
#include "power.h"
#include "sprites.h"
#include "meminfo.h"
#include "mind.h"
#include "life.h"
#include "needs.h"
#include "net.h"
#include "prey.h"
#include "rtc.h"
#include "save.h"
#include "species.h"
#include "sd.h"
#include "spider.h"
#include "test_console.h"
#include "terrarium.h"
#include "theme.h"
#include "ui_hud.h"
#include "ui_menu.h"
#include "ui_setup.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/misc/cache/instance/lv_image_header_cache.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_lv_adapter.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "spidarium";

/* The single authoritative state. */
game_state_t g;
game_ui_t    ui;
playfield_t  g_play;
uint8_t      g_decor_mode;
uint8_t      g_decor_selected;
int          g_decor_move_slot = -1;
static bool s_gift_moving;
static bool s_testing_placement;

static uint32_t s_last_logic;
static uint32_t s_last_save;
static bool game_save_screenshot(void);
static bool game_save_album_photo(void);
static lv_draw_buf_t *s_sd_capture;
static const char *s_sd_message;

/* The two game timers, kept so they can be paused.
 *
 * WiFi work (init, scan, connect) must run as the ONLY thing happening. The
 * radio allocates internal DMA-capable RAM and drives its own high-priority
 * tasks, and running that concurrently with the 20 Hz logic tick and the 30 Hz
 * render tick starved the LCD SPI driver and crashed the device. Pausing both
 * timers while the radio is busy is the same approach used previously for WiFi
 * and OTA: stop the game, do the network thing, resume. */
static lv_timer_t *s_logic_timer;
static lv_timer_t *s_render_timer;
static bool        s_paused;

/* Defined once here. Declared extern in game_config.h so every translation
 * unit shares this single copy. */
const temperament_t k_temperaments[TEMPERAMENT_COUNT] = {
    /* name      aim    catch  speed  hunger  sleep */
    { "bold",   0.85f, 1.10f, 1.10f, 1.00f, 15 },
    { "timid",  1.25f, 0.95f, 0.90f, 0.90f, 20 },
    { "greedy", 1.00f, 1.05f, 1.00f, 1.25f, 15 },
    { "lazy",   1.15f, 0.90f, 0.80f, 0.85f, 30 },
};

const temperament_t *game_temperament(void)
{
    uint8_t t = g.temperament;
    if (t >= TEMPERAMENT_COUNT) t = 0;
    return &k_temperaments[t];
}

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */
uint32_t game_now(void)
{
    return lv_tick_get();
}

void game_add_silk(uint32_t amount)
{
    /* Clamp to SILK_MAX, not UINT32_MAX. Saturating at the type limit let the
     * balance reach 305,213 in normal play, at which point every price is
     * meaningless and a greyed shop row is indistinguishable from a Silk
     * problem. The cap is far above the total cost of everything purchasable,
     * so it never blocks legitimate play. */
    if (amount > SILK_MAX - (g.silk > SILK_MAX ? SILK_MAX : g.silk)) {
        g.silk = SILK_MAX;
    } else {
        g.silk += amount;
    }
}
void game_add_xp(uint32_t amount)
{
    g.xp += amount;

    uint32_t level = g.xp / XP_PER_LEVEL + 1;
    if (level != g.level) {
        g.level = level;
        ESP_LOGI(TAG, "level up -> %lu", (unsigned long)level);
        if (level >= 5) game_award(ACH_LEVEL_5);
    }
}

void game_award(uint32_t achievement)
{
    if (g.achievements & achievement) return;

    /* Set the bit BEFORE granting XP. game_add_xp() can itself call
     * game_award() (level 5), and this ordering is what stops that from
     * recursing. */
    g.achievements |= achievement;
    ESP_LOGI(TAG, "achievement 0x%08lx", (unsigned long)achievement);

    /* Achievements used to be inert flags. They now pay XP, so progression
     * has teeth. */
    game_add_xp(XP_ACHIEVEMENT);
    game_add_silk(ACHIEVEMENT_SILK);
    ESP_LOGI(TAG, "achievement reward +%u XP +%u Silk", XP_ACHIEVEMENT, ACHIEVEMENT_SILK);
}

void game_check_achievements(void)
{
    uint32_t before = g.achievements;
    if (g.cleans >= 10) game_award(ACH_TEN_CLEANS);
    if (g.plays >= 10) game_award(ACH_TEN_PLAYS);
    if (g.meals >= 25) game_award(ACH_TWENTY_FIVE_MEALS);
    if (g.bond >= 50) game_award(ACH_BOND_50);
    if (g.daily_streak >= 3) game_award(ACH_STREAK_3);
    if (decor_placed_count() >= 3) game_award(ACH_DECOR_3);
    int backgrounds = 0;
    for (int i = 0; i < THEME_COUNT; i++) {
        if (theme_owned(i)) backgrounds++;
        if (border_owned(i)) game_award(ACH_FIRST_BORDER);
    }
    if (backgrounds >= 4) game_award(ACH_BACKGROUNDS_4);
    if (g.gift.used) game_award(ACH_FIRST_GIFT);
    if (g.achievements != before) game_save_now();
}

bool game_save_now(void)
{
    if (s_testing_placement) return true;
    bool accepted = save_request(&g, sizeof g);
    if (!accepted) ESP_LOGE(TAG, "game save not queued");
    return accepted;
}

/* ------------------------------------------------------------------ */
/* Persistence                                                         */
/* ------------------------------------------------------------------ */
static void state_defaults(void)
{
    memset(&g, 0, sizeof g);
    companion_defaults(&g.companion);
    life_defaults(&g.life);
    g.hunger  = 65;
    g.mood    = 60;
    g.energy  = 80;
    g.hygiene = 80;
    g.seed    = 0xC0FFEE;
    g.state   = SPIDER_IDLE;
    g.stage   = STAGE_SPIDERLING;
    g.level   = 1;
    g.muted   = 0;
    g.silk = 20;
    g.brightness = 50;
    g.volume = AUDIO_VOLUME;
    g.backgrounds_owned = 1u;
    g.border_id = 255;
    g.screen_timeout = 3;
    g.music_enabled = 1;
    g.usb_sounds = 1;
    g.music_volume = 100;

    /* Sentinel: rolled in game_init() AFTER srand(). Rolling here would give
     * every fresh boot the same temperament, because this runs before the RNG
     * is seeded. */
    g.temperament = 0xFF;
    g.bond        = 0;

    /* Accent defaults, chosen so a fresh spider looks exactly like the old
     * hardcoded one: black pupils, white fangs, grey legs. */
    g.pupil_colour = 0;   /* black */
    g.fang_colour  = 1;   /* white */
    g.leg_colour   = 2;   /* grey  */

    /* Sentinel: 0 is a real in-game day, so "never claimed" needs a value that
     * cannot collide with it. Without this a brand-new pet could never claim
     * its first check-in, because daily_day == 0 == today. */
    g.daily_day = DAILY_NEVER;
    g.daily_base_day = DAILY_NEVER;

    /* Every decor slot starts empty. kind 0 is a real item, so a zeroed struct
     * would otherwise place twelve small rocks on a brand-new pet. */
    decor_clear();
}

static void state_finish_load(void)
{
    /* Runtime-only fields never survive a reboot. */
    g.state        = SPIDER_IDLE;
    g.eat_until_ms = 0;
    g.blink_until  = 0;
    g.blink_window = 0;
    g.gift_mode    = 0;
    g_decor_mode   = 0;

    /* Guard against a corrupt-but-valid-header blob. */
    if (g.level == 0) g.level = 1;
    if (g.brightness < 10 || g.brightness > 100) g.brightness = 50;
    if (g.volume > 100) g.volume = AUDIO_VOLUME;
    if (g.power_mode > 2) g.power_mode = 0;
    if (g.screen_timeout > 4) g.screen_timeout = 3;
    if (g.deep_sleep_after > 4) g.deep_sleep_after = 0;
    if (g.rtc_wake > 2) g.rtc_wake = 0;
    theme_restore();
    if (g.gift.used && g.gift.kind < GIFT_KINDS)
        gift_mark_owned(g.gift.kind);
    if (g.stage > STAGE_ADULT) g.stage = STAGE_SPIDERLING;
    if (g.hunger > 100) g.hunger = 100;
    if (g.mood > 100) g.mood = 100;
    if (g.energy > 100) g.energy = 100;
    if (g.hygiene > 100) g.hygiene = 100;

    /* Drop any decor slot whose kind is out of range, so a corrupt blob cannot
     * index past the definition table. */
    for (int i = 0; i < DECOR_SLOTS; i++)
        if (g.decor[i].kind != 0xFF && g.decor[i].kind >= DECOR_COUNT)
            g.decor[i].kind = 0xFF;
}

static void state_load(void)
{
    state_defaults();
    if (!save_load(&g, sizeof g)) ESP_LOGI(TAG, "starting from defaults");
    state_finish_load();

    /* Catch up on the time the device was off. The RTC is the clock of record;
     * without it there is no trustworthy elapsed time, so nothing is applied
     * rather than inventing a delta from a 1970 epoch. */
    time_t now = 0;
    if (pet_rtc_read(&now) == ESP_OK && now > 0) {
        if (g.last_seen > 0 && (uint32_t)now > g.last_seen) {
            needs_apply_offline((uint32_t)now - g.last_seen);
        }
        g.last_seen = (uint32_t)now;
    }
}

/* ------------------------------------------------------------------ */
/* Playfield                                                           */
/* ------------------------------------------------------------------ */
static void keep_gift_in_playfield(void)
{
    if (!g.gift.used) return;
    if (g.gift.x < g_play.left) g.gift.x = g_play.left;
    if (g.gift.x > g_play.right) g.gift.x = g_play.right;
    if (g.gift.y < g_play.top) g.gift.y = g_play.top;
    if (g.gift.y > g_play.bottom) g.gift.y = g_play.bottom;
}

static void compute_playfield(void)
{
    /* Only the top-left Care button stays visible; hunting uses the full field. */
    g_play.left   = PLAY_MARGIN;
    g_play.right  = ui.W - PLAY_MARGIN;
    g_play.top    = PLAY_MARGIN;
    g_play.bottom = ui.H - PLAY_MARGIN;

    if (g_play.right - g_play.left < 60) {
        g_play.left = 10;
        g_play.right = ui.W - 10;
    }
    if (g_play.bottom - g_play.top < 60) {
        g_play.top = ui.H / 3;
        g_play.bottom = ui.H * 2 / 3;
    }

    /* Park the spider inside the field. */
    if (g.spider_x < g_play.left || g.spider_x > g_play.right ||
        g.spider_y < g_play.top  || g.spider_y > g_play.bottom) {
        g.spider_x = (int16_t)((g_play.left + g_play.right) / 2);
        g.spider_y = (int16_t)((g_play.top + g_play.bottom) / 2);
    }
    keep_gift_in_playfield();
}

/* ------------------------------------------------------------------ */
/* Event callbacks                                                     */
/* ------------------------------------------------------------------ */
void game_on_feed(lv_event_t *e)
{
    (void)e;
    if (prey_count() >= MAX_PREY) {
        ESP_LOGW(TAG, "feed rejected: tank full");
        ui_hud_bubble("Tank Full / Catch a Bug First", 0xf5c86b, 1800);
        return;
    }
    /* Feeding wakes a sleeping spider. Prey only becomes food when the spider
     * catches it, so dropping a bug in for a spider that is asleep did nothing
     * visible: the player paid a tap, the bug wandered, and the spider stayed
     * curled up. That is the "stuck asleep so I cannot feed them" report -- the
     * spider had energy, just not enough to wake on its own. */
    if (g.state == SPIDER_SLEEP) {
        behaviour_force_sleep(false);
        ESP_LOGI(TAG, "feed: woke the spider to hunt");
    }
    g.taps++;
    needs_feed();
    /* No bubble: the spider reacting IS the feedback. */
}

void game_on_clean(lv_event_t *e)
{
    (void)e;
    /* Clean always works. The reward scales with how much the tank improved, so
     * a spotless tank pays nothing and a filthy one pays the full amount. */
    int prey_before = prey_count();
    bool needed = needs_clean_needed();
    uint32_t xp = needs_clean();
    if (!needed) {
        ui_hud_bubble("Already Clean", 0xf5c86b, 1800);
        return;
    }
    /* A tank holding only bugs is cleaned for 0 XP, so say what actually
     * happened rather than showing a reward that was not paid. */
    if (xp == 0 && prey_before > 0 && prey_count() < prey_before) {
        ui_hud_bubble("Bugs Shooed Out", 0xf5c86b, 1800);
    }
    g.taps++;
    game_save_now();
}

static uint32_t play_reward(uint8_t score, uint8_t kind)
{
    uint32_t before = g.xp;
    needs_play();
    companion_event(COMPANION_PLAY, kind);
    /* Score drives the payout: a clean sweep is worth roughly four times a
     * blank run, so playing well is visibly better than tapping through. */
    game_add_xp(2 + score * 4);
    game_add_silk(score * 3);
    game_save_now();
    return g.xp - before;
}

void game_on_play(lv_event_t *e)
{
    (void)e;
    g.taps++;
    behaviour_aim_cancel();
    ui_menu_open_minigame(play_reward);
}

void game_on_sleep(lv_event_t *e)
{
    (void)e;
    g.taps++;
    needs_sleep();
}

static int16_t s_place_x, s_place_y, s_place_dx, s_place_dy;
static bool s_place_dragging;
/* A press that has grabbed the item but has not yet travelled far enough to be
 * a drag. See game_on_terrarium_press(). */
static bool s_place_armed;
/* Where the arming press landed, so the drag threshold is measured from the
 * finger's own start point rather than from the item. Distinct from
 * s_tap_origin, which belongs to the enclosure tap gesture. */
static lv_point_t s_place_origin;
static uint8_t s_place_blocking;
static int s_place_layer;
static void placement_position(int32_t x, int32_t y);

uint8_t game_placement_blocking(void) { return s_place_blocking; }
int game_placement_layer(void)
{
    /* The STAGED index, not the live one.
     *
     * The layer arrows only write s_place_layer; the reorder is committed by
     * Done. Reading the live index here made the arrows appear dead: pressing
     * one changed the staged value, the label re-read the live value, and
     * nothing moved on screen until Done was pressed. The staged value is seeded
     * from the live index whenever the selection or the band changes, so it is
     * the right thing to display. */
    return s_place_layer;
}
int game_placement_layers(void)
{
    /* A gift is a single item, but it still has a position among the decor it
     * overlaps, which is what the player is arranging. */
    if (g.gift_mode) return decor_gift_layer_count(DECOR_WALK_OVER);
    /* Count only the items this one actually overlaps, at the position it is
     * currently staged at. A new item has no slot yet, so the count comes from
     * its kind and position rather than from a slot lookup. */
    uint8_t kind = g_decor_selected;
    if (g_decor_move_slot >= 0) {
        const decor_item_t *item = decor_at(g_decor_move_slot);
        if (item) kind = item->kind;
    }
    return decor_layer_count_at(s_place_blocking, kind, s_place_x, s_place_y,
                                g_decor_move_slot) + 1;
}
bool game_placement_existing(void)
{
    return (g_decor_mode && g_decor_move_slot >= 0) || (g.gift_mode && s_gift_moving);
}

/* How many things the top selector can step through: every placed decor item
 * plus the gift, if one is placed.
 *
 * The gift is included because it is a placed object the player can move, and
 * reaching it used to mean cancelling out to the menu and picking "Move" there.
 * The selector is the one control that switches WHICH item is being moved, so
 * leaving the gift out of it made the gift the only object you could not reach
 * from the overlay. */
int game_placement_item_total(void)
{
    int slots = decor_slot_count();
    int total = 0;
    for (int i = 0; i < slots; i++) if (decor_at(i)) total++;
    if (g.gift.used) total++;
    return total;
}

/* Which placed item is being moved, as "name  n / total", for the top selector.
 * Returns false only when there is no placed item to name (a new placement).
 *
 * This used to also return false when `total < 2`, on the theory that a single
 * item has nothing to cycle to. That hid the whole selector -- arrows, caption
 * and all -- whenever the tank held one prop, which read as the control having
 * been removed. The arrows are now shown and DISABLED at "1 / 1" instead, so the
 * control is always where the player expects it and its state is honest.
 *
 * The gift is named too, so the selector reads "Holo Garden 3 / 4" while the
 * gift is the staged item. */
bool game_placement_item_label(char *buf, size_t len)
{
    if (!buf || len == 0) return false;
    buf[0] = '\0';
    if (!g_decor_mode && !g.gift_mode) return false;
    int slots = decor_slot_count();
    int total = 0, index = 0;
    for (int i = 0; i < slots; i++) {
        if (!decor_at(i)) continue;
        total++;
        if (g_decor_mode && i == g_decor_move_slot) index = total;
    }
    if (g.gift.used) {
        total++;
        if (g.gift_mode) index = total;
    }
    if (index == 0) return false;
    if (g.gift_mode) {
        snprintf(buf, len, "%s\n%d / %d", gift_kind_name(g.gift_selected), index, total);
        return true;
    }
    const decor_item_t *item = decor_at(g_decor_move_slot);
    if (!item) return false;
    const decor_def_t *def = decor_def(item->kind);
    snprintf(buf, len, "%s\n%d / %d", def ? def->name : "Decor", index, total);
    return true;
}

void game_on_placement_blocking(lv_event_t *e)
{
    (void)e;
    /* A gift has no path control: it is always walked over. See the note in
     * behaviour.c for why the Solid option was removed rather than kept. */
    if (!g_decor_mode) return;
    uint8_t previous = s_place_blocking;
    /* Three selectable paths. Climb was removed: the 2.5D lift never read
     * convincingly, so the large structures are ordinary Walk Behind items. */
    if (s_place_blocking >= DECOR_SELECTABLE_PATHS) s_place_blocking = DECOR_WALK_OVER;
    s_place_blocking = (uint8_t)((s_place_blocking + 1) % DECOR_SELECTABLE_PATHS);
    if ((previous == DECOR_WALK_BEHIND) != (s_place_blocking == DECOR_WALK_BEHIND)) {
        /* Changing band changes which items the layer is ordered against, so
         * the staged index has to be re-seeded to the top of the new band. */
        s_place_layer = game_placement_layers() - 1;
    }
    placement_position(s_place_x, s_place_y);
    ui_hud_set_place_mode(true, NULL);
}

void game_on_placement_layer(lv_event_t *e)
{
    if (!g_decor_mode && !g.gift_mode) return;
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    /* The staged index is the DISPLAYED index, which for decor counts the gift.
     * Seed it from the live value rather than trusting s_place_layer, which is
     * only written by this handler and by the band change. */
    int current = game_placement_layer();
    int next = current + step;
    if (next < 0 || next >= game_placement_layers()) return;
    s_place_layer = next;
    placement_position(s_place_x, s_place_y);
    ui_hud_set_place_mode(true, NULL);
}

/* Step the item being moved to the previous/next placed object.
 *
 * The top arrows used to be a second copy of the layer control, which was
 * redundant: the bottom row already does that, and the player had no way to
 * switch to a different item without cancelling out to the menu and starting
 * again. Cycling the selection is the thing that was actually missing.
 *
 * The ring is the placed decor in slot order followed by the gift, so the gift
 * is reachable from the overlay like any other object. Stepping onto the gift
 * switches the mode flags: `g.gift_mode` drives the commit path, the preview
 * kind and the "always walked over" rule, so it has to follow the selection
 * rather than being fixed for the whole edit. */
static void placement_step_item(int step)
{
    if ((!g_decor_mode && !g.gift_mode) || step == 0) return;
    int slots = decor_slot_count();
    int total = game_placement_item_total();
    if (total < 2) return;

    /* Build the ring as a flat index: 0..slots-1 are decor slots (occupied ones
     * only), and `slots` is the gift. Walking the flat index keeps the wrap
     * arithmetic in one place. */
    int ring[64];
    int ring_len = 0;
    for (int i = 0; i < slots && ring_len < 63; i++)
        if (decor_at(i)) ring[ring_len++] = i;
    int gift_pos = -1;
    if (g.gift.used && ring_len < 63) {
        gift_pos = ring_len;
        ring[ring_len++] = slots;   /* sentinel: the gift */
    }
    if (ring_len < 2) return;

    int current = -1;
    for (int i = 0; i < ring_len; i++) {
        if (g.gift_mode && ring[i] == slots) { current = i; break; }
        if (g_decor_mode && ring[i] == g_decor_move_slot) { current = i; break; }
    }
    if (current < 0) return;

    int next = (current + step + ring_len) % ring_len;
    if (next == current) return;

    if (ring[next] == slots) {
        /* Switch to the gift. */
        g_decor_mode = 0;
        g_decor_move_slot = -1;
        g.gift_mode = 1;
        s_gift_moving = true;
        g.gift_selected = g.gift.kind;
        s_place_blocking = DECOR_WALK_OVER;
        s_place_layer = decor_gift_layer_index(s_place_blocking);
        s_place_dragging = false;
        placement_position(g.gift.x, g.gift.y);
        ui_hud_set_place_mode(true, "Slide to move; Done saves");
        return;
    }

    /* Switch to a decor item. */
    int slot = ring[next];
    g.gift_mode = 0;
    s_gift_moving = false;
    g_decor_mode = 1;
    g_decor_move_slot = slot;
    s_place_blocking = decor_at(slot)->blocking;
    s_place_layer = decor_layer_index_with_gift(slot);
    g_decor_selected = decor_at(slot)->kind;
    s_place_dragging = false;
    placement_position(decor_at(slot)->x, decor_at(slot)->y);
    ui_hud_set_place_mode(true, "Slide to move; Done saves");
}

void game_on_placement_item(lv_event_t *e)
{
    placement_step_item((int)(intptr_t)lv_event_get_user_data(e));
}
void game_on_placement_put_away(lv_event_t *e)
{
    (void)e;
    if (g_decor_mode && g_decor_move_slot >= 0) {
        if (!decor_remove(g_decor_move_slot)) {
            ESP_LOGW(TAG, "put away rejected: decor missing");
            ui_hud_bubble("Cannot Put Away", 0xf5c86b, 1800);
            return;
        }
        terrarium_refresh_decor();
    } else if (g.gift_mode && s_gift_moving) {
        g.gift.used = 0;
        terrarium_refresh_gift();
    } else return;
    game_on_placement_cancel(NULL);
    if (!game_save_now()) ui_hud_bubble("Save Not Queued / See Log", 0xf5c86b, 2200);
}

static void placement_position(int32_t x, int32_t y)
{
    uint8_t kind = g.gift_mode ? g.gift_selected : g_decor_selected;
    int radius = g.gift_mode ? 0 : decor_def(kind)->radius;
    int left = g_play.left + radius, right = g_play.right - radius;
    int top = g_play.top + radius, bottom = g_play.bottom - radius;
    s_place_x = x < left ? left : x > right ? right : x;
    s_place_y = y < top ? top : y > bottom ? bottom : y;
    /* Moving the item changes which items it overlaps, so the layer count can
     * change under the player's finger. Clamp the staged index rather than
     * letting it point past the end of a now-shorter list. */
    int layers = game_placement_layers();
    if (s_place_layer >= layers) s_place_layer = layers - 1;
    if (s_place_layer < 0) s_place_layer = 0;
    terrarium_preview_item(g.gift_mode != 0, kind, g_decor_mode ? g_decor_move_slot : -1,
                           s_place_x, s_place_y, s_place_blocking, s_place_layer);
    /* Refresh the layer row as the item moves.
     *
     * Without this the row only updated when a button was pressed, so dragging
     * an item onto another one left the count showing the OLD overlap -- the
     * layer control appeared to do nothing until you pressed Done and started
     * moving again, which is exactly the "layers don't kick in until an item is
     * moved with another item" report. */
    ui_hud_placement_changed();
}

void game_on_placement_cancel(lv_event_t *e)
{
    (void)e;
    g.gift_mode = 0;
    g_decor_mode = 0;
    g_decor_move_slot = -1;
    s_gift_moving = false;
    s_place_dragging = false;
    terrarium_preview_hide();
    ui_hud_set_place_mode(false, NULL);
}

/* Place a gift through the real commit path, for the serial console. The UI
 * reaches this via game_on_placement_done(); the console needs the same path so
 * the layer commit and the preview teardown are exercised for real rather than
 * simulated. */
bool game_place_gift_for_test(uint8_t kind, int16_t x, int16_t y)
{
    if (kind >= GIFT_KINDS) return false;
    g.gift_selected = kind;
    g.gift_mode = 1;
    s_gift_moving = false;
    s_place_blocking = DECOR_WALK_OVER;
    s_place_layer = 0;
    placement_position(x, y);
    game_on_placement_done(NULL);
    return g.gift.used && g.gift.kind == kind;
}

void game_on_placement_done(lv_event_t *e)
{
    (void)e;
    if (!g.gift_mode && !g_decor_mode) return;
    if (g_decor_mode) {
        int slot = g_decor_move_slot;
        bool ok;
        if (slot >= 0) ok = decor_move(slot, s_place_x, s_place_y);
        else { slot = decor_place(g_decor_selected, s_place_x, s_place_y); ok = slot >= 0; }
        if (!ok) {
            ESP_LOGW(TAG, "decor placement commit failed");
            ui_hud_bubble("Cannot Place / See Log", 0xf5c86b, 1800);
            return;
        }
        if (!decor_set_path(slot, s_place_blocking) || decor_set_layer(slot, s_place_layer) < 0) {
            ESP_LOGE(TAG, "placement path/layer commit failed");
            ui_hud_bubble("Cannot Set Layer / See Log", 0xf5c86b, 1800);
            return;
        }
        terrarium_preview_hide();
        terrarium_refresh_decor();
    } else {
        if (!s_gift_moving && !gift_purchase(g.gift_selected)) {
            ui_hud_bubble("Not Enough Silk", 0xf5c86b, 1800);
            return;
        }
        if (!s_gift_moving) {
            g.gift.kind = g.gift_selected;
            g.gift.variant = (uint8_t)(rand() % 3);
        }
        g.gift.used = 1;
        g.gift.x = s_place_x;
        g.gift.y = s_place_y;
        /* The gift is always walked over; it has no path control any more. */
        g.gift_path = DECOR_WALK_OVER;
        if (!decor_gift_set_layer(g.gift_path, s_place_layer)) {
            ESP_LOGW(TAG, "gift layer commit failed: %d", s_place_layer);
        }
        /* Hide the preview BEFORE refreshing the gift. The preview image is a
         * sibling of the gift layer, so refreshing first left both on screen
         * for a frame and, if the layer stayed hidden, permanently. */
        terrarium_preview_hide();
        terrarium_refresh_gift();
    }
    game_on_placement_cancel(NULL);
    if (!game_save_now()) ui_hud_bubble("Save Not Queued / See Log", 0xf5c86b, 2200);
}

void game_on_gift(lv_event_t *e)
{
    (void)e;
    g.taps++;

    /* Pressing GIFT opens the picker. Choosing a kind there closes the menu and
     * enters placement mode, so the player picks WHAT to give before deciding
     * WHERE. The old version cycled kinds blindly on repeated presses, which
     * meant you could not see the options or go back to one you had passed. */
    if (g.gift_mode) {
        /* Cancel placement. */
        game_on_placement_cancel(NULL);
        return;
    }

    if (ui_hud_gift_menu_visible()) {
        ui_hud_gift_menu_hide();
        return;
    }

    /* Food out means the spider is hunting; placing or moving a gift would
     * compete with the aim gesture and shove the spider's cover around. */
    if (prey_count() > 0) {
        ESP_LOGW(TAG, "gift placement rejected: %d live prey in the tank", prey_count());
        ui_hud_bubble("Catch the Bugs First", 0xf5c86b, 2000);
        return;
    }

    ui_hud_gift_menu_show();
}

/* Cancel decor placement from the corner X. The item stays owned, so the
 * player can place it later from the Decor menu. */
void game_cancel_decor_placement(void)
{
    if (!g_decor_mode) return;
    game_on_placement_cancel(NULL);
    ESP_LOGI(TAG, "decor placement cancelled");
}

/* Called by the HUD when the player taps a kind in the picker. */
void game_gift_chosen(uint8_t kind)
{
    s_gift_moving = false;
    if (kind >= GIFT_KINDS) return;

    if (!gift_can_place(kind)) {
        ESP_LOGW(TAG, "gift selection rejected: locked or unaffordable");
        ui_hud_bubble("Earn Silk by Playing", 0xf5c86b, 1800);
        return;
    }

    g.gift_selected = kind;
    ui_hud_gift_menu_hide();
    ui_hud_set_chrome(false);

    g.gift_mode = 1;
    behaviour_aim_cancel();
    s_place_dragging = false;
    /* The gift is always walked over, so there is no path to seed. */
    s_place_blocking = DECOR_WALK_OVER;
    s_place_layer = s_gift_moving ? decor_gift_layer_index(s_place_blocking) : 0;
    placement_position(ui.W / 2, ui.H / 2);
    ui_hud_set_gift_mode(true);

}

/* ------------------------------------------------------------------ */
/* Menu system                                                         */
/* ------------------------------------------------------------------ */
/* A small hierarchy: the main menu lists categories, and each category opens
 * its own list. This is where the action buttons and stats will eventually
 * move, so the structure matters more than the current contents. */

enum {
    MENU_MAIN = 0,
    MENU_THEME,
    MENU_SOUND,
    MENU_ACHIEVEMENTS,
    MENU_RESET,
    MENU_SETTINGS,
    MENU_WIFI,
    MENU_SD,
    MENU_WIFI_SAVED,
    MENU_WIFI_NETWORK,
    MENU_OTA,
    MENU_SPECIES,
    MENU_ABOUT,
    MENU_POWER,
    MENU_POWER_SLEEP,
    MENU_DISPLAY,
    MENU_DAILY,
    MENU_SHOP,
    MENU_DECOR,
    MENU_DECOR_ITEM,
    MENU_GIFT_ITEM,
    MENU_SOUNDTRACK,
    MENU_BORDERS,
    MENU_WIFI_SCAN,
    MENU_HELP,
    MENU_PERSONALITY,
    MENU_TODAY,
    MENU_DIARY,
    MENU_MEMORY,
    MENU_TRAINING,
    MENU_TRICK,
    MENU_CALENDAR,
    MENU_ALBUM,
    MENU_PHOTO_STORAGE,
    MENU_PHOTO,
    MENU_PHOTO_ACTIONS,
    MENU_PHOTO_DELETE,
    MENU_GENERATIONS,
    MENU_RETIRED,
    MENU_RETIRE_CONFIRM,
    MENU_RESUME_CONFIRM,
    MENU_COUNT,
};

static void menu_open(int which);
static void back_to_main(void);

/* Which menu is currently on screen, or -1 when none is. Tracked here because
 * ui_menu only knows that *a* menu is open, not which one. */
static int s_open_menu = -1;
static int s_menu_pages[MENU_COUNT];
static bool s_menu_page_known[MENU_COUNT];
static int s_page_menu;
static bool s_page_loading;
static void remember_menu_page(int page)
{
    if (s_page_loading) return;
    s_menu_pages[s_page_menu] = page;
    s_menu_page_known[s_page_menu] = true;
}
/* The row table handed to ui_menu_open(). File scope so a value changed
 * outside the menu (the physical volume buttons) can be pushed into the open
 * panel without rebuilding it. */
static ui_menu_item_t s_menu_items[DECOR_COUNT + 1];
/* Slot whose per-item decor menu is open. */
static int s_decor_item_slot = -1;
static uint8_t s_memory_slot;
static uint8_t s_trick_slot, s_retired_slot, s_photo_slot;
static bool s_pet_change_waiting;
static lv_obj_t *s_daylight_overlay, *s_weather[6], *s_visitor;
static lv_image_dsc_t s_album_image;
static album_image_t s_album_loaded;

/* Render-tick cost accounting. The 30 Hz timer gives 33,333 us per tick, so a
 * tick that approaches that is a visible stutter. Accumulated here rather than
 * measured from the console, because a console round trip is dominated by the
 * timer period and cannot see the difference. */
static uint32_t s_render_us_total;
static uint32_t s_render_ticks;
/* Frame-interval spread. The average tick cost says nothing about smoothness: a
 * run of 20 ms ticks with an occasional 120 ms one has a fine average and still
 * reads as a skip. These track the worst interval and how many intervals
 * exceeded twice the timer period, which is the number that matches what the
 * eye sees. */
static uint32_t s_frame_gap_max_us;
static uint32_t s_frame_gap_over;
static uint32_t s_frame_gap_last_us;
/* Per-section accumulators, so a regression can be attributed to a subsystem
 * instead of guessed at. Only the sections that have ever been suspected of
 * costing too much are instrumented; the rest are the remainder. */
static uint32_t s_sec_ambient_us, s_sec_terrarium_us, s_sec_spider_us, s_sec_hud_us;
/* Flush accounting. The render tick's own cost says nothing about the frame
 * rate: LVGL's refresh runs inside lv_timer_handler(), outside the tick, and it
 * is the refresh that decides how often the panel is actually updated. These
 * count refreshes and the pixels pushed, so "the frame rate is 9 Hz" can be
 * attributed to either too many dirty pixels or a slow transfer. */
static uint32_t s_refresh_count, s_flush_pixels, s_flush_max_pixels, s_flush_calls;
static uint32_t s_refresh_us_total, s_refresh_us_max;
static int64_t  s_refresh_start_us;
void game_diary_removed(unsigned slot)
{
    if (s_open_menu != MENU_MEMORY || s_memory_slot == 255) return;
    if (slot == s_memory_slot) s_memory_slot = 255;
    else if (slot < s_memory_slot) s_memory_slot--;
}
static struct { decor_edit_t operation; int value, x, y, count; } s_decor_edit;
static lv_obj_t *s_light_overlay;
static lv_timer_t *s_pet_tap_timer;
static lv_point_t s_pet_tap;
static lv_point_t s_tap_origin, s_last_tap;
static uint32_t s_last_tap_at;
static bool s_tap_valid, s_tap_pending;

#define ENCLOSURE_DOUBLE_TAP_MS 450
#define ENCLOSURE_TAP_SLOP 24
#define ENCLOSURE_DOUBLE_TAP_SLOP 48

static void cancel_enclosure_tap(void)
{
    s_tap_pending = false;
    if (s_pet_tap_timer) { lv_timer_delete(s_pet_tap_timer); s_pet_tap_timer = NULL; }
}

void game_cancel_pending_tap(void)
{
    s_tap_valid = false;
    cancel_enclosure_tap();
}

/* The enclosure light, as a tint and an opacity.
 *
 * The old version was a single dark rectangle at 12-24/255 opacity, which is a
 * 5-9% wash -- invisible on a 480x480 AMOLED in daylight, and the reason the
 * "day and night" setting appeared to do nothing. It also only ever darkened,
 * so every period looked the same, just slightly dimmer.
 *
 * This version changes COLOUR as well as opacity, so the tank reads as a
 * different time of day rather than as a slightly dirty screen.
 *
 * The opacities are a compromise. The first pass used 26/54/96, which was
 * clearly visible but stacked with the light shafts to grey the whole tank out
 * -- the "screen looks a bit hazy" report. These are roughly half that: still
 * unmistakably a different time of day, without putting a film over the art.
 * Night is the one that has to carry the effect, so it stays the strongest. */
typedef struct {
    uint32_t colour;
    uint8_t  opa;
} light_tint_t;

static const light_tint_t k_light_tints[4] = {
    { 0xffd9a0, 16 },   /* morning: warm gold */
    { 0x000000, 0  },   /* afternoon: no tint */
    { 0xff9a4d, 30 },   /* evening: amber */
    { 0x1a2f6b, 52 },   /* night: deep blue */
};

static void apply_enclosure_lights(void)
{
    if (s_light_overlay) lv_obj_set_hidden(s_light_overlay, !g.lights_dimmed);
}

/* The dimmed overlay drifts a few percent so the tank reads as a room with a
 * light source rather than a photograph with a filter on it. Only touched while
 * the overlay is actually visible, so a lit tank costs nothing. */
static void tick_enclosure_lights(void)
{
    if (!s_light_overlay || !g.lights_dimmed) return;
    /* The overlay covers the WHOLE tank, so a redundant setter here invalidates
     * the entire screen -- the most expensive invalidation in the app. The
     * opacity is a slow sine, so it only changes by one step every few frames;
     * pushing it unconditionally was costing most of the frame rate. */
    static uint8_t s_last_opa = 0xFF;
    uint8_t opa = ambient_light_opa();
    if (opa == s_last_opa) return;
    s_last_opa = opa;
    lv_obj_set_style_bg_opa(s_light_overlay, opa, 0);
}

/* The day/night tint. Separate from the manual dim: this is the clock, that is
 * the player's switch. Both can be on at once, which is why they are two
 * overlays rather than one.
 *
 * This overlay covers the whole tank, so every setter here dirties all 230,400
 * pixels. It used to be pushed unconditionally once a second, which alone cost
 * a third of the frame budget for a tint that changes four times a day. The
 * applied state is cached and only a real change touches the object. */
static void apply_daylight_tint(uint8_t period, bool available)
{
    if (!s_daylight_overlay) return;
    static int s_applied = -1;   /* -1 = never applied, 0..3 = tint index */
    int want;
    if (!available || !g.life.daynight) {
        want = -1;
    } else {
        if (period > 3) period = 1;
        want = k_light_tints[period].opa ? (int)period : -1;
    }
    if (want == s_applied) return;
    s_applied = want;
    if (want < 0) {
        lv_obj_set_hidden(s_daylight_overlay, true);
        return;
    }
    const light_tint_t *tint = &k_light_tints[want];
    lv_obj_set_style_bg_color(s_daylight_overlay, lv_color_hex(tint->colour), 0);
    lv_obj_set_style_bg_opa(s_daylight_overlay, tint->opa, 0);
    lv_obj_set_hidden(s_daylight_overlay, false);
}

static void delayed_pet(lv_timer_t *timer)
{
    (void)timer;
    s_pet_tap_timer = NULL;
    if (!ui_menu_visible() && !ui_hud_chrome_visible() && !g.gift_mode && !g_decor_mode &&
        needs_pet(s_pet_tap.x, s_pet_tap.y)) {
        g.taps++;
        game_save_now();
    }
}

static void enclosure_double_tap(void)
{
    cancel_enclosure_tap();
    behaviour_aim_cancel();
    g.lights_dimmed = !g.lights_dimmed;
    apply_enclosure_lights();
    game_save_now();
}
static int s_saved_network;
static char s_join_ssid[33];
static char s_wifi_message[80];

static void back_to_wifi(void) { menu_open(MENU_WIFI); }
static void back_to_settings(void) { menu_open(MENU_SETTINGS); }
static void back_to_power(void) { menu_open(MENU_POWER); }
static void back_to_sound(void) { menu_open(MENU_SOUND); ui_menu_set_page(1); }
static void back_to_about(void) { menu_open(MENU_ABOUT); }
static void back_to_ota(void) { menu_open(MENU_OTA); }
static uint8_t s_help_topic, s_achievement;
static void back_to_help(void) { menu_open(MENU_HELP); ui_menu_set_page(s_help_topic / 4); }
static void on_help_pick(uint8_t id)
{
    if (id >= HELP_TOPIC_COUNT) { ESP_LOGE(TAG, "invalid help topic %u", id); return; }
    s_help_topic = id;
    ui_menu_open_text(HELP_TOPICS[id].name, HELP_TOPICS[id].text);
    ui_menu_set_back(back_to_help);
}
static void back_to_achievements(void) { menu_open(MENU_ACHIEVEMENTS); ui_menu_set_page(s_achievement / 6); }
static void on_achievement_pick(uint8_t id)
{
    if (id >= ACHIEVEMENT_COUNT) { ESP_LOGE(TAG, "invalid achievement %u", id); return; }
    s_achievement = id;
    char text[256];
    snprintf(text, sizeof text, "%s.\n\nReward: +15 XP and +20 Silk, paid automatically once.\n\n%s",
             ACHIEVEMENTS[id].goal, g.achievements & ACHIEVEMENTS[id].bit ?
             "Earned. Existing earned achievements are not paid twice." : "Keep caring for your spider to unlock this milestone.");
    ui_menu_open_text(ACHIEVEMENTS[id].name, text);
    ui_menu_set_back(back_to_achievements);
}
static void on_about_pick(uint8_t id) { if (id == 3) menu_open(MENU_OTA); }

static bool on_password(const char *password)
{
    if (!net_connect_to(s_join_ssid, password)) return false;
    menu_open(MENU_WIFI);
    return true;
}

static bool on_ssid(const char *ssid)
{
    if (!ssid[0] || strlen(ssid) > 32) return false;
    snprintf(s_join_ssid, sizeof s_join_ssid, "%s", ssid);
    ui_menu_open_input("WiFi Password", "", true, 63, on_password);
    ui_menu_set_back(back_to_wifi);
    return true;
}

static bool on_ota_url(const char *url)
{
    if (!net_ota_start(url)) return false;
    menu_open(MENU_OTA);
    return true;
}

static void on_ota_pick(uint8_t id)
{
    if (id == 1) {
        ui_menu_open_input("Update URL (HTTPS)", "", false, 255, on_ota_url);
        ui_menu_set_back(back_to_ota);
        return;
    }
    if (!net_ota_latest()) ui_hud_bubble("Update Rejected; Save a Network", 0xf5c86b, 2200);
    menu_open(MENU_OTA);
}

static void on_saved_network(uint8_t id)
{
    s_saved_network = id;
    menu_open(MENU_WIFI_NETWORK);
}

static void on_network_action(uint8_t id)
{
    bool accepted = id == 0 ? net_sync_saved(s_saved_network) :
                             net_forget_saved(s_saved_network);
    if (!accepted) snprintf(s_wifi_message, sizeof s_wifi_message, "Request rejected; network missing or busy");
    menu_open(MENU_WIFI);
}

/* Factory reset: wipe the save, put the state back to defaults, and drop the
 * player into the new-tank screen so they can start over. This exists so new
 * tanks can be retested without reflashing the device. */
static void do_factory_reset(void)
{
    if (!save_request_erase()) {
        ESP_LOGE(TAG, "factory reset not queued");
        return;
    }
    state_defaults();
    audio_set_muted(false);

    /* The live objects still carry the old spider's colours, so push the new
     * defaults into them before showing the setup screen. */
    spider_refresh_colours();
    terrarium_rebuild();
    terrarium_refresh_gift();

    ui_setup_show();
}

static void on_theme_pick(uint8_t id)
{
    if (!theme_set(id)) {
        ui_hud_bubble("Not Enough Silk", 0xf5c86b, 1800);
        menu_open(MENU_THEME);
        ui_menu_set_page(id / 6);
        return;
    }
    terrarium_rebuild();
    ambient_retint();
    menu_open(MENU_THEME);   /* stay in the submenu so the choice is visible */
}

static void on_border_pick(uint8_t id)
{
    if (border_set(id)) terrarium_rebuild();
    else ui_hud_bubble("Border Purchase Failed", 0xf5c86b, 1800);
    menu_open(MENU_BORDERS);
    ui_menu_set_page(id == 255 ? 0 : (id + 1) / 6);
}

static void on_sound_pick(uint8_t id)
{
    if (id == 4) { menu_open(MENU_SOUNDTRACK); return; }
    int page = ui_menu_page();
    if (id == 1) audio_set_muted(!audio_muted());
    if (id == 2) {
        g.music_enabled = !g.music_enabled;
        audio_set_music(g.music_enabled);
    }
    if (id == 5) g.usb_sounds = !g.usb_sounds;
    game_save_now();
    menu_open(MENU_SOUND);
    ui_menu_set_page(page);
}

static void on_soundtrack_pick(uint8_t id)
{
    if (id >= AUDIO_MUSIC_TRACK_COUNT) {
        ESP_LOGW(TAG, "soundtrack rejected: invalid track %u", id);
        return;
    }
    g.music_track = id;
    audio_set_music_track(id);
    game_save_now();
    menu_open(MENU_SOUNDTRACK);
}

/* Slider rows report live while the menu stays open, so the value is applied
 * and persisted without rebuilding the panel under the player's finger. */
static void on_volume_slider(uint8_t id, uint8_t value)
{
    if (id == 3) {
        g.music_volume = value;
        audio_set_music_volume(value);
    } else audio_set_volume(value);
    game_save_now();
}

/* The physical volume buttons changed the level behind the menu's back. Push
 * the new value into the open Sound panel so its slider and percentage follow
 * immediately instead of waiting for the player to back out and re-enter.
 * Runs on the LVGL task (buttons_tick is called from the logic timer). */
void game_volume_changed(void)
{
    if (s_open_menu != MENU_SOUND || !ui_menu_showing(s_menu_items)) return;
    ui_menu_set_slider_value(0, audio_volume());
}

static void on_brightness_slider(uint8_t id, uint8_t value)
{
    (void)id;
    if (value < 10) value = 10;
    g.brightness = value;
    game_save_now();
}

static void on_main_pick(uint8_t id)
{
    switch (id) {
    case MENU_DAILY:        menu_open(MENU_DAILY);        break;
    case MENU_SHOP:         menu_open(MENU_SHOP);         break;
    case MENU_THEME:        menu_open(MENU_THEME);        break;
    case MENU_SOUND:        menu_open(MENU_SOUND);        break;
    case MENU_ACHIEVEMENTS: menu_open(MENU_ACHIEVEMENTS); break;
    case MENU_RESET:        menu_open(MENU_RESET);        break;
    case MENU_SETTINGS:     menu_open(MENU_SETTINGS);     break;
    case MENU_SPECIES:      menu_open(MENU_SPECIES);      break;
    case MENU_DECOR:        menu_open(MENU_DECOR);        break;
    case MENU_BORDERS:      menu_open(MENU_BORDERS);      break;
    case MENU_HELP:         menu_open(MENU_HELP);         break;
    case MENU_GIFT_ITEM:    menu_open(MENU_GIFT_ITEM);    break;
    case MENU_PERSONALITY: menu_open(MENU_PERSONALITY); break;
    case MENU_TODAY:       menu_open(MENU_TODAY); break;
    case MENU_DIARY:       menu_open(MENU_DIARY); break;
    case MENU_TRAINING:    menu_open(MENU_TRAINING); break;
    case MENU_CALENDAR:    menu_open(MENU_CALENDAR); break;
    case MENU_ALBUM:
        if (!album_request_scan()) ESP_LOGW(TAG, "album scan not queued");
        menu_open(MENU_ALBUM);
        break;
    case MENU_GENERATIONS:
        menu_open(MENU_GENERATIONS);
        break;
    default: break;
    }
}

static void back_to_training(void) { menu_open(MENU_TRAINING); }
static void back_to_album(void) { menu_open(MENU_ALBUM); }
static void back_to_photo_storage(void) { menu_open(MENU_PHOTO_STORAGE); }
static void back_to_photo_actions(void) { menu_open(MENU_PHOTO_ACTIONS); }
static void back_to_generations(void) { menu_open(MENU_GENERATIONS); }
static void back_to_retired(void) { menu_open(MENU_RETIRED); }
static void on_daily_pick(uint8_t id);
static void back_to_diary(void)
{
    menu_open(MENU_DIARY);
    if (s_memory_slot < g.companion.diary_count)
        ui_menu_set_page((g.companion.diary_count - 1 - s_memory_slot) / 4);
}
static void on_memory_pick(uint8_t id)
{
    if (id < g.companion.diary_count) { s_memory_slot = id; menu_open(MENU_MEMORY); }
    else if (id == 255) { companion_pin(s_memory_slot); menu_open(MENU_MEMORY); }
}

static bool life_ready_to_animate(void)
{
    bool ready = !behaviour_trick_active() && !behaviour_aim_active() &&
        !behaviour_pouncing() && !behaviour_sleep_forced() &&
        (g.state == SPIDER_IDLE || g.state == SPIDER_WANDER || g.state == SPIDER_PLAY);
    if (!ready) {
        ESP_LOGW(TAG, "training rejected: spider busy or asleep");
        ui_hud_bubble("Let Your Spider Finish First", 0xf5c86b, 2400);
    }
    return ready;
}

static void on_training_pick(uint8_t id)
{
    if (id < LIFE_TRICKS) { s_trick_slot = id; menu_open(MENU_TRICK); }
}

static void on_trick_pick(uint8_t id)
{
    if (!life_ready_to_animate()) return;
    bool ok = id == 0 ? life_train(s_trick_slot) : id == 1 && life_perform(s_trick_slot);
    if (!ok) {
        ui_hud_bubble("Rest, Practice and Try Again / See Log", 0xf5c86b, 2400);
        menu_open(MENU_TRICK);
        return;
    }
    ui_menu_close();
    ui_hud_set_chrome(false);
    if (behaviour_trick(s_trick_slot))
        ui_hud_bubble(life_trick_name(s_trick_slot), 0x76d6b1, 2000);
}

static void on_calendar_pick(uint8_t id)
{
    if (id == 2) { g.life.daynight ^= 1; game_save_now(); }
    else if (id == 3) { g.life.weather ^= 1; game_save_now(); }
    else if (id == 4 && !life_claim_visitor())
        ui_hud_bubble("No Visitor Reward Available / See Log", 0xf5c86b, 2400);
    else if (id == 5 && !life_claim_season())
        ui_hud_bubble("Season Reward Unavailable / See Log", 0xf5c86b, 2400);
    menu_open(MENU_CALENDAR);
}

static void pet_changed(void)
{
    state_finish_load();
    behaviour_force_sleep(false);
    behaviour_set_state(SPIDER_IDLE);
    behaviour_sync_position();
    needs_reset_runtime(lv_tick_get());
    terrarium_clear_poop();
    spider_refresh_colours();
    s_pet_change_waiting = true;
    ui_hud_bubble("Saving Family / Keep Power Connected", 0xf5c86b, 2600);
    menu_open(MENU_GENERATIONS);
}

static void on_generations_pick(uint8_t id)
{
    if (id == 0) menu_open(MENU_RETIRE_CONFIRM);
    else if (id >= 1 && id <= g.life.retired_count) {
        s_retired_slot = id - 1;
        menu_open(MENU_RETIRED);
    }
}

static void on_retired_pick(uint8_t id)
{
    if (id == 0) menu_open(MENU_RESUME_CONFIRM);
}

static void on_family_confirm(uint8_t id)
{
    if (id != 0) { menu_open(MENU_GENERATIONS); return; }
    if (save_busy() || net_busy() || sd_busy() || save_restore_pending()) {
        ESP_LOGW(TAG, "family change rejected: persistence/radio busy");
        ui_hud_bubble("Busy / Try Again When Saving Finishes", 0xf5c86b, 2400);
        return;
    }
    bool ok = s_open_menu == MENU_RETIRE_CONFIRM ?
              life_retire_confirmed() : life_resume(s_retired_slot);
    if (ok) pet_changed();
    else {
        ui_hud_bubble("Family Change Rejected / See Log", 0xf5c86b, 2400);
        menu_open(MENU_GENERATIONS);
    }
}

static void on_album_pick(uint8_t id)
{
    if (id == 0) {
        if (!game_save_album_photo())
            ui_hud_bubble("Photo Not Queued / See Log", 0xf5c86b, 2400);
        menu_open(MENU_ALBUM);
    } else if (id == 1) {
        if (!album_request_scan()) ESP_LOGW(TAG, "photo storage scan not queued");
        menu_open(MENU_PHOTO_STORAGE);
    }
}

static void on_photo_storage_pick(uint8_t id)
{
    if (id >= 2 && id < ALBUM_CAPACITY + 2) {
        s_photo_slot = id - 2;
        menu_open(MENU_PHOTO_ACTIONS);
    }
}

static void on_photo_pick(uint8_t id)
{
    if (id == 0) {
        if (album_request_load(s_photo_slot)) menu_open(MENU_PHOTO);
        else ui_hud_bubble("Photo Load Rejected / See Log", 0xf5c86b, 2400);
    } else if (id == 1) menu_open(MENU_PHOTO_DELETE);
}

static void on_photo_delete_pick(uint8_t id)
{
    if (id == 0) {
        if (!album_request_delete(s_photo_slot))
            ui_hud_bubble("Photo Delete Not Queued / See Log", 0xf5c86b, 2400);
        menu_open(MENU_PHOTO_STORAGE);
    } else if (id == 1) menu_open(MENU_PHOTO_ACTIONS);
}

/* Today's rows carry their own ids: 3 is the check-in and 4..4+SLOTS-1 are the
 * claimable tasks. The callback is wired straight to this function, so the ids
 * are what arrives here -- there is no longer a translating shim. */
static void on_daily_pick(uint8_t id)
{
    if (id == 3) {
        uint32_t xp = needs_daily_claim();
        if (xp == 0) ESP_LOGI(TAG, "daily: nothing to claim");
        game_save_now();
        menu_open(MENU_TODAY);
        return;
    }
    if (id >= 4 && id < 4 + DAILY_TASK_SLOTS) {
        uint32_t xp = needs_daily_task_claim_at((uint8_t)(id - 4));
        if (xp == 0) ESP_LOGI(TAG, "daily: task %u not ready", (unsigned)(id - 4));
        game_save_now();
        menu_open(MENU_TODAY);
        return;
    }
    back_to_main();
}

static void on_shop_pick(uint8_t id)
{
    if (id < TREAT_COUNT) {
        /* A refusal is already logged by treat_buy; the menu re-renders with the
         * unchanged Silk balance so the player sees it did not happen. */
        treat_buy((treat_t)id);
        game_save_now();
        menu_open(MENU_SHOP);
        ui_menu_set_page(id / 6);
        return;
    }
    back_to_main();
}

/* Stage the visible position until the player explicitly commits it. */
static void decor_begin_placement(uint8_t kind)
{
    const decor_def_t *def = decor_def(kind);
    if (!def) return;
    if (g.decor_locked) {
        ESP_LOGW(TAG, "decor placement rejected: decor is locked");
        ui_hud_bubble("Decor Locked / Settings > Lock Decor", 0xf5c86b, 2200);
        return;
    }
    /* Food out means the spider is hunting; see decor_begin_move(). */
    if (prey_count() > 0) {
        ESP_LOGW(TAG, "decor placement rejected: %d live prey in the tank", prey_count());
        ui_hud_bubble("Catch the Bugs First", 0xf5c86b, 2000);
        return;
    }
    g_decor_selected = kind;
    g_decor_mode = 1;
    g_decor_move_slot = -1;
    ui_menu_close();
    ui_hud_set_chrome(false);
    behaviour_aim_cancel();
    s_place_dragging = false;
    s_place_blocking = def->blocking;
    /* Seed the layer to the top of the band; placement_position() re-seeds it
     * from the actual staged position, which is what the count depends on. */
    s_place_layer = 0;
    placement_position(ui.W / 2, ui.H / 2);
    ui_hud_set_place_mode(true, "Slide to position; Done saves");
}

/* Keep position, path and layer staged until Done. */
static void decor_begin_move(int slot)
{
    if (!decor_at(slot)) return;
    if (g.decor_locked) {
        ESP_LOGW(TAG, "decor move rejected: decor is locked");
        ui_hud_bubble("Decor Locked / Settings > Lock Decor", 0xf5c86b, 2200);
        return;
    }
    /* Live prey in the tank means the spider is hunting. Moving furniture under
     * a hunt let the player shove a bug's cover away mid-pounce, and the drag
     * gesture competed with the aim gesture. */
    if (prey_count() > 0) {
        ESP_LOGW(TAG, "decor move rejected: %d live prey in the tank", prey_count());
        ui_hud_bubble("Catch the Bugs First", 0xf5c86b, 2000);
        return;
    }
    g_decor_move_slot = slot;
    s_place_blocking = decor_at(slot)->blocking;
    s_place_layer = decor_layer_index_with_gift(slot);
    g_decor_selected = decor_at(slot)->kind;
    g_decor_mode = 1;
    ui_menu_close();
    ui_hud_set_chrome(false);
    behaviour_aim_cancel();
    s_place_dragging = false;
    placement_position(decor_at(slot)->x, decor_at(slot)->y);
    ui_hud_set_place_mode(true, "Slide to move; Done saves");
}

static void on_decor_pick(uint8_t id)
{
    if (id >= DECOR_COUNT) {
        ESP_LOGW(TAG, "decor menu rejected invalid id %u", id);
        menu_open(MENU_DECOR);
        return;
    }
    const decor_def_t *def = decor_def(id);
    if (!def) return;
    if (!decor_unlocked(id)) {
        ui_hud_bubble("Level Too Low", 0xf5c86b, 1800);
        menu_open(MENU_DECOR);
        ui_menu_set_page(id / 6);
        return;
    }
    /* Buying and placing are one action: the player picked the item because
     * they want it in the tank, so a second confirmation is just friction. */
    if (!decor_owned(id) && !decor_buy(id)) {
        ui_hud_bubble("Not Enough Silk", 0xf5c86b, 1800);
        menu_open(MENU_DECOR);
        ui_menu_set_page(id / 6);
        return;
    }
    game_save_now();
    decor_begin_placement(id);
}

static void on_gift_item_pick(uint8_t id)
{
    if (!g.gift.used) { menu_open(MENU_MAIN); return; }
    if (id == 0) {
        /* Same rule as decor: while prey is out the spider is hunting, and a
         * drag gesture would compete with the aim gesture. */
        if (prey_count() > 0) {
            ESP_LOGW(TAG, "gift move rejected: %d live prey in the tank", prey_count());
            ui_hud_bubble("Catch the Bugs First", 0xf5c86b, 2000);
            menu_open(MENU_MAIN);
            return;
        }
        s_gift_moving = true;
        g.gift_mode = 1;
        g.gift_selected = g.gift.kind;
        /* The gift is always walked over, so there is no path to seed. */
        s_place_blocking = DECOR_WALK_OVER;
        s_place_layer = decor_gift_layer_index(s_place_blocking);
        ui_menu_close();
        ui_hud_set_chrome(false);
        behaviour_aim_cancel();
        s_place_dragging = false;
        placement_position(g.gift.x, g.gift.y);
        ui_hud_set_place_mode(true, "Slide to move; Done saves");
    } else {
        g.gift.used = 0;
        terrarium_refresh_gift();
        game_save_now();
        menu_open(MENU_MAIN);
    }
}

/* Settings submenu: connectivity and storage. Both are infrastructure for OTA
 * and backups rather than gameplay, so they live behind their own entry rather
 * than cluttering the main list. */
static void on_settings_pick(uint8_t id)
{
    switch (id) {
    case 0:
        menu_open(MENU_WIFI);
        break;
    case 1:
        s_sd_message = sd_request(SD_JOB_MOUNT) ? NULL : "Card Check Rejected; Try Again";
        if (s_sd_message) ESP_LOGW(TAG, "SD probe not queued");
        menu_open(MENU_SD);
        break;
    case 2: menu_open(MENU_ABOUT); break;
    case 3: menu_open(MENU_POWER); break;
    case 4: menu_open(MENU_DISPLAY); break;
    case 5: menu_open(MENU_SOUND); break;
    case 6: {
        /* One switch drives both halves of unattended play: wild prey keeps
         * arriving and the spider hunts it without the player aiming. */
        int page = ui_menu_page();
        bool on = !g.auto_feed;
        g.auto_feed = on;
        g.auto_hunt = on;
        if (!on) behaviour_aim_cancel();
        game_save_now();
        menu_open(MENU_SETTINGS);
        ui_menu_set_page(page);
        break;
    }
    case 7: {
        int page = ui_menu_page();
        g.auto_clean = !g.auto_clean;
        game_save_now();
        menu_open(MENU_SETTINGS);
        ui_menu_set_page(page);
        break;
    }
    case 9: {
        int page = ui_menu_page();
        g.decor_locked = !g.decor_locked;
        /* A lock that lands mid-placement would strand the player in a mode
         * they can no longer commit, so cancel any staged edit first. */
        if (g.decor_locked && g_decor_mode) game_cancel_decor_placement();
        game_save_now();
        menu_open(MENU_SETTINGS);
        ui_menu_set_page(page);
        break;
    }
    case 8: menu_open(MENU_RESET); break;
    default: break;
    }
}

static void on_wifi_pick(uint8_t id)
{
    s_wifi_message[0] = 0;
    if (id == 0) {
        if (!net_scan_start())
            snprintf(s_wifi_message, sizeof s_wifi_message, "Scan rejected: worker unavailable or busy");
        menu_open(MENU_WIFI_SCAN);
        return;
    }
    if (id == 1) {
        ui_menu_open_input("Network Name (SSID)", "", false, 32, on_ssid);
        ui_menu_set_back(back_to_wifi);
        return;
    }
    if (id == 2) { menu_open(MENU_WIFI_SAVED); return; }
    int i = (int)id - 4;
    if (i >= 0 && i < net_scan_count()) {
        snprintf(s_join_ssid, sizeof s_join_ssid, "%s", net_scan_ssid(i));
        if (net_scan_secure(i)) {
            ui_menu_open_input("WiFi Password", "", true, 63, on_password);
            ui_menu_set_back(back_to_wifi);
            return;
        }
        if (!net_connect_to(s_join_ssid, ""))
            snprintf(s_wifi_message, sizeof s_wifi_message, "Connection rejected; saved list may be full");
    }
    menu_open(MENU_WIFI);
}

/* SD actions: backup and restore the save. */
static void on_sd_pick(uint8_t id)
{
    bool accepted = false;
    if (id == 0) {
        accepted = sd_request(SD_JOB_BACKUP);
    } else if (id == 1) {
        accepted = sd_request(SD_JOB_RESTORE);
    }
    s_sd_message = accepted ? NULL : "Request Rejected; Try Again";
    if (!accepted) ESP_LOGW(TAG, "SD menu action %u not queued", id);
    menu_open(MENU_SD);
}

/* The reset submenu is a confirmation step: the destructive action is never one
 * tap away from the main list. */
static void on_reset_pick(uint8_t id)
{
    if (id == 0) do_factory_reset();
}

static void on_power_pick(uint8_t id)
{
    switch (id) {
    case 2: g.power_mode = (g.power_mode + 1) % 3; break;
    case 3: g.screen_timeout = (g.screen_timeout + 1) % 5; break;
    case 4: g.deep_sleep_after = (g.deep_sleep_after + 1) % 5; break;
    case 5: g.rtc_wake = (g.rtc_wake + 1) % 3; break;
    case 6:
        if (!power_request_deep_sleep(g.rtc_wake == 0 ? 900 : g.rtc_wake == 1 ? 3600 : 21600))
            ui_hud_bubble("Sleep Rejected; Worker Busy", 0xf5c86b, 2200);
        else ui_menu_close();
        return;
    default: break;
    }
    game_save_now();
    /* Stay in whichever page the row belongs to, so changing the wake interval
     * does not bounce the player back to the top of the battery menu. */
    menu_open(id >= 4 ? MENU_POWER_SLEEP : MENU_POWER);
}

static void on_display_pick(uint8_t id)
{
    if (id == 1) power_rotation_cycle();
    else if (id == 2) {
        g.life.light_gesture_disabled = !g.life.light_gesture_disabled;
        cancel_enclosure_tap();
        game_save_now();
    } else if (id == 3) {
        g.lights_dimmed = !g.lights_dimmed;
        apply_enclosure_lights();
        game_save_now();
    }
    menu_open(MENU_DISPLAY);
}
/* BACK from a submenu returns to the main menu; BACK from the main menu just
 * closes (ui_menu_set_back is not set there, so the menu closes itself). */
static void back_to_main(void)
{
    menu_open(MENU_MAIN);
}

static const uint8_t k_track_background[AUDIO_MUSIC_TRACK_COUNT] = {2,18,6,14,1,13,29,45,37,21};

/* The icon for a task kind. Each kind gets its own artwork, which is also what
 * keeps the three Today rows visually distinct -- a list of three identical
 * icons reads as one repeated row rather than three goals. */
static const char *task_icon(uint8_t task)
{
    switch (task) {
    case 0: return "request_hunt";
    case 1: return "request_clean";
    case 2: return "request_play";
    default: return "request_pet";
    }
}

static const char *memory_icon(uint8_t kind)
{
    static const char *const icons[] = {
        "diary", "memory_treat", "memory_place", "memory_game", "memory_bond",
        "memory_growth", "memory_recovery", "memory_achievement", "memory_requests"
    };
    return icons[kind <= 8 ? kind : 0];
}

/* Today's rows: the check-in, then DAILY_TASK_SLOTS claimable tasks.
 *
 * Every row is something the player CLAIMS. The companion request used to be a
 * third row that could only be dismissed, which made Today read as a chore list
 * with one optional extra; merging it into the task rotation means every row is
 * a goal with a payout. */
static void daily_items(ui_menu_item_t *items)
{
    static char claim_sub[64];
    static char task_sub[DAILY_TASK_SLOTS][64];
    static char task_name[DAILY_TASK_SLOTS][48];
    uint32_t today = needs_daily_today();
    if (needs_daily_available()) {
        uint32_t next = (g.daily_day != DAILY_NEVER && g.daily_day + 1 == today &&
                         g.daily_streak > 0)
                      ? (g.daily_streak < DAILY_STREAK_MAX ? g.daily_streak + 1u : DAILY_STREAK_MAX)
                      : 1u;
        uint32_t step = next - 1u;
        snprintf(claim_sub, sizeof claim_sub, "Day %u / +%lu XP +%lu Silk",
                 (unsigned)next,
                 (unsigned long)(DAILY_XP_BASE + step * DAILY_XP_PER_DAY),
                 (unsigned long)(DAILY_SILK_BASE + step * DAILY_SILK_PER_DAY));
    } else {
        snprintf(claim_sub, sizeof claim_sub, "Claimed / Streak %u", (unsigned)g.daily_streak);
    }
    items[0] = (ui_menu_item_t){"Check In", claim_sub, needs_daily_available(), false, false, 3};
    for (int i = 0; i < DAILY_TASK_SLOTS; i++) {
        ui_menu_format_name(task_name[i], sizeof task_name[i], needs_daily_task_name_at((uint8_t)i));
        if (!needs_daily_task_ready_at((uint8_t)i) &&
            needs_daily_task_progress_at((uint8_t)i) >= needs_daily_task_goal_at((uint8_t)i)) {
            snprintf(task_sub[i], sizeof task_sub[i], "Done / +%u XP +%u Silk",
                     DAILY_TASK_XP, DAILY_TASK_SILK);
        } else if (needs_daily_task_ready_at((uint8_t)i)) {
            snprintf(task_sub[i], sizeof task_sub[i], "Complete / Tap to Claim");
        } else {
            snprintf(task_sub[i], sizeof task_sub[i], "%lu of %lu",
                     (unsigned long)needs_daily_task_progress_at((uint8_t)i),
                     (unsigned long)needs_daily_task_goal_at((uint8_t)i));
        }
        items[1 + i] = (ui_menu_item_t){task_name[i], task_sub[i],
            needs_daily_task_ready_at((uint8_t)i), false, false, (uint8_t)(4 + i)};
    }
}

static void menu_open(int which)
{
    cancel_enclosure_tap();
    if (which == MENU_DAILY) which = MENU_TODAY;
    if (which != MENU_PHOTO && s_album_loaded.pixels) {
        ui_menu_close();
        lv_image_cache_drop(&s_album_image);
        lv_image_header_cache_drop(&s_album_image);
        album_release_image(&s_album_loaded);
        memset(&s_album_image, 0, sizeof s_album_image);
    }
    ui_menu_item_t *items = s_menu_items;

    /* Remember which menu is on screen so the render tick can tell whether the
     * WiFi list is still the one being looked at. Without this, a scan that
     * finishes while the player has navigated elsewhere would yank them back. */
    s_open_menu = which;

    switch (which) {
    case MENU_MAIN: {
        /* Every row carries a subtext. A list where some rows have a second
         * line and others do not reads as unfinished, and it also makes the
         * rows different heights, so the eye cannot scan the column. */
        static char daily_sub[48], ach_sub[32], species_sub[32], decor_sub[48];
        if (needs_daily_available()) {
            snprintf(daily_sub, sizeof daily_sub, "Check In Ready");
        } else if (needs_daily_any_task_ready()) {
            snprintf(daily_sub, sizeof daily_sub, "Task Complete");
        } else {
            snprintf(daily_sub, sizeof daily_sub, "Streak %u", (unsigned)g.daily_streak);
        }
        int earned = 0;
        for (int i = 0; i < ACHIEVEMENT_COUNT; i++) {
            if (g.achievements & ACHIEVEMENTS[i].bit) earned++;
        }
        snprintf(ach_sub, sizeof ach_sub, "%d of %d Earned", earned, ACHIEVEMENT_COUNT);
        int unlocked = 0;
        for (int i = 0; i < PREY_SPECIES_COUNT; i++) {
            if (species_unlocked((prey_species_t)i)) unlocked++;
        }
        snprintf(species_sub, sizeof species_sub, "%d of %d Unlocked",
                 unlocked, PREY_SPECIES_COUNT);
        snprintf(decor_sub, sizeof decor_sub, "%d Placed", decor_placed_count());
        /* Page order is deliberate. The first page is the four things a player
         * reaches for while looking at the tank -- Decor, Treats, Backgrounds,
         * Borders -- and the last page is the reference material: Photo Album,
         * Help and Settings. The middle pages hold the progress screens, whose
         * order does not matter. With a page size of four and fifteen rows, the
         * last page holds three, so indices 12..14 must be the reference rows. */
        items[0] = (ui_menu_item_t){ "Decor", decor_sub, true, false, true, MENU_DECOR };
        items[1] = (ui_menu_item_t){ "Treats", "Spend Silk", true, false, true, MENU_SHOP };
        items[2] = (ui_menu_item_t){ "Backgrounds", "Enclosure Themes", true, false, true, MENU_THEME };
        items[3] = (ui_menu_item_t){ "Borders", "Mix and Match Frames", true, false, true, MENU_BORDERS };
        items[4] = (ui_menu_item_t){ "Your Spider", "Personality and Favorites", true, false, true, MENU_PERSONALITY };
        items[5] = (ui_menu_item_t){ "Today", daily_sub, true, false, true, MENU_TODAY };
        items[6] = (ui_menu_item_t){ "Training", "Learn Gentle Tricks", true, false, true, MENU_TRAINING };
        items[7] = (ui_menu_item_t){ "Calendar", "Rhythms, Seasons and Visitors", true, false, true, MENU_CALENDAR };
        items[8] = (ui_menu_item_t){ "Achievements", ach_sub, true, false, true, MENU_ACHIEVEMENTS };
        items[9] = (ui_menu_item_t){ "Prey Species", species_sub, true, false, true, MENU_SPECIES };
        items[10] = (ui_menu_item_t){ "Diary", "Memories Together", true, false, true, MENU_DIARY };
        items[11] = (ui_menu_item_t){ "Generations", "Optional / Keep Every Spider Safe", true, false, true, MENU_GENERATIONS };
        items[12] = (ui_menu_item_t){ "Photo Album", "Capture and Revisit Moments", true, false, true, MENU_ALBUM };
        items[13] = (ui_menu_item_t){ "Help Guide", "All Features and Controls", true, false, true, MENU_HELP };
        items[14] = (ui_menu_item_t){ "Settings", "Sound, Display, WiFi", true, false, true, MENU_SETTINGS };
        ui_menu_open("Spidarium", items, 15, on_main_pick);
        ui_menu_set_item_icon(0, sprite_icon_index("decor"));
        ui_menu_set_item_icon(1, sprite_icon_index("treats"));
        ui_menu_set_item_icon(2, sprite_icon_index("backgrounds"));
        ui_menu_set_item_icon(3, sprite_border_index(g.border_id == 255 ? g.background_id : g.border_id));
        ui_menu_set_item_icon(4, sprite_icon_index("personality"));
        ui_menu_set_item_icon(5, sprite_icon_index("today"));
        ui_menu_set_item_icon(6, sprite_icon_index("training"));
        ui_menu_set_item_icon(7, sprite_icon_index("calendar"));
        ui_menu_set_item_icon(10, sprite_icon_index("diary"));
        ui_menu_set_item_icon(11, sprite_icon_index("generations"));
        ui_menu_set_item_icon(12, sprite_icon_index("album"));
        break;
    }
    case MENU_ALBUM: {
        static char status[96];
        snprintf(status, sizeof status, "%s", album_status_text());
        items[0] = (ui_menu_item_t){"Take Photo", "Real Playfield / SD Card Required / Eight Slots",
            !sd_busy() && !net_busy() && !s_sd_capture, false, false, 0};
        items[1] = (ui_menu_item_t){"Storage Status", status, false, false, false, 254};
        items[2] = (ui_menu_item_t){"Photo Storage", "View and Delete Saved Photos",
            !sd_busy(), false, true, 1};
        ui_menu_open("Photo Album", items, 3, on_album_pick);
        ui_menu_set_item_icon(0, sprite_icon_index("camera"));
        ui_menu_set_item_icon(1, sprite_icon_index("status"));
        ui_menu_set_item_icon(2, sprite_icon_index("album"));
        ui_menu_set_back(back_to_main);
        break;
    }
    case MENU_PHOTO_STORAGE: {
        static char names[ALBUM_CAPACITY][24], details[ALBUM_CAPACITY][64];
        int count = 0;
        const album_slot_t *slots = album_slots();
        for (int i = 0; i < ALBUM_CAPACITY; i++) {
            if (album_busy()) break;
            if (!slots[i].occupied) continue;
            snprintf(names[i], sizeof names[i], "Photo %d", i + 1);
            snprintf(details[i], sizeof details[i], "%s", slots[i].valid ?
                     "Saved on SD / View or Delete" : "Unreadable / Tap for Delete");
            items[count++] = (ui_menu_item_t){names[i], details[i],
                !sd_busy(), false, true, i + 2};
        }
        if (!count) {
            items[count++] = (ui_menu_item_t){album_busy() ? "Loading Photos" : "No Saved Photos",
                "Take Photo in Photo Album", false, false, false, 254};
        }
        ui_menu_open("Photo Storage", items, count, on_photo_storage_pick);
        for (int i = 0; i < count; i++) ui_menu_set_item_icon(i, sprite_icon_index("photo"));
        ui_menu_set_back(back_to_album);
        break;
    }
    case MENU_PHOTO: {
        if (s_album_loaded.pixels) {
            ui_menu_open_image("Photo Album", &s_album_image);
        } else {
            items[0] = (ui_menu_item_t){album_busy() ? "Loading Photo" : "Photo Unavailable",
                album_status_text(), false, false, false, 254};
            ui_menu_open_detail("Photo Album", items, 1, NULL);
            ui_menu_set_item_icon(0, sprite_icon_index("photo"));
        }
        ui_menu_set_back(back_to_photo_actions);
        break;
    }
    case MENU_PHOTO_ACTIONS: {
        static char title[32];
        snprintf(title, sizeof title, "Photo %u", s_photo_slot + 1);
        bool ready = !sd_busy() && !net_busy();
        items[0] = (ui_menu_item_t){"View Photo", "Open Saved Playfield Image",
            ready && album_slots()[s_photo_slot].valid, false, true, 0};
        items[1] = (ui_menu_item_t){"Delete Photo", "Free This Slot / Confirmation Required",
            ready, false, true, 1};
        ui_menu_open_detail(title, items, 2, on_photo_pick);
        ui_menu_set_item_icon(0, sprite_icon_index("photo"));
        ui_menu_set_item_icon(1, sprite_icon_index("delete_photo"));
        ui_menu_set_destructive(1);
        ui_menu_set_back(back_to_photo_storage);
        break;
    }
    case MENU_PHOTO_DELETE: {
        items[0] = (ui_menu_item_t){"Delete This Photo?", "Only This SD Photo / Cannot Be Undone",
            false, false, false, 254};
        items[1] = (ui_menu_item_t){"Confirm Delete", "Pet and Other Photos Stay Safe",
            !sd_busy() && !net_busy(), false, false, 0};
        items[2] = (ui_menu_item_t){"Cancel", "Keep This Photo", true, false, false, 1};
        ui_menu_open_detail("Delete Photo?", items, 3, on_photo_delete_pick);
        ui_menu_set_item_icon(0, sprite_icon_index("photo"));
        ui_menu_set_item_icon(1, sprite_icon_index("delete_photo"));
        ui_menu_set_item_icon(2, sprite_icon_index("confirm_cancel"));
        ui_menu_set_destructive(0);
        ui_menu_set_back(back_to_photo_actions);
        break;
    }
    case MENU_TRAINING: {
        static char details[LIFE_TRICKS][80];
        static const char *const icons[] = {"trick_wave", "trick_spin", "trick_bow"};
        for (int i = 0; i < LIFE_TRICKS; i++) {
            snprintf(details[i], sizeof details[i], "%u of 5 / %s", g.life.progress[i],
                     g.life.progress[i] >= 5 ? "Learned" : "Practice Gently");
            items[i] = (ui_menu_item_t){life_trick_name(i), details[i], true, false, true, i};
        }
        ui_menu_open("Training", items, LIFE_TRICKS, on_training_pick);
        for (int i = 0; i < LIFE_TRICKS; i++) ui_menu_set_item_icon(i, sprite_icon_index(icons[i]));
        ui_menu_set_back(back_to_main);
        break;
    }
    case MENU_TRICK: {
        static char progress[80], practice[80];
        snprintf(progress, sizeof progress, "%u of 5 / Healthy, Awake / Energy 20+", g.life.progress[s_trick_slot]);
        snprintf(practice, sizeof practice, "Costs 2 Energy / Rest %u Seconds", g.life.cooldown);
        items[0] = (ui_menu_item_t){"Learning", progress, false, false, false, 254};
        items[1] = (ui_menu_item_t){"Practice", practice,
            g.life.progress[s_trick_slot] < 5 && !g.life.cooldown, false, false, 0};
        items[2] = (ui_menu_item_t){"Perform", "A Little Show / No Reward Grinding",
            g.life.progress[s_trick_slot] >= 5 && !g.life.cooldown, false, false, 1};
        ui_menu_open_detail(life_trick_name(s_trick_slot), items, 3, on_trick_pick);
        static const char *const icons[] = {"trick_wave", "trick_spin", "trick_bow"};
        ui_menu_set_item_icon(0, sprite_icon_index(icons[s_trick_slot]));
        ui_menu_set_item_icon(1, sprite_icon_index("practice"));
        ui_menu_set_item_icon(2, sprite_icon_index("perform"));
        ui_menu_set_back(back_to_training);
        break;
    }
    case MENU_CALENDAR: {
        static char clock_text[64], activity[64], visitor[64], season[64];
        time_t epoch = time(NULL);
        life_clock_t clock = life_clock(epoch > 0 && (uint64_t)epoch <= UINT32_MAX ? (uint32_t)epoch : 0);
        snprintf(clock_text, sizeof clock_text, "%s / %s",
                 clock.available ? life_phase_name(clock.period) : "Clock Not Set",
                 clock.available ? life_season_name(clock.season) : "Sync Clock in WiFi");
        snprintf(activity, sizeof activity, "%s", clock.available ?
                 life_activity_name(clock.activity) : "No Scheduled Events Without Clock");
        snprintf(visitor, sizeof visitor, "%s", life_visitor_available() ?
                 "A Friendly Visitor / Tap for 3 Silk" : "Occasional Afternoon Visits");
        snprintf(season, sizeof season, "%s", clock.available &&
                 clock.season_key > g.life.season_highwater ? "Season Keepsake / Tap for 10 Silk" : "Collected or Clock Unavailable");
        items[0] = (ui_menu_item_t){"Enclosure Rhythm", clock_text, false, false, false, 0};
        items[1] = (ui_menu_item_t){"Scheduled Activity", activity, false, false, false, 1};
        items[2] = (ui_menu_item_t){"Day and Night", "Subtle Light / No Forced Sleep", true, g.life.daynight, false, 2};
        items[3] = (ui_menu_item_t){"Gentle Weather", "Six Quiet Edge Particles / Optional", true, g.life.weather, false, 3};
        items[4] = (ui_menu_item_t){"Visitor", visitor, life_visitor_available(), false, false, 4};
        items[5] = (ui_menu_item_t){"Seasonal Reward", season, clock.available &&
            clock.season_key > g.life.season_highwater, false, false, 5};
        ui_menu_open("Calendar", items, 6, on_calendar_pick);
        static const char *const icons[] = {"calendar", "activity", "daylight", "weather", "visitor", "season_reward"};
        for (int i = 0; i < 6; i++) ui_menu_set_item_icon(i, sprite_icon_index(icons[i]));
        ui_menu_set_item_checkbox(2);
        ui_menu_set_item_checkbox(3);
        ui_menu_set_back(back_to_main);
        break;
    }
    case MENU_GENERATIONS: {
        static char names[LIFE_RETIRED_CAP][20], details[LIFE_RETIRED_CAP][64];
        items[0] = (ui_menu_item_t){"Retire and Raise Another", "Adult Only / Archived, Never Lost",
            !s_pet_change_waiting && g.stage == STAGE_ADULT && !g.sick &&
            g.life.retired_count < LIFE_RETIRED_CAP, false, true, 0};
        for (int i = 0; i < g.life.retired_count; i++) {
            memcpy(names[i], g.life.retired[i].core + offsetof(game_state_t, name), sizeof g.name);
            names[i][sizeof g.name] = 0;
            if (!names[i][0]) snprintf(names[i], sizeof names[i], "Spider %d", i + 1);
            snprintf(details[i], sizeof details[i], "Retired / Safe / Tap to Revisit");
            items[i + 1] = (ui_menu_item_t){names[i], details[i], !s_pet_change_waiting, false, true, i + 1};
        }
        ui_menu_open(s_pet_change_waiting ? "Family / Saving" : "Generations", items,
                     1 + g.life.retired_count, on_generations_pick);
        ui_menu_set_item_icon(0, sprite_icon_index("retire"));
        for (int i = 0; i < g.life.retired_count; i++) ui_menu_set_item_icon(i + 1, sprite_icon_index("generations"));
        ui_menu_set_back(back_to_main);
        break;
    }
    case MENU_RETIRED: {
        if (s_retired_slot >= g.life.retired_count) { menu_open(MENU_GENERATIONS); break; }
        static char name[sizeof g.name + 1], identity[96], memories[96];
        const life_pet_t *pet = &g.life.retired[s_retired_slot];
        memcpy(name, pet->core + offsetof(game_state_t, name), sizeof g.name);
        name[sizeof g.name] = 0;
        uint32_t level;
        memcpy(&level, pet->core + offsetof(game_state_t, level), sizeof level);
        snprintf(identity, sizeof identity, "%s / Level %" PRIu32 " / Bond %u",
                 name[0] ? name : "Your Spider", level, pet->core[offsetof(game_state_t, bond)]);
        snprintf(memories, sizeof memories, "Wave %u / Spin %u / Bow %u / Diary %u",
                 pet->progress[0], pet->progress[1], pet->progress[2], pet->companion.diary_count);
        items[0] = (ui_menu_item_t){"Resume This Spider", "Current Spider Takes This Safe Archive Slot", true, false, true, 0};
        items[1] = (ui_menu_item_t){"Family Memory", identity, false, false, false, 254};
        items[2] = (ui_menu_item_t){"Learning and Memories", memories, false, false, false, 253};
        items[3] = (ui_menu_item_t){"Always Safe", "No Death / No Automatic Replacement", false, false, false, 252};
        ui_menu_open_detail("Retired Spider", items, 4, on_retired_pick);
        ui_menu_set_item_icon(0, sprite_icon_index("resume"));
        ui_menu_set_item_icon(1, sprite_icon_index("personality"));
        ui_menu_set_item_icon(2, sprite_icon_index("diary"));
        ui_menu_set_item_icon(3, sprite_icon_index("generations"));
        ui_menu_set_back(back_to_generations);
        break;
    }
    case MENU_RETIRE_CONFIRM:
    case MENU_RESUME_CONFIRM: {
        bool retire = which == MENU_RETIRE_CONFIRM;
        items[0] = (ui_menu_item_t){retire ? "Keep This Adult in Your Family" : "Swap Active and Archived Spiders",
            retire ? "Three Safe Archive Slots / No Eviction" : "Both Spiders Kept / Shared Habitat and Silk",
            false, false, false, 254};
        items[1] = (ui_menu_item_t){retire ? "Confirm Retirement" : "Confirm Resume",
            retire ? "Raise a Hatchling / Inherit Parent Colors" : "No Spider Deleted",
            !s_pet_change_waiting, false, false, 0};
        items[2] = (ui_menu_item_t){"Cancel", "Keep Everything As It Is", true, false, false, 1};
        ui_menu_open_detail(retire ? "Retire This Adult?" : "Resume This Spider?", items, 3, on_family_confirm);
        ui_menu_set_item_icon(0, sprite_icon_index("generations"));
        ui_menu_set_item_icon(1, sprite_icon_index(retire ? "retire" : "resume"));
        ui_menu_set_item_icon(2, sprite_icon_index("confirm_cancel"));
        ui_menu_set_back(retire ? back_to_generations : back_to_retired);
        break;
    }
    case MENU_PERSONALITY:
    case MENU_TODAY:
    case MENU_DIARY:
    case MENU_MEMORY: {
        typedef struct { char title[64], detail[128]; } companion_row_t;
        static companion_row_t *rows;
        if (!rows) rows = heap_caps_calloc(COMPANION_DIARY_CAP, sizeof *rows,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!rows) {
            ESP_LOGE(TAG, "companion menu allocation failed");
            ui_hud_bubble("Memory Unavailable / See Log", 0xf5c86b, 2400);
            break;
        }
        companion_tick();
        int count = 0;
        const char *heading = "Your Spider";
        ui_menu_pick_cb callback = on_main_pick;
        if (which == MENU_PERSONALITY) {
            static const char *const games[] = {"Bug Hunt", "Color Match", "Quick Tap",
                                                "Web Weave", "Pounce"};
            const companion_state_t *c = &g.companion;
            snprintf(rows[0].detail, sizeof rows[0].detail, "%s / Bond %u / %s",
                     game_temperament()->name, g.bond,
                     g.bond >= 75 ? "Devoted" : g.bond >= 50 ? "Trusted" : g.bond >= 25 ? "Familiar" : "Getting Acquainted");
            snprintf(rows[1].detail, sizeof rows[1].detail, "%s",
                     c->favorite_treat == 255 ? "Still Discovering / Try Treats" : treat_name(c->favorite_treat));
            snprintf(rows[2].detail, sizeof rows[2].detail, "%s",
                     c->favorite_gift == 255 ? "Still Discovering / Visits Build Familiarity" : gift_kind_name(c->favorite_gift));
            snprintf(rows[3].detail, sizeof rows[3].detail, "%s",
                     c->favorite_play == 255 ? "Still Discovering / Finish Games" : games[c->favorite_play]);
            const char *labels[] = {"Temperament and Bond", "Favorite Treat", "Favorite Place", "Favorite Game"};
            for (int i = 0; i < 4; i++)
                items[count++] = (ui_menu_item_t){labels[i], rows[i].detail, false, false, false, i};
        } else if (which == MENU_TODAY) {
            heading = g.companion.clock_mode == 1 ? "Today" : "Today / Play Day";
            callback = on_daily_pick;
            /* Every row is claimable: the check-in plus three rotating tasks.
             * The companion request used to be a third row that could only be
             * dismissed, which made Today read as a chore list with one optional
             * extra; merging it into the task rotation means every row is a goal
             * with a payout. */
            daily_items(items);
            count = 1 + DAILY_TASK_SLOTS;
        } else {
            heading = "Diary";
            callback = on_memory_pick;
            if (which == MENU_MEMORY) {
                if (s_memory_slot >= g.companion.diary_count) {
                    items[count++] = (ui_menu_item_t){"Memory Rolled Out", "Older Unpinned Memories Are Replaced", false, false, false, 254};
                    ui_menu_open_detail("Diary", items, count, on_memory_pick);
                    ui_menu_set_item_icon(0, sprite_icon_index("diary"));
                    ui_menu_set_back(back_to_diary);
                    break;
                }
                companion_memory_text(s_memory_slot, rows[0].title, sizeof rows[0].title,
                                      rows[0].detail, sizeof rows[0].detail);
                items[count++] = (ui_menu_item_t){rows[0].title, rows[0].detail, false, false, false, 254};
                items[count++] = (ui_menu_item_t){g.companion.diary[s_memory_slot].pinned ? "Unpin Memory" : "Pin Memory",
                    "Eight Pins / Oldest Unpinned Memories Roll Out", true, false, false, 255};
            } else {
                for (int i = g.companion.diary_count - 1; i >= 0; i--) {
                    companion_memory_text(i, rows[count].title, sizeof rows[count].title,
                                          rows[count].detail, sizeof rows[count].detail);
                    companion_memory_t *m = &g.companion.diary[i];
                    char date[32];
                    if (m->calendar) {
                        time_t stamp = (time_t)m->day * 86400;
                        struct tm tm;
                        gmtime_r(&stamp, &tm);
                        strftime(date, sizeof date, "%Y-%m-%d", &tm);
                    } else snprintf(date, sizeof date, "Play Day %lu", (unsigned long)m->day + 1);
                    snprintf(rows[count].detail, sizeof rows[count].detail, "%s%s / Tap to Read",
                             m->pinned ? "Pinned / " : "", date);
                    items[count] = (ui_menu_item_t){rows[count].title, rows[count].detail, true, false, true, i};
                    count++;
                }
                if (!count) items[count++] = (ui_menu_item_t){"No Memories Yet", "Favorites and Milestones Appear Here", false, false, false, 254};
            }
        }
        if (which == MENU_MEMORY || which == MENU_PERSONALITY)
            ui_menu_open_detail(heading, items, count, callback);
        else ui_menu_open(heading, items, count, callback);
        for (int i = 0; i < count; i++) {
            const char *icon;
            if (which == MENU_PERSONALITY) {
                static const char *const icons[] = {"bond", "favorite_treat", "favorite_place", "favorite_game"};
                icon = icons[i];
            } else if (which == MENU_TODAY) {
                icon = i == 0 ? "checkin" : task_icon(needs_daily_task_at((uint8_t)(i - 1)));
            } else if (which == MENU_MEMORY) {
                icon = i == 0 ? memory_icon(g.companion.diary[s_memory_slot].kind) :
                       g.companion.diary[s_memory_slot].pinned ? "unpin" : "pin";
            } else {
                icon = count == 1 && !g.companion.diary_count ? "diary" :
                       memory_icon(g.companion.diary[items[i].id].kind);
            }
            ui_menu_set_item_icon(i, sprite_icon_index(icon));
        }
        ui_menu_set_back(which == MENU_MEMORY ? back_to_diary : back_to_main);
        break;
    }
    case MENU_SETTINGS: {
        items[0] = (ui_menu_item_t){ "WiFi", "Networks and Time Sync", true, false, true, 0 };
        items[1] = (ui_menu_item_t){ "SD Card", "Back Up and Restore", true, false, true, 1 };
        items[2] = (ui_menu_item_t){ "About", "Version and Hardware", true, false, true, 2 };
        items[3] = (ui_menu_item_t){ "Battery", "Charge, Runtime and Sleep", true, false, true, 3 };
        items[4] = (ui_menu_item_t){ "Display", "Brightness and Rotation", true, false, true, 4 };
        items[5] = (ui_menu_item_t){ "Sound", "Volume and Mute", true, false, true, 5 };
        items[6] = (ui_menu_item_t){ "Auto Feed", "Wild Prey and Unattended Hunting",
                                     true, g.auto_feed, false, 6 };
        items[7] = (ui_menu_item_t){ "Auto Clean", "Sweep Droppings Automatically",
                                     true, g.auto_clean, false, 7 };
        items[8] = (ui_menu_item_t){ "Lock Decor", "Placed Items Cannot Be Moved",
                                     true, g.decor_locked, false, 9 };
        items[9] = (ui_menu_item_t){ "Wipe Spider", "Erases This Spider", true, false, true, 8 };
        ui_menu_open("Settings", items, 10, on_settings_pick);
        ui_menu_set_item_checkbox(6);
        ui_menu_set_item_checkbox(7);
        ui_menu_set_item_checkbox(8);
        ui_menu_set_destructive(9);
        ui_menu_set_back(back_to_main);
        break;
    }
    case MENU_POWER: {
        /* Two groups, in the order a player actually thinks about them:
         *
         *   1. What the battery is doing right now (read-only).
         *   2. How the screen behaves (the settings people come here to change).
         *
         * Deep sleep used to sit in the same list as the timeout, which made it
         * look like another display preference rather than a separate mode that
         * stops the pet until it wakes. It now has its own submenu, and the
         * confusing "Automatic Sleep" row (which only ever described RTC mode)
         * is gone -- the mode row already says "RTC Sleep". */
        static char status[80], runtime[48];
        power_status(status, sizeof status);
        power_runtime_text(runtime, sizeof runtime);
        items[0] = (ui_menu_item_t){ "Battery", status, false, false, false, 0 };
        items[1] = (ui_menu_item_t){ "Runtime", runtime, false, false, false, 0 };
        items[2] = (ui_menu_item_t){ "Power Mode", power_mode_name(g.power_mode), true, false, false, 2 };
        items[3] = (ui_menu_item_t){ "Screen Timeout", power_timeout_name(g.screen_timeout), true, false, false, 3 };
        items[4] = (ui_menu_item_t){ "Deep Sleep", power_delay_name(g.deep_sleep_after), true, false, true, 4 };
        ui_menu_open("Battery", items, 5, on_power_pick);
        ui_menu_set_back(back_to_settings);
        break;
    }
    case MENU_POWER_SLEEP: {
        /* Everything about actually stopping, in one place, with the
         * consequence spelled out on each row. */
        items[0] = (ui_menu_item_t){ "Sleep After Idle", power_delay_name(g.deep_sleep_after),
                                     true, false, false, 4 };
        items[1] = (ui_menu_item_t){ "Wake Interval", power_wake_name(g.rtc_wake),
                                     true, false, false, 5 };
        items[2] = (ui_menu_item_t){ "Sleep Now", "Saves First / Right Button Wakes",
                                     !net_busy(), false, true, 6 };
        items[3] = (ui_menu_item_t){ "Pet Ages While Asleep", "RTC Catch-Up on Wake / No Radio",
                                     false, false, false, 8 };
        ui_menu_open("Deep Sleep", items, 4, on_power_pick);
        ui_menu_set_back(back_to_power);
        break;
    }
    case MENU_DISPLAY: {
        /* The slider row's readout is derived from its value by refresh_page(). */
        items[0] = (ui_menu_item_t){ "Brightness", NULL, true, false, false, 0 };
        items[1] = (ui_menu_item_t){ "Screen Rotation", power_rotation_name(power_rotation()),
                                     true, false, true, 1 };
        items[2] = (ui_menu_item_t){"Double-Tap Lights", "Allow Enclosure Light Gesture",
            true, !g.life.light_gesture_disabled, false, 2};
        items[3] = (ui_menu_item_t){"Dim Enclosure", "Manual Light Control / Always Available",
            true, g.lights_dimmed, false, 3};
        ui_menu_open("Display", items, 4, on_display_pick);
        ui_menu_set_item_slider(0, g.brightness, on_brightness_slider);
        ui_menu_set_item_icon(2, sprite_icon_index("light_gesture"));
        ui_menu_set_item_icon(3, sprite_icon_index("enclosure_light"));
        ui_menu_set_item_checkbox(2);
        ui_menu_set_item_checkbox(3);
        ui_menu_set_back(back_to_settings);
        break;
    }

    case MENU_WIFI: {
        int n = 0;
        if (net_busy()) {
            items[n++] = (ui_menu_item_t){ "Working; Please Wait", "Radio Is Busy",
                                           false, false, false, 0 };
            ui_menu_open("WiFi", items, n, on_wifi_pick);
            ui_menu_set_back(back_to_settings);
            break;
        }
        items[n++] = (ui_menu_item_t){ "Network Status", s_wifi_message[0] ? s_wifi_message : net_status_text(),
                                       false, false, false, 0 };
        items[n++] = (ui_menu_item_t){ "Scan Networks", "Find Nearby Networks",
                                       true, false, false, 0 };
        items[n++] = (ui_menu_item_t){ "Add Network", "Enter Name and Password",
                                       true, false, true, 1 };
        items[n++] = (ui_menu_item_t){ "Saved Networks", "Join or Forget",
                                       true, false, true, 2 };
        ui_menu_open("WiFi", items, n, on_wifi_pick);
        ui_menu_set_back(back_to_settings);
        break;
    }
    case MENU_WIFI_SCAN: {
        int n = 0;
        items[n++] = (ui_menu_item_t){ "Scan Status", s_wifi_message[0] ? s_wifi_message : net_status_text(),
                                       false, false, false, 0 };
        static char details[12][32];
        int found = net_scan_count();
        for (int i = 0; i < found && n < 20; i++) {
            const char *ssid = net_scan_ssid(i);
            if (!ssid[0]) continue;   /* hidden network */
            bool secure = net_scan_secure(i);
            snprintf(details[i], sizeof details[i], "%d dBm / %s", net_scan_rssi(i),
                     secure ? "Secured" : "Open");
            items[n].label      = ssid;
            items[n].detail     = details[i];
            items[n].enabled    = true;
            items[n].selected   = (strcmp(ssid, net_ssid()) == 0);
            items[n].is_submenu = secure;
            items[n].id         = (uint8_t)(i + 4);
            n++;
        }
        ui_menu_open("Nearby Networks", items, n, on_wifi_pick);
        for (int i = 0; i < n; i++) ui_menu_set_item_icon(i, sprite_icon_index(i ? "wifi" : "status"));
        ui_menu_set_back(back_to_wifi);
        break;
    }
    case MENU_WIFI_SAVED: {
        int count = net_saved_count();
        for (int i = 0; i < count; i++)
            items[i] = (ui_menu_item_t){ net_saved_ssid(i), "Tap to Manage",
                                         true, false, true, i };
        if (!count) items[0] = (ui_menu_item_t){ "No Saved Networks", "Add One First",
                                                 false, false, false, 0 };
        ui_menu_open("Saved Networks", items, count ? count : 1, on_saved_network);
        ui_menu_set_back(back_to_wifi);
        break;
    }
    case MENU_WIFI_NETWORK:
        items[0] = (ui_menu_item_t){ "Sync RTC Now", "Set Clock From Network", true, false, false, 0 };
        items[1] = (ui_menu_item_t){ "Forget Network", "Remove Saved Credentials", true, false, false, 1 };
        ui_menu_open(net_saved_ssid(s_saved_network), items, 2, on_network_action);
        ui_menu_set_back(back_to_wifi);
        break;
    case MENU_OTA:
        items[0] = (ui_menu_item_t){ "Update From GitHub", "Latest Stable Release / Preserves Pet", net_has_credentials(), false, false, 0 };
        items[1] = (ui_menu_item_t){ "Advanced HTTPS URL", "Application Binary / Trusted Host Only", net_has_credentials(), false, true, 1 };
        items[2] = (ui_menu_item_t){ "Update Status", net_status_text(), false, false, false, 2 };
        items[3] = (ui_menu_item_t){ "After Installation", "Press RESET to Apply / Art Changes Need USB", false, false, false, 3 };
        ui_menu_open("Firmware Update", items, 4, on_ota_pick);
        ui_menu_set_back(back_to_about);
        break;

    case MENU_SD: {
        /* Mount on demand. Probing at boot stole DMA buffers from the LCD
         * driver and logged a burst of sdmmc errors on units with no card. */
        bool have = sd_present();
        items[0] = (ui_menu_item_t){ "Back Up Save",
                                     have ? "Write to Card" : sd_status_text(),
                                     have, false, false, 0 };
        items[1] = (ui_menu_item_t){ "Restore Save",
                                     !have ? sd_status_text() : sd_backup_exists() ? "Read From Card" : "No Backup",
                                     have && sd_backup_exists(), false, false, 1 };
        items[2] = (ui_menu_item_t){ "SD Status", s_sd_message ? s_sd_message : sd_status_text(),
                                     false, false, false, 2 };
        ui_menu_open("SD Card", items, 3, on_sd_pick);
        ui_menu_set_back(back_to_settings);
        break;
    }

    case MENU_RESET: {
        items[0] = (ui_menu_item_t){ "Start a New Tank", "Erases This Spider", true, false, false, 0 };
        ui_menu_open("New Tank", items, 1, on_reset_pick);
        ui_menu_set_destructive(0);
        ui_menu_set_back(back_to_settings);
        break;
    }

    case MENU_THEME: {
        static char names[THEME_COUNT][32];
        static char theme_sub[THEME_COUNT][32];
        for (int i = 0; i < THEME_COUNT; i++) {
            const theme_def_t *t = theme_get((uint8_t)i);
            ui_menu_format_name(names[i], sizeof names[i], t->name);
            /* Every row carries a subtext, so the list scans as one column. */
            if (!theme_unlocked((uint8_t)i)) {
                snprintf(theme_sub[i], sizeof theme_sub[i], "Level %lu",
                         (unsigned long)t->unlock_level);
            } else if (theme_current() == t) {
                snprintf(theme_sub[i], sizeof theme_sub[i], "In Use");
            } else if (theme_owned(i)) {
                snprintf(theme_sub[i], sizeof theme_sub[i], "Owned / Apply Free");
            } else {
                snprintf(theme_sub[i], sizeof theme_sub[i], "%u Silk / Buy", theme_cost(i));
            }
            items[i].label     = names[i];
            items[i].detail    = theme_sub[i];
            items[i].enabled   = theme_unlocked((uint8_t)i) &&
                (theme_owned(i) || g.silk >= theme_cost(i));
            items[i].selected  = (theme_current() == t);
            items[i].is_submenu = false;
            items[i].id        = (uint8_t)i;
        }
        ui_menu_open_grid("Backgrounds", items, THEME_COUNT, on_theme_pick);
        for (int i = 0; i < THEME_COUNT; i++)
            ui_menu_set_item_icon(i, sprite_background_index(i));
        ui_menu_set_back(back_to_main);
        break;
    }

    case MENU_BORDERS: {
        static char names[THEME_COUNT][32], details[THEME_COUNT][40];
        items[0] = (ui_menu_item_t){ "No Border", "Remove Frame / Free", true,
                                     g.border_id == 255, false, 255 };
        for (int i = 0; i < THEME_COUNT; i++) {
            ui_menu_format_name(names[i], sizeof names[i], theme_get(i)->name);
            if (g.border_id == i) snprintf(details[i], sizeof details[i], "In Use");
            else if (border_owned(i)) snprintf(details[i], sizeof details[i], "Owned / Apply Free");
            else snprintf(details[i], sizeof details[i], "%u Silk / Buy Frame", border_cost(i));
            items[i + 1] = (ui_menu_item_t){ names[i], details[i], border_owned(i) || g.silk >= border_cost(i),
                                            g.border_id == i, false, i };
        }
        ui_menu_open_grid("Borders", items, THEME_COUNT + 1, on_border_pick);
        for (int i = 0; i < THEME_COUNT; i++) ui_menu_set_item_icon(i + 1, sprite_border_index(i));
        ui_menu_set_back(back_to_main);
        break;
    }
    case MENU_SOUND: {
        /* The slider row's readout is derived from its value by refresh_page(),
         * so no detail string is needed here. */
        items[0] = (ui_menu_item_t){ "Volume", NULL, true, false, false, 0 };
        items[1] = (ui_menu_item_t){ "Sound", audio_muted() ? "Off / All Audio Muted" : "On / Effects and Music",
                                    true, !audio_muted(), false, 1 };
        items[2] = (ui_menu_item_t){ "Background Music", "Gentle Ambient Score", true, g.music_enabled, false, 2 };
        items[3] = (ui_menu_item_t){ "Music Volume", NULL, true, false, false, 3 };
        items[4] = (ui_menu_item_t){ "Soundtrack", audio_music_track_name(g.music_track),
                                    true, false, true, 4 };
        items[5] = (ui_menu_item_t){ "USB Chimes", "Plug In and Unplug", true, g.usb_sounds, false, 5 };
        ui_menu_open("Sound", items, 6, on_sound_pick);
        ui_menu_set_item_checkbox(1);
        ui_menu_set_item_checkbox(2);
        ui_menu_set_item_checkbox(5);
        ui_menu_set_item_slider(0, audio_volume(), on_volume_slider);
        ui_menu_set_item_slider(3, g.music_volume, on_volume_slider);
        ui_menu_set_item_icon(3, sprite_icon_index("music_volume"));
        ui_menu_set_item_icon(4, sprite_icon_index("soundtrack"));
        ui_menu_set_item_icon(5, sprite_icon_index("usb_chimes"));
        ui_menu_set_back(back_to_settings);
        break;
    }
    case MENU_SOUNDTRACK:
        for (int i = 0; i < AUDIO_MUSIC_TRACK_COUNT; i++)
            items[i] = (ui_menu_item_t){ audio_music_track_name(i),
                i == g.music_track ? "Selected / Original Ambient Score" : "Choose This Score",
                true, i == g.music_track, false, i };
        ui_menu_open("Soundtrack", items, AUDIO_MUSIC_TRACK_COUNT, on_soundtrack_pick);
        for (int i = 0; i < AUDIO_MUSIC_TRACK_COUNT; i++)
            ui_menu_set_item_icon(i, sprite_background_index(k_track_background[i]));
        ui_menu_set_back(back_to_sound);
        break;

    case MENU_ACHIEVEMENTS: {
        for (int i = 0; i < ACHIEVEMENT_COUNT; i++) {
            bool got = (g.achievements & ACHIEVEMENTS[i].bit) != 0;
            items[i].label     = ACHIEVEMENTS[i].name;
            items[i].detail    = got ? "Earned / +15 XP +20 Silk" : "Goal / +15 XP +20 Silk";
            items[i].enabled   = true;
            items[i].selected  = got;
            items[i].is_submenu = false;
            items[i].id        = (uint8_t)i;
        }
        ui_menu_open_grid("Achievements", items, ACHIEVEMENT_COUNT, on_achievement_pick);
        for (int i = 0; i < ACHIEVEMENT_COUNT; i++) ui_menu_set_item_icon(i, sprite_achievement_index(ACHIEVEMENTS[i].bit));
        ui_menu_set_back(back_to_main);
        break;
    }
    case MENU_SPECIES:
        static char species_detail[PREY_SPECIES_COUNT][64];
        static char species_names[PREY_SPECIES_COUNT][32];
        for (int i = 0; i < PREY_SPECIES_COUNT; i++) {
            const species_def_t *species = species_get((prey_species_t)i);
            /* The nutrition is shown as a plain multiplier so the player can
             * see why a waxworm is worth waiting for. */
            snprintf(species_detail[i], sizeof species_detail[i],
                "%s / level %lu / food x%.1f",
                species_unlocked(i) ? "Available" : "Locked",
                (unsigned long)species->unlock_level,
                (double)species->hunger_mult);
            ui_menu_format_name(species_names[i], sizeof species_names[i], species->name);
            items[i] = (ui_menu_item_t){ species_names[i],
                species_detail[i], false, species_unlocked(i), false, i };
        }
        static char species_title[48];
        snprintf(species_title, sizeof species_title, "Prey / Lv %lu", (unsigned long)g.level);
        ui_menu_open_grid(species_title, items, PREY_SPECIES_COUNT, NULL);
        for (int i = 0; i < PREY_SPECIES_COUNT; i++)
            ui_menu_set_item_icon(i, sprite_prey_index(i, false, false, 0));
        ui_menu_set_back(back_to_main);
        break;
    case MENU_SHOP: {
        static char shop_sub[TREAT_COUNT][48];
        static char shop_name[TREAT_COUNT][32];
        for (int i = 0; i < TREAT_COUNT; i++) {
            uint32_t cost = treat_cost((treat_t)i);
            ui_menu_format_name(shop_name[i], sizeof shop_name[i],
                                treat_name((treat_t)i));
            snprintf(shop_sub[i], sizeof shop_sub[i], "%lu Silk%s",
                     (unsigned long)cost, g.silk < cost ? " / Need More" : "");
            items[i] = (ui_menu_item_t){ shop_name[i], shop_sub[i],
                                         g.silk >= cost, false, true, (uint8_t)i };
        }
        static char shop_title[48];
        snprintf(shop_title, sizeof shop_title, "Treats / %lu Silk",
                 (unsigned long)g.silk);
        ui_menu_open_grid(shop_title, items, TREAT_COUNT, on_shop_pick);
        for (int i = 0; i < TREAT_COUNT; i++) ui_menu_set_item_icon(i, sprite_treat_index(i));
        ui_menu_set_back(back_to_main);
        break;
    }
    case MENU_DECOR: {
        static char decor_sub[DECOR_COUNT][48];
        static char decor_name[DECOR_COUNT][32];
        for (int i = 0; i < DECOR_COUNT; i++) {
            const decor_def_t *def = decor_def((uint8_t)i);
            ui_menu_format_name(decor_name[i], sizeof decor_name[i], def->name);
            if (!decor_unlocked((uint8_t)i)) {
                /* Level-locked. Showing the Silk cost here read as "you need
                 * 52 Silk", so a player with plenty of Silk could not tell why
                 * the row was grey. Name the actual blocker. */
                snprintf(decor_sub[i], sizeof decor_sub[i], "Level %u Needed",
                         def->unlock_level);
            } else if (decor_owned((uint8_t)i)) {
                snprintf(decor_sub[i], sizeof decor_sub[i], "Owned / %s",
                         def->climb_height ? "Climbable" : def->blocking ? "Blocks" : "Walk Over");
            } else {
                snprintf(decor_sub[i], sizeof decor_sub[i], "%u Silk / %s",
                         def->cost, def->climb_height ? "Climbable" : def->blocking ? "Blocks" : "Walk Over");
            }
            /* `selected` is deliberately NOT "owned". ui_menu_open() jumps to
             * the page of the first selected row, so marking owned kinds
             * selected yanked the player to page 2 the moment they bought
             * anything past the third row. The owned state is already in the
             * subtext and the checkmark is not needed here. */
            items[i] = (ui_menu_item_t){ decor_name[i], decor_sub[i],
                                         decor_unlocked(i) && (decor_owned(i) || g.silk >= def->cost),
                                         false, true, (uint8_t)i };
        }
        static char decor_title[48];
        snprintf(decor_title, sizeof decor_title, "Decor / %lu Silk",
                 (unsigned long)g.silk);
        ui_menu_open_grid(decor_title, items, DECOR_COUNT, on_decor_pick);
        /* Every row gets its own sprite, so the shop reads as a catalogue of
         * things rather than a list of names. */
        for (int i = 0; i < DECOR_COUNT; i++)
            ui_menu_set_item_icon((uint8_t)i, sprite_decor_item_index((uint8_t)i));
        ui_menu_set_back(back_to_main);
        break;
    }
    case MENU_DECOR_ITEM:
        decor_begin_move(s_decor_item_slot);
        break;
    case MENU_GIFT_ITEM:
        on_gift_item_pick(0);
        break;
    case MENU_ABOUT:
        items[0] = (ui_menu_item_t){ "Spidarium", esp_app_get_description()->version,
                                     false, false, false, 0 };
        items[1] = (ui_menu_item_t){ "ESP32-S3 / 480x480 AMOLED", "8 MB PSRAM / 16 MB Flash",
                                     false, false, false, 0 };
        items[2] = (ui_menu_item_t){ "Personal, Non-Commercial", "See LICENSE",
                                     false, false, false, 0 };
        items[3] = (ui_menu_item_t){ "Firmware Update", "GitHub Latest Release", true, false, true, 3 };
        ui_menu_open("About", items, 4, on_about_pick);
        ui_menu_set_back(back_to_settings);
        break;
    case MENU_HELP:
        for (int i = 0; i < HELP_TOPIC_COUNT; i++)
            items[i] = (ui_menu_item_t){ HELP_TOPICS[i].name, "Tap to Read", true, false, true, i };
        ui_menu_open("Help Guide", items, HELP_TOPIC_COUNT, on_help_pick);
        for (int i = 0; i < HELP_TOPIC_COUNT; i++) ui_menu_set_item_icon(i, sprite_icon_index(HELP_TOPICS[i].icon));
        ui_menu_set_back(back_to_main);
        break;

    default:
        break;
    }
    if (s_open_menu == which && ui_menu_showing(items)) {
        if (s_menu_page_known[which]) ui_menu_set_page(s_menu_pages[which]);
        s_page_menu = which;
        s_page_loading = which == MENU_PHOTO_STORAGE && album_busy();
        ui_menu_set_close_page_cb(remember_menu_page);
    }
}

/* ------------------------------------------------------------------ */
/* Pause                                                               */
/* ------------------------------------------------------------------ */
/* LVGL-task-only gameplay pause. Keep the service/render timer alive so the
 * UI can process requests and resume. Network exclusivity uses the adapter's
 * worker-side pause/acknowledgement API instead, including LCD composition. */
void game_pause(void)
{
    if (s_paused) return;
    s_paused = true;

    if (s_logic_timer)  lv_timer_pause(s_logic_timer);

    ESP_LOGI(TAG, "game paused (WiFi/OTA exclusive access to DMA RAM)");
}

/* LVGL-task-only; reset elapsed time rather than simulating the pause. */
void game_resume(void)
{
    if (!s_paused) return;
    s_paused = false;

    /* Reset the delta baseline, or the first tick after a long pause sees a
     * huge dt and the spider teleports. */
    s_last_logic = lv_tick_get();

    if (s_logic_timer)  lv_timer_resume(s_logic_timer);

    ESP_LOGI(TAG, "game resumed (DMA RAM shared again)");
}

bool game_paused(void)
{
    return s_paused || save_restore_pending() || net_busy() || power_sleep_pending();
}

void game_on_menu(lv_event_t *e)
{
    (void)e;
    g.taps++;
    menu_open(MENU_MAIN);
}

/* Open the per-item decor menu for a slot, for the serial console. */
bool game_ui_open_decor_item(int slot)
{
    if (!decor_at(slot)) return false;
    s_decor_item_slot = slot;
    menu_open(MENU_DECOR_ITEM);
    return true;
}

/* Open a menu by name, for the serial console.
 *
 * This calls the same menu_open() the UI calls, so a test through here exercises
 * the real path, including the SD page's queued on-demand probe. */
bool game_open_menu(const char *name)
{
    if      (!strcmp(name, "main"))     menu_open(MENU_MAIN);
    else if (!strcmp(name, "settings")) menu_open(MENU_SETTINGS);
    else if (!strcmp(name, "wifi"))     menu_open(MENU_WIFI);
    else if (!strcmp(name, "saved"))    menu_open(MENU_WIFI_SAVED);
    else if (!strcmp(name, "ota"))      menu_open(MENU_OTA);
    else if (!strcmp(name, "password")) game_ui_open_wifi_password_entry();
    else if (!strcmp(name, "species"))  menu_open(MENU_SPECIES);
    else if (!strcmp(name, "about"))    menu_open(MENU_ABOUT);
    else if (!strcmp(name, "help"))     menu_open(MENU_HELP);
    else if (!strcmp(name, "personality")) menu_open(MENU_PERSONALITY);
    else if (!strcmp(name, "today"))    menu_open(MENU_TODAY);
    else if (!strcmp(name, "training")) menu_open(MENU_TRAINING);
    else if (!strcmp(name, "calendar")) menu_open(MENU_CALENDAR);
    else if (!strcmp(name, "album")) {
        if (!album_request_scan()) ESP_LOGW(TAG, "console album scan not queued");
        menu_open(MENU_ALBUM);
    }
    else if (!strcmp(name, "photos")) {
        if (!album_request_scan()) ESP_LOGW(TAG, "console photo storage scan not queued");
        menu_open(MENU_PHOTO_STORAGE);
    }
    else if (!strcmp(name, "generations")) {
        menu_open(MENU_GENERATIONS);
    }
    else if (!strcmp(name, "diary"))    menu_open(MENU_DIARY);
    else if (!strcmp(name, "sd"))       on_settings_pick(1);
    else if (!strcmp(name, "theme"))    menu_open(MENU_THEME);
    else if (!strcmp(name, "borders"))  menu_open(MENU_BORDERS);
    else if (!strcmp(name, "sound"))    menu_open(MENU_SOUND);
    else if (!strcmp(name, "soundtrack")) menu_open(MENU_SOUNDTRACK);
    else if (!strcmp(name, "ach"))      menu_open(MENU_ACHIEVEMENTS);
    else if (!strcmp(name, "reset"))    menu_open(MENU_RESET);
    else if (!strcmp(name, "close"))    { ui_menu_close(); ui_hud_gift_menu_hide(); }
    else if (!strcmp(name, "power")) menu_open(MENU_POWER);
    else if (!strcmp(name, "display")) menu_open(MENU_DISPLAY);
    else if (!strcmp(name, "daily")) menu_open(MENU_DAILY);
    else if (!strcmp(name, "shop")) menu_open(MENU_SHOP);
    else if (!strcmp(name, "decor")) menu_open(MENU_DECOR);
    else if (!strcmp(name, "decoritem")) menu_open(MENU_DECOR_ITEM);
    else return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* UI marshalling for the serial console                               */
/* ------------------------------------------------------------------ */
/* The console runs on its own task. Anything that touches lv_* must run on the
 * LVGL task instead, or it corrupts LVGL's internal state -- the same class of
 * bug as calling lv_timer_pause() from a worker, which crashed the device.
 *
 * So the console posts a request and the render tick performs it. That is the
 * request-plus-acknowledgement pattern the rest of the project uses. */
static atomic_int s_ui_req = GAME_UI_NONE;
static int s_ui_value;
static atomic_bool s_ui_ok;
static lv_draw_buf_t *s_capture;
const lv_draw_buf_t *game_screen_capture(void) { return s_capture; }

static lv_draw_buf_t *capture_target(int target)
{
    if (net_busy()) {
        ESP_LOGW(TAG, "screenshot rejected: radio operation in progress");
        return NULL;
    }
    if (target != 2) return lv_snapshot_take(target == 1 ? cspider_obj() : lv_screen_active(),
        target == 1 ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_RGB565);
    ui_hud_capture_hide(true);
    ui_menu_capture_hide(true);
    lv_draw_buf_t *image = lv_snapshot_take(ui.screen, LV_COLOR_FORMAT_RGB565);
    ui_menu_capture_hide(false);
    ui_hud_capture_hide(false);
    return image;
}

static bool game_save_screenshot(void)
{
    if (sd_busy() || s_sd_capture || save_restore_pending()) {
        ESP_LOGW(TAG, "screenshot rejected: SD busy or restore awaiting reboot");
        return false;
    }
    lv_draw_buf_t *capture = capture_target(2);
    if (!capture) { ESP_LOGE(TAG, "playfield snapshot allocation failed"); return false; }
    sd_screenshot_t image = { .width = capture->header.w, .height = capture->header.h,
                             .stride = capture->header.stride };
    image.pixels = capture->data;
    if (!sd_request_screenshot(&image)) {
        lv_draw_buf_destroy(capture);
        return false;
    }
    s_sd_capture = capture;
    return true;
}

static bool game_save_album_photo(void)
{
    if (sd_busy() || s_sd_capture || save_restore_pending() || net_busy()) {
        ESP_LOGW(TAG, "album capture rejected: storage/radio busy");
        return false;
    }
    lv_draw_buf_t *capture = capture_target(2);
    if (!capture) { ESP_LOGE(TAG, "album snapshot allocation failed"); return false; }
    sd_screenshot_t image = {.pixels = capture->data, .width = capture->header.w,
        .height = capture->header.h, .stride = capture->header.stride};
    if (!album_request_capture(&image)) {
        lv_draw_buf_destroy(capture);
        return false;
    }
    s_sd_capture = capture;
    return true;
}

static bool capture_test(void)
{
    if (!ui_menu_visible() || lv_screen_active() != ui.screen) {
        ESP_LOGE(TAG, "capture test requires an open menu on the tank");
        return false;
    }
    game_state_t *state = heap_caps_malloc(sizeof g, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!state) { ESP_LOGE(TAG, "capture test backup allocation failed"); return false; }
    *state = g;
    lv_draw_buf_t *before = capture_target(0);
    lv_draw_buf_t *field = capture_target(2);
    lv_draw_buf_t *after = capture_target(0);
    bool restored = before && after && before->header.w == after->header.w &&
        before->header.h == after->header.h;
    bool differs = false;
    bool dimensions = field && field->header.w == ui.W && field->header.h == ui.H;
    if (restored && dimensions) {
        for (unsigned y = 0; y < before->header.h; y++) {
            restored = restored && !memcmp(before->data + y * before->header.stride,
                after->data + y * after->header.stride, before->header.w * 2);
            differs = differs || memcmp(before->data + y * before->header.stride,
                field->data + y * field->header.stride, before->header.w * 2);
        }
    }
    bool unchanged = !memcmp(state, &g, sizeof g);
    heap_caps_free(state);
    if (before) lv_draw_buf_destroy(before);
    if (field) lv_draw_buf_destroy(field);
    if (after) lv_draw_buf_destroy(after);
    ESP_LOGI(TAG, "capture test: menu_restored=%d playfield_differs=%d dimensions=%d state_unchanged=%d",
        restored, differs, dimensions, unchanged);
    return restored && differs && dimensions && unchanged;
}
enum { UI_PREPARING = -1 };

static bool test_gifts(void)
{
    game_state_t *original = heap_caps_malloc(sizeof g, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!original) { ESP_LOGE(TAG, "gift test backup allocation failed"); return false; }
    *original = g;
    int tested = 0;
    for (int kind = 0; kind < GIFT_KINDS; kind++) {
        g.gift.used = 1;
        g.gift.kind = kind;
        gift_compute();
        const gift_bonus_t *bonus = gift_bonus();
        bool effective = bonus->energy_regen_mult != 1.0f ||
            bonus->hygiene_decay_mult != 1.0f || bonus->pounce_range_mult != 1.0f ||
            bonus->aim_window_mult != 1.0f || bonus->wild_spawn_bonus != 0 ||
            bonus->pounce_success_mult != 1.0f || bonus->movement_efficiency_mult != 1.0f ||
            bonus->mood_mult != 1.0f || bonus->fear_mult != 1.0f ||
            bonus->prey_flee_penalty != 1.0f || bonus->catch_success_mult != 1.0f ||
            bonus->prey_detection_range_mult != 1.0f || bonus->all_stats_decay_mult != 1.0f;
        if (effective) tested++;
        printf("GIFT %d %s effective=%d eR=%.2f hD=%.2f range=%.2f catch=%.2f decay=%.2f\n",
               kind, gift_kind_name(kind), effective, bonus->energy_regen_mult,
               bonus->hygiene_decay_mult, bonus->pounce_range_mult,
               bonus->catch_success_mult * bonus->pounce_success_mult,
               bonus->all_stats_decay_mult);
    }
    g.gift.used = 0;
    gift_compute();
    bool removed = gift_bonus()->energy_regen_mult == 1.0f &&
                   gift_bonus()->all_stats_decay_mult == 1.0f &&
                   gift_bonus()->catch_success_mult == 1.0f;
    g = *original;
    heap_caps_free(original);
    gift_compute();
    bool economy = gift_economy_test();
    bool visits = behaviour_gift_test();
    bool blocks = behaviour_gift_block_test();
    return tested == GIFT_KINDS && removed && economy && visits && blocks;
}

bool game_ui_call(game_ui_action_t action)
{
    return game_ui_call_value(action, 0);
}

bool game_ui_call_value(game_ui_action_t action, int value)
{
    if (power_sleep_pending() || action <= GAME_UI_NONE || action > GAME_UI_MENU_PICK ||
        (action == GAME_UI_MENU_PICK && (value < 0 || value > 255)) ||
        (action == GAME_UI_ADD_SILK && value <= 0) ||
        (action == GAME_UI_SET_LEVEL && (value < 1 || value > 99)) ||
        (action == GAME_UI_DEEP_SLEEP && (value < 5 || value > 86400)) ||
        (action == GAME_UI_SCREEN_CAPTURE && (value < 0 || value > 2)) ||
        (action >= GAME_UI_SET_HUNGER && action <= GAME_UI_SET_SICK && (value < 0 || value > 100)) ||
        ((action == GAME_UI_SET_AUTO_FEED || action == GAME_UI_SET_AUTO_HUNT ||
          action == GAME_UI_SET_MUSIC || action == GAME_UI_SET_MUTED) && (value < 0 || value > 1)) ||
        ((action == GAME_UI_SET_VOLUME || action == GAME_UI_SET_MUSIC_VOLUME) &&
         (value < 0 || value > 100)) ||
        (action == GAME_UI_SET_MUSIC_TRACK && (value < 0 || value >= AUDIO_MUSIC_TRACK_COUNT)) ||
        (action == GAME_UI_BACKGROUND && (value < 0 || value >= THEME_COUNT)) ||
        (action == GAME_UI_BORDER && value != 255 && (value < 0 || value >= THEME_COUNT)) ||
        (action == GAME_UI_STATE && (value < 0 || value > SPIDER_PERCH)) ||
        (action == GAME_UI_SPAWN && (value < -1 || value >= PREY_SPECIES_COUNT))) {
        ESP_LOGW(TAG, "UI request rejected: invalid action or value");
        return false;
    }
    int expected = GAME_UI_NONE;
    if (!atomic_compare_exchange_strong(&s_ui_req, &expected, UI_PREPARING)) {
        ESP_LOGW(TAG, "UI request rejected: busy");
        return false;
    }
    s_ui_value = value;
    atomic_store(&s_ui_ok, true);
    atomic_store(&s_ui_req, action);

    /* Wait for the render tick to pick it up. Bounded, so a stalled render
     * timer cannot hang the console forever. */
    int attempts = action == GAME_UI_TEST_UI ? 1000 : action == GAME_UI_DECOR_TEST ? 500 : 100;
    for (int i = 0; i < attempts && s_ui_req != GAME_UI_NONE; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_ui_req != GAME_UI_NONE) {
        ESP_LOGW(TAG, "UI request acknowledgement timed out");
        return false;
    }
    return atomic_load(&s_ui_ok);
}

typedef struct { lv_point_t point; lv_indev_state_t state; } test_pointer_t;

static void test_pointer_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    test_pointer_t *pointer = lv_indev_get_user_data(indev);
    data->point = pointer->point;
    data->state = pointer->state;
}

bool game_ui_decor_edit(decor_edit_t operation, int value, int x, int y, int *count)
{
    if (s_ui_req != GAME_UI_NONE || (unsigned)operation > DECOR_EDIT_DONE ||
        x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX) {
        ESP_LOGE(TAG, "decor edit request rejected: busy or invalid input");
        return false;
    }
    s_decor_edit.operation = operation;
    s_decor_edit.value = value;
    s_decor_edit.x = x;
    s_decor_edit.y = y;
    bool ok = game_ui_call(GAME_UI_DECOR_EDIT);
    if (ok && count) *count = s_decor_edit.count;
    return ok;
}

static void test_pointer_send(lv_indev_t *indev, test_pointer_t *pointer,
                              int x, int y, lv_indev_state_t state)
{
    pointer->point = (lv_point_t){x, y};
    pointer->state = state;
    lv_indev_read(indev);
}

static bool test_furniture_pointer(void)
{
    game_state_t *original = heap_caps_malloc(sizeof g, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!original) { ESP_LOGE(TAG, "pointer test backup allocation failed"); return false; }
    *original = g;
    decor_item_t *original_extra = decor_detach_extra();
    s_testing_placement = true;
    ui_menu_close();
    ui_hud_set_chrome(false);
    decor_clear();
    /* The decor gate refuses a move while live prey is in the tank, so the
     * pointer test must start from an empty tank or it tests the gate instead
     * of the drag. */
    prey_clear();
    g.decor_owned = 1u;
    int slot = decor_place(0, 110, 150);
    g.gift = (gift_t){0};
    terrarium_refresh_decor();
    lv_indev_t *indev = lv_indev_create();
    if (!indev) {
        ESP_LOGE(TAG, "placement test: pointer allocation failed");
        g = *original;
        heap_caps_free(original);
        decor_restore_extra(original_extra);
        s_testing_placement = false;
        return false;
    }
    test_pointer_t pointer = {0};
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, test_pointer_read);
    lv_indev_set_user_data(indev, &pointer);
    lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
    lv_indev_set_long_press_time(indev, 0);
    lv_obj_update_layout(ui.screen);
    test_pointer_send(indev, &pointer, 110, 150, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 110, 150, LV_INDEV_STATE_PRESSED);
    bool held = !ui_menu_visible() && g_decor_mode && g_decor_move_slot == slot;
    test_pointer_send(indev, &pointer, 110, 150, LV_INDEV_STATE_RELEASED);
    s_decor_item_slot = slot;
    decor_begin_move(slot);
    test_pointer_send(indev, &pointer, 110, 150, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 330, 350, LV_INDEV_STATE_PRESSED);
    bool staged = g_decor_mode && s_place_x == 330 && s_place_y == 350 &&
                  g.decor[slot].x == 110 && g.decor[slot].y == 150;
    test_pointer_send(indev, &pointer, 330, 350, LV_INDEV_STATE_RELEASED);
    staged = staged && g_decor_mode && g.decor[slot].x == 110;
    test_pointer_send(indev, &pointer, 372, 436, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 372, 436, LV_INDEV_STATE_RELEASED);
    bool moved = slot >= 0 && g.decor[slot].x == 330 && g.decor[slot].y == 350 &&
                 !g_decor_mode && g.decor_owned == 1;
    g.gift.kind = 0; g.gift.variant = 2; g.gift.used = 1; g.gift.x = 200; g.gift.y = 200;
    terrarium_refresh_gift();
    uint32_t silk = g.silk, owned = g.gifts_owned;
    on_gift_item_pick(0);
    test_pointer_send(indev, &pointer, 200, 200, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 350, 180, LV_INDEV_STATE_PRESSED);
    staged = staged && g.gift_mode && s_place_x == 350 && s_place_y == 180 &&
             g.gift.x == 200 && g.gift.y == 200;
    test_pointer_send(indev, &pointer, 350, 180, LV_INDEV_STATE_RELEASED);
    staged = staged && g.gift_mode && g.gift.x == 200;
    /* The gift has no path control any more: the handler is a no-op, so the
     * staged path cannot leave Walk Over and the commit cannot store anything
     * else. */
    game_on_placement_blocking(NULL);
    staged = staged && s_place_blocking == DECOR_WALK_OVER;
    test_pointer_send(indev, &pointer, 372, 436, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 372, 436, LV_INDEV_STATE_RELEASED);
    bool gift_moved = !g.gift_mode && g.gift.x == 350 && g.gift.y == 180 &&
                      g.gift.variant == 2 && g.silk == silk && g.gifts_owned == owned &&
                      g.gift_path == DECOR_WALK_OVER;
    on_gift_item_pick(0);
    test_pointer_send(indev, &pointer, 350, 180, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 300, 240, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 300, 240, LV_INDEV_STATE_RELEASED);
    test_pointer_send(indev, &pointer, 108, 436, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 108, 436, LV_INDEV_STATE_RELEASED);
    bool cancelled = !g.gift_mode && g.gift.x == 350 && g.gift.y == 180 &&
                     g.gift.variant == 2 && g.silk == silk;
    decor_begin_move(slot);
    placement_position(-100, 1000);
    cancelled = cancelled && g.decor[slot].x == 330 &&
                s_place_x == g_play.left + decor_def(0)->radius &&
                s_place_y == g_play.bottom - decor_def(0)->radius;
    game_on_placement_cancel(NULL);
    cancelled = cancelled && g.decor[slot].x == 330 && g.decor[slot].y == 350;
    int count = decor_placed_count();
    decor_begin_placement(0);
    placement_position(180, 260);
    bool created = decor_placed_count() == count;
    game_on_placement_done(NULL);
    created = created && decor_placed_count() == count + 1;
    g.gifts_owned |= 1u;
    game_gift_chosen(0);
    placement_position(260, 260);
    created = created && g.gift.x == 350;
    game_on_placement_done(NULL);
    created = created && g.gift.x == 260 && g.gift.y == 260 && g.silk == silk;
    decor_begin_move(slot);
    uint8_t blocking = g.decor[slot].blocking;
    test_pointer_send(indev, &pointer, 124, 388, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 124, 388, LV_INDEV_STATE_RELEASED);
    bool actions = game_placement_blocking() != blocking && g.decor[slot].blocking == blocking;
    game_on_placement_cancel(NULL);
    actions = actions && g.decor[slot].blocking == blocking;
    decor_begin_move(slot);
    game_on_placement_blocking(NULL);
    game_on_placement_done(NULL);
    actions = actions && g.decor[slot].blocking != blocking;
    decor_begin_move(slot);
    test_pointer_send(indev, &pointer, 240, 436, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 240, 436, LV_INDEV_STATE_RELEASED);
    actions = actions && !g_decor_mode && !decor_at(slot) && !ui_menu_visible() && decor_owned(0);
    on_gift_item_pick(0);
    test_pointer_send(indev, &pointer, 240, 436, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 240, 436, LV_INDEV_STATE_RELEASED);
    actions = actions && !g.gift_mode && !g.gift.used && !ui_menu_visible() && g.silk == silk;
    decor_clear();
    int first = decor_place(0, 120, 150);
    decor_place(0, 220, 150);
    int last = decor_place(0, 320, 150);
    terrarium_refresh_decor();
    decor_begin_move(last);
    /* The layer arrows moved into the compact strip: Back is now at (240,340). */
    test_pointer_send(indev, &pointer, 240, 388, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 240, 388, LV_INDEV_STATE_RELEASED);
    bool layers = s_place_layer == 1 && decor_at(last)->x == 320 &&
        terrarium_preview_test(DECOR_WALK_OVER, 1);
    game_on_placement_cancel(NULL);
    layers = layers && decor_at(last)->x == 320 && decor_at(first)->x == 120;
    decor_begin_move(last);
    test_pointer_send(indev, &pointer, 240, 388, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 240, 388, LV_INDEV_STATE_RELEASED);
    game_on_placement_done(NULL);
    layers = layers && decor_at(1)->x == 320 && decor_at(2)->x == 220 && terrarium_decor_test();
    decor_begin_move(first);
    /* A walk-behind item only covers the spider with its top half, so the
     * preview is in the canopy layer only while the spider is above the item's
     * centre line. Put the spider above it to exercise that branch. */
    g.spider_y = 100;
    placement_position(120, 150);
    /* One cycling button replaces the three path buttons, so Walk Over ->
     * Walk Behind is two taps. */
    test_pointer_send(indev, &pointer, 124, 388, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 124, 388, LV_INDEV_STATE_RELEASED);
    test_pointer_send(indev, &pointer, 124, 388, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 124, 388, LV_INDEV_STATE_RELEASED);
    bool paths = s_place_blocking == DECOR_WALK_BEHIND && decor_at(first)->blocking == 0 &&
        terrarium_preview_test(DECOR_WALK_BEHIND, 0);
    game_on_placement_done(NULL);
    /* Walk Behind blocks, so the committed item must now block its own centre.
     * It used to be non-blocking, which let the spider walk over the bottom of
     * the sprite and then pop behind the top. */
    paths = paths && decor_at(first)->blocking == DECOR_WALK_BEHIND &&
        decor_blocks_point(120, 150) && decor_hit_test(120, 150) == first &&
        terrarium_decor_test();
    decor_begin_placement(0);
    game_on_placement_blocking(NULL);
    game_on_placement_blocking(NULL);
    paths = paths && s_place_blocking == DECOR_WALK_BEHIND &&
        terrarium_preview_test(DECOR_WALK_BEHIND, 1);
    game_on_placement_cancel(NULL);
    lv_indev_set_long_press_time(indev, 550);
    cancel_enclosure_tap();
    g.life.light_gesture_disabled = 0;
    uint8_t before_lights = g.lights_dimmed;
    bool lights = true;
    for (int i = 0; i < 6; i++) {
        int x = i % 2 ? 435 : 400;
        test_pointer_send(indev, &pointer, x, 400, LV_INDEV_STATE_PRESSED);
        test_pointer_send(indev, &pointer, x + 8, 407, LV_INDEV_STATE_PRESSED);
        if (i % 2) s_last_tap_at = game_now() - 350;
        test_pointer_send(indev, &pointer, x + 8, 407, LV_INDEV_STATE_RELEASED);
        lights = lights && g.lights_dimmed == (before_lights ^ (((i + 1) / 2) % 2));
    }
    cancel_enclosure_tap();
    before_lights = g.lights_dimmed;
    test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_RELEASED);
    s_last_tap_at = game_now() - 451;
    test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_RELEASED);
    lights = lights && g.lights_dimmed == before_lights;
    cancel_enclosure_tap();
    g.life.light_gesture_disabled = 1;
    for (int i = 0; i < 2; i++) {
        test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_PRESSED);
        test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_RELEASED);
    }
    lights = lights && g.lights_dimmed == before_lights;
    cancel_enclosure_tap();
    for (int i = 0; i < 2; i++) {
        test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_PRESSED);
        test_pointer_send(indev, &pointer, 450, 400, LV_INDEV_STATE_PRESSED);
        test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_PRESSED);
        test_pointer_send(indev, &pointer, 400, 400, LV_INDEV_STATE_RELEASED);
    }
    lights = lights && g.lights_dimmed == before_lights;
    cancel_enclosure_tap();
    lv_indev_delete(indev);
    g = *original;
    heap_caps_free(original);
    decor_restore_extra(original_extra);
    apply_enclosure_lights();
    game_on_placement_cancel(NULL);
    s_testing_placement = false;
    ui_hud_set_place_mode(false, NULL);
    terrarium_refresh_gift();
    terrarium_refresh_decor();
    ESP_LOGI(TAG, "furniture pointer: hold=%d staged=%d decor_move=%d gift_move=%d cancel=%d new=%d actions=%d lights=%d paths=%d layers=%d",
             held, staged, moved, gift_moved, cancelled, created, actions, lights, paths, layers);
    return held && staged && moved && gift_moved && cancelled && created && actions && lights && paths && layers;
}

/* The top placement arrows select the ITEM, not the layer.
 *
 * They used to be a second copy of the layer control, which was redundant and
 * left no way to switch to a different placed item without cancelling out to the
 * menu and starting again. This is a separate test from the pointer test so a
 * failure here is not masked by the pre-existing pointer baseline. */
static bool test_placement_item_selector(void)
{
    game_state_t *original = heap_caps_malloc(sizeof g, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!original) { ESP_LOGE(TAG, "item selector test backup allocation failed"); return false; }
    *original = g;
    decor_item_t *original_extra = decor_detach_extra();
    uint32_t original_owned = g.decor_owned;
    s_testing_placement = true;
    decor_clear();
    /* decor_place() refuses an unowned kind, and the surrounding uitest may have
     * reset ownership, so grant the cheapest kind explicitly. */
    g.decor_owned |= 1u << DECOR_ROCK_SMALL;

    bool ok = true;

    /* A SINGLE placed item must still show the selector, with its arrows
     * disabled.
     *
     * The label used to return false when `total < 2`, which hid the arrows, the
     * caption and all -- so a tank holding one prop looked like the control had
     * been removed. The row is now always present while moving an existing item
     * and the arrows carry the "nothing to step to" state instead.
     *
     * The gift is cleared for this block: it counts as a placed object now, so
     * leaving the board's gift in place would make the total 2 and the arrows
     * legitimately live. */
    {
        gift_t saved_gift = g.gift;
        uint8_t saved_layer = g.gift_layer;
        g.gift.used = 0;
        terrarium_refresh_gift();

        int only = decor_place(DECOR_ROCK_SMALL, 200, 150);
        terrarium_refresh_decor();
        decor_begin_move(only);
        char one[48];
        bool shown = game_placement_item_label(one, sizeof one);
        if (!shown || !strstr(one, "1 / 1")) {
            ESP_LOGE(TAG, "selector: single item did not show \"1 / 1\" (shown=%d label=\"%s\")",
                     shown, one);
            ok = false;
        }
        if (game_placement_item_total() != 1) {
            ESP_LOGE(TAG, "selector: single item total=%d", game_placement_item_total());
            ok = false;
        }
        bool sel_visible = false, sel_enabled = true;
        ui_hud_placement_selector_state(&sel_visible, &sel_enabled);
        if (!sel_visible) {
            ESP_LOGE(TAG, "selector: arrows hidden with a single item placed");
            ok = false;
        }
        if (sel_enabled) {
            ESP_LOGE(TAG, "selector: arrows enabled with nothing to step to");
            ok = false;
        }
        game_on_placement_cancel(NULL);
        decor_clear();
        g.gift = saved_gift;
        g.gift_layer = saved_layer;
        terrarium_refresh_gift();
    }

    int first = decor_place(DECOR_ROCK_SMALL, 120, 150);
    decor_place(DECOR_ROCK_SMALL, 220, 150);
    decor_place(DECOR_ROCK_SMALL, 320, 150);
    terrarium_refresh_decor();

    char label[48];
    if (first < 0 || decor_placed_count() != 3) {
        ESP_LOGE(TAG, "selector: setup failed first=%d placed=%d", first, decor_placed_count());
        ok = false;
    }
    /* The gift is cleared for the decor-only blocks below, so "1 / 3" is the
     * whole ring. The gift's own behaviour is tested separately. */
    gift_t saved_gift = g.gift;
    uint8_t saved_gift_layer = g.gift_layer;
    g.gift.used = 0;
    terrarium_refresh_gift();
    decor_begin_move(first);
    if (!game_placement_item_label(label, sizeof label) || !strstr(label, "1 / 3")) {
        ESP_LOGE(TAG, "selector: initial label = \"%s\", expected \"1 / 3\"", label);
        ok = false;
    }
    /* With three items placed the selector arrows must be live. */
    {
        bool sel_visible = false, sel_enabled = false;
        ui_hud_placement_selector_state(&sel_visible, &sel_enabled);
        if (!sel_visible || !sel_enabled) {
            ESP_LOGE(TAG, "selector: arrows not live with 3 items (visible=%d enabled=%d)",
                     sel_visible, sel_enabled);
            ok = false;
        }
    }
    /* The layer row must be HIDDEN here: the three rocks are 100 px apart and a
     * small rock's artwork half-extent is 2*9*115/100 = 20.7 px, so their reach
     * is 41 px and they do not touch. Offering the layer control against an item
     * at the far end of the tank was the "layer arrows are usable even though
     * there is no overlapping item" report. */
    {
        bool layer_visible = true;
        ui_hud_placement_layer_state(&layer_visible, NULL);
        if (layer_visible) {
            ESP_LOGE(TAG, "selector: layer row shown with no overlapping item");
            ok = false;
        }
    }
    /* Drag the first rock onto the second: now they overlap and the row must
     * appear. This is the other half of the rule -- the control has to be there
     * the moment there IS something to order against. */
    {
        int16_t second_x = decor_at(1)->x, second_y = decor_at(1)->y;
        placement_position(second_x + 4, second_y);
        bool layer_visible = false;
        ui_hud_placement_layer_state(&layer_visible, NULL);
        if (!layer_visible) {
            ESP_LOGE(TAG, "selector: layer row hidden while overlapping another item");
            ok = false;
        }
        /* Put it back so the rest of the test sees the original layout. */
        placement_position(decor_at(first)->x, decor_at(first)->y);
    }
    /* A DECOR item dropped on the GIFT must get the layer row.
     *
     * decor_layer_count_at() used to ignore the gift entirely while
     * decor_gift_layer_count() counted decor, so the relationship was one-way: a
     * rock sitting exactly on the gift reported overlapping=0, the row never
     * appeared, and the two could not be ordered -- the "layer selection arrows
     * are completely missing" report. Both directions must now see each other. */
    {
        g.gift.used = 1;
        g.gift.kind = 0;
        g.gift.x = decor_at(first)->x;
        g.gift.y = decor_at(first)->y;
        g.gift_path = DECOR_WALK_OVER;
        g.gift_layer = 0;
        placement_position(decor_at(first)->x, decor_at(first)->y);
        bool layer_visible = false;
        ui_hud_placement_layer_state(&layer_visible, NULL);
        if (!layer_visible) {
            ESP_LOGE(TAG, "selector: layer row hidden for decor overlapping the gift");
            ok = false;
        }
        /* The count must include the gift, so the range is 2 not 1. */
        if (game_placement_layers() < 2) {
            ESP_LOGE(TAG, "selector: gift not counted in the decor layer range (%d)",
                     game_placement_layers());
            ok = false;
        }
        /* And the arrows must actually DO something. The decor's slot order
         * cannot move the gift, so reaching the far end has to move the GIFT's
         * own index -- otherwise the row would appear but the arrows would be
         * dead whenever the gift was the only thing in contact. */
        int layers = game_placement_layers();
        /* Target the end the item is NOT already at, or the reorder is a no-op
         * and the assertion below would pass without proving anything. */
        int target = decor_layer_index_with_gift(g_decor_move_slot) == 0 ? layers - 1 : 0;
        int gift_before = g.gift_layer;
        decor_set_layer(g_decor_move_slot, target);
        int reached = decor_layer_index_with_gift(g_decor_move_slot);
        if (reached != target) {
            ESP_LOGE(TAG, "selector: layer arrow did not reach %d (at %d)",
                     target, reached);
            ok = false;
        }
        if (g.gift_layer == gift_before) {
            ESP_LOGE(TAG, "selector: ordering decor against the gift did not move the gift "
                     "(layer still %d)", g.gift_layer);
            ok = false;
        }
        /* Clear the gift again: the decor-only stepping below expects a
         * three-item ring, and the board's real gift is restored at the end. */
        g.gift.used = 0;
        g.gift_layer = 0;
        terrarium_refresh_gift();
        placement_position(decor_at(first)->x, decor_at(first)->y);
    }
    /* The selector must CYCLE THROUGH THE GIFT.
     *
     * The gift is a placed object the player can move, and reaching it used to
     * mean cancelling out to the menu. The ring is the placed decor in slot
     * order followed by the gift, so stepping forward from the last decor item
     * lands on the gift and stepping again wraps to the first decor item. */
    {
        g.gift.used = 1;
        g.gift.kind = 0;
        g.gift.x = 240;
        g.gift.y = 300;
        g.gift_path = DECOR_WALK_OVER;
        g.gift_layer = 0;
        terrarium_refresh_gift();

        /* Three decor + one gift = four things to step through. */
        if (game_placement_item_total() != 4) {
            ESP_LOGE(TAG, "selector: total with gift = %d, expected 4",
                     game_placement_item_total());
            ok = false;
        }
        /* From the last decor item, forward lands on the gift. */
        g_decor_move_slot = 2;
        g_decor_mode = 1;
        g.gift_mode = 0;
        placement_step_item(1);
        if (!g.gift_mode || g_decor_mode) {
            ESP_LOGE(TAG, "selector: stepping past the last decor did not select the gift "
                     "(gift_mode=%d decor_mode=%d)", g.gift_mode, g_decor_mode);
            ok = false;
        }
        if (!game_placement_item_label(label, sizeof label) || !strstr(label, "4 / 4")) {
            ESP_LOGE(TAG, "selector: gift label = \"%s\", expected \"4 / 4\"", label);
            ok = false;
        }
        /* And forward again wraps back to the first decor item. */
        placement_step_item(1);
        if (g.gift_mode || !g_decor_mode || g_decor_move_slot != first) {
            ESP_LOGE(TAG, "selector: stepping past the gift did not wrap to the first decor "
                     "(gift_mode=%d decor_mode=%d slot=%d)",
                     g.gift_mode, g_decor_mode, g_decor_move_slot);
            ok = false;
        }
        /* Backwards from the first decor item lands on the gift. */
        placement_step_item(-1);
        if (!g.gift_mode) {
            ESP_LOGE(TAG, "selector: stepping back from the first decor did not select the gift");
            ok = false;
        }
        g.gift.used = 0;
        g.gift_layer = 0;
        terrarium_refresh_gift();
        g.gift_mode = 0;
        g_decor_mode = 1;
        g_decor_move_slot = first;
        placement_position(decor_at(first)->x, decor_at(first)->y);
    }
    /* Step forward twice: first -> second -> third. */
    placement_step_item(1);
    if (g_decor_move_slot != 1 || !game_placement_item_label(label, sizeof label) ||
        !strstr(label, "2 / 3")) {
        ESP_LOGE(TAG, "selector: after step 1 slot=%d label=\"%s\", expected slot 1 \"2 / 3\"",
                 g_decor_move_slot, label);
        ok = false;
    }
    placement_step_item(1);
    if (g_decor_move_slot != 2 || !game_placement_item_label(label, sizeof label) ||
        !strstr(label, "3 / 3")) {
        ESP_LOGE(TAG, "selector: after step 2 slot=%d label=\"%s\", expected slot 2 \"3 / 3\"",
                 g_decor_move_slot, label);
        ok = false;
    }
    /* And wrap back to the first. */
    placement_step_item(1);
    if (g_decor_move_slot != first) {
        ESP_LOGE(TAG, "selector: wrap forward landed on %d, expected %d",
                 g_decor_move_slot, first);
        ok = false;
    }
    /* The staged position follows the selection, so the preview is over the
     * newly selected item rather than the old one. */
    if (s_place_x != decor_at(first)->x || s_place_y != decor_at(first)->y) {
        ESP_LOGE(TAG, "selector: staged pos (%d,%d) != item pos (%d,%d)",
                 s_place_x, s_place_y, decor_at(first)->x, decor_at(first)->y);
        ok = false;
    }
    /* Stepping backwards from the first wraps to the last. */
    placement_step_item(-1);
    if (g_decor_move_slot != 2) {
        ESP_LOGE(TAG, "selector: wrap backward landed on %d, expected 2", g_decor_move_slot);
        ok = false;
    }
    /* Every item except the staged one must still be VISIBLE.
     *
     * The preview hides the real object so only the draggable copy shows. It
     * used to hide the new slot without unhiding the old one, so each item the
     * player stepped away from stayed invisible for the rest of the edit -- the
     * "other non active decor items go invisible" report. This walks the whole
     * ring and asserts the invariant after every step. */
    for (int step = 0; step < 4 && ok; step++) {
        placement_step_item(1);
        for (int i = 0; i < decor_placed_count(); i++) {
            bool hidden = terrarium_decor_hidden(i);
            if (i == g_decor_move_slot) {
                if (!hidden) {
                    ESP_LOGE(TAG, "selector: staged slot %d is still visible", i);
                    ok = false;
                }
            } else if (hidden) {
                ESP_LOGE(TAG, "selector: non-staged slot %d went invisible", i);
                ok = false;
            }
        }
    }
    /* A new item has no slot to cycle, so the row is hidden. */
    game_on_placement_cancel(NULL);
    decor_begin_placement(0);
    if (game_placement_item_label(label, sizeof label)) {
        ESP_LOGE(TAG, "selector: a new placement still showed a label \"%s\"", label);
        ok = false;
    }
    game_on_placement_cancel(NULL);

    /* A TAP must not move the item; only a drag may.
     *
     * The press handler used to call placement_position() immediately, so a tap
     * anywhere on the tank snapped the item to the finger. Missing a button by a
     * few pixels therefore threw the item across the enclosure. This drives the
     * real LVGL pointer path and asserts the staged position is unchanged after
     * a tap, then moves after a drag past the slop. */
    {
        int slot = decor_place(DECOR_ROCK_SMALL, 200, 250);
        terrarium_refresh_decor();
        decor_begin_move(slot);
        int16_t before_x = s_place_x, before_y = s_place_y;

        test_pointer_t pointer = {0};
        lv_indev_t *indev = lv_indev_create();
        if (!indev) {
            ESP_LOGE(TAG, "placement tap test: pointer allocation failed");
            ok = false;
        } else {
            lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(indev, test_pointer_read);
            lv_indev_set_user_data(indev, &pointer);
            lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
            lv_indev_set_long_press_time(indev, 0);
            lv_obj_update_layout(ui.screen);

            /* Tap well away from the item AND clear of every control row: the
             * top row occupies y=16..72, the middle y=328..384 and the bottom
             * y=400..456, so y=150 is bare tank. */
            test_pointer_send(indev, &pointer, 400, 150, LV_INDEV_STATE_PRESSED);
            test_pointer_send(indev, &pointer, 400, 150, LV_INDEV_STATE_RELEASED);
            if (s_place_x != before_x || s_place_y != before_y) {
                ESP_LOGE(TAG, "placement tap: item moved on a tap (%d,%d -> %d,%d)",
                         before_x, before_y, s_place_x, s_place_y);
                ok = false;
            }

            /* Tap ON the item: still nothing may move. Re-read the staged
             * position first, so a failure above cannot cascade. */
            before_x = s_place_x;
            before_y = s_place_y;
            test_pointer_send(indev, &pointer, before_x, before_y, LV_INDEV_STATE_PRESSED);
            test_pointer_send(indev, &pointer, before_x, before_y, LV_INDEV_STATE_RELEASED);
            if (s_place_x != before_x || s_place_y != before_y) {
                ESP_LOGE(TAG, "placement tap: item moved on a tap on itself");
                ok = false;
            }

            /* A real drag past the slop must move it. */
            before_x = s_place_x;
            before_y = s_place_y;
            test_pointer_send(indev, &pointer, before_x, before_y, LV_INDEV_STATE_PRESSED);
            test_pointer_send(indev, &pointer, before_x + 60, before_y + 40,
                              LV_INDEV_STATE_PRESSED);
            test_pointer_send(indev, &pointer, before_x + 60, before_y + 40,
                              LV_INDEV_STATE_RELEASED);
            if (s_place_x == before_x && s_place_y == before_y) {
                ESP_LOGE(TAG, "placement tap: a drag did not move the item");
                ok = false;
            }
            lv_indev_delete(indev);
        }
        game_on_placement_cancel(NULL);
    }

    decor_clear();
    g = *original;
    g.decor_owned = original_owned;
    decor_restore_extra(original_extra);
    terrarium_refresh_decor();
    terrarium_refresh_gift();
    s_testing_placement = false;
    heap_caps_free(original);
    ESP_LOGI(TAG, "placement item selector: %s", ok ? "PASS" : "FAIL");
    return ok;
}

static bool test_hunt_pointer(int32_t target_x, int32_t target_y)
{
    test_pointer_t pointer = {0};
    lv_indev_t *indev = lv_indev_create();
    if (!indev) { ESP_LOGE(TAG, "hunt test: pointer allocation failed"); return false; }
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, test_pointer_read);
    lv_indev_set_user_data(indev, &pointer);
    lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
    lv_obj_update_layout(ui.screen);
    test_pointer_send(indev, &pointer, target_x, target_y, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, target_x, target_y, LV_INDEV_STATE_RELEASED);
    bool tap = !behaviour_pouncing() && !behaviour_aim_dragging();

    /* Hit the real Care button, then tap the tank to dismiss without shooting. */
    test_pointer_send(indev, &pointer, 29, 29, LV_INDEV_STATE_PRESSED);
    test_pointer_send(indev, &pointer, 29, 29, LV_INDEV_STATE_RELEASED);
    bool overlay = ui_hud_chrome_visible();
    test_pointer_send(indev, &pointer, target_x, target_y, LV_INDEV_STATE_PRESSED);
    overlay = overlay && !behaviour_aim_dragging();
    test_pointer_send(indev, &pointer, target_x, target_y, LV_INDEV_STATE_RELEASED);
    overlay = overlay && !ui_hud_chrome_visible() && !behaviour_pouncing();

    int start_x = g.spider_x, start_y = g.spider_y;
    test_pointer_send(indev, &pointer, start_x, start_y, LV_INDEV_STATE_PRESSED);
    bool press = behaviour_aim_dragging();
    test_pointer_send(indev, &pointer, target_x, target_y, LV_INDEV_STATE_PRESSED);
    behaviour_tick(game_now() + 5000, 0.25f);
    prey_tick(game_now() + 5000, 0.25f);
    int32_t px = 0, py = 0;
    bool held = behaviour_aim_dragging() && g.spider_x == start_x && g.spider_y == start_y &&
        prey_nearest(&px, &py) && px == target_x && py == target_y;
    test_pointer_send(indev, &pointer, target_x, target_y, LV_INDEV_STATE_RELEASED);
    bool released = behaviour_pouncing() && !behaviour_aim_dragging() &&
        behaviour_pounce_by_player() && g.energy < 80;
    uint32_t now = game_now();
    for (int i = 1; i <= 8; i++) behaviour_tick(now + i * 50, 0.05f);
    bool landed = !behaviour_pouncing() && g.state == SPIDER_EAT &&
        abs(g.spider_x - target_x) <= 1 && abs(g.spider_y - target_y) <= 1;
    float x, y;
    behaviour_pos(&x, &y);
    bool subpixel = fabsf(x - g.spider_x) < 1 && fabsf(y - g.spider_y) < 1;
    behaviour_set_state(SPIDER_WANDER);
    behaviour_seek_to(target_x + 40, target_y);
    behaviour_tick(now + 450, 0.05f);
    subpixel = subpixel && abs(g.spider_x - target_x) < 8 && abs(g.spider_y - target_y) < 8;
    lv_indev_delete(indev);
    bool ok = press && held && tap && overlay && released && landed && subpixel;
    printf("HUNT press=%d held=%d tap=%d overlay=%d released=%d landed=%d subpixel=%d\n",
           press, held, tap, overlay, released, landed, subpixel);
    return ok;
}

static bool test_hunt(void)
{
    if (behaviour_pouncing() || terrarium_poop_count() == MAX_POOP) {
        ESP_LOGW(TAG, "hunt test requires no active jump and a free poop slot");
        return false;
    }
    game_state_t *original = heap_caps_malloc(sizeof g, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!original) { ESP_LOGE(TAG, "hunt test backup allocation failed"); return false; }
    *original = g;
    lv_obj_t *poop[MAX_POOP];
    memcpy(poop, ui.poop, sizeof poop);
    int original_prey = prey_count();
    ui_menu_close();
    ui_hud_set_chrome(false);
    behaviour_set_state(SPIDER_IDLE);
    g.spider_x = 176;
    g.spider_y = 240;
    g.spider_heading = 0;
    g.hunger = 50;
    g.energy = 80;
    g.gift_mode = 0;
    behaviour_sync_position();
    bool ok = prey_hunt_test(test_hunt_pointer);
    behaviour_set_state(SPIDER_IDLE);
    for (int i = 0; i < MAX_POOP; i++)
        if (ui.poop[i] && ui.poop[i] != poop[i]) {
            lv_obj_delete(ui.poop[i]);
            ui.poop[i] = poop[i];
        }
    g = *original;
    behaviour_sync_position();
    spider_launch_pounce(0, 0);
    ok = ok && prey_count() == original_prey && !memcmp(&g, original, sizeof g);
    printf("%s hunttest restored=%d\n", ok ? "OK" : "ERR",
           !memcmp(&g, original, sizeof g));
    heap_caps_free(original);
    return ok;
}

/* Called from game_tick_render(), on the LVGL task. */
static void ui_service_request(void)
{
    static bool scanloop_paused, scanloop_chrome;
    int req = s_ui_req;
    if (req <= GAME_UI_NONE) return;

    switch ((game_ui_action_t)req) {
    case GAME_UI_SCREEN_CAPTURE:
        if (s_capture) lv_draw_buf_destroy(s_capture);
        s_capture = capture_target(s_ui_value);
        atomic_store(&s_ui_ok, s_capture != NULL);
        break;
    case GAME_UI_SCREEN_SD:
        atomic_store(&s_ui_ok, game_save_screenshot());
        break;
    case GAME_UI_SCREEN_TEST:
        atomic_store(&s_ui_ok, capture_test());
        break;
    case GAME_UI_SCREEN_RELEASE:
        if (s_capture) lv_draw_buf_destroy(s_capture);
        s_capture = NULL;
        break;
    case GAME_UI_FEED:
        atomic_store(&s_ui_ok, prey_count() < MAX_PREY);
        game_on_feed(NULL);
        break;
    case GAME_UI_CLEAN: {
        /* Success means the tank actually changed, not that XP was paid: a tank
         * holding only bugs is cleaned for 0 XP, and reporting that as a
         * failure would contradict what the player just saw. */
        bool needed = needs_clean_needed();
        needs_clean();
        atomic_store(&s_ui_ok, needed);
        break;
    }
    case GAME_UI_PLAY:  game_on_play(NULL);  break;
    case GAME_UI_WAKE: power_wake(); break;
    case GAME_UI_SLEEP: game_on_sleep(NULL); break;
    case GAME_UI_DEEP_SLEEP:
        atomic_store(&s_ui_ok, power_request_deep_sleep((uint32_t)s_ui_value));
        break;
    case GAME_UI_MENU:  game_on_menu(NULL);  break;
    case GAME_UI_SAVE:  atomic_store(&s_ui_ok, game_save_now()); break;
    case GAME_UI_TEST_GIFTS: s_ui_ok = test_gifts(); break;
    case GAME_UI_REFRESH:
        spider_refresh_colours();
        terrarium_rebuild();
        break;
    case GAME_UI_RENDER: cspider_render(game_now()); break;
    case GAME_UI_SD_BACKUP:
        atomic_store(&s_ui_ok, sd_request(SD_JOB_BACKUP));
        break;
    case GAME_UI_SD_RESTORE:
        atomic_store(&s_ui_ok, sd_request(SD_JOB_RESTORE));
        break;
    case GAME_UI_SPAWN:
        atomic_store(&s_ui_ok, s_ui_value < 0 ? prey_spawn() :
                     prey_spawn_species((prey_species_t)s_ui_value));
        break;
    case GAME_UI_SPECKLE:
        /* Creates LVGL objects, so it must run here on the LVGL task rather
         * than on the console task. */
        for (int i = 0; i < s_ui_value; i++) {
            terrarium_add_bug_poop(g_play.left + rand() % (g_play.right - g_play.left),
                                   g_play.top + rand() % (g_play.bottom - g_play.top));
        }
        ESP_LOGI(TAG, "speckle: added %d, poop=%d dirt=%d", s_ui_value,
                 terrarium_poop_count(), terrarium_dirt());
        atomic_store(&s_ui_ok, true);
        break;
    case GAME_UI_STATE: behaviour_set_state((spider_state_t)s_ui_value); break;
    case GAME_UI_SET_HUNGER:  g.hunger = (uint8_t)s_ui_value; break;
    case GAME_UI_SET_MOOD:    g.mood = (uint8_t)s_ui_value; break;
    case GAME_UI_SET_ENERGY:  g.energy = (uint8_t)s_ui_value; break;
    case GAME_UI_SET_HYGIENE: g.hygiene = (uint8_t)s_ui_value; break;
    case GAME_UI_SET_BOND:    g.bond = (uint8_t)s_ui_value; break;
    case GAME_UI_SET_SICK:    g.sick = s_ui_value != 0; break;
    case GAME_UI_SET_AUTO_FEED:
        if (g.auto_feed != s_ui_value) on_settings_pick(6);
        break;
    case GAME_UI_SET_AUTO_HUNT:
        if (g.auto_hunt != s_ui_value) on_settings_pick(6);
        break;
    case GAME_UI_SET_VOLUME:
        g.volume = (uint8_t)s_ui_value;
        audio_set_volume(g.volume);
        game_volume_changed();
        break;
    case GAME_UI_SET_MUSIC:
        g.music_enabled = s_ui_value != 0;
        audio_set_music(g.music_enabled);
        game_save_now();
        break;
    case GAME_UI_SET_MUSIC_VOLUME:
        g.music_volume = (uint8_t)s_ui_value;
        audio_set_music_volume(g.music_volume);
        game_save_now();
        break;
    case GAME_UI_SET_MUSIC_TRACK:
        g.music_track = (uint8_t)s_ui_value;
        audio_set_music_track(g.music_track);
        game_save_now();
        break;
    case GAME_UI_SET_MUTED:
        audio_set_muted(s_ui_value != 0);
        game_save_now();
        break;
    case GAME_UI_SET_USB_SOUNDS:
        g.usb_sounds = s_ui_value != 0;
        game_save_now();
        if (s_open_menu == MENU_SOUND) {
            int page = ui_menu_page();
            menu_open(MENU_SOUND);
            ui_menu_set_page(page);
        }
        break;
    case GAME_UI_SET_DECOR_LOCKED:
        g.decor_locked = s_ui_value != 0;
        if (g.decor_locked && g_decor_mode) game_cancel_decor_placement();
        game_save_now();
        if (s_open_menu == MENU_SETTINGS) {
            int page = ui_menu_page();
            menu_open(MENU_SETTINGS);
            ui_menu_set_page(page);
        }
        break;
    case GAME_UI_SET_AUTO_CLEAN:
        g.auto_clean = s_ui_value != 0;
        game_save_now();
        if (s_open_menu == MENU_SETTINGS) {
            int page = ui_menu_page();
            menu_open(MENU_SETTINGS);
            ui_menu_set_page(page);
        }
        break;
    case GAME_UI_SET_LEVEL: {
        /* Test hook: jump straight to a level so every decor kind can be
         * inspected without grinding XP. Level is derived from XP
         * (xp / XP_PER_LEVEL + 1), so this sets XP to the floor of the
         * requested level rather than writing g.level directly -- otherwise
         * the next game_add_xp() would immediately recompute it back down. */
        if (s_ui_value < 1 || s_ui_value > 99) {
            ESP_LOGW(TAG, "level request rejected: %d", s_ui_value);
            atomic_store(&s_ui_ok, false);
            break;
        }
        uint32_t before_xp = g.xp, before_level = g.level;
        g.xp = (uint32_t)(s_ui_value - 1) * XP_PER_LEVEL;
        g.level = (uint32_t)s_ui_value;
        bool queued = game_save_now();
        if (!queued) { g.xp = before_xp; g.level = before_level; }
        ESP_LOGI(TAG, "level set to %d (xp=%lu)", s_ui_value,
                 (unsigned long)g.xp);
        atomic_store(&s_ui_ok, queued);
        break;
    }
    case GAME_UI_ADD_SILK: {
        uint32_t before = g.silk;
        if ((uint32_t)s_ui_value > UINT32_MAX - before) {
            ESP_LOGW(TAG, "Silk grant rejected: balance would overflow");
            atomic_store(&s_ui_ok, false);
            break;
        }
        game_add_silk((uint32_t)s_ui_value);
        bool queued = game_save_now();
        if (!queued) g.silk = before;
        atomic_store(&s_ui_ok, queued);
        break;
    }
    case GAME_UI_BORDER:
        atomic_store(&s_ui_ok, border_set(s_ui_value));
        terrarium_rebuild();
        break;
    case GAME_UI_BACKGROUND:
        atomic_store(&s_ui_ok, theme_set(s_ui_value));
        if (atomic_load(&s_ui_ok)) terrarium_rebuild();
        break;
    case GAME_UI_TEST_HUNT: atomic_store(&s_ui_ok, test_hunt()); break;
    case GAME_UI_TEST_GAIT: {
        bool ok = cspider_gait_test();
        printf("%s gaittest cadence=20/30/60 repeated=100 front_poses=10240\n",
               ok ? "OK" : "ERR");
        atomic_store(&s_ui_ok, ok);
        break;
    }
    case GAME_UI_TEST_PREY: {
        bool ok = prey_progression_test();
        printf("%s preytest levels=1,2,4,6,10,13,16,20 spawn_gates=%d\n",
               ok ? "OK" : "ERR", ok);
        atomic_store(&s_ui_ok, ok);
        break;
    }
    case GAME_UI_SETUP_DONE: {
        /* Test-only: finish the naming screen without tapping, so the tank can
         * be captured on a freshly erased save. */
        ESP_LOGI(TAG, "setupdone: naming and loading the tank");
        if (!g.name[0]) snprintf(g.name, sizeof g.name, "Test Spider");
        game_save_now();
        lv_screen_load(ui.screen);
        ESP_LOGI(TAG, "setupdone: tank loaded");
        atomic_store(&s_ui_ok, true);
        break;
    }
    case GAME_UI_DECOR_EDIT: {
        bool ok = true;
        int value = s_decor_edit.value;
        switch (s_decor_edit.operation) {
        case DECOR_EDIT_STATUS: break;
        case DECOR_EDIT_CLEAR: decor_clear(); break;
        case DECOR_EDIT_PLACE:
            if (value < 0 || value >= DECOR_COUNT) { ok = false; break; }
            if (!decor_owned(value)) {
                uint32_t silk = g.silk, level = g.level;
                g.silk = decor_def(value)->cost;
                g.level = decor_def(value)->unlock_level;
                ok = decor_buy(value);
                g.silk = silk; g.level = level;
            }
            if (ok) {
                s_decor_edit.count = decor_place(value, s_decor_edit.x, s_decor_edit.y);
                ok = s_decor_edit.count >= 0;
            }
            break;
        case DECOR_EDIT_MOVE: ok = decor_move(value, s_decor_edit.x, s_decor_edit.y); break;
        case DECOR_EDIT_BLOCK: ok = decor_toggle_blocking(value); break;
        case DECOR_EDIT_BEGIN:
            /* Open the interactive overlay on an existing slot. This is the same
             * path the decor menu takes, so the console exercises the real UI
             * rather than a parallel one. */
            ok = decor_at(value) != NULL;
            if (ok) decor_begin_move(value);
            break;
        case DECOR_EDIT_STEP:
            /* Drive the top item selector exactly as the arrows do, so the
             * console can screenshot the ring including the gift. */
            ok = (g_decor_mode || g.gift_mode) && (value == 1 || value == -1);
            if (ok) placement_step_item(value);
            break;
        case DECOR_EDIT_LAYER:
            /* Drive the bottom layer control exactly as its arrows do. */
            ok = (g_decor_mode || g.gift_mode) && (value == 1 || value == -1);
            if (ok) {
                int next = game_placement_layer() + value;
                ok = next >= 0 && next < game_placement_layers();
                if (ok) {
                    s_place_layer = next;
                    placement_position(s_place_x, s_place_y);
                    ui_hud_set_place_mode(true, NULL);
                }
            }
            break;
        case DECOR_EDIT_DONE:
            /* Commit through the real Done handler. */
            ok = g_decor_mode || g.gift_mode;
            if (ok) game_on_placement_done(NULL);
            break;
        }
        if (s_decor_edit.operation != DECOR_EDIT_PLACE) s_decor_edit.count = decor_placed_count();
        /* BEGIN opens the overlay and must not save or rebuild: the item is
         * staged, not committed, and a refresh would unhide it under the
         * preview. */
        if (ok && s_decor_edit.operation != DECOR_EDIT_STATUS &&
            s_decor_edit.operation != DECOR_EDIT_BEGIN &&
            s_decor_edit.operation != DECOR_EDIT_STEP &&
            s_decor_edit.operation != DECOR_EDIT_LAYER &&
            s_decor_edit.operation != DECOR_EDIT_DONE) {
            terrarium_refresh_decor();
            ok = game_save_now();
        }
        if (!ok) ESP_LOGE(TAG, "decor edit failed operation=%u value=%d", s_decor_edit.operation, value);
        atomic_store(&s_ui_ok, ok);
        break;
    }
    case GAME_UI_DECOR_TEST: {
        /* Exercise the real render path: place one of every kind, confirm the
         * layer matches, then restore the player's enclosure. */
        decor_item_t saved[DECOR_SLOTS];
        memcpy(saved, g.decor, sizeof saved);
        uint32_t extra_count = g.decor_extra_count, extra_owned = g.decor_owned_extra;
        uint8_t high_owned = g.catalogue_owned_high;
        uint8_t climb_owned = g.climb_owned;
        decor_item_t *extra = decor_detach_extra();
        uint32_t owned = g.decor_owned;
        uint32_t silk = g.silk;
        uint32_t level = g.level;
        decor_clear();
        g.decor_owned = 0xFFFFFFFFu;
        g.decor_owned_extra = 0xFFFFFFFFu;
        g.catalogue_owned_high |= 0xF0;
        /* The climb family has its own ownership bitmask, separate from the two
         * decor masks. Without this the last three kinds cannot be placed and
         * the test fails on them rather than on anything it is checking. */
        g.climb_owned = 0xFF;
        g.level = 20;
        g.silk = 100000;
        bool ok = true;
        for (int i = 0; i < DECOR_COUNT; i++) {
            int slot = decor_place((uint8_t)i, (int16_t)(60 + i * 30),
                                   (int16_t)(120 + (i % 3) * 90));
            if (slot < 0) {
                ESP_LOGE(TAG, "decortest: place kind %d failed", i);
                ok = false;
            }
        }
        terrarium_refresh_decor();
        if (!terrarium_test()) {
            ESP_LOGE(TAG, "decortest: terrarium_test failed");
            ok = false;
        }
        if (!terrarium_decor_test()) {
            ESP_LOGE(TAG, "decortest: terrarium_decor_test failed");
            ok = false;
        }        memcpy(g.decor, saved, sizeof saved);
        decor_restore_extra(extra);
        g.decor_extra_count = extra_count;
        g.decor_owned_extra = extra_owned;
        g.catalogue_owned_high = high_owned;
        g.decor_owned = owned;
        g.climb_owned = climb_owned;
        g.silk = silk;
        g.level = level;
        terrarium_refresh_decor();
        atomic_store(&s_ui_ok, ok);
        break;
    }
    case GAME_UI_SCANLOOP_BEGIN:
        scanloop_paused = s_paused;
        scanloop_chrome = ui_hud_chrome_visible();
        behaviour_aim_cancel();
        ui_menu_close();
        ui_hud_set_chrome(false);
        game_pause();
        break;
    case GAME_UI_SCANLOOP_END:
        ui_hud_set_chrome(scanloop_chrome);
        if (!scanloop_paused) game_resume();
        break;
    case GAME_UI_MENU_PICK:
        atomic_store(&s_ui_ok, ui_menu_test_pick((uint8_t)s_ui_value));
        if (!atomic_load(&s_ui_ok)) ESP_LOGW(TAG, "menu pick rejected: item unavailable");
        break;
    case GAME_UI_TEST_UI: {
        ui_menu_close();
        int photo_page = s_menu_pages[MENU_PHOTO_STORAGE];
        bool photo_known = s_menu_page_known[MENU_PHOTO_STORAGE];
        s_page_menu = MENU_PHOTO_STORAGE;
        s_menu_pages[MENU_PHOTO_STORAGE] = 2;
        s_page_loading = true;
        remember_menu_page(0);
        bool loading_page_ok = s_menu_pages[MENU_PHOTO_STORAGE] == 2;
        s_menu_pages[MENU_PHOTO_STORAGE] = photo_page;
        s_menu_page_known[MENU_PHOTO_STORAGE] = photo_known;
        s_page_loading = false;
        ui_hud_set_chrome(true);
        bool menu_ok = ui_menu_test();
        bool hud_ok = ui_hud_test();
        bool backgrounds_ok = terrarium_test();
        bool hunger_ok = needs_hunger_test();
        bool power_ok = power_ui_test();
        bool save_ok = save_format_test();
        bool companion_ok = companion_test();
        bool life_ok = life_test() && loading_page_ok;
        menu_open(MENU_TRAINING);
        life_ok = life_ok && ui_menu_test_distinct_icons() && ui_menu_test_pick(0) &&
                  s_open_menu == MENU_TRICK && ui_menu_test_distinct_icons();
        menu_open(MENU_CALENDAR);
        life_ok = life_ok && ui_menu_test_distinct_icons();
        menu_open(MENU_GENERATIONS);
        life_ok = life_ok && !strcmp(s_menu_items[0].label, "Retire and Raise Another");
        menu_open(MENU_MAIN);
        static const char *const root_order[] = {
            "Decor", "Treats", "Backgrounds", "Borders", "Your Spider", "Today",
            "Training", "Calendar", "Achievements", "Prey Species", "Diary",
            "Generations", "Photo Album", "Help Guide", "Settings"
        };
        for (unsigned i = 0; i < sizeof root_order / sizeof root_order[0]; i++)
            life_ok = life_ok && !strcmp(s_menu_items[i].label, root_order[i]);
        menu_open(MENU_DISPLAY);
        life_ok = life_ok && ui_menu_test_distinct_icons() &&
            !strcmp(s_menu_items[2].label, "Double-Tap Lights");
        menu_open(MENU_SETTINGS);
        for (unsigned i = 0; i < 8; i++)
            life_ok = life_ok && strcmp(s_menu_items[i].label, "Save Screenshot");
        s_photo_slot = 0;
        menu_open(MENU_ALBUM);
        life_ok = life_ok && !strcmp(s_menu_items[0].label, "Take Photo") &&
            !strcmp(s_menu_items[1].label, "Storage Status") &&
            !strcmp(s_menu_items[2].label, "Photo Storage") &&
            ui_menu_test_distinct_icons();
        on_photo_storage_pick(2);
        life_ok = life_ok && ui_menu_test_back() && s_open_menu == MENU_PHOTO_STORAGE &&
            strcmp(s_menu_items[0].label, "Take Photo") &&
            strcmp(s_menu_items[0].label, "Storage Status") &&
            ui_menu_test_back() && s_open_menu == MENU_ALBUM;
        menu_open(MENU_MAIN);
        life_ok = life_ok && ui_menu_test_pick(MENU_GENERATIONS) &&
            ui_menu_test_back() && s_open_menu == MENU_MAIN && ui_menu_page() == 2;
        menu_open(MENU_MAIN);
        life_ok = life_ok && ui_menu_test_pick(MENU_SETTINGS) &&
            ui_menu_test_pick(5) && ui_menu_test_back() &&
            s_open_menu == MENU_SETTINGS && ui_menu_page() == 1 &&
            ui_menu_test_back() && s_open_menu == MENU_MAIN && ui_menu_page() == 3;
        menu_open(MENU_PHOTO_ACTIONS);
        life_ok = life_ok && ui_menu_test_distinct_icons() && ui_menu_test_pick(1) &&
            s_open_menu == MENU_PHOTO_DELETE && ui_menu_test_distinct_icons() &&
            ui_menu_test_pick(1) && s_open_menu == MENU_PHOTO_ACTIONS;
        menu_open(MENU_PERSONALITY);
        companion_ok = ui_menu_visible() && ui_menu_test_distinct_icons() &&
                       !strcmp(s_menu_items[0].label, "Temperament and Bond") && companion_ok;
        menu_open(MENU_TODAY);
        companion_ok = ui_menu_visible() && ui_menu_test_distinct_icons() &&
                       !strcmp(s_menu_items[0].label, "Check In") &&
                       s_menu_items[0].id == 3 && s_menu_items[1].id == 4 &&
                       s_menu_items[2].id == 5 && s_menu_items[3].id == 6 && companion_ok;
        /* Every Today row is claimable: the check-in plus three rotating tasks.
         * There is no dismissible request row any more. */
        companion_ok = s_menu_items[1].label[0] && s_menu_items[2].label[0] &&
                       s_menu_items[3].label[0] && companion_ok;
        menu_open(MENU_DAILY);
        companion_ok = s_open_menu == MENU_TODAY && !strcmp(s_menu_items[0].label, "Check In") && companion_ok;
        game_state_t *daily_backup = heap_caps_malloc(sizeof g, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (daily_backup) {
            *daily_backup = g;
            s_testing_placement = true;
            g.daily_day = DAILY_NEVER;
            g.daily_streak = 0;
            menu_open(MENU_TODAY);
            companion_ok = ui_menu_test_pick(3) && companion_ok;
            uint32_t claimed_silk = g.silk, claimed_xp = g.xp;
            companion_ok = s_open_menu == MENU_TODAY && !s_menu_items[0].enabled &&
                           !ui_menu_test_pick(3) && g.silk == claimed_silk && g.xp == claimed_xp &&
                           g.daily_streak == 1 && companion_ok;
            g = *daily_backup;
            heap_caps_free(daily_backup);
            s_testing_placement = false;
        } else {
            ESP_LOGE(TAG, "daily UI test backup allocation failed");
            companion_ok = false;
        }
        menu_open(MENU_DIARY);
        companion_ok = ui_menu_visible() && s_open_menu == MENU_DIARY && companion_ok;
        s_open_menu = MENU_MEMORY; s_memory_slot = 5;
        game_diary_removed(2);
        companion_ok = s_memory_slot == 4 && companion_ok;
        game_diary_removed(4);
        companion_ok = s_memory_slot == 255 && companion_ok;
        s_open_menu = MENU_DIARY;
        bool decor_ok = decor_test();
        bool pointer_ok = test_furniture_pointer();
        bool selector_ok = test_placement_item_selector();        bool original_muted = audio_muted();
        uint8_t original_music = g.music_enabled;
        s_testing_placement = true;
        on_sound_pick(1);
        bool sound_ok = audio_muted() != original_muted &&
                        g.music_enabled == original_music &&
                        !strcmp(s_menu_items[1].label, "Sound");
        on_sound_pick(1);
        sound_ok = sound_ok && ui_menu_test_distinct_icons() && audio_muted() == original_muted &&
                   g.music_enabled == original_music;
        uint8_t original_usb_sounds = g.usb_sounds;
        ui_menu_set_page(1);
        on_sound_pick(5);
        sound_ok = sound_ok && g.usb_sounds != original_usb_sounds &&
                   audio_muted() == original_muted && g.music_enabled == original_music &&
                   ui_menu_page() == 1 && !strcmp(s_menu_items[5].label, "USB Chimes");
        on_sound_pick(5);
        sound_ok = sound_ok && g.usb_sounds == original_usb_sounds && ui_menu_page() == 1;
        uint8_t original_music_volume = g.music_volume;
        uint8_t original_track = g.music_track;
        uint8_t original_volume = audio_volume();
        on_volume_slider(3, 35);
        on_soundtrack_pick(AUDIO_MUSIC_TRACK_COUNT - 1);
        bool music_ok = g.music_volume == 35 && audio_music_volume() == 35 &&
                        g.music_track == AUDIO_MUSIC_TRACK_COUNT - 1 &&
                        audio_music_track() == AUDIO_MUSIC_TRACK_COUNT - 1 &&
                        ui_menu_page() == (AUDIO_MUSIC_TRACK_COUNT - 1) / 4 &&
                        audio_volume() == original_volume;
        g.music_volume = original_music_volume;
        g.music_track = original_track;
        audio_set_music_volume(original_music_volume);
        audio_set_music_track(original_track);
        menu_open(MENU_SD);
        bool sd_feedback = !strcmp(s_menu_items[2].label, "SD Status") &&
                           !strcmp(s_menu_items[2].detail, s_sd_message ? s_sd_message : sd_status_text());
        menu_open(MENU_MAIN);
        bool borders_route = ui_menu_test_pick(MENU_BORDERS) && ui_menu_visible() &&
                             s_open_menu == MENU_BORDERS && !strcmp(s_menu_items[0].label, "No Border");
        uint32_t original_silk = g.silk;
        g.silk = 0;
        menu_open(MENU_SHOP);
        bool shop_gates = !s_menu_items[0].enabled && !ui_menu_test_pick(0);
        ui_menu_set_page(1);
        on_shop_pick(TREAT_COUNT - 1);
        shop_gates = shop_gates && ui_menu_visible() && ui_menu_page() == (TREAT_COUNT - 1) / 6 && g.silk == 0;
        g.silk = original_silk;
        menu_open(MENU_ABOUT);
        bool help_routes = ui_menu_test_pick(3) && s_open_menu == MENU_OTA;
        menu_open(MENU_MAIN);
        help_routes = help_routes && ui_menu_test_pick(MENU_HELP) && s_open_menu == MENU_HELP &&
                      ui_menu_test_pick(0) && ui_menu_visible();
        back_to_help();
        for (int i = 0; i < HELP_TOPIC_COUNT; i++) {
            on_help_pick(i);
            help_routes = ui_menu_test_text_fits() && help_routes;
        }
        game_state_t *original_state = heap_caps_malloc(sizeof g, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!original_state) {
            ESP_LOGE(TAG, "UI test state allocation failed");
            s_testing_placement = false;
            atomic_store(&s_ui_ok, false);
            break;
        }
        *original_state = g;
        g.achievements = 0; g.xp = 0; g.level = 1; g.silk = 0;
        bool rewards = true;
        for (int i = 0; i < ACHIEVEMENT_COUNT; i++) {
            uint32_t prior = g.silk;
            game_award(ACHIEVEMENTS[i].bit);
            uint32_t paid = g.silk;
            game_award(ACHIEVEMENTS[i].bit);
            rewards = rewards && (g.achievements & ACHIEVEMENTS[i].bit) &&
                      paid >= prior && paid == g.silk;
        }
        rewards = rewards && g.silk == ACHIEVEMENT_COUNT * ACHIEVEMENT_SILK &&
                  g.xp == ACHIEVEMENT_COUNT * XP_ACHIEVEMENT;
        g.achievements = 0; g.stage = STAGE_SPIDERLING; g.age_ticks = 2 * 2880;
        needs_update_stage();
        bool molt = g.stage == STAGE_JUVENILE && g.state == SPIDER_MOLT &&
                    (g.achievements & ACH_FIRST_MOLT);
        behaviour_tick(game_now() + 801u, .05f);
        molt = molt && g.state != SPIDER_MOLT;
        g = *original_state;
        behaviour_set_state(SPIDER_IDLE);
        g.state = original_state->state;
        heap_caps_free(original_state);
        s_testing_placement = false;
        ui_menu_close();
        gift_t original_gift = g.gift;
        g.gift.used = 1;
        g.gift.x = 0;
        g.gift.y = ui.H;
        keep_gift_in_playfield();
        bool gift_bounds = g.gift.x == g_play.left && g.gift.y == g_play.bottom;
        g.gift = original_gift;
        printf("%s uitest pages=%d keyboard=%d editor=%d hud=%d backgrounds=%d hunger=%d power=%d save=%d decor=%d pointer=%d selector=%d bounds=%d sound=%d music=%d sd_feedback=%d borders=%d shop_gates=%d help=%d rewards=%d molt=%d companion=%d life=%d\n",
               menu_ok && hud_ok && backgrounds_ok && gift_bounds && hunger_ok &&
               power_ok && save_ok && decor_ok && pointer_ok && selector_ok && sound_ok && music_ok && sd_feedback &&
               borders_route && shop_gates && help_routes && rewards && molt && companion_ok && life_ok ? "OK" : "ERR",
               menu_ok, menu_ok, menu_ok, hud_ok, backgrounds_ok, hunger_ok,
               power_ok, save_ok, decor_ok, pointer_ok, selector_ok, gift_bounds, sound_ok, music_ok, sd_feedback,
               borders_route, shop_gates, help_routes, rewards, molt, companion_ok, life_ok);
        break;
    }
    default: break;
    }
    s_ui_req = GAME_UI_NONE;
}

/* Open a menu by name from the console, marshalled onto the LVGL task.
 *
 * Opening a menu creates and deletes LVGL objects. Doing that from the console
 * task corrupted LVGL's event list and crashed with "LoadProhibited in
 * lv_event_mark_deleted" -- the same class of bug as calling lv_timer_pause()
 * from a worker. So the request is posted and the render tick performs it. */
static char s_ui_menu_name[16];
static atomic_bool s_ui_menu_req;
static atomic_bool s_ui_menu_done;
static atomic_bool s_ui_menu_ok;

bool game_ui_open_menu(const char *name)
{
    if (!name || strlen(name) >= sizeof s_ui_menu_name || s_ui_menu_req) {
        ESP_LOGW(TAG, "menu request rejected: invalid name or busy");
        return false;
    }

    strncpy(s_ui_menu_name, name, sizeof s_ui_menu_name - 1);
    s_ui_menu_name[sizeof s_ui_menu_name - 1] = '\0';

    s_ui_menu_done = false;
    s_ui_menu_ok   = false;
    s_ui_menu_req  = true;

    /* Bounded wait, so a stalled render timer cannot hang the console. */
    for (int i = 0; i < 100 && !s_ui_menu_done; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!s_ui_menu_done) ESP_LOGW(TAG, "menu acknowledgement timed out");
    return s_ui_menu_done && s_ui_menu_ok;
}
/* Called from game_tick_render(), on the LVGL task. */
static void ui_service_menu_request(void)
{
    if (!s_ui_menu_req) return;

    s_ui_menu_req = false;

    const char *name = s_ui_menu_name;

    if (!strcmp(name, "gift")) {
        ui_hud_gift_menu_show();
        s_ui_menu_ok = true;
    } else if (!strcmp(name, "chrome")) {
        ui_hud_set_chrome(true);
        s_ui_menu_ok = true;
    } else if (!strcmp(name, "nochrome")) {
        ui_hud_set_chrome(false);
        s_ui_menu_ok = true;
    } else if (!strncmp(name, "item", 4)) {
        /* "item N" opens the per-item decor menu for slot N. */
        int slot = atoi(name + 4);
        s_ui_menu_ok = game_ui_open_decor_item(slot);
    } else {
        s_ui_menu_ok = game_open_menu(name);
    }

    s_ui_menu_done = true;
}

/* Slingshot: press starts a pull, move updates it, release fires.
 *
 * The chrome check is essential and is NOT redundant with the one in the tap
 * handler. LVGL dispatches PRESSED before CLICKED, so without this the press
 * started a drag, the click then dismissed the chrome, and the RELEASED handler
 * fired the shot -- so "tap anywhere to dismiss" also pounced. Gating here
 * means the dismissing tap never begins a pull in the first place. */
void game_on_terrarium_press(lv_event_t *e)
{
    (void)e;
    s_tap_valid = false;
    if (ui_hud_chrome_visible() || ui_menu_visible() ||
        game_paused() || g.gift_mode || g_decor_mode) cancel_enclosure_tap();
    else {
        lv_indev_t *indev = lv_indev_active();
        if (indev) {
            lv_indev_get_point(indev, &s_tap_origin);
            s_tap_valid = true;
        }
    }
    if (ui_hud_chrome_visible() || ui_menu_visible() || game_paused()) return;
    if (g.gift_mode || g_decor_mode) {
        lv_indev_t *indev = lv_indev_active();
        if (!indev) return;
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        int reach = g.gift_mode ? 40
                  : decor_is_climb_structure(g_decor_selected)
                    ? decor_def(g_decor_selected)->radius
                    : decor_def(g_decor_selected)->radius * 2 + 12;
        bool grabbed = abs(p.x - s_place_x) <= reach && abs(p.y - s_place_y) <= reach;
        s_place_dx = grabbed ? s_place_x - p.x : 0;
        s_place_dy = grabbed ? s_place_y - p.y : 0;
        /* A press only ARMS the drag; it does not move the item.
         *
         * This used to call placement_position() immediately, so a tap anywhere
         * on the tank snapped the item to the finger. Missing a button by a few
         * pixels therefore threw the item across the enclosure, which is the
         * "tapping shouldn't move the decor item" report. The item now moves
         * only once the finger has travelled past ENCLOSURE_TAP_SLOP, which is
         * the same threshold the rest of the tank uses to tell a tap from a
         * drag. */
        s_place_dragging = false;
        s_place_armed = grabbed;
        s_place_origin = p;
        return;
    }
    if (!behaviour_aim_active()) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p = {0, 0};
    if (indev) lv_indev_get_point(indev, &p);

    behaviour_aim_drag_begin(p.x, p.y);
}

void game_on_terrarium_pressing(lv_event_t *e)
{
    (void)e;
    lv_indev_t *active = lv_indev_active();
    if (s_tap_valid && active) {
        lv_point_t p;
        lv_indev_get_point(active, &p);
        int dx = p.x - s_tap_origin.x, dy = p.y - s_tap_origin.y;
        int distance = dx * dx + dy * dy;
        if (distance > ENCLOSURE_TAP_SLOP * ENCLOSURE_TAP_SLOP ||
            (behaviour_aim_dragging() && distance >= AIM_MIN_PULL_PX * AIM_MIN_PULL_PX)) {
            s_tap_valid = false;
            cancel_enclosure_tap();
        }
    }
    if (ui_hud_chrome_visible() || ui_menu_visible() || game_paused()) {
        behaviour_aim_cancel();
        return;
    }
    if (g.gift_mode || g_decor_mode) {
        lv_indev_t *indev = lv_indev_active();
        if (s_place_armed && indev) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            /* Promote the armed press to a real drag only once the finger has
             * moved past the tap slop. Until then the item stays exactly where
             * it was, so a tap that misses a button cannot displace it. */
            if (!s_place_dragging) {
                int dx = p.x - s_place_origin.x, dy = p.y - s_place_origin.y;
                if (dx * dx + dy * dy <= ENCLOSURE_TAP_SLOP * ENCLOSURE_TAP_SLOP) return;
                s_place_dragging = true;
            }
            placement_position(p.x + s_place_dx, p.y + s_place_dy);
        }
        return;
    }
    if (!behaviour_aim_dragging()) return;
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = {0, 0};
    if (indev) lv_indev_get_point(indev, &p);

    behaviour_aim_drag_move(p.x, p.y);
}

void game_on_terrarium_release(lv_event_t *e)
{
    (void)e;
    if (g.gift_mode || g_decor_mode) {
        game_on_terrarium_pressing(e);
        s_place_dragging = false;
        s_place_armed = false;
        return;
    }
    if (ui_hud_chrome_visible() || ui_menu_visible() || game_paused() || g.gift_mode ||
        g_decor_mode || !behaviour_aim_dragging()) {
        behaviour_aim_cancel();
        return;
    }

    if (behaviour_aim_release()) {
        game_cancel_pending_tap();
        g.taps++;
    }
}

/* Hold an item to edit it directly over the enclosure, without a submenu. */
void game_on_terrarium_long_press(lv_event_t *e)
{
    (void)e;
    s_tap_valid = false;
    cancel_enclosure_tap();
    if (ui_hud_chrome_visible() || ui_menu_visible() || game_paused() ||
        g.gift_mode || g_decor_mode) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p = {0, 0};
    lv_indev_get_point(indev, &p);

    int slot = decor_hit_test((int16_t)p.x, (int16_t)p.y);
    bool canopy = slot >= 0 && decor_at(slot)->blocking == DECOR_WALK_BEHIND;
    if (!canopy && g.gift.used && abs(p.x - g.gift.x) <= 32 && abs(p.y - g.gift.y) <= 32) {
        behaviour_aim_cancel();
        on_gift_item_pick(0);
        s_place_dragging = true;
        s_place_dx = s_place_x - p.x;
        s_place_dy = s_place_y - p.y;
        return;
    }
    if (slot < 0) return;

    /* A long press must not also fire the pounce on release. */
    behaviour_aim_cancel();
    s_decor_item_slot = slot;
    decor_begin_move(slot);
    /* decor_begin_move() refuses when decor is locked or prey is out, so only
     * start the drag if it actually staged the item. */
    if (!g_decor_mode) return;
    s_place_dragging = true;
    s_place_dx = s_place_x - p.x;
    s_place_dy = s_place_y - p.y;
}

void game_on_terrarium_tap(lv_event_t *e)
{
    (void)e;
    if (ui_menu_visible()) return;
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = {0, 0};
    if (indev) lv_indev_get_point(indev, &p);

    if (g_decor_mode || g.gift_mode || game_paused()) {
        cancel_enclosure_tap();
        return;
    }
    if (ui_hud_chrome_visible()) {
        cancel_enclosure_tap();
        ui_hud_dismiss_if_visible();
        return;
    }
    if (indev) {
        int dx = p.x - s_tap_origin.x, dy = p.y - s_tap_origin.y;
        int distance = dx * dx + dy * dy;
        if (!s_tap_valid || distance > ENCLOSURE_TAP_SLOP * ENCLOSURE_TAP_SLOP ||
            (behaviour_aim_active() && distance >= AIM_MIN_PULL_PX * AIM_MIN_PULL_PX)) {
            cancel_enclosure_tap();
            return;
        }
        dx = p.x - s_last_tap.x; dy = p.y - s_last_tap.y;
        uint32_t now = game_now();
        if (!g.life.light_gesture_disabled && s_tap_pending && now - s_last_tap_at <= ENCLOSURE_DOUBLE_TAP_MS &&
            dx * dx + dy * dy <= ENCLOSURE_DOUBLE_TAP_SLOP * ENCLOSURE_DOUBLE_TAP_SLOP) {
            enclosure_double_tap();
            return;
        }
        s_last_tap = p;
        s_last_tap_at = now;
        s_tap_pending = true;
    }

    /* Care has its own hit target; edge drags no longer reveal a tray. */

    /* Remember where the player touched: a bonded spider turns toward it. */
    if (indev) behaviour_note_tap(p.x, p.y);

    /* 2. Aiming is handled by the press/move/release handlers, not by a click.
     * A click here means the player tapped without dragging, which the release
     * handler treats as a cancel. */
    if (behaviour_aim_active()) {
        return;
    }

    /* 3. A tap on the spider itself is a pet.
     *
     * Petting was removed once because it competed with the slingshot for the
     * same gesture. The ambiguity is resolved by requiring the tap to land on
     * the spider's body: a pull that starts on the spider still fires, because
     * the release handler only treats a drag past AIM_MIN_PULL_PX as a shot and
     * a click only arrives when the finger did not drag. */
    if (indev) {
        s_pet_tap = p;
        if (s_pet_tap_timer) lv_timer_delete(s_pet_tap_timer);
        s_pet_tap_timer = lv_timer_create(delayed_pet, ENCLOSURE_DOUBLE_TAP_MS + 25, NULL);
        lv_timer_set_repeat_count(s_pet_tap_timer, 1);
        return;
    }

    /* 4. Empty tap: nothing happens, and nothing is said.
     *
     * The player explicitly asked for the on-screen hints to go. "tap the
     * spider" and "wait for TAP!" were instructions, not feedback, and they
     * appeared on every stray tap. Silence is the correct response to a tap
     * that hit nothing. */
    g.taps++;
}

/* ------------------------------------------------------------------ */
/* Timers                                                              */
/* ------------------------------------------------------------------ */
void game_tick_logic(lv_timer_t *timer)
{
    (void)timer;
    /* Only a genuine pause stops the clock. Menus, the care HUD, a locked
     * screen and the first-run setup screen used to return here, which froze
     * hunger and hygiene whenever the player left the device on a menu, with
     * the screen off, or before naming the spider. Needs must keep advancing;
     * only the parts that need a visible, interactive tank are skipped below. */
    if (s_paused || save_restore_pending() || net_busy() || power_sleep_pending()) {
        s_last_logic = lv_tick_get();
        return;
    }

    bool screen_off = power_sleeping() || buttons_locked();
    bool tank_hidden = screen_off || ui_menu_visible() || ui_hud_chrome_visible() ||
                       lv_screen_active() != ui.screen;

    /* Motes are invisible behind a menu or a dark screen; moving them is wasted
     * work, and on a hidden screen it is wasted work that still costs a redraw. */
    ambient_set_visible(!tank_hidden);

    uint32_t now = lv_tick_get();
    float dt = (float)(now - s_last_logic) / 1000.0f;
    s_last_logic = now;

    /* Clamp dt so a long stall cannot teleport the spider. */
    if (dt <= 0.0f || dt > 0.25f) dt = 1.0f / (float)LOGIC_HZ;

    /* The gift bonus is global, but recompute each tick so replacing or
     * removing the gift takes effect immediately. */
    gift_compute();

    /* WiFi reconnect backoff. Cheap when connected or off. */
    net_tick(now);
    audio_tick(now);

    /* The radio is NOT started automatically.
     *
     * Bringing WiFi up allocates a large block of internal DMA-capable RAM for
     * its RX/TX pools. The LCD SPI driver needs the same pool for every frame,
     * and the two cannot coexist: starting WiFi at boot produced a steady
     * stream of "Draw bitmap failed: ESP_ERR_NO_MEM" and a corrupted display.
     *
     * Deferring it to the first tick was not enough -- the allocation is
     * permanent, so the display starves from then on regardless of when it
     * happens. The radio is therefore started only when the player opens the
     * WiFi menu, which is the only time it is needed. */

    /* The mind decides what the spider WANTS; behaviour decides what it can
     * actually do. Keeping them separate means the intent layer can never
     * break the safety guarantees in the FSM. */
    if (!tank_hidden) mind_tick(now);

    /* Stamp the wall clock so a power-off or a dead battery is measurable at
     * the next boot. Cheap: one RTC read per minute, not per tick. */
    static uint32_t next_stamp;
    if ((int32_t)(now - next_stamp) >= 0) {
        next_stamp = now + 60000u;
        time_t utc = 0;
        if (pet_rtc_read(&utc) == ESP_OK && utc > 0) g.last_seen = (uint32_t)utc;
    }

    needs_tick(now, dt);
    game_check_achievements();
    companion_tick();
    life_tick_active(now, !tank_hidden);
    static uint32_t request_notice_day = UINT32_MAX;
    if (!tank_hidden && !g.gift_mode && !g_decor_mode && now > 15000 &&
        request_notice_day != g.companion.request_day &&
        (!g.companion.requests[0].state || !g.companion.requests[1].state)) {
        request_notice_day = g.companion.request_day;
        ui_hud_bubble("Optional Requests / Menu > Today", 0x76d6b1, 2400);
    }
    buttons_tick(now);
    if (!tank_hidden) {
        behaviour_tick(now, dt);
        prey_tick(now, dt);
    }

    /* A bonded spider contributes Silk, not free items that bypass the shop. */
    static uint32_t next_gift;
    if (g.bond >= BOND_GIFT && !g.gift.used) {
        if (next_gift == 0) {
            next_gift = now + 120000u + (uint32_t)(rand() % 120000);
        } else if (now >= next_gift) {
            next_gift = now + 120000u + (uint32_t)(rand() % 120000);
            game_add_silk(2);
            audio_play(SFX_TAP);
            ESP_LOGI(TAG, "bond reward: +2 Silk");
        }
    } else if (g.bond < BOND_GIFT) {
        next_gift = 0;
    }

    if (now - s_last_save >= SAVE_PERIOD_MS) {
        s_last_save = now;
        game_save_now();
    }

    /* Heartbeat: makes an unattended soak verifiable from the serial log. */
    static uint32_t last_beat;
    if (now - last_beat >= 30000u) {
        last_beat = now;
        const gift_bonus_t *gb = gift_bonus();
        ESP_LOGI(TAG, "hb state=%u pos=(%d,%d) h=%u m=%u e=%u hy=%u prey=%d sick=%u lvl=%lu aim=%d streak=%u bond=%u temp=%s gift=%s eR=%.2f hD=%.2f pR=%.2f aW=%.2f",
                 (unsigned)g.state, g.spider_x, g.spider_y,
                 g.hunger, g.mood, g.energy, g.hygiene,
                 prey_count(), g.sick, (unsigned long)g.level,
                 behaviour_aim_dragging() ? 1 : 0, (unsigned)g.streak,
                 (unsigned)g.bond, game_temperament()->name,
                 gift_present() ? gift_kind_name(gift_kind()) : "none",
                 gb->energy_regen_mult, gb->hygiene_decay_mult,
                 gb->pounce_range_mult, gb->aim_window_mult);
    }
}

static void life_visual_tick(uint32_t now)
{
    static uint32_t next_clock, next_expression;
    static life_clock_t clock;
    static uint8_t last_activity = 255;
    if ((int32_t)(now - next_clock) >= 0) {
        next_clock = now + 1000u;
        /* Read the clock through life_clock_now() rather than time(NULL), so a
         * console override (and the test harness) drives the tint and the shafts
         * as well as the mind. Calling time(NULL) here meant `rtcset` changed
         * what the brain saw but not what the screen showed. */
        clock = life_clock_now();
        apply_daylight_tint(clock.period, clock.available);
        lv_obj_set_hidden(s_visitor, !clock.visitor);
        if (clock.available && last_activity != clock.activity) {
            last_activity = clock.activity;
            if (clock.activity && g.state != SPIDER_SLEEP && !g.sick) {
                ui_hud_bubble(life_activity_name(clock.activity), 0x76d6b1, 2000);
                if (g.state == SPIDER_IDLE && g.mood >= 60) cspider_pet_react();
            }
        }
    }
    /* Weather specks. Six objects, each getting a hidden flag and a position --
     * twelve setters per frame, and each one is a dirty rectangle. They drift
     * slowly enough that 10 Hz is indistinguishable from 30 Hz, so the whole
     * block is throttled rather than pushed every tick. */
    static uint32_t next_weather;
    if (next_weather == 0 || (int32_t)(now - next_weather) >= 0) {
        next_weather = now + 100u;
        for (unsigned i = 0; i < 6; i++) {
            bool visible = clock.available && g.life.weather;
            lv_obj_set_hidden(s_weather[i], !visible);
            if (visible) {
                int y = 40 + (int)((now / (clock.season == 0 ? 120u : 75u) + i * 59u) % (ui.H - 80));
                lv_obj_set_pos(s_weather[i], i & 1 ? ui.W - 14 : 10, y);
            }
        }
    }
    if (!next_expression) next_expression = now + 45000u;
    if ((int32_t)(now - next_expression) >= 0) {
        next_expression = now + 45000u + (uint32_t)g.temperament * 5000u;
        if (g.state == SPIDER_IDLE && !g.sick && g.energy >= 30 && g.mood >= 60) {
            cspider_pet_react();
            ui_hud_bubble(g.bond >= 50 ? "Happy to See You" :
                          g.temperament == 1 ? "A Shy Little Hello" : "Feeling Curious",
                          0x76d6b1, 1600);
        }
    }
}

/* LVGL refresh instrumentation.
 *
 * The render tick is only part of a frame. lv_timer_handler() runs the refresh
 * after the timers, and the refresh is where the panel transfer happens, so the
 * interval between two render ticks is really "tick work + refresh work". These
 * callbacks measure the refresh half directly: how long it took, how many
 * flushes it issued, and how many pixels those flushes covered.
 *
 * This instrumentation found the real cause of the "frame skip": LVGL keeps at
 * most LV_INV_BUF_SIZE dirty rectangles per frame, and when a frame produces
 * more it discards the whole list and redraws the ENTIRE screen (see the
 * `inv_p >= LV_INV_BUF_SIZE` branch in lv_refr.c). The tank invalidates ~100
 * areas per frame, so every frame was a full 480x480 software redraw: 76 ms of
 * draw time, which capped the game at 8.9 Hz. Raising the buffer to 128 in
 * main/CMakeLists.txt dropped the dirty area from 170k px to 15k px per frame
 * and restored 30 Hz. Keep these counters: they are the only way to see that
 * regression coming back. */
/* Every invalidation LVGL accepts, before it is merged into the refresh set.
 * This is the number that explains a slow refresh: the flush area is the union
 * of these, so a single full-screen invalidation per frame makes every frame a
 * full-screen transfer no matter how little actually changed. */
static uint32_t s_inv_count, s_inv_pixels, s_inv_max_pixels, s_inv_full;
/* Draw vs transfer. A refresh is "render the dirty area into the buffer" then
 * "push the buffer to the panel". They have completely different fixes -- fewer
 * dirty pixels versus a faster bus -- so they are timed separately. */
static uint32_t s_draw_us_total, s_xfer_us_total, s_prep_us_total;
static int64_t  s_render_start_us, s_flush_start_us, s_last_flush_end_us;

static void on_refresh_start(lv_event_t *event)
{
    (void)event;
    s_refresh_start_us = esp_timer_get_time();
}

/* LV_EVENT_RENDER_START fires once per refresh, immediately before the dirty
 * areas are drawn into the draw buffer. The gap between it and the first flush
 * is the software draw cost. */
static void on_render_start(lv_event_t *event)
{
    (void)event;
    s_render_start_us = esp_timer_get_time();
    /* Clear the previous refresh's last-flush stamp. The final FLUSH_FINISH of a
     * refresh can arrive AFTER REFR_READY (the transfer is asynchronous), so
     * without this the first band of the next refresh measured its draw time
     * from that stale stamp and swallowed the whole inter-refresh gap -- which
     * made "draw" look like 31 ms when it was really the idle time between
     * frames. */
    s_last_flush_end_us = 0;
    /* REFR_START -> RENDER_START is the prep phase: lv_obj_update_layout() over
     * every layer, lv_refr_join_area() and refr_sync_areas(). It is neither draw
     * nor transfer, so it is invisible in both of those timers. */
    if (s_refresh_start_us) {
        s_prep_us_total += (uint32_t)(s_render_start_us - s_refresh_start_us);
    }
}

static void on_refresh_ready(lv_event_t *event)
{
    (void)event;
    if (!s_refresh_start_us) return;
    uint32_t us = (uint32_t)(esp_timer_get_time() - s_refresh_start_us);
    s_refresh_start_us = 0;
    s_last_flush_end_us = 0;
    s_render_start_us = 0;
    s_flush_start_us = 0;
    s_refresh_count++;
    s_refresh_us_total += us;
    if (us > s_refresh_us_max) s_refresh_us_max = us;
}

static void on_flush_start(lv_event_t *event)
{
    const lv_area_t *area = lv_event_get_param(event);
    if (!area) return;
    uint32_t pixels = (uint32_t)lv_area_get_size(area);
    s_flush_calls++;
    s_flush_pixels += pixels;
    if (pixels > s_flush_max_pixels) s_flush_max_pixels = pixels;
    /* Draw time is everything that is NOT transfer. In partial render mode the
     * refresh alternates draw-band / flush-band, so the draw cost of bands 2..N
     * happens between the previous FLUSH_FINISH and this FLUSH_START. Timing
     * only REFR_START -> first FLUSH_START measured band 1 and hid the rest. */
    int64_t now = esp_timer_get_time();
    if (s_last_flush_end_us) {
        s_draw_us_total += (uint32_t)(now - s_last_flush_end_us);
    } else if (s_render_start_us) {
        s_draw_us_total += (uint32_t)(now - s_render_start_us);
    }
    s_render_start_us = 0;
    s_flush_start_us = now;
}

/* LV_EVENT_FLUSH_FINISH fires when the panel transfer for one area completed,
 * so the gap from FLUSH_START is the bus time for that area. */
static void on_flush_finish(lv_event_t *event)
{
    (void)event;
    if (!s_flush_start_us) return;
    int64_t now = esp_timer_get_time();
    s_xfer_us_total += (uint32_t)(now - s_flush_start_us);
    s_flush_start_us = 0;
    s_last_flush_end_us = now;
}

/* Every invalidation LVGL accepts, before it is merged into the refresh set.
 * This is the number that explains a slow refresh: the flush area is the union
 * of these, so a single full-screen invalidation per frame makes every frame a
 * full-screen transfer no matter how little actually changed. */
static uint32_t s_inv_count, s_inv_pixels, s_inv_max_pixels, s_inv_full;
/* Which subsystem was running when the invalidation happened. LVGL sends
 * INVALIDATE_AREA on the *display*, so the event carries no object pointer --
 * the only way to attribute a full-screen dirty rect is to stamp the caller. */
static const char *s_inv_tag = "boot";
static uint32_t s_inv_tag_px[8];
static uint32_t s_inv_tag_full[8];
static uint32_t s_inv_tag_gap[8];
static const char *s_inv_tag_name[8];
static int s_inv_tag_count;

static void inv_tag(const char *name)
{
    s_inv_tag = name;
}

static void inv_tag_account(uint32_t pixels, bool full)
{
    for (int i = 0; i < s_inv_tag_count; i++) {
        if (s_inv_tag_name[i] == s_inv_tag) {
            s_inv_tag_px[i] += pixels;
            if (full) s_inv_tag_full[i]++;
            return;
        }
    }
    if (s_inv_tag_count < 8) {
        s_inv_tag_name[s_inv_tag_count] = s_inv_tag;
        s_inv_tag_px[s_inv_tag_count] = pixels;
        s_inv_tag_full[s_inv_tag_count] = full ? 1 : 0;
        s_inv_tag_count++;
    }
}

/* Count a long frame gap against the subsystem that ran last. Same slot table as
 * the invalidation accounting, so one `INVTAG` line reports both. */
static void inv_tag_gap(void)
{
    for (int i = 0; i < s_inv_tag_count; i++) {
        if (s_inv_tag_name[i] == s_inv_tag) {
            s_inv_tag_gap[i]++;
            return;
        }
    }
    if (s_inv_tag_count < 8) {
        s_inv_tag_name[s_inv_tag_count] = s_inv_tag;
        s_inv_tag_gap[s_inv_tag_count] = 1;
        s_inv_tag_count++;
    }
}

static void on_invalidate_area(lv_event_t *event)
{
    const lv_area_t *area = lv_event_get_param(event);
    if (!area) return;
    uint32_t pixels = (uint32_t)lv_area_get_size(area);
    s_inv_count++;
    s_inv_pixels += pixels;
    if (pixels > s_inv_max_pixels) s_inv_max_pixels = pixels;
    if (pixels >= (uint32_t)(ui.W * ui.H)) s_inv_full++;
    inv_tag_account(pixels, pixels >= (uint32_t)(ui.W * ui.H));
}

/* Report and reset the render-tick cost. Called from the console so a
 * performance regression is a number rather than a feeling. */
void game_render_cost_report(void)
{
    uint32_t ticks = s_render_ticks;
    uint32_t total = s_render_us_total;
    uint32_t terr = s_sec_terrarium_us, amb = s_sec_ambient_us;
    uint32_t spid = s_sec_spider_us, hud = s_sec_hud_us;
    uint32_t gap_max = s_frame_gap_max_us, gap_over = s_frame_gap_over;
    uint32_t gap_last = s_frame_gap_last_us;
    uint32_t refreshes = s_refresh_count, flush_calls = s_flush_calls;
    uint32_t flush_px = s_flush_pixels, flush_max = s_flush_max_pixels;
    uint32_t refr_us = s_refresh_us_total, refr_max = s_refresh_us_max;
    uint32_t inv_count = s_inv_count, inv_px = s_inv_pixels;
    uint32_t inv_max = s_inv_max_pixels, inv_full = s_inv_full;
    uint32_t draw_us = s_draw_us_total, xfer_us = s_xfer_us_total;
    uint32_t prep_us = s_prep_us_total;
    s_render_ticks = 0;
    s_render_us_total = 0;
    s_sec_terrarium_us = s_sec_ambient_us = s_sec_spider_us = s_sec_hud_us = 0;
    s_frame_gap_max_us = 0;
    s_frame_gap_over = 0;
    s_refresh_count = s_flush_pixels = s_flush_max_pixels = s_flush_calls = 0;
    s_refresh_us_total = s_refresh_us_max = 0;
    s_inv_count = s_inv_pixels = s_inv_max_pixels = s_inv_full = 0;
    s_draw_us_total = s_xfer_us_total = s_prep_us_total = 0;
    printf("RENDERCOST ticks=%lu avg_us=%lu budget_us=%d\n",
           (unsigned long)ticks,
           (unsigned long)(ticks ? total / ticks : 0),
           1000000 / RENDER_HZ);
    /* The spread, not the average, is what "choppy" means. `over` counts
     * intervals longer than two timer periods -- each one is a visible skip. */
    printf("RENDERSPREAD gap_max_us=%lu gap_last_us=%lu over_2x=%lu\n",
           (unsigned long)gap_max, (unsigned long)gap_last, (unsigned long)gap_over);
    /* The refresh half. `px` is the total flushed area, so px/refresh is the
     * average dirty area per frame: 230,400 means the whole screen every time. */
    printf("REFRESH count=%lu avg_us=%lu max_us=%lu flush=%lu px=%lu px_max=%lu px_avg=%lu\n",
           (unsigned long)refreshes,
           (unsigned long)(refreshes ? refr_us / refreshes : 0),
           (unsigned long)refr_max,
           (unsigned long)flush_calls,
           (unsigned long)flush_px,
           (unsigned long)flush_max,
           (unsigned long)(refreshes ? flush_px / refreshes : 0));
    printf("INVALIDATE count=%lu px=%lu px_max=%lu full=%lu\n",
           (unsigned long)inv_count, (unsigned long)inv_px,
           (unsigned long)inv_max, (unsigned long)inv_full);
    printf("REFRSPLIT draw_us=%lu xfer_us=%lu prep_us=%lu\n",
           (unsigned long)(refreshes ? draw_us / refreshes : 0),
           (unsigned long)(refreshes ? xfer_us / refreshes : 0),
           (unsigned long)(refreshes ? prep_us / refreshes : 0));
    for (int i = 0; i < s_inv_tag_count; i++) {
        printf("INVTAG %s px=%lu full=%lu gap=%lu\n", s_inv_tag_name[i],
               (unsigned long)s_inv_tag_px[i],
               (unsigned long)s_inv_tag_full[i],
               (unsigned long)s_inv_tag_gap[i]);
    }
    memset(s_inv_tag_px, 0, sizeof s_inv_tag_px);
    memset(s_inv_tag_full, 0, sizeof s_inv_tag_full);
    memset(s_inv_tag_gap, 0, sizeof s_inv_tag_gap);
    s_inv_tag_count = 0;
    /* Per-section averages. `other` is the remainder: life_visual_tick, the
     * enclosure lights, the depth sorts and the timer overhead. */
    if (ticks) {
        printf("RENDERSECT terrarium=%lu ambient=%lu spider=%lu hud=%lu other=%ld\n",
               (unsigned long)(terr / ticks), (unsigned long)(amb / ticks),
               (unsigned long)(spid / ticks), (unsigned long)(hud / ticks),
               (long)(total / ticks) - (long)((terr + amb + spid + hud) / ticks));
    }
}

void game_tick_render(lv_timer_t *timer)
{
    inv_tag("boot");
    power_tick();
    static bool save_failure_shown;
    if (!save_busy() && !save_last_ok() && !save_failure_shown) {
        ui_hud_bubble("Save Failed / See Log", 0xf5c86b, 2400);
        save_failure_shown = true;
    } else if (save_last_ok()) save_failure_shown = false;
    (void)timer;

    uint32_t now = lv_tick_get();
    int family_save = s_pet_change_waiting ? life_generation_save_status() : -1;
    if (s_pet_change_waiting && family_save >= 0) {
        s_pet_change_waiting = false;
        ui_hud_bubble(family_save ? "Family Saved / Both Spiders Safe" :
                      "Family Save Failed / Keep Power On / See Log",
                      family_save ? 0x76d6b1 : 0xf5c86b, 3200);
        if (s_open_menu == MENU_GENERATIONS) menu_open(MENU_GENERATIONS);
    }

    /* Perform any UI action the serial console asked for. This runs on the LVGL
     * task, which is the only thread allowed to touch lv_*. */
    ui_service_request();
    ui_service_menu_request();
    /* Refresh the WiFi menu when a radio job finishes.
     *
     * The scan runs on the net task and the UI never waits for it, so the menu
     * has to be rebuilt once the results are in. This is the poll that closes
     * the loop: while net_busy() the menu shows "scanning...", and the moment
     * it clears the list is rebuilt with the networks found.
     *
     * Keyed on net_generation() rather than the busy edge, because rebuilding
     * the menu calls net_init() -- an edge test would re-trigger itself. */
    /* The menu can be closed from inside ui_menu.c (BACK, or a row that
     * dismisses it), so the tracker is resynced here rather than at every
     * close site. */
    if (!ui_menu_visible()) {
        s_open_menu = -1;
        if (s_album_loaded.pixels) {
            lv_image_cache_drop(&s_album_image);
            lv_image_header_cache_drop(&s_album_image);
            album_release_image(&s_album_loaded);
            memset(&s_album_image, 0, sizeof s_album_image);
        }
    }

    static uint32_t seen_gen;
    uint32_t gen = net_generation();
    if (gen != seen_gen) {
        seen_gen = gen;
        /* Preserve navigation while refreshing network-dependent actions. */
        if (s_open_menu == MENU_WIFI) menu_open(MENU_WIFI);
        else if (s_open_menu == MENU_WIFI_SCAN) menu_open(MENU_WIFI_SCAN);
        else if (s_open_menu == MENU_OTA) menu_open(MENU_OTA);
        else if (s_open_menu == MENU_SETTINGS) {
            int page = ui_menu_page();
            menu_open(MENU_SETTINGS);
            ui_menu_set_page(page);
        }
    }
    static uint32_t seen_sd_gen;
    uint32_t sd_gen = sd_generation();
    if (sd_gen != seen_sd_gen) {
        seen_sd_gen = sd_gen;
        if (s_sd_capture && !sd_busy()) {
            lv_draw_buf_destroy(s_sd_capture);
            s_sd_capture = NULL;
        }
        if (sd_take_restored(&g)) {
            audio_set_volume(g.volume);
            audio_set_music_volume(g.music_volume);
            audio_set_music_track(g.music_track);
            audio_set_music(g.music_enabled);
            apply_enclosure_lights();
            state_finish_load();
            keep_gift_in_playfield();
            audio_set_muted(g.muted != 0);
            spider_refresh_colours();
            terrarium_rebuild();
            terrarium_refresh_gift();
        }
        if (s_open_menu == MENU_SD) menu_open(MENU_SD);
        else if (s_open_menu == MENU_SETTINGS && ui_menu_visible()) {
            int page = ui_menu_page();
            menu_open(MENU_SETTINGS);
            ui_menu_set_page(page);
        }
    }
    static uint32_t seen_album_gen;
    uint32_t album_gen = album_generation();
    if (album_gen != seen_album_gen) {
        seen_album_gen = album_gen;
        album_image_t image = {0};
        if (album_take_image(&image)) {
            if (s_open_menu == MENU_PHOTO && image.slot == s_photo_slot) {
                s_album_loaded = image;
                s_album_image = (lv_image_dsc_t){
                    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                        .w = image.width, .h = image.height, .stride = image.stride},
                    .data_size = image.stride * image.height, .data = image.pixels
                };
            } else album_release_image(&image);
        }
        if (s_open_menu == MENU_ALBUM || s_open_menu == MENU_PHOTO_STORAGE ||
            s_open_menu == MENU_PHOTO) menu_open(s_open_menu);
    }

    if (s_paused || save_restore_pending() || power_sleep_pending() || power_sleeping() || net_busy() || ui_menu_visible() || ui_hud_chrome_visible() ||
        lv_screen_active() != ui.screen) {
        inv_tag("chrome");
        if (!s_paused && !net_busy() && !ui_menu_visible() &&
            lv_screen_active() == ui.screen) ui_hud_tick(now);
        return;
    }
    /* Re-read the clock: ui_service_request() above may have created objects
     * stamped with a later lv_tick_get(). Passing the older `now` to
     * terrarium_tick() made now - stamp negative, which wraps to a huge
     * unsigned value and expires the object on the very tick it was made. */
    now = lv_tick_get();
    int64_t render_start = esp_timer_get_time();
    inv_tag("life");
    life_visual_tick(now);
    int64_t t0 = esp_timer_get_time();
    inv_tag("terrarium");
    terrarium_tick(now);
    int64_t t1 = esp_timer_get_time();
    inv_tag("ambient");
    ambient_tick(now);
    int64_t t2 = esp_timer_get_time();
    inv_tag("lights");
    tick_enclosure_lights();
    terrarium_decor_depth();
    terrarium_gift_depth();
    inv_tag("spider");
    cspider_render(now);
    spider_render(now);   /* aim visuals only */
    prey_render(now);
    int64_t t3 = esp_timer_get_time();
    inv_tag("hud");
    ui_hud_tick(now);
    int64_t t4 = esp_timer_get_time();
    inv_tag("idle");
    s_sec_terrarium_us += (uint32_t)(t1 - t0);
    s_sec_ambient_us += (uint32_t)(t2 - t1);
    s_sec_spider_us += (uint32_t)(t3 - t2);
    s_sec_hud_us += (uint32_t)(t4 - t3);
    /* Accumulate the real cost of a render tick, so a regression shows up as a
     * number rather than as "the movement feels choppy". The 30 Hz timer gives
     * 33,333 us per tick; anything approaching that is a stutter. */
    s_render_us_total += (uint32_t)(esp_timer_get_time() - render_start);
    s_render_ticks++;

    /* Frame-interval spread. `render_start` is the moment this tick began, so
     * the gap between consecutive starts is the interval the eye actually sees.
     * A tick that overruns its period shows up here as a long gap on the NEXT
     * tick, which is exactly the "frame skip" the player reports. */
    {
        static int64_t s_prev_start;
        if (s_prev_start) {
            uint32_t gap = (uint32_t)(render_start - s_prev_start);
            s_frame_gap_last_us = gap;
            if (gap > s_frame_gap_max_us) s_frame_gap_max_us = gap;
            if (gap > (uint32_t)(2 * RENDER_PERIOD_MS * 1000)) {
                s_frame_gap_over++;
                /* Attribute the gap to the subsystem that ran last. Logging here
                 * would perturb the very thing being measured (a UART write from
                 * the LVGL task costs milliseconds), so the counts are reported
                 * by `rendercost` instead. */
                inv_tag_gap();
            }
        }
        s_prev_start = render_start;
    }
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

/* LVGL callers queue requests; the network worker owns pause and teardown. */
void game_ui_start_wifi_scan(void)
{
    if (!net_scan_start()) {
        ESP_LOGE(TAG, "WiFi scan failed to start");
    }
}

void game_ui_connect_wifi(int scan_index)
{
    if (scan_index < 0 || scan_index >= net_scan_count()) {
        ESP_LOGW(TAG, "invalid WiFi scan selection %d", scan_index);
        return;
    }

    on_wifi_pick((uint8_t)(scan_index + 4));
}

void game_ui_forget_wifi(void)
{
    if (!net_forget_saved(0)) ESP_LOGW(TAG, "forget WiFi rejected");
}

void game_ui_open_wifi_password_entry(void)
{
    ui_menu_open_input("Network Name (SSID)", "", false, 32, on_ssid);
    ui_menu_set_back(back_to_wifi);
}

void game_init(void)
{
    state_load();

    /* Seed the RNG: it was never seeded, so placement repeated every boot. */
    srand((unsigned int)(g.seed ^ (uint32_t)lv_tick_get()));

    /* Roll the temperament once, now that the RNG is seeded. A v1 save has no
     * temperament field, so the sentinel survives the load and this fires. */
    if (g.temperament >= TEMPERAMENT_COUNT) {
        g.temperament = (uint8_t)(rand() % TEMPERAMENT_COUNT);
        ESP_LOGI(TAG, "temperament: %s", game_temperament()->name);
    }

    /* bsp_display_start() already launched the LVGL worker task, which runs
     * lv_timer_handler() over the same object tree. Hold the adapter lock for
     * the whole UI build or we race it (Load access fault). */
    if (esp_lv_adapter_lock(-1) != ESP_OK) {
        ESP_LOGE(TAG, "game_init: failed to acquire LVGL adapter lock");
        return;
    }

    memset(&ui, 0, sizeof ui);
    ui.W = LV_HOR_RES;
    ui.H = LV_VER_RES;
    if (ui.W < 40) ui.W = 480;
    if (ui.H < 40) ui.H = 480;

    compute_playfield();

    /* --- terrarium screen -------------------------------------------- */
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, ui.W, ui.H);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x04050a), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_scrollable(scr, false);
    ui.screen = scr;
    /* Every UI layer hangs off the rotation wrapper so a quarter turn moves
     * the whole interface, not just the background. */
    lv_obj_t *root = power_rotation_layer(scr);

    /* Tap layer sits above the background but below the spider. */
    ui.tap_layer = lv_obj_create(root);
    lv_obj_remove_style_all(ui.tap_layer);
    lv_obj_set_size(ui.tap_layer, ui.W, ui.H);
    lv_obj_set_pos(ui.tap_layer, 0, 0);
    lv_obj_set_style_bg_opa(ui.tap_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui.tap_layer, 0, 0);
    lv_obj_set_scrollable(ui.tap_layer, false);
    lv_obj_set_clickable(ui.tap_layer, true);
    lv_obj_set_press_lock(ui.tap_layer, true);
    lv_obj_add_event_cb(ui.tap_layer, game_on_terrarium_tap, LV_EVENT_CLICKED, NULL);
    /* Slingshot needs the raw press/move/release stream, not just clicks. */
    lv_obj_add_event_cb(ui.tap_layer, game_on_terrarium_press,
                        LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ui.tap_layer, game_on_terrarium_pressing,
                        LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(ui.tap_layer, game_on_terrarium_release,
                        LV_EVENT_RELEASED, NULL);
    /* Hold on a placed decor item to open its move/blocking menu. */
    lv_obj_add_event_cb(ui.tap_layer, game_on_terrarium_long_press,
                        LV_EVENT_LONG_PRESSED, NULL);

    terrarium_init(root);
    /* Ambient motes sit above the background but below the creature, so the
     * spider walks in front of them. Created here because they need the
     * background canvas's parent as their layer. */
    ambient_init();
    ui.creature_layer = lv_obj_create(root);
    lv_obj_remove_style_all(ui.creature_layer);
    lv_obj_set_size(ui.creature_layer, ui.W, ui.H);
    lv_obj_set_clickable(ui.creature_layer, false);
    lv_obj_set_scrollable(ui.creature_layer, false);

    /* The creature is a single canvas, not a scene graph. cspider draws the
     * whole spider into one 96x96 buffer each frame; spider_init() now only
     * creates the slingshot aim visuals. */
    cspider_init(ui.creature_layer);
    spider_init(ui.creature_layer);
    terrarium_raise_canopy();

    s_daylight_overlay = lv_obj_create(root);
    lv_obj_remove_style_all(s_daylight_overlay);
    lv_obj_set_size(s_daylight_overlay, ui.W, ui.H);
    lv_obj_set_style_bg_color(s_daylight_overlay, lv_color_hex(0x101b35), 0);
    lv_obj_set_clickable(s_daylight_overlay, false);
    lv_obj_set_scrollable(s_daylight_overlay, false);
    lv_obj_set_hidden(s_daylight_overlay, true);
    for (int i = 0; i < 6; i++) {
        s_weather[i] = lv_obj_create(root);
        lv_obj_remove_style_all(s_weather[i]);
        lv_obj_set_size(s_weather[i], 3, 3);
        lv_obj_set_style_bg_color(s_weather[i], lv_color_hex(0xb1c8b7), 0);
        lv_obj_set_style_bg_opa(s_weather[i], 40, 0);
        lv_obj_set_clickable(s_weather[i], false);
        lv_obj_set_hidden(s_weather[i], true);
    }
    s_visitor = lv_image_create(root);
    const lv_image_dsc_t *visitor = assets_image(sprite_icon_index("visitor"));
    if (visitor) lv_image_set_src(s_visitor, visitor);
    else ESP_LOGE(TAG, "visitor artwork unavailable");
    lv_obj_set_pos(s_visitor, ui.W - 64, ui.H - 64);
    lv_obj_set_clickable(s_visitor, false);
    lv_obj_set_hidden(s_visitor, true);

    s_light_overlay = lv_obj_create(root);
    lv_obj_remove_style_all(s_light_overlay);
    lv_obj_set_size(s_light_overlay, ui.W, ui.H);
    lv_obj_set_style_bg_color(s_light_overlay, lv_color_hex(0x070c20), 0);
    lv_obj_set_style_bg_opa(s_light_overlay, 110, 0);
    lv_obj_set_clickable(s_light_overlay, false);
    lv_obj_set_scrollable(s_light_overlay, false);
    apply_enclosure_lights();
    ui_hud_init(root);

    /* The tap layer must sit BELOW the HUD, or it swallows every button press
     * and the buttons appear dead -- pressing FEED just ran the terrarium tap
     * handler, which fell through to "tap the spider". It is created before the
     * HUD, but LVGL's hit test walks children in reverse creation order, so
     * sending it to the back makes the ordering explicit and independent of
     * creation order. */
    lv_obj_move_background(ui.tap_layer);

    /* --- first-start setup -------------------------------------------- */
    ui_setup_init();

    /* --- audio -------------------------------------------------------- */
    audio_init();
    audio_set_music_volume(g.music_volume);
    audio_set_music_track(g.music_track);
    audio_set_music(g.music_enabled);

    /* Network worker and RTC were initialized before display startup. */
    net_init();
    sd_init();

    /* --- timers ------------------------------------------------------- */
    s_last_logic = lv_tick_get();
    s_last_save  = s_last_logic;

    s_logic_timer  = lv_timer_create(game_tick_logic,  LOGIC_PERIOD_MS,  NULL);
    s_render_timer = lv_timer_create(game_tick_render, RENDER_PERIOD_MS, NULL);

    /* Refresh instrumentation. Registered on the active display so the numbers
     * come from LVGL itself rather than from a guess about the timer period. */
    lv_display_t *disp = lv_display_get_default();
    if (disp) {
        lv_display_add_event_cb(disp, on_refresh_start, LV_EVENT_REFR_START, NULL);
        lv_display_add_event_cb(disp, on_refresh_ready, LV_EVENT_REFR_READY, NULL);
        lv_display_add_event_cb(disp, on_flush_start, LV_EVENT_FLUSH_START, NULL);
        lv_display_add_event_cb(disp, on_invalidate_area, LV_EVENT_INVALIDATE_AREA, NULL);
        lv_display_add_event_cb(disp, on_render_start, LV_EVENT_RENDER_START, NULL);
        lv_display_add_event_cb(disp, on_flush_finish, LV_EVENT_FLUSH_FINISH, NULL);
    }

    /* A brand-new spider needs a name and a colour before the tank opens. */
    if (ui_setup_needed()) {
        ui_setup_show();
    } else {
        lv_screen_load(scr);
    }
    esp_lv_adapter_unlock();

    ESP_LOGI(TAG, "Spidarium ready (%ldx%ld) stage=%s level=%lu",
             (long)ui.W, (long)ui.H, needs_stage_name(),
             (unsigned long)g.level);

    /* Where the RAM went, once everything is up. This is the baseline the
     * budget in docs/memory_budget.md is written against. */
    mem_report_detail("boot complete");
}
