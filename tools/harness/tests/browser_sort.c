/*
BROWSER_SORT.C (test)

The server browser's column titles (menu_functions.c's browser_sort_column,
browser_sort_headers_show and browser_sort), as join_game.xml lays them out,
from test_browser_sort.py's orders.inc and under_test.inc: which sort, the
arrow over the one sorted by, the focus, a press on one and on it again, the
A press a join posts, and LAN's browser.
*/

#include "harness.h"
#include "orders.inc"

#define NUMBEROF(array) (sizeof(array) / sizeof(array[0]))
#define SOUND_CURSOR 1

struct widget_instance
{
	char const *name;
	boolean visible, disabled;
	struct widget_instance *child, *next, *parent, *focused_child;
	union { struct { short selected_index; } list; } parameters;
};

static struct widget_instance *named(struct widget_instance *widget, char const *name, long nth)
{
	struct widget_instance *child;

	(void)nth;
	for (child = widget ? widget->child : NULL; child; child = child->next)
	{
		struct widget_instance *found = !strcmp(child->name, name) ? child : named(child, name, 0);

		if (found)
			return found;
	}
	return NULL;
}

static void visible_set(struct widget_instance *widget, boolean visible)
{
	if (widget)
		widget->visible = visible;
}

/* the lobby's order (p2p_lobby.c), the browser's mode and rows, the join's
press, the sound */
static int lobby_order, lobby_reversed;
static int p2p_lobby_order(int *reversed)
{
	if (reversed)
		*reversed = lobby_reversed;
	return lobby_order;
}
static void p2p_lobby_set_order(int order, int reversed)
{
	lobby_order = order;
	lobby_reversed = reversed;
}
enum { _multiplayer_mode_lan, _multiplayer_mode_direct_link, _multiplayer_mode_server_browser };
static struct { int mode; } multiplayer = { _multiplayer_mode_server_browser };
static struct { boolean ready; short first, chosen; } lobby_browser;
static int join_presses;
static boolean lobby_browser_select(struct widget_instance *widget, short controller, short row,
	boolean *widget_deleted)
{
	(void)widget, (void)controller, (void)row, (void)widget_deleted;
	join_presses++;
	return TRUE;
}
#define ui_play_audio_feedback_sound(sound) ((void)0)

#include "under_test.inc"

/* join_game_header's titles, in its order */
enum { LOCKED, DEDICATED, SERVER_NAME, MAP, CLASSIC, TYPE, PLAYERS, PING, TITLES };
static char const *const titles[TITLES] = { "op_sort_header_locked", "op_sort_header_dedicated",
	"op_sort_header_server_name", "op_sort_header_map", "op_sort_header_classic", "op_sort_header_type",
	"op_sort_header_players", "op_sort_header_ping" };
static struct widget_instance headers, title[TITLES], arrows[TITLES], up[TITLES], down[TITLES];

static void setup(void)
{
	int index;

	memset(&headers, 0, sizeof(headers));
	headers.name = "join_game_header";
	headers.child = &title[0];
	headers.focused_child = &title[0];
	for (index = 0; index < TITLES; index++)
	{
		title[index] = (struct widget_instance){ .name = titles[index], .visible = TRUE, .parent = &headers,
			.next = index < TITLES - 1 ? &title[index + 1] : NULL };
		if (index != LOCKED && index != DEDICATED && index != CLASSIC)
		{
			title[index].child = &arrows[index];
			arrows[index] = (struct widget_instance){ .name = "header_sort_arrows", .visible = TRUE, .child = &up[index] };
			up[index] = (struct widget_instance){ .name = "header_sort_up_arrow", .visible = TRUE, .next = &down[index] };
			down[index] = (struct widget_instance){ .name = "header_sort_down_arrow", .visible = TRUE };
		}
	}
}

/* the arrow shown: 'u' up, 'd' down, '-' none, over each title with arrows */
static int arrows_are(char ping, char players, char name)
{
	int index;

	for (index = 0; index < TITLES; index++)
	{
		char shown, expected;

		if (!title[index].child)
			continue;
		shown = !arrows[index].visible ? '-' : up[index].visible && !down[index].visible ? 'u' :
			down[index].visible && !up[index].visible ? 'd' : '?';
		expected = index == PING ? ping : index == PLAYERS ? players : index == SERVER_NAME ? name : '-';
		if (shown != expected)
			return 0;
	}
	return 1;
}

static boolean press(int index)
{
	boolean deleted = FALSE;

	return browser_sort(&title[index], 0, &deleted);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	setup();
	CASE("columns")
	{
		CHECK(browser_sort_column(&title[PLAYERS]) == P2P_LOBBY_ORDER_PLAYERS &&
			browser_sort_column(&title[PING]) == P2P_LOBBY_ORDER_PING &&
			browser_sort_column(&title[SERVER_NAME]) == P2P_LOBBY_ORDER_NAME, "PLAYERS, PING and SERVER NAME sort");
		CHECK(browser_sort_column(&title[MAP]) == NONE && browser_sort_column(&title[TYPE]) == NONE &&
			browser_sort_column(&title[LOCKED]) == NONE, "the other titles sort nothing");
	}
	else CASE("arrows")
	{
		p2p_lobby_set_order(P2P_LOBBY_ORDER_PLAYERS, 0);
		browser_sort_headers_show(&headers);
		CHECK(arrows_are('-', 'd', '-'), "the most players first: down over PLAYERS only");
		p2p_lobby_set_order(P2P_LOBBY_ORDER_PLAYERS, 1);
		browser_sort_headers_show(&headers);
		CHECK(arrows_are('-', 'u', '-'), "the fewest players first: up");
		p2p_lobby_set_order(P2P_LOBBY_ORDER_PING, 0);
		browser_sort_headers_show(&headers);
		CHECK(arrows_are('u', '-', '-'), "the lowest ping first: up over PING only");
		p2p_lobby_set_order(P2P_LOBBY_ORDER_PING, 1);
		browser_sort_headers_show(&headers);
		CHECK(arrows_are('d', '-', '-'), "the highest ping first: down");
		p2p_lobby_set_order(P2P_LOBBY_ORDER_NAME, 0);
		browser_sort_headers_show(&headers);
		CHECK(arrows_are('-', '-', 'u'), "names A to Z: up over SERVER NAME only");
	}
	else CASE("focus")
	{
		int index;

		p2p_lobby_set_order(P2P_LOBBY_ORDER_PING, 0);
		browser_sort_headers_show(&headers);
		for (index = 0; index < TITLES; index++)
			CHECK(title[index].disabled == (index != PLAYERS && index != PING && index != SERVER_NAME),
				"only the titles that sort take the focus (%s)", titles[index]);
		CHECK(headers.focused_child == &title[PING] && headers.parameters.list.selected_index == PING,
			"the title sorted by has the focus");
	}
	else CASE("toggle")
	{
		lobby_browser.first = 5;
		lobby_browser.chosen = 7;
		CHECK(press(PING) && lobby_order == P2P_LOBBY_ORDER_PING && !lobby_reversed, "PING: the lowest first");
		CHECK(lobby_browser.first == 0 && lobby_browser.chosen == 0, "the list back at its first game");
		CHECK(arrows_are('u', '-', '-'), "PING's arrow shown");
		press(PING);
		CHECK(lobby_order == P2P_LOBBY_ORDER_PING && lobby_reversed && arrows_are('d', '-', '-'),
			"PING again: the highest first");
		press(PING);
		CHECK(lobby_order == P2P_LOBBY_ORDER_PING && !lobby_reversed, "PING a third time: the lowest first again");
		press(PING);
		press(PLAYERS);
		CHECK(lobby_order == P2P_LOBBY_ORDER_PLAYERS && !lobby_reversed && arrows_are('-', 'd', '-'),
			"another title: its own way first, whatever the last was");
		press(SERVER_NAME);
		CHECK(lobby_order == P2P_LOBBY_ORDER_NAME && !lobby_reversed, "SERVER NAME: A to Z");
		press(MAP);
		CHECK(lobby_order == P2P_LOBBY_ORDER_NAME && !lobby_reversed, "MAP sorts nothing");
		CHECK(join_presses == 0, "no join");
	}
	else CASE("ready")
	{
		lobby_browser.ready = TRUE;
		p2p_lobby_set_order(P2P_LOBBY_ORDER_PLAYERS, 0);
		press(PING);
		CHECK(join_presses == 1 && lobby_order == P2P_LOBBY_ORDER_PLAYERS,
			"the A press a join posts joins, sorting nothing");
	}
	else CASE("lan")
	{
		multiplayer.mode = _multiplayer_mode_lan;
		p2p_lobby_set_order(P2P_LOBBY_ORDER_PLAYERS, 0);
		press(PING);
		CHECK(lobby_order == P2P_LOBBY_ORDER_PLAYERS && !lobby_reversed, "LAN's titles sort nothing");
	}
	else return 2;
	return 0;
}
