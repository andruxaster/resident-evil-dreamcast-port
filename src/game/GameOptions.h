// GameOptions.h - the title screen's OPTIONS menu.
//
// Port-added (no original address). A state of its own, chained from
// title_state when the OPTIONS row is confirmed and chaining back to it when
// it is left - not a sub-state of the title. In game, ingame_options_menu
// (below) takes the place of the original's option screen (OptionsMenu.cpp,
// 0x004761b0), which stays in the tree but is no longer opened.
//
// Four pages, drawn in the 8x8 font on a black screen:
//
//   DISPLAY  window mode, resolution, vsync
//   INPUT    every control, for the keyboard and for the gamepad
//   GAME     the [Game] keys of config.ini and the asset region; an entry
//            whose assets are not installed cannot be selected
//   BONUS    the outfit each playable character wears (with the model shown
//            beside the list) and the bonus weapons a new game starts with
//
// Everything is written back when the menu is left: config.ini for the first
// three pages, SAVE/bonus.dat for the last (Bonus.h).
#pragma once

void game_options_state(void);

// TitleScreen.cpp: makes the next title_state open on its menu, cursor on
// OPTIONS, instead of on PRESS START BUTTON.
void title_resume_on_menu(void);

// GameLoop.cpp check_menus_state: the in-game options (DISPLAY, INPUT, BACK TO
// TITLE), opened by the original's OPTIONS chord instead of options_menu.
void ingame_options_menu(void);
