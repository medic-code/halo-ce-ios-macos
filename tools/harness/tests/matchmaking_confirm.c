/* MATCHMAKING_CONFIRM.C (test): MATCHMAKING's YES and NO for X's removal and LEAVE PARTY (menu_functions.c) */

#include "harness.h"
#include "enums.inc"

struct widget_instance
{
	char const *name;
	struct widget_instance *parent, *child, *next, *focused_child;
	union { struct { short selected_index; } list; } parameters;
};

enum { SOUND_FORWARD, SOUND_BACK };
#define MATCHMAKING_ROWS 2

static struct
{
	short asking, asked;
	char asked_name[P2P_PARTY_NAME_SIZE + 1];
	struct widget_instance *asked_from, *asked_button;
} matchmaking_screen;

static struct p2p_party_status party;
static int left, removed = NONE;

static struct widget_instance *named(struct widget_instance *widget, char const *name, long nth)
{
	struct widget_instance *child, *found;

	(void)nth;
	if (!strcmp(widget->name, name))
		return widget;
	for (child = widget->child; child; child = child->next)
		if ((found = named(child, name, 0)))
			return found;
	return NULL;
}

static struct widget_instance *screen_of(struct widget_instance *widget)
{
	while (widget->parent)
		widget = widget->parent;
	return widget;
}

static void ui_play_audio_feedback_sound(int sound) { (void)sound; }
static boolean campaign_fail(void) { return FALSE; }
static void p2p_party_get_status(struct p2p_party_status *status) { *status = party; }
static void p2p_party_leave(void) { left++; }
static void p2p_party_notice_clear(void) { }
static void p2p_party_remove(int index) { removed = index; }

#include "under_test.inc"

static struct widget_instance screen, list, rows[MATCHMAKING_ROWS + 3], bar, buttons[4];
static char const *const button_names[] = { "button_invite", "button_search", "button_yes", "button_no" };

static void setup(void)
{
	int index;

	memset(&screen, 0, sizeof(screen));
	screen.name = "screen";
	screen.child = &list;
	list = (struct widget_instance){ .name = "options_menu", .parent = &screen, .child = &rows[0] };
	for (index = 0; index < MATCHMAKING_ROWS + 3; index++)
		rows[index] = (struct widget_instance){ .name = "row", .parent = &list, .next = &rows[index + 1] };
	rows[MATCHMAKING_ROWS + 2].next = &bar;
	bar = (struct widget_instance){ .name = "button_bar", .parent = &list, .child = &buttons[0] };
	for (index = 0; index < 4; index++)
		buttons[index] = (struct widget_instance){ .name = button_names[index], .parent = &bar,
			.next = index < 3 ? &buttons[index + 1] : NULL };
	memset(&party, 0, sizeof(party));
	party.count = 3;
	strcpy(party.members[0].name, "Chief");
	strcpy(party.members[1].name, "Kat");
	strcpy(party.members[2].name, "Rook");
	left = 0;
	removed = NONE;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	setup();
	CASE("keys_remove")
	{
		list.focused_child = &rows[MATCHMAKING_ROWS + 1];
		bar.focused_child = &buttons[0];
		CHECK(matchmaking_remove_ask(&rows[MATCHMAKING_ROWS + 1]), "X on Kat asks");
		CHECK(bar.focused_child == &buttons[3], "the confirm opens on NO");
		CHECK(matchmaking_answer(&buttons[2], TRUE), "YES");
		CHECK(removed == 1, "Kat removed: %d", removed);
		CHECK(list.focused_child == &rows[MATCHMAKING_ROWS + 1] && list.parameters.list.selected_index == MATCHMAKING_ROWS + 1,
			"the focus back on Kat's row");
		CHECK(bar.focused_child == &buttons[0] && bar.parameters.list.selected_index == 0, "and on INVITE");
	}
	CASE("pointer_remove")
	{
		list.focused_child = &rows[MATCHMAKING_ROWS + 1];
		bar.focused_child = NULL;
		CHECK(matchmaking_remove_ask(&rows[MATCHMAKING_ROWS + 1]), "X on Kat asks");
		bar.focused_child = NULL;
		CHECK(matchmaking_answer(&buttons[2], TRUE), "YES with no button focused");
		CHECK(removed == 1, "Kat removed: %d", removed);
		CHECK(!bar.focused_child, "no button focused, as before");
	}
	CASE("pointer_leave")
	{
		list.focused_child = NULL;
		bar.focused_child = NULL;
		CHECK(matchmaking_ask(&bar, _matchmaking_ask_leave), "LEAVE PARTY asks");
		list.focused_child = NULL;
		bar.focused_child = NULL;
		CHECK(matchmaking_answer(&buttons[2], TRUE), "YES with nothing focused");
		CHECK(left == 1, "the party left");
		CHECK(!list.focused_child && !bar.focused_child, "nothing focused, as before");
	}
	return 0;
}
