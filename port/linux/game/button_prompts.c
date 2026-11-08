/*
BUTTON_PROMPTS.C

The keys that the HUD's messages and the menus' text name in place of the
controller's buttons, for the player on the keyboard and mouse.
*/

#include "cseries.h"
#include "input/input_abstraction.h"

#include "button_prompts.h"
#include "controller_remap.h"
#include "halo_keyboard.h"

#include <stdio.h>

/* ---------- constants */

/* in the order of hud_messaging.c's and ui_widget.c's enum hud_icon_type */
enum
{
	_prompt_icon_a_button,
	_prompt_icon_b_button,
	_prompt_icon_x_button,
	_prompt_icon_y_button,
	_prompt_icon_black_button,
	_prompt_icon_white_button,
	_prompt_icon_left_trigger,
	_prompt_icon_right_trigger,
	_prompt_icon_dpad_up,
	_prompt_icon_dpad_down,
	_prompt_icon_dpad_left,
	_prompt_icon_dpad_right,
	_prompt_icon_start_button,
	_prompt_icon_back_button,
	_prompt_icon_left_thumb,
	_prompt_icon_right_thumb,
	_prompt_icon_left_stick,
	_prompt_icon_right_stick,
	_prompt_icon_action,
	_prompt_icon_throw_grenade,
	_prompt_icon_primary_trigger,
	_prompt_icon_integrated_light,
	_prompt_icon_jump,
	_prompt_icon_use_equipment,
	_prompt_icon_rotate_weapons,
	_prompt_icon_rotate_grenades,
	_prompt_icon_crouch,
	_prompt_icon_zoom,
	_prompt_icon_accept,
	_prompt_icon_back,
	_prompt_icon_move,
	_prompt_icon_look,
	NUMBER_OF_PROMPT_ICONS
};

#define PROMPT_NONE -1
#define PROMPT_MOVEMENT -2
#define PROMPT_MOUSE -3

#define BUTTON_PROMPT_TEXT_LENGTH 48

/* ---------- globals */

/* %action to %accept: the keyboard's action (%accept is jump's, as the game
draws it); %back, %move and %look: the button the game draws */
static signed char const icon_actions[NUMBER_OF_PROMPT_ICONS - _prompt_icon_action] =
{
	HALO_KEYBOARD_ACTION,
	HALO_KEYBOARD_THROW_GRENADE,
	HALO_KEYBOARD_FIRE,
	HALO_KEYBOARD_FLASHLIGHT,
	HALO_KEYBOARD_JUMP,
	HALO_KEYBOARD_MELEE,
	HALO_KEYBOARD_SWITCH_WEAPON,
	HALO_KEYBOARD_SWITCH_GRENADE,
	HALO_KEYBOARD_CROUCH,
	HALO_KEYBOARD_ZOOM,
	HALO_KEYBOARD_JUMP,
	_prompt_icon_back_button,
	_prompt_icon_left_stick,
	_prompt_icon_right_stick,
};

/* in the order of input_abstraction.c's keyboard_game_controls, which has
no key for Start and Back: here the pause menu's and the scores' */
static signed char const control_actions[] =
{
	HALO_KEYBOARD_JUMP,
	HALO_KEYBOARD_SWITCH_GRENADE,
	HALO_KEYBOARD_ACTION,
	HALO_KEYBOARD_SWITCH_WEAPON,
	HALO_KEYBOARD_MELEE,
	HALO_KEYBOARD_FLASHLIGHT,
	HALO_KEYBOARD_THROW_GRENADE,
	HALO_KEYBOARD_FIRE,
	HALO_KEYBOARD_PAUSE,
	HALO_KEYBOARD_SCOREBOARD,
	HALO_KEYBOARD_CROUCH,
	HALO_KEYBOARD_ZOOM,
};

/* as xinput_sdl.c's keyboard_gamepad presses the buttons */
static char const *const menu_keys[_prompt_icon_right_stick + 1] =
{
	"Enter", "Escape", "Delete", "Tab", NULL, NULL, NULL, NULL,
	"Up", "Down", "Left", "Right", NULL, "F1", NULL, NULL,
	"WASD", NULL,
};

/* ---------- private code */

static void movement_keys(
	char *text,
	unsigned int size)
{
	static int const actions[] = { HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_STRAFE_LEFT,
		HALO_KEYBOARD_MOVE_BACKWARD, HALO_KEYBOARD_STRAFE_RIGHT };
	char keys[NUMBEROF(actions)][BUTTON_PROMPT_TEXT_LENGTH];
	boolean letters = TRUE;
	short index;

	for (index = 0; index < NUMBEROF(actions); index++)
	{
		halo_keyboard_action_key(actions[index], keys[index], sizeof(keys[index]));
		letters = letters && keys[index][0] && !keys[index][1];
	}
	snprintf(text, size, letters ? "%s%s%s%s" : "%s/%s/%s/%s", keys[0], keys[1], keys[2], keys[3]);
}

static short button_action(
	short local_player_index,
	short button)
{
	struct game_input_preferences preferences;
	short control;

	if (button == _prompt_icon_left_stick)
		return PROMPT_MOVEMENT;
	if (button == _prompt_icon_right_stick)
		return PROMPT_MOUSE;
	input_abstraction_get_local_player_preferences(local_player_index, &preferences);
	controller_remap_prompt(local_player_index, &preferences);
	for (control = 0; control < NUMBEROF(control_actions); control++)
	{
		if (preferences.game_control_to_xbox_buttons[control] == button)
			return control_actions[control];
	}
	return PROMPT_NONE;
}

/* ---------- public code */

wchar_t const *button_prompt_text(
	short local_player_index,
	short icon_type)
{
	static wchar_t text[BUTTON_PROMPT_TEXT_LENGTH];
	char name[BUTTON_PROMPT_TEXT_LENGTH];
	int prompts = halo_keyboard_prompts(local_player_index);
	short button = icon_type;
	short action = PROMPT_NONE;
	short index;

	if (prompts == HALO_KEYBOARD_PROMPTS_NONE || icon_type < 0 || icon_type >= NUMBER_OF_PROMPT_ICONS)
		return NULL;
	if (icon_type >= _prompt_icon_back)
		button = icon_actions[icon_type - _prompt_icon_action];
	else if (icon_type >= _prompt_icon_action)
		action = icon_actions[icon_type - _prompt_icon_action];
	name[0] = 0;
	if (prompts == HALO_KEYBOARD_PROMPTS_MENUS && (action == PROMPT_NONE || icon_type == _prompt_icon_accept))
	{
		if (icon_type == _prompt_icon_accept)
			button = _prompt_icon_a_button;
		else if (icon_type == _prompt_icon_back)
			button = _prompt_icon_b_button;
		if (menu_keys[button])
			snprintf(name, sizeof(name), "%s", menu_keys[button]);
	}
	else
	{
		if (action == PROMPT_NONE)
			action = button_action(local_player_index, button);
		if (action == PROMPT_MOVEMENT)
			movement_keys(name, sizeof(name));
		else if (action == PROMPT_MOUSE)
			snprintf(name, sizeof(name), "%s", "Mouse");
		else if (action != PROMPT_NONE)
			halo_keyboard_action_key(action, name, sizeof(name));
	}
	if (!name[0])
		return NULL;
	for (index = 0; name[index] && index < NUMBEROF(text) - 1; index++)
		text[index] = (wchar_t)(unsigned char)name[index];
	text[index] = 0;
	return text;
}

void button_prompt_scores_key(
	short local_player_index,
	wchar_t *text,
	long size)
{
	static wchar_t const button[] = L"BACK";
	wchar_t const *key = button_prompt_text(local_player_index, _prompt_icon_back_button);
	wchar_t rest[BUTTON_PROMPT_TEXT_LENGTH * 2];
	long at;
	long index;

	if (!key || size <= 0)
		return;
	for (at = 0; text[at]; at++)
	{
		for (index = 0; button[index] && text[at + index] == button[index]; index++)
			;
		if (!button[index])
			break;
	}
	if (!text[at])
		return;
	for (index = 0; text[at + NUMBEROF(button) - 1 + index] && index < NUMBEROF(rest) - 1; index++)
		rest[index] = text[at + NUMBEROF(button) - 1 + index];
	rest[index] = 0;
	for (index = 0; key[index] && at < size - 1; index++)
		text[at++] = key[index];
	for (index = 0; rest[index] && at < size - 1; index++)
		text[at++] = rest[index];
	text[at] = 0;
}
