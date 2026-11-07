/*
 * Spidarium — serial test console.
 *
 * A command interpreter on the console UART so the device can be driven and
 * inspected from a script rather than by tapping the screen. See test_console.c
 * for the command list and the reasoning.
 */
#pragma once

/* Start the console task. Call once, after the game is up. */
void test_console_init(void);
