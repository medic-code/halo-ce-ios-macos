/*
BROWSER_FILTERS.C (test)

The server browser's FILTERS panel (menu_functions.c's section of that name),
as join_game.filters.xml lays it out, from test_browser_filters.py's lobby.inc
and under_test.inc: the lobby's filter from the rows' values, the panel
shown and hidden, OK, the help of each row, and SHOW MISSING MAPS.
*/

#include "harness.h"

#define NUMBEROF(array) (sizeof(array) / sizeof(array[0]))
#define PIN(value, low, high) ((value) < (low) ? (low) : (value) > (high) ? (high) : (value))
#define csmemset memset
#define csmemcpy memcpy
#define _stricmp strcasecmp
#define SOUND_FORWARD 2
#define ui_play_audio_feedback_sound(sound) ((void)0)
#define P2P_LISTING_MAP_SIZE 32
#define LOBBY_BROWSER_GAMES 256

#include <strings.h>
#include "lobby.inc"

struct widget_instance
{
	char const *name;
	boolean visible;
	struct widget_instance *child, *next, *parent, *focused_child;
	struct
	{
		struct { short selected_index, number_of_items; struct widget_instance *extended_description; } list;
		struct { short string_list_index; } text_box;
	} parameters;
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

static struct widget_instance *screen_of(struct widget_instance *widget)
{
	while (widget->parent)
		widget = widget->parent;
	return widget;
}

/* the lobby's filter (p2p_lobby.c), the browser's mode and games */
static struct p2p_lobby_filter lobby_filter;
static int filters_set;
static void p2p_lobby_set_filter(const struct p2p_lobby_filter *filter)
{
	lobby_filter = *filter;
	filters_set++;
}
enum { _multiplayer_mode_lan, _multiplayer_mode_direct_link, _multiplayer_mode_server_browser };
static struct { int mode; } multiplayer = { _multiplayer_mode_server_browser };
struct p2p_listing { char map[P2P_LISTING_MAP_SIZE + 1]; };
static struct { short first, chosen, missing_maps; } lobby_browser;

/* the maps: the Xbox's two and one Custom Edition map (custom_maps\coolmap),
a campaign level (a30), and Custom Edition files (coolmap and oldmap, which
the map list has not: another version's) */
static char const *const level_names[] = { "levels\\test\\bloodgulch\\bloodgulch",
	"levels\\test\\beavercreek\\beavercreek", "custom_maps\\coolmap" };
static short ui_widget_port_multiplayer_maps(char const *const **names, short *last_used)
{
	(void)last_used;
	*names = level_names;
	return (short)NUMBEROF(level_names);
}
static char const *scenario_name(char const *path)
{
	char const *name = strrchr(path, '\\');

	return name ? name + 1 : path;
}
static short campaign_level_of(char const *map)
{
	return !strcmp(map, "a30") ? 1 : NONE;
}
static int files_looked_for;
static boolean custom_edition_map_file_present(char const *map)
{
	files_looked_for++;
	return !strcmp(map, "coolmap") || !strcmp(map, "oldmap");
}

#include "under_test.inc"

/* the browser's screen: its list (with FILTERS' button), and the panel:
its header, then its menu of six rows (a label and a spinner each) and the
buttons' row, its help */
static struct widget_instance screen, list, filters_button, panel, header, menu, rows[7], labels[6], spinners[6], help;
static short const items[6] = { 2, 2, 6, 6, 3, 2 };

static void setup(void)
{
	int index;

	screen = (struct widget_instance){ .name = "join_game_screen", .visible = TRUE, .child = &list };
	list = (struct widget_instance){ .name = "join_game_items_list", .visible = TRUE, .parent = &screen,
		.next = &panel, .child = &filters_button, .focused_child = &filters_button };
	filters_button = (struct widget_instance){ .name = "join_game_button_filters", .visible = TRUE, .parent = &list };
	panel = (struct widget_instance){ .name = "filters_screen", .parent = &screen, .child = &header,
		.focused_child = &header };
	header = (struct widget_instance){ .name = "header_filters", .visible = TRUE, .parent = &panel, .next = &menu };
	menu = (struct widget_instance){ .name = "filters_options_menu", .visible = TRUE, .parent = &panel,
		.child = &rows[0], .focused_child = &rows[6] };
	menu.parameters.list.selected_index = 6;
	menu.parameters.list.extended_description = &help;
	help = (struct widget_instance){ .name = "filters_help", .visible = TRUE };
	help.parameters.text_box.string_list_index = 99;
	for (index = 0; index < 7; index++)
	{
		rows[index] = (struct widget_instance){ .name = index < 6 ? "op_row" : "filters_button_bar", .visible = TRUE,
			.parent = &menu, .next = index < 6 ? &rows[index + 1] : NULL };
		if (index == 6)
			continue;
		rows[index].child = &labels[index];
		rows[index].focused_child = &spinners[index];
		labels[index] = (struct widget_instance){ .name = "label", .visible = TRUE, .parent = &rows[index],
			.next = &spinners[index] };
		spinners[index] = (struct widget_instance){ .name = filter_rows[index].spinner, .visible = TRUE,
			.parent = &rows[index] };
		spinners[index].parameters.list.number_of_items = items[index];
	}
}

static int help_shown(int row, short value)
{
	help.parameters.text_box.string_list_index = 99;
	menu.focused_child = &rows[row];
	if (row < 6)
		spinners[row].parameters.list.selected_index = value;
	browser_filters_help(&menu);
	return help.parameters.text_box.string_list_index;
}

static short installed(char const *const *maps, short count)
{
	struct p2p_listing games[8];
	short index;

	for (index = 0; index < count; index++)
		snprintf(games[index].map, sizeof(games[index].map), "%s", maps[index]);
	return lobby_browser_installed_games(games, count);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	setup();
	CASE("apply")
	{
		browser_filter_apply();
		CHECK(filters_set == 1 && !lobby_filter.hide_empty && !lobby_filter.hide_full && !lobby_filter.ping_limit &&
			!lobby_filter.engine_type && lobby_filter.teams == P2P_LOBBY_TEAMS_ANY, "at the start every game shows");
		filter_values[_filter_empty] = FALSE;
		filter_values[_filter_full] = FALSE;
		filter_values[_filter_ping] = 2;
		filter_values[_filter_game_type] = 3;
		filter_values[_filter_teams] = P2P_LOBBY_TEAMS_ONLY;
		browser_filter_apply();
		CHECK(lobby_filter.hide_empty && lobby_filter.hide_full, "SHOW EMPTY and FULL SERVERS NO hide them");
		CHECK(lobby_filter.ping_limit == 100, "PING < 100 is a limit of 100 ms (%d)", lobby_filter.ping_limit);
		CHECK(lobby_filter.engine_type == 3, "GAME TYPE ODDBALL is engine 3");
		CHECK(lobby_filter.teams == P2P_LOBBY_TEAMS_ONLY, "TEAMPLAY OPTIONS TEAMPLAY: team games only");
		filter_values[_filter_ping] = 5;
		browser_filter_apply();
		CHECK(lobby_filter.ping_limit == 300, "PING < 300 (%d)", lobby_filter.ping_limit);
	}
	else CASE("show")
	{
		int index;

		filter_values[_filter_full] = FALSE;
		filter_values[_filter_ping] = 4;
		browser_filters_show(&screen, TRUE);
		CHECK(panel.visible && !list.visible, "the panel shows over the browser, whose rows are hidden");
		CHECK(screen.focused_child == &panel && panel.focused_child == &menu && menu.focused_child == &rows[0] &&
			menu.parameters.list.selected_index == 0, "the focus on the panel's first row");
		for (index = 0; index < 6; index++)
			CHECK(spinners[index].parameters.list.selected_index == filter_values[index], "row %d shows its value", index);
		browser_filters_show(&screen, FALSE);
		CHECK(!panel.visible && list.visible && screen.focused_child == &list && list.focused_child == &filters_button,
			"hidden, the focus back on FILTERS");
	}
	else CASE("save")
	{
		short const chosen[6] = { 0, 1, 1, 2, 1, 0 };
		int index;

		browser_filters_show(&screen, TRUE);
		lobby_browser.first = 4;
		lobby_browser.chosen = 6;
		for (index = 0; index < 6; index++)
			spinners[index].parameters.list.selected_index = chosen[index];
		CHECK(browser_filters_save(&rows[6]), "OK succeeds");
		for (index = 0; index < 6; index++)
			CHECK(filter_values[index] == chosen[index], "row %d's value kept", index);
		CHECK(filters_set == 1 && lobby_filter.hide_empty && !lobby_filter.hide_full && lobby_filter.ping_limit == 50 &&
			lobby_filter.engine_type == 2 && lobby_filter.teams == P2P_LOBBY_TEAMS_FREE_FOR_ALL, "and applied");
		CHECK(!panel.visible && list.visible && screen.focused_child == &list, "the panel hidden");
		CHECK(lobby_browser.first == 0 && lobby_browser.chosen == 0, "the browser from its first game");
	}
	else CASE("shown")
	{
		CHECK(!browser_filters_shown(&screen), "hidden at the start");
		browser_filters_show(&screen, TRUE);
		CHECK(browser_filters_shown(&screen), "shown");
		multiplayer.mode = _multiplayer_mode_lan;
		CHECK(!browser_filters_shown(&screen), "LAN's browser has none");
	}
	else CASE("help")
	{
		CHECK(help_shown(0, 0) == 0 && help_shown(0, 1) == 1, "SHOW EMPTY SERVERS: NO's help, YES's");
		CHECK(help_shown(1, 0) == 2 && help_shown(1, 1) == 3, "SHOW FULL SERVERS: NO's help, YES's");
		CHECK(help_shown(2, 0) == 4 && help_shown(2, 3) == 4, "PING: one help");
		CHECK(help_shown(3, 0) == 5 && help_shown(3, 5) == 5, "GAME TYPE: one help");
		CHECK(help_shown(4, 0) == 6 && help_shown(4, 1) == 7 && help_shown(4, 2) == 8,
			"TEAMPLAY OPTIONS: ANY's, FREE-FOR-ALL's, TEAMPLAY's");
		CHECK(help_shown(5, 0) == 9 && help_shown(5, 1) == 10, "SHOW MISSING MAPS: NO's help, YES's");
		CHECK(help_shown(6, 0) == 99, "the buttons' row leaves the help");
	}
	else CASE("maps")
	{
		static char const *const maps[] = { "bloodgulch", "a30", "coolmap", "oldmap", "nomap",
			"a_custom_map_named_to_the_limit_", "BEAVERCREEK" };
		short count;

		CHECK(installed(maps, 7) == 7 && lobby_browser.missing_maps == 0 && files_looked_for == 0,
			"SHOW MISSING MAPS YES: every game, no map looked for");
		filter_values[_filter_missing_maps] = FALSE;
		count = installed(maps, 7);
		CHECK(count == 6 && lobby_browser.missing_maps == 1, "NO hides the game on a map not here (%d)", count);
		CHECK(files_looked_for == 2, "only maps not in the lists are looked for on disk (%d)", files_looked_for);
		count = installed(maps, 7);
		CHECK(count == 6 && files_looked_for == 2, "each map looked for once (%d)", files_looked_for);
	}
	else return 2;
	return 0;
}
