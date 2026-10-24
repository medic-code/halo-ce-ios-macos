/* The real keys that the button prompts name for the keyboard's player, over fake bindings and button layouts. */
#include "harness.h"
#include <wchar.h>
#define NUMBEROF(array) ((int)(sizeof(array) / sizeof((array)[0])))
#include "config.inc"

/* the bindings as numbers, and their names as Controls Setup shows them */
enum { KEY_NONE = -1, KEY_W = 1, KEY_A, KEY_S, KEY_D, KEY_SPACE, KEY_E, KEY_Q, KEY_F, KEY_G, KEY_R, KEY_Z, KEY_X, KEY_C,
	KEY_TAB, KEY_ESCAPE, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_F10, KEY_V, KEY_T, KEY_Y, KEY_1, KEY_LCTRL,
	MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE, WHEEL };
static char const *const key_names[] = { "", "W", "A", "S", "D", "Space", "E", "Q", "F", "G", "R", "Z", "X", "C",
	"Tab", "Escape", "Up", "Down", "Left", "Right", "F10", "V", "T", "Y", "1", "Left Ctrl",
	"Mouse Left", "Mouse Right", "Mouse Middle", "Wheel" };
static int bindings[NUMBER_OF_HALO_KEYBOARD_ACTIONS][MAXIMUM_BINDINGS];

static void halo_input_name(int input, char *name, size_t size)
{
	snprintf(name, size, "%s", input > 0 && input < NUMBEROF(key_names) ? key_names[input] : "");
}

/* (the layout's label of a key: as named, on a US layout) */
static void halo_input_shown_name(const char *binding, char *shown, size_t size)
{
	snprintf(shown, size, "%s", binding);
}

static int keyboard_prompts = HALO_KEYBOARD_PROMPTS_GAME;
static int halo_keyboard_prompts(short controller_index)
{
	return controller_index == 0 ? keyboard_prompts : HALO_KEYBOARD_PROMPTS_NONE;
}

static struct game_input_preferences layout;
static void input_abstraction_get_local_player_preferences(short local_player_index,
	struct game_input_preferences *preferences)
{
	*preferences = layout;
}

#include "under_test.inc"

static void default_bindings(void)
{
	static int const defaults[NUMBER_OF_HALO_KEYBOARD_ACTIONS][MAXIMUM_BINDINGS] =
	{
		{ KEY_W, KEY_NONE }, { KEY_S, KEY_NONE }, { KEY_A, KEY_NONE }, { KEY_D, KEY_NONE }, { KEY_SPACE, KEY_NONE },
		{ KEY_LCTRL, KEY_C }, { MOUSE_LEFT, KEY_NONE }, { MOUSE_RIGHT, KEY_G }, { KEY_F, KEY_NONE }, { KEY_R, KEY_NONE },
		{ KEY_Z, MOUSE_MIDDLE }, { WHEEL, KEY_1 }, { KEY_X, KEY_NONE }, { KEY_E, KEY_NONE }, { KEY_Q, KEY_NONE },
		{ KEY_TAB, KEY_NONE }, { KEY_ESCAPE, KEY_NONE }, { KEY_F10, KEY_NONE }, { KEY_V, KEY_NONE }, { KEY_T, KEY_NONE },
		{ KEY_Y, KEY_NONE },
	};

	memcpy(bindings, defaults, sizeof(bindings));
}

/* the Xbox's standard layout (input_abstraction.c's defaults) */
static void standard_layout(void)
{
	static byte const buttons[12] = { _prompt_icon_a_button, _prompt_icon_black_button, _prompt_icon_x_button,
		_prompt_icon_y_button, _prompt_icon_b_button, _prompt_icon_white_button, _prompt_icon_left_trigger,
		_prompt_icon_right_trigger, _prompt_icon_start_button, _prompt_icon_back_button, _prompt_icon_left_thumb,
		_prompt_icon_right_thumb };

	memcpy(layout.game_control_to_xbox_buttons, buttons, sizeof(buttons));
}

static void expect(short icon, wchar_t const *expected)
{
	wchar_t const *text = button_prompt_text(0, icon);

	if (!expected)
		CHECK(!text, "icon %d named a key, %ls", icon, text);
	else
		CHECK(text && !wcscmp(text, expected), "icon %d named %ls, not %ls", icon, text ? text : L"(the icon)", expected);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	short icon;

	default_bindings();
	standard_layout();
	CASE("controller-unchanged")
	{
		keyboard_prompts = HALO_KEYBOARD_PROMPTS_NONE;
		for (icon = -1; icon <= NUMBER_OF_PROMPT_ICONS + 8; icon++)
			expect(icon, NULL);
		return 0;
	}
	CASE("other-players")
	{
		for (icon = 0; icon < NUMBER_OF_PROMPT_ICONS; icon++)
			CHECK(!button_prompt_text(1, icon), "player 2 was shown a key for icon %d", icon);
		return 0;
	}
	CASE("action-keys")
	{
		expect(_prompt_icon_action, L"E");
		expect(_prompt_icon_throw_grenade, L"Mouse Right");
		expect(_prompt_icon_primary_trigger, L"Mouse Left");
		expect(_prompt_icon_integrated_light, L"Q");
		expect(_prompt_icon_jump, L"Space");
		expect(_prompt_icon_use_equipment, L"F");
		expect(_prompt_icon_rotate_weapons, L"Wheel");
		expect(_prompt_icon_rotate_grenades, L"X");
		expect(_prompt_icon_crouch, L"Left Ctrl");
		expect(_prompt_icon_zoom, L"Z");
		expect(_prompt_icon_accept, L"Space");
		expect(_prompt_icon_move, L"WASD");
		expect(_prompt_icon_look, L"Mouse");
		return 0;
	}
	CASE("rebound")
	{
		bindings[HALO_KEYBOARD_ACTION][0] = KEY_F;
		expect(_prompt_icon_action, L"F");
		/* the second binding when the first is cleared */
		bindings[HALO_KEYBOARD_ZOOM][0] = KEY_NONE;
		expect(_prompt_icon_zoom, L"Mouse Middle");
		bindings[HALO_KEYBOARD_MOVE_FORWARD][0] = KEY_UP;
		bindings[HALO_KEYBOARD_STRAFE_LEFT][0] = KEY_LEFT;
		bindings[HALO_KEYBOARD_MOVE_BACKWARD][0] = KEY_DOWN;
		bindings[HALO_KEYBOARD_STRAFE_RIGHT][0] = KEY_RIGHT;
		expect(_prompt_icon_move, L"Up/Left/Down/Right");
		return 0;
	}
	CASE("unbound")
	{
		bindings[HALO_KEYBOARD_FLASHLIGHT][0] = KEY_NONE;
		expect(_prompt_icon_integrated_light, L"FLASHLIGHT");
		bindings[HALO_KEYBOARD_SWITCH_WEAPON][0] = KEY_NONE;
		bindings[HALO_KEYBOARD_SWITCH_WEAPON][1] = KEY_NONE;
		expect(_prompt_icon_rotate_weapons, L"SWITCH WEAPON");
		return 0;
	}
	CASE("buttons-in-game")
	{
		expect(_prompt_icon_x_button, L"E");
		expect(_prompt_icon_a_button, L"Space");
		expect(_prompt_icon_right_trigger, L"Mouse Left");
		expect(_prompt_icon_start_button, L"Escape");
		expect(_prompt_icon_back_button, L"Tab");
		expect(_prompt_icon_back, L"Tab");
		expect(_prompt_icon_left_stick, L"WASD");
		expect(_prompt_icon_right_stick, L"Mouse");
		expect(_prompt_icon_dpad_up, NULL);
		/* Bumper Jumper: action on B */
		layout.game_control_to_xbox_buttons[2] = _prompt_icon_b_button;
		layout.game_control_to_xbox_buttons[4] = _prompt_icon_x_button;
		expect(_prompt_icon_b_button, L"E");
		expect(_prompt_icon_x_button, L"F");
		/* the action's own icon does not follow the layout */
		expect(_prompt_icon_action, L"E");
		return 0;
	}
	CASE("menus")
	{
		keyboard_prompts = HALO_KEYBOARD_PROMPTS_MENUS;
		expect(_prompt_icon_a_button, L"Enter");
		expect(_prompt_icon_b_button, L"Escape");
		expect(_prompt_icon_x_button, L"Delete");
		expect(_prompt_icon_y_button, L"Tab");
		expect(_prompt_icon_back_button, L"F1");
		expect(_prompt_icon_dpad_left, L"Left");
		expect(_prompt_icon_accept, L"Enter");
		expect(_prompt_icon_back, L"Escape");
		expect(_prompt_icon_right_stick, NULL);
		expect(_prompt_icon_left_trigger, NULL);
		/* an action names its binding still */
		expect(_prompt_icon_action, L"E");
		return 0;
	}
	CASE("custom-icons")
	{
		for (icon = NUMBER_OF_PROMPT_ICONS; icon < NUMBER_OF_PROMPT_ICONS + 8; icon++)
			expect(icon, NULL);
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
