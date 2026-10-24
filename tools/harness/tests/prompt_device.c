/* The real choice of the device that controller 1's button prompts name, over fake keyboard and gamepad reads. */
#include "harness.h"
typedef int BOOL;
#define SDL_SCANCODE_COUNT 512
#define PLATFORM_MOUSE_BUTTON_COUNT 8
struct platform_input_state
{
	unsigned char keys[SDL_SCANCODE_COUNT], mouse_buttons[PLATFORM_MOUSE_BUTTON_COUNT];
	float mouse_dx, mouse_dy, mouse_wheel;
	BOOL menus;
};
#include "config.inc"
#include "under_test.inc"

static struct platform_input_state input;
static int gamepad_inputs;

/* a read of port 0, as prompt_devices_read makes it */
static void read_port(void)
{
	prompt_device_update(keyboard_inputs_held(&input), gamepad_inputs);
	prompts_in_menus = input.menus;
}

static void key(int scancode, int down)
{
	input.keys[scancode] = (unsigned char)down;
	read_port();
}

static void pad(int held)
{
	gamepad_inputs = held;
	read_port();
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("starts-keyboard")
	{
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_GAME, "a desktop build did not start with keys");
		read_port();
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_GAME, "a read with nothing pressed changed it");
		return 0;
	}
	CASE("gamepad-then-key")
	{
		pad(1);
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_NONE, "a gamepad button kept the keys");
		pad(0);
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_NONE, "letting go of the button changed it");
		key(8, 1);
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_GAME, "a key press kept the controller's");
		key(8, 0);
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_GAME, "letting go of the key changed it");
		return 0;
	}
	CASE("held-stick")
	{
		/* a stick held while a key is pressed: the key is the newer press */
		pad(1);
		key(26, 1);
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_GAME, "a key press did not take the prompts");
		pad(1);
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_GAME, "a held stick took them back");
		pad(2);
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_NONE, "a second gamepad press did not");
		return 0;
	}
	CASE("held-key")
	{
		key(26, 1);
		pad(1);
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_NONE, "a gamepad press did not take the prompts");
		key(26, 1);
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_NONE, "a held key took them back");
		return 0;
	}
	CASE("mouse")
	{
		pad(1);
		pad(0);
		input.mouse_dx = 40.0f;
		read_port();
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_NONE, "moving the mouse switched to keys");
		input.mouse_wheel = 1.0f;
		read_port();
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_GAME, "the wheel did not switch to keys");
		input.mouse_wheel = 0.0f;
		pad(1);
		input.mouse_buttons[1] = 1;
		read_port();
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_GAME, "a mouse button did not switch to keys");
		return 0;
	}
	CASE("both-at-once")
	{
		gamepad_inputs = 1;
		key(8, 1);
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_GAME, "the keyboard did not win a tie");
		return 0;
	}
	CASE("other-controllers")
	{
		key(8, 1);
		CHECK(halo_keyboard_prompts(1) == HALO_KEYBOARD_PROMPTS_NONE, "controller 2 was shown keys");
		CHECK(halo_keyboard_prompts(3) == HALO_KEYBOARD_PROMPTS_NONE, "controller 4 was shown keys");
		CHECK(halo_keyboard_prompts(NONE) == HALO_KEYBOARD_PROMPTS_NONE, "no controller was shown keys");
		return 0;
	}
	CASE("menus")
	{
		input.menus = TRUE;
		key(8, 1);
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_MENUS, "a menu up did not name the menus' keys");
		input.menus = FALSE;
		read_port();
		CHECK(halo_keyboard_prompts(0) == HALO_KEYBOARD_PROMPTS_GAME, "the game did not name the bindings");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
