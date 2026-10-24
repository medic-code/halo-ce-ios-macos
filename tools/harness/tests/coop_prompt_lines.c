/* The real button that co-op's spectate and skip-vote lines name: A, or jump's key for the keyboard's player. */
#include "harness.h"
#include <wchar.h>
#include "config.inc"

static char jump_key[32] = "Space";
static int halo_keyboard_prompts(short controller_index)
{
	return controller_index == 0 ? HALO_KEYBOARD_PROMPTS_GAME : HALO_KEYBOARD_PROMPTS_NONE;
}

static void halo_keyboard_action_key(int action, char *name, unsigned int size)
{
	snprintf(name, size, "%s", action == HALO_KEYBOARD_JUMP ? jump_key : "?");
}

#include "under_test.inc"

static void expect(short local_player_index, wchar_t const *expected)
{
	wchar_t name[32];

	next_button_name(local_player_index, name, 32);
	CHECK(!wcscmp(name, expected), "player %d's line named %ls, not %ls", local_player_index, name, expected);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("keyboard")
	{
		expect(0, L"SPACE");
		snprintf(jump_key, sizeof(jump_key), "%s", "Mouse 4");
		expect(0, L"MOUSE 4");
		return 0;
	}
	CASE("controller")
	{
		expect(1, L"A");
		expect(3, L"A");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
