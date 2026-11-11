/*
 * Spidarium — virtual pet jumping spider (Waveshare ESP32-S3 AMOLED 2.16")
 * Public interface for the game module.
 *
 * This header is the shared contract between the engine modules. The single
 * authoritative game state lives in game.c and is exposed as `g` so the
 * behaviour, needs, prey and UI modules can read/write it without threading a
 * pointer through every call.
 */
#pragma once
#include "lvgl.h"
#include "decor.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MAX_PREY   3
#define MAX_POOP   8

/* Gift kinds. There is exactly ONE gift in the tank at a time: the player
 * gives it, and the spider keeps it until it is replaced. This replaces the
 * old 12-slot decor system, which was fiddly to place and read as clutter.
 * Existing kind IDs remain stable as the catalogue grows. */
#define GIFT_KINDS 36
#define NUM_LEGS   8
#define NUM_EYES   8
#define NUM_STATS  4

/* Behaviour state machine. */
typedef enum {
    SPIDER_IDLE = 0,   /* resting, looking around            */
    SPIDER_WANDER,     /* strolling to a random spot         */
    SPIDER_STALK,      /* creeping toward prey               */
    SPIDER_AIM,        /* crouched, waiting for the player   */
    SPIDER_POUNCE,     /* ballistic leap                     */
    SPIDER_EAT,        /* chewing on a caught bug            */
    SPIDER_SLEEP,      /* curled up, regenerating energy     */
    SPIDER_PLAY,       /* hopping after a pet / play         */
    SPIDER_MOLT,       /* shedding the exoskeleton           */
    SPIDER_SICK,       /* neglected: sluggish, dull, refuses prey */
} spider_state_t;

/* Life stage, derived from age. */
typedef enum {
    STAGE_SPIDERLING = 0,
    STAGE_JUVENILE,
    STAGE_ADULT,
} spider_stage_t;

/* Animation clips, indexed into the sprite table. */
typedef enum {
    ANIM_WALK = 0,
    ANIM_IDLE,
    ANIM_EAT,
    ANIM_SLEEP,
    ANIM_POUNCE,
    ANIM_MOLT,
    ANIM_COUNT,
} spider_anim_t;

/* Prey species. */
typedef enum {
    PREY_FLY = 0,
    PREY_CRICKET,
    PREY_MOTH,
    PREY_BEETLE,
    PREY_ROACH,
    PREY_MEALWORM,
    PREY_FRUITFLY,
    PREY_WAXWORM,
    PREY_SPECIES_COUNT,
} prey_species_t;

/* Personality. The table is defined once in game.c; declaring it here as
 * extern keeps a single copy across translation units. */
typedef struct {
    const char *name;
    float aim_window_mult;
    float catch_mult;
    float speed_mult;
    float hunger_decay_mult;
    uint8_t sleep_threshold;
} temperament_t;

#define TEMPERAMENT_COUNT 4

extern const temperament_t k_temperaments[TEMPERAMENT_COUNT];

/* Sound effects. */
typedef enum {
    SFX_EAT = 0,
    SFX_POUNCE,
    SFX_MOLT,
    SFX_TAP,
    SFX_ALERT,
    SFX_AIM,
    SFX_PERFECT,
    SFX_RUSTLE,
    SFX_CHIRP,
    SFX_DROP,
    SFX_COUNT,
} sfx_t;

/* Achievement bits. */
#define ACH_FIRST_MEAL      (1u << 0)
#define ACH_FIRST_PET       (1u << 1)
#define ACH_FIRST_CLEAN     (1u << 2)
#define ACH_FIRST_PLAY      (1u << 3)
#define ACH_FIRST_HUNT      (1u << 4)
#define ACH_FIRST_MOLT      (1u << 5)
#define ACH_TEN_MEALS       (1u << 6)
#define ACH_ADULT           (1u << 7)
#define ACH_LEVEL_5         (1u << 8)
#define ACH_SURVIVED_SICK   (1u << 9)
#define ACH_FIRST_PERFECT   (1u << 10)

/* The single gift the spider has been given. */
typedef struct {
    uint8_t kind;      /* 0..GIFT_KINDS-1 */
    uint8_t variant;   /* per-kind colour/shape variation */
    uint8_t used;      /* 0 = no gift yet */
    int16_t x, y;      /* playfield-local coords */
} gift_t;

/* Everything that survives a reboot. */
typedef struct {
    uint8_t  hunger;    /* 100 = full   */
    uint8_t  mood;      /* 100 = happy  */
    uint8_t  energy;    /* 100 = awake  */
    uint8_t  hygiene;   /* 100 = clean  */
    uint32_t age_ticks; /* 30 s ticks   */
    uint32_t meals;
    uint32_t pets;
    uint32_t cleans;
    uint32_t plays;
    uint32_t xp;
    uint32_t seed;
    int16_t  spider_x, spider_y;
    int16_t  heading_deg;
    gift_t   gift;

    /* --- life cycle / progression --- */
    uint8_t  stage;          /* spider_stage_t                        */
    uint8_t  sick;           /* 1 while neglected                     */
    uint8_t  muted;          /* audio muted                           */
    uint8_t  gift_mode;      /* 1 while placing the gift              */
    uint8_t  gift_selected;  /* chosen gift kind in placement mode    */
    uint32_t level;
    uint32_t achievements;   /* ACH_* bitmask                         */
    uint32_t days_lived;
    uint32_t last_molt_day;

    /* --- runtime scratch (persisted, but reset on load) --- */
    uint8_t  state;           /* spider_state_t                       */
    uint32_t taps;            /* total user interactions              */
    int32_t  eat_until_ms;    /* lv_tick deadline for the EAT state   */
    uint32_t blink_window;    /* next scheduled blink                 */
    uint32_t blink_until;     /* blink currently active until         */
    float    spider_heading;  /* radians, direction of travel         */

    /* --- hunting streak (appended; saves are disposable) --- */
    uint16_t streak;          /* consecutive successful catches       */
    uint16_t best_streak;

    /* --- personality (appended; saves are disposable) --- */
    uint8_t  temperament;     /* 0 bold, 1 timid, 2 greedy, 3 lazy; 0xFF = unset */
    uint16_t bond;            /* 0..100, grows with care and successful hunts */

    /* --- identity (appended; saves are disposable) --- */
    char     name[16];        /* player-chosen name; empty = unnamed */
    uint8_t  colour;          /* body colour index; 0xFF = unset */
    uint8_t  pupil_colour;    /* pupil colour index */
    uint8_t  leg_colour;      /* leg colour index */
    uint8_t  fang_colour;     /* fang colour index */
    uint32_t silk;
    uint32_t gifts_owned;
    uint8_t brightness;
    uint8_t auto_feed;       /* timed wild prey / daily visitors; opt-in */
    uint8_t auto_hunt;       /* unattended stalk / crouch / pounce; opt-in */
    uint8_t volume;          /* speaker volume 0..100 */
    uint32_t last_seen;      /* UTC epoch of the last tick; 0 = never */

    /* --- daily loop (appended; saves are disposable) --- */
    uint32_t daily_day;       /* in-game day the check-in was last claimed; 0 = never */
    uint32_t daily_base_day;  /* in-game day the task counter was snapshotted */
    uint32_t daily_task_base; /* counter value when daily_base_day began */
    uint8_t  daily_streak;    /* consecutive claimed days, capped at DAILY_STREAK_MAX */
    uint8_t  daily_task_done; /* 1 once today's task has been paid */

    /* --- petting budget (appended; saves are disposable) --- */
    uint32_t pet_xp_day;      /* in-game day the pet XP budget was last reset */
    uint8_t  pet_xp_today;    /* XP already paid for petting on that day */

    /* --- placeable decor (appended; saves are disposable) --- */
    uint32_t decor_owned;     /* bitmask of decor_kind_t ever bought */
    decor_item_t decor[DECOR_SLOTS];  /* kind 0xFF = empty slot */
    uint32_t backgrounds_owned;
    uint8_t background_id;
    uint8_t power_mode;       /* balanced, saver, RTC sleep */
    uint8_t screen_timeout;   /* 30s, 1m, 2m, 5m, never */
    uint8_t deep_sleep_after; /* never, 5m, 15m, 30m, 1h */
    uint8_t rtc_wake;         /* 15m, 1h, 6h */
    /* Former padding: low nibble gifts 32..35, high nibble decor 64..67. */
    uint8_t catalogue_owned_high;
    uint16_t backgrounds_owned_extra; /* backgrounds 32..47 */
    uint32_t decor_owned_extra;
    uint8_t music_enabled;
    uint8_t lights_dimmed;
    uint8_t music_volume;
    uint8_t music_track;
    uint32_t decor_extra_count;
    uint32_t borders_owned;
    uint16_t borders_owned_extra;
    uint8_t border_id;        /* 255 = no frame; otherwise background theme ID */
} game_state_t;

_Static_assert(sizeof(game_state_t) == 268, "save payload ABI changed");
_Static_assert(offsetof(game_state_t, borders_owned) == 260, "border ownership ABI changed");
_Static_assert(offsetof(game_state_t, border_id) == 266, "border selection ABI changed");
_Static_assert(offsetof(game_state_t, catalogue_owned_high) == 245, "catalogue ownership ABI changed");
_Static_assert(offsetof(game_state_t, backgrounds_owned_extra) == 246, "background extension ABI changed");
_Static_assert(offsetof(game_state_t, decor_owned_extra) == 248, "extra ownership ABI changed");
_Static_assert(offsetof(game_state_t, decor_extra_count) == 256, "extra decor ABI changed");
_Static_assert(offsetof(game_state_t, music_volume) == 254, "music volume ABI changed");
_Static_assert(offsetof(game_state_t, music_track) == 255, "music track ABI changed");
_Static_assert(offsetof(game_state_t, backgrounds_owned) == 236, "background ownership ABI changed");
_Static_assert(offsetof(game_state_t, background_id) == 240, "background selection ABI changed");
_Static_assert(offsetof(game_state_t, brightness) == 128, "brightness ABI changed");
_Static_assert(offsetof(game_state_t, auto_feed) == 129, "auto_feed ABI changed");
_Static_assert(offsetof(game_state_t, auto_hunt) == 130, "auto_hunt ABI changed");
_Static_assert(offsetof(game_state_t, volume) == 131, "volume ABI changed");
_Static_assert(offsetof(game_state_t, last_seen) == 132, "last_seen ABI changed");
_Static_assert(offsetof(game_state_t, daily_day) == 136, "daily_day ABI changed");
_Static_assert(offsetof(game_state_t, daily_base_day) == 140, "daily_base_day ABI changed");
_Static_assert(offsetof(game_state_t, daily_task_base) == 144, "daily_task_base ABI changed");
_Static_assert(offsetof(game_state_t, daily_streak) == 148, "daily_streak ABI changed");
_Static_assert(offsetof(game_state_t, daily_task_done) == 149, "daily_task_done ABI changed");
_Static_assert(offsetof(game_state_t, pet_xp_day) == 152, "pet_xp_day ABI changed");
_Static_assert(offsetof(game_state_t, pet_xp_today) == 156, "pet_xp_today ABI changed");
_Static_assert(offsetof(game_state_t, decor_owned) == 160, "decor_owned ABI changed");
_Static_assert(offsetof(game_state_t, decor) == 164, "decor ABI changed");

/* Live LVGL object handles. */
typedef struct {
    lv_obj_t *screen;
    lv_obj_t *creature_layer;
    lv_obj_t *bg_canvas;
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *stage_label;
    lv_obj_t *age_label;
    lv_obj_t *bar_track[NUM_STATS];
    lv_obj_t *bar_fill[NUM_STATS];
    lv_obj_t *stat_food, *stat_mood, *stat_spark;   /* aliases into bar_fill[] */
    lv_obj_t *btn_feed, *btn_clean, *btn_play, *btn_decor;
    lv_obj_t *btn_sleep, *btn_status, *btn_theme;
    lv_obj_t *tap_layer;
    lv_obj_t *spider;          /* sprite object (pettable)               */
    lv_obj_t *crumb;           /* legacy food bug (unused with sprites)  */
    lv_obj_t *abdomen, *cephalothorax;
    lv_obj_t *legs[NUM_LEGS];
    lv_obj_t *eyes[NUM_EYES];
    lv_obj_t *fang_l, *fang_r;
    lv_obj_t *gift;            /* the single gift object                 */
    lv_obj_t *poop[MAX_POOP];
    lv_obj_t *prey[MAX_PREY];
    lv_obj_t *bubble;
    int32_t W, H;
    int32_t bar_w;
} game_ui_t;

/* Playfield rectangle the spider and prey are confined to. */
typedef struct {
    int32_t left, right, top, bottom;
} playfield_t;

/* The single authoritative state, defined in game.c. */
extern game_state_t g;
extern game_ui_t    ui;
extern playfield_t  g_play;

/* Decor placement mode. Runtime-only, so it lives outside game_state_t and
 * never changes the save payload. */
extern uint8_t g_decor_mode;      /* 1 while placing decor */
extern uint8_t g_decor_selected;  /* chosen decor kind in placement mode */
/* Slot being repositioned, or 0xFF when not moving. */
extern int g_decor_move_slot;
void game_add_silk(uint32_t amount);

void game_init(void);

/* The active temperament. Never NULL; falls back to bold if the stored value
 * is out of range. */
const temperament_t *game_temperament(void);
void game_tick_logic(lv_timer_t *timer);   /* 20 Hz */
void game_tick_render(lv_timer_t *timer);  /* 30 Hz */

/* LVGL-task-only gameplay pause; service timer remains alive.
 * Network workers use esp_lv_adapter_pause/resume, not these functions. */
void game_pause(void);
void game_resume(void);
bool game_paused(void);

/* LVGL 9 event callbacks. */
void game_on_feed(lv_event_t *e);
void game_on_clean(lv_event_t *e);
void game_on_play(lv_event_t *e);
void game_on_sleep(lv_event_t *e);
void game_on_gift(lv_event_t *e);
void game_on_menu(lv_event_t *e);
/* Hold on a placed decor item to open its move/blocking menu. */
void game_on_terrarium_long_press(lv_event_t *e);

/* Open a menu by name, for the serial console. Returns false if the name is
 * unknown. Calls the same menu_open() the UI calls, so a test through here
 * exercises the real path. */
bool game_open_menu(const char *name);

/* UI actions the serial console can request.
 *
 * The console runs on its own task, so it cannot call lv_* directly. It posts
 * one of these and the render tick performs it on the LVGL task. */
typedef enum {
    GAME_UI_NONE = 0,
    GAME_UI_FEED,
    GAME_UI_CLEAN,
    GAME_UI_PLAY,
    GAME_UI_SLEEP,
    GAME_UI_MENU,
    GAME_UI_SAVE,
    GAME_UI_TEST_GIFTS,
    GAME_UI_REFRESH,
    GAME_UI_RENDER,
    GAME_UI_SPAWN,
    GAME_UI_SPECKLE,
    GAME_UI_STATE,
    GAME_UI_SET_HUNGER,
    GAME_UI_SET_MOOD,
    GAME_UI_SET_ENERGY,
    GAME_UI_SET_HYGIENE,
    GAME_UI_SET_BOND,
    GAME_UI_SET_SICK,
    GAME_UI_SET_AUTO_FEED,
    GAME_UI_SET_AUTO_HUNT,
    GAME_UI_SET_VOLUME,
    GAME_UI_SET_MUSIC,
    GAME_UI_SET_MUSIC_VOLUME,
    GAME_UI_SET_MUSIC_TRACK,
    GAME_UI_SET_MUTED,
    GAME_UI_TEST_UI,
    GAME_UI_BACKGROUND,
    GAME_UI_TEST_HUNT,
    GAME_UI_TEST_GAIT,
    GAME_UI_TEST_PREY,
    GAME_UI_SD_BACKUP,
    GAME_UI_SD_RESTORE,
    GAME_UI_WAKE,
    GAME_UI_SCANLOOP_BEGIN,
    GAME_UI_SCANLOOP_END,
    GAME_UI_SCREEN_CAPTURE,
    GAME_UI_SCREEN_SD,
    GAME_UI_SCREEN_TEST,
    GAME_UI_SCREEN_RELEASE,
    GAME_UI_DECOR_TEST,
    GAME_UI_DECOR_EDIT,
    GAME_UI_SETUP_DONE,
    GAME_UI_DEEP_SLEEP,
    GAME_UI_BORDER,
} game_ui_action_t;
typedef enum {
    DECOR_EDIT_STATUS, DECOR_EDIT_CLEAR, DECOR_EDIT_PLACE, DECOR_EDIT_MOVE, DECOR_EDIT_BLOCK
} decor_edit_t;
bool game_ui_decor_edit(decor_edit_t operation, int value, int x, int y, int *count);
const lv_draw_buf_t *game_screen_capture(void);

/* Request a UI action. Blocks until the render tick has performed it, bounded
 * so a stalled render timer cannot hang the caller. */
bool game_ui_call(game_ui_action_t action);
bool game_ui_call_value(game_ui_action_t action, int value);

/* Open a menu by name from the console, marshalled onto the LVGL task.
 * Returns false if the name is unknown. */
bool game_ui_open_menu(const char *name);
/* Open the per-item decor menu for a slot. Returns false when the slot is
 * empty. Used by the serial console. */
bool game_ui_open_decor_item(int slot);

/* Called by the HUD gift picker when the player chooses a kind. */
void game_gift_chosen(uint8_t kind);

/* Leave decor placement mode without placing anything. */
void game_cancel_decor_placement(void);

/* WiFi menu actions, marshalled from menu callbacks onto the LVGL task. */
void game_ui_start_wifi_scan(void);
void game_ui_connect_wifi(int scan_index);
void game_ui_forget_wifi(void);
void game_ui_open_wifi_password_entry(void);

void game_on_terrarium_tap(lv_event_t *e);
void game_on_terrarium_press(lv_event_t *e);
void game_on_terrarium_pressing(lv_event_t *e);
void game_on_terrarium_release(lv_event_t *e);

/* Helpers shared across modules. */
uint32_t game_now(void);
void game_add_xp(uint32_t amount);
void game_award(uint32_t achievement);
bool game_save_now(void);

/* ------------------------------------------------------------------ */
/* Daily loop                                                          */
/* ------------------------------------------------------------------ */
/* The daily loop gives the player a reason to open the app tomorrow: a
 * check-in whose reward grows with a consecutive-day streak, plus one rotating
 * task per in-game day. Both are driven from needs_tick(), so they advance with
 * the same compressed clock as aging and decay. */

/* The in-game day the daily loop is currently on. */
uint32_t needs_daily_today(void);

/* True when today's check-in has not been claimed yet. */
bool needs_daily_available(void);

/* Claim today's check-in. Returns the XP paid (0 if already claimed). */
uint32_t needs_daily_claim(void);

/* The rotating task for today: 0..DAILY_TASK_COUNT-1. */
uint8_t needs_daily_task(void);

/* Human-readable name of a task, for the menu and the console. */
const char *needs_daily_task_name(uint8_t task);

/* Progress toward today's task, 0..needs_daily_task_goal(). */
uint32_t needs_daily_task_progress(void);
uint32_t needs_daily_task_goal(void);

/* True when today's task is complete but unpaid. */
bool needs_daily_task_ready(void);
/* Pay today's task if it is complete. Returns the XP paid (0 if not ready). */
uint32_t needs_daily_task_claim(void);

/* Force the in-game day, for the serial console. Snapshots the task baseline
 * exactly as a real rollover does. */
void needs_daily_force_day(uint32_t day);

/* Drop the console day override so the real clock takes over again. */
void needs_daily_clear_override(void);

/* ------------------------------------------------------------------ */
/* Treats: the recurring Silk sink                                     */
/* ------------------------------------------------------------------ */
/* Gifts are permanent and one-time, so once all eighteen are owned Silk has
 * nothing left to buy. Treats cost Silk every time and give a short boost. */
typedef enum {
    TREAT_HUNGER = 0,
    TREAT_MOOD,
    TREAT_ENERGY,
    TREAT_MOTH,
    TREAT_DEW,
    TREAT_NEST,
    TREAT_MOON,
    TREAT_AMBER,
    TREAT_NEON,
    TREAT_STAR,
    TREAT_LOTUS,
    TREAT_RUNE,
} treat_t;

uint32_t treat_cost(treat_t kind);
const char *treat_name(treat_t kind);
/* Buy and apply a treat. Returns false (and logs why) if Silk is short. */
bool treat_buy(treat_t kind);

/* The physical volume buttons changed the level. If the Sound menu is open,
 * push the new value into its slider and percentage readout. Must be called on
 * the LVGL task. */
void game_volume_changed(void);
