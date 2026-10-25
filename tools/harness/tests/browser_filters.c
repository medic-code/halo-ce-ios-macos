/*
BROWSER_FILTERS.C (test)

The server browser's FILTERS panel (menu_functions.c's section of that name),
as join_game.filters.xml lays it out, from test_browser_filters.py's
under_test.inc: the panel shown and hidden, OK, and the help of each row.
*/

#include "harness.h"

#define SOUND_FORWARD 2
#define ui_play_audio_feedback_sound(sound) ((void)0)

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

enum { _multiplayer_mode_lan, _multiplayer_mode_direct_link, _multiplayer_mode_server_browser };
static struct { int mode; } multiplayer = { _multiplayer_mode_server_browser };

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

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	setup();
	CASE("show")
	{
		browser_filters_show(&screen, TRUE);
		CHECK(panel.visible && !list.visible, "the panel shows over the browser, whose rows are hidden");
		CHECK(screen.focused_child == &panel && panel.focused_child == &menu && menu.focused_child == &rows[0] &&
			menu.parameters.list.selected_index == 0, "the focus on the panel's first row");
		browser_filters_show(&screen, FALSE);
		CHECK(!panel.visible && list.visible && screen.focused_child == &list && list.focused_child == &filters_button,
			"hidden, the focus back on FILTERS");
	}
	else CASE("save")
	{
		browser_filters_show(&screen, TRUE);
		CHECK(browser_filters_save(&rows[6]), "OK succeeds");
		CHECK(!panel.visible && list.visible && screen.focused_child == &list, "the panel hidden");
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
	else return 2;
	return 0;
}
