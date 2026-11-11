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
    { "fairy glade", 1, 0x344338, 0x2c3930, 0x5d805b, 0xc5c69c, 0xb497c1 },
    { "moonflower grove", 1, 0x30374c, 0x293041, 0x5d698a, 0xb9c4d2, 0xb9a2c2 },
    { "cathedral", 1, 0x3c3547, 0x332d3e, 0x645572, 0xc4b3c9, 0x9b6b85 },
    { "rose courtyard", 1, 0x44343c, 0x3a2d34, 0x755865, 0xc9a8b1, 0x9e657d },
    { "library", 1, 0x493b31, 0x3f332b, 0x816248, 0xcbb18a, 0x9d855c },
    { "observatory", 1, 0x30394c, 0x293142, 0x5d6e87, 0xb1c3d6, 0xb99c66 },
    { "kitchen", 1, 0x5b5144, 0x51483c, 0x9b8466, 0xd7c3a2, 0xae977a },
    { "sewing room", 1, 0x4c3b42, 0x42343a, 0x896e80, 0xd3b6c0, 0xad8a9b },
    { "witch garden", 1, 0x303c35, 0x29332d, 0x596c51, 0xb0bd93, 0xa28bba },
    { "moon altar", 1, 0x3a324b, 0x322b41, 0x6a587c, 0xc4b1d1, 0xab86bd },
    { "primeval jungle", 1, 0x35402d, 0x2d3727, 0x657847, 0xb7bd87, 0x9b9257 },
    { "amber basin", 1, 0x4e3b2b, 0x443326, 0x86623d, 0xd5b47c, 0xb18a43 },
    { "neon alley", 1, 0x2d3446, 0x262d3c, 0x596682, 0xb2c5d2, 0xb46ca4 },
    { "circuit garden", 1, 0x2e3e3d, 0x273535, 0x527b71, 0x9fcaba, 0x74a393 },
    { "lunar base", 1, 0x4b505b, 0x424650, 0x7d8599, 0xc4ccda, 0x95acba },
    { "starship", 1, 0x353c4e, 0x2e3444, 0x69768d, 0xb6c9d8, 0x6fa5ae },
    { "enchanted court", 1, 0x424034, 0x39372d, 0x7c7455, 0xc7c098, 0xa58bb6 },
    { "dragon heath", 1, 0x4a3834, 0x40302d, 0x825c50, 0xc5aa85, 0xa0745b },
    { "lost city", 1, 0x40473c, 0x373d34, 0x75816a, 0xc2c3a1, 0x9ca16a },
    { "sunken temple", 1, 0x304548, 0x293c3f, 0x557c7e, 0xa2c8bf, 0x78a4a0 },
    { "zen garden", 1, 0x676352, 0x5d594a, 0x8e8a70, 0xd3ceb0, 0xa8a085 },
    { "bamboo grove", 1, 0x3d4836, 0x353e2f, 0x71865b, 0xc4cc9d, 0x9ca776 },
    { "sakura courtyard", 1, 0x51424a, 0x473940, 0x886b7a, 0xd7bac9, 0xb78ba6 },
    { "cloud sanctuary", 1, 0x555d70, 0x4b5364, 0x8795af, 0xd0d8e6, 0xb1bdd3 },
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
    52, 60, 60, 54, 58, 64, 72, 68,
    64, 72, 80, 74, 62, 78, 54, 58, 68, 82, 72, 76,
    84, 88, 90, 96, 86, 92, 94, 98, 70, 74, 78, 100
};

uint16_t theme_cost(uint8_t id) { return id < THEME_COUNT ? k_costs[id] : 0; }
bool theme_owned(uint8_t id)
{
    if (id >= THEME_COUNT) return false;
    return id < 32 ? (g.backgrounds_owned & (1u << id)) != 0 :
        (g.backgrounds_owned_extra & (1u << (id - 32))) != 0;
}

void theme_restore(void)
{
    /* Honour the former separate preference once, without charging for the
     * player's currently equipped habitat during v8 migration. */
    if (g.background_id == 255 && s_current < THEME_COUNT)
        g.background_id = s_current;
    if (g.background_id >= THEME_COUNT) g.background_id = 0;
    g.backgrounds_owned |= 1u;
    if (g.background_id < 32) g.backgrounds_owned |= 1u << g.background_id;
    else g.backgrounds_owned_extra |= 1u << (g.background_id - 32);
    s_current = g.background_id;
    if (g.border_id != 255 && !border_owned(g.border_id)) {
        ESP_LOGW("spidarium.theme", "unowned/invalid border %u removed", g.border_id);
        g.border_id = 255;
    }
}

bool border_owned(uint8_t id)
{
    if (id >= THEME_COUNT) return false;
    return id < 32 ? (g.borders_owned & (1u << id)) != 0 :
        (g.borders_owned_extra & (1u << (id - 32))) != 0;
}

uint16_t border_cost(uint8_t id)
{
    return id < THEME_COUNT ? 10 + theme_cost(id) / 2 : 0;
}

bool border_set(uint8_t id)
{
    if (id != 255 && id >= THEME_COUNT) {
        ESP_LOGW("spidarium.theme", "border rejected: invalid ID %u", id);
        return false;
    }
    uint16_t cost = id == 255 || border_owned(id) ? 0 : border_cost(id);
    if (g.silk < cost) {
        ESP_LOGW("spidarium.theme", "border rejected: need %u Silk", cost);
        return false;
    }
    game_state_t snapshot = g;
    g.silk -= cost;
    g.border_id = id;
    if (id != 255) {
        if (id < 32) g.borders_owned |= 1u << id;
        else g.borders_owned_extra |= 1u << (id - 32);
    }
    if (!save_request(&g, sizeof g)) { g = snapshot; return false; }
    return true;
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
    if (id < 32) g.backgrounds_owned |= 1u << id;
    else g.backgrounds_owned_extra |= 1u << (id - 32);
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
