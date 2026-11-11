/*
 * Spidarium — background themes, definitions.
 *
 * Add a palette here and its logical-pixel scene in terrarium.c.
 */
#include "theme.h"

#include "game.h"
#include "save.h"
#include "esp_log.h"

const theme_def_t k_themes[THEME_COUNT] = {
    /* name        lvl  substrate top  bottom     glass      web        dust */
    { "cork",       1, 0x3a3028, 0x332b25, 0x76563b, 0xb5b9a0, 0xa68150 },
    { "moss",       1, 0x302f25, 0x292b22, 0x3e5538, 0xb3bf9d, 0x708052 },
    { "leaf litter",1, 0x362b24, 0x2e261f, 0x735238, 0xb6b69b, 0xa17947 },
    { "sand",       1, 0x76684c, 0x6c6047, 0x5a4530, 0xc0c3a8, 0x9d8052 },
    { "fern",       1, 0x292f25, 0x232921, 0x4a6742, 0xb6c3a1, 0x829564 },
    { "orchid",     1, 0x352c27, 0x2b2521, 0x496144, 0xbdc4a5, 0x817955 },
    { "woodland",   1, 0x302c24, 0x28261f, 0x655039, 0xb7bea0, 0x7f8050 },
    { "minimal",    1, 0x747764, 0x6b705e, 0x4b594a, 0xc4c9aa, 0x87916d },
    { "mushroom",   1, 0x332c22, 0x2b251d, 0x6b4f34, 0xb8bda0, 0x8a7a52 },
    { "gothic",     1, 0x2b2b30, 0x242428, 0x4a4a52, 0xb0b0ba, 0x6e6e78 },
    { "seashell",   1, 0x6f6552, 0x655c4a, 0x5c4a38, 0xc6c4ac, 0x9c8a6c },
    { "succulent",  1, 0x2e332c, 0x272b25, 0x4c5c48, 0xb6c0a8, 0x7e8e6c },
    { "willow",     1, 0x2c3126, 0x252a20, 0x4a5c3e, 0xb8c2a2, 0x7c8e5e },
    { "desert",     1, 0x6e6044, 0x64573d, 0x5a4630, 0xc2bfa2, 0x9a8452 },
    { "crystal",    1, 0x2c3038, 0x252930, 0x4a5460, 0xb4bcc6, 0x74808c },
    { "cemetery",   1, 0x2c302a, 0x252922, 0x4a5244, 0xb2b8a4, 0x76806a },
    { "house",      1, 0x493729, 0x403126, 0x936a42, 0xc4a16d, 0x9a734a },
    { "dark cave",  1, 0x252b3b, 0x1d2231, 0x41586c, 0x9bacc3, 0x558b98 },
    { "night",      1, 0x222c42, 0x1c2438, 0x455b78, 0xa2b4d2, 0xbaa66a },
    { "autumn",     1, 0x463027, 0x3c2923, 0x84513a, 0xc49b69, 0xb2703e },
    { "snow",       1, 0x687d8b, 0x5f7483, 0x7594a8, 0xc5d5d9, 0xa9d0df },
    { "pond",       1, 0x30443a, 0x273b34, 0x3a7067, 0x9ebd9d, 0x5e9f91 },
    { "ruins",      1, 0x423c33, 0x38342d, 0x766c57, 0xb7aa8a, 0x9a8057 },
    { "marsh",      1, 0x363b2c, 0x2d3327, 0x4b6250, 0xa2ad88, 0x6d8661 },
};

static uint8_t s_current;

void theme_init(void)
{
    uint8_t id = 0;
    if (save_background_load(&id)) {
        if (id < THEME_COUNT) {
            s_current = id;
            return;
        }
        ESP_LOGW("spidarium.theme", "stored background %u is invalid; using cork", id);
    }
    s_current = 0;
}

const theme_def_t *theme_get(uint8_t id)
{
    return &k_themes[id % THEME_COUNT];
}

const theme_def_t *theme_current(void)
{
    return theme_get(s_current);
}

static const uint16_t k_costs[THEME_COUNT] = {
    0, 12, 12, 16, 20, 24, 28, 16, 32, 40, 28, 24, 32, 36, 48, 48,
    52, 60, 60, 54, 58, 64, 72, 68
};

uint16_t theme_cost(uint8_t id) { return id < THEME_COUNT ? k_costs[id] : 0; }
bool theme_owned(uint8_t id)
{
    return id < THEME_COUNT && (g.backgrounds_owned & (1u << id));
}

void theme_restore(void)
{
    /* Honour the former separate preference once, without charging for the
     * player's currently equipped habitat during v8 migration. */
    if (g.background_id == 255 && s_current < THEME_COUNT)
        g.background_id = s_current;
    if (g.background_id >= THEME_COUNT) g.background_id = 0;
    g.backgrounds_owned = (g.backgrounds_owned & 0xFFFFFFu) | 1u | (1u << g.background_id);
    s_current = g.background_id;
}

bool theme_unlocked(uint8_t id)
{
    return g.level >= theme_get(id)->unlock_level;
}

bool theme_set(uint8_t id)
{
    if (id >= THEME_COUNT || !theme_unlocked(id)) {
        ESP_LOGW("spidarium.theme", "background rejected: invalid or locked");
        return false;
    }
    uint16_t cost = theme_owned(id) ? 0 : theme_cost(id);
    if (g.silk < cost) {
        ESP_LOGW("spidarium.theme", "background rejected: need %u Silk", cost);
        return false;
    }
    game_state_t snapshot = g;
    g.silk -= cost;
    g.backgrounds_owned |= 1u << id;
    g.background_id = id;
    if (!save_request(&g, sizeof g)) { g = snapshot; return false; }
    s_current = id;
    return true;
}

uint8_t theme_next(void)
{
    /* Walk forward to the next unlocked theme. If none is unlocked beyond the
     * current one, wrap to the first unlocked. */
    for (int i = 1; i <= THEME_COUNT; i++) {
        uint8_t cand = (uint8_t)((s_current + i) % THEME_COUNT);
        if (theme_unlocked(cand) && theme_owned(cand)) {
            if (theme_set(cand)) return s_current;
            break;
        }
    }
    return s_current;
}
