#pragma once

#include "game.h"

#define ACHIEVEMENT_COUNT 20
#define ACHIEVEMENT_SILK 20

typedef struct {
    uint32_t bit;
    const char *name;
    const char *goal;
} achievement_def_t;

static const achievement_def_t ACHIEVEMENTS[ACHIEVEMENT_COUNT] = {
    {ACH_FIRST_MEAL, "First Meal", "Catch and eat one prey"},
    {ACH_FIRST_PET, "First Pet", "Tap the spider gently"},
    {ACH_FIRST_CLEAN, "First Clean", "Clean a dirty enclosure"},
    {ACH_FIRST_PLAY, "First Play", "Finish one minigame"},
    {ACH_FIRST_HUNT, "First Hunt", "Land a successful pounce"},
    {ACH_FIRST_MOLT, "First Molt", "Reach age two days"},
    {ACH_TEN_MEALS, "Ten Meals", "Eat ten prey"},
    {ACH_ADULT, "Adulthood", "Reach age five days"},
    {ACH_LEVEL_5, "Level 5", "Earn 200 total XP"},
    {ACH_SURVIVED_SICK, "Survived Sickness", "Recover with food and mood"},
    {ACH_FIRST_PERFECT, "Perfect Hunt", "Catch prey inside the aim ring"},
    {ACH_TEN_CLEANS, "Tidy Keeper", "Clean ten dirty enclosures"},
    {ACH_TEN_PLAYS, "Playmate", "Finish ten minigames"},
    {ACH_TWENTY_FIVE_MEALS, "Hungry Hunter", "Eat twenty-five prey"},
    {ACH_BOND_50, "Trusted Friend", "Reach bond fifty"},
    {ACH_STREAK_3, "Regular Visitor", "Claim three days in a row"},
    {ACH_DECOR_3, "Interior Designer", "Place three decorations"},
    {ACH_BACKGROUNDS_4, "Scene Collector", "Own four backgrounds"},
    {ACH_FIRST_BORDER, "Framed", "Buy an enclosure border"},
    {ACH_FIRST_GIFT, "Gift Giver", "Place a gift in the enclosure"},
};
