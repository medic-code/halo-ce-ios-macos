/* MATCHMAKING_CONFIRM.C (test): MATCHMAKING's YES and NO for X's removal, LEAVE PARTY and a friend's invite (menu_functions.c) */

#include <wchar.h>
#include "harness.h"
#include "enums.inc"

struct widget_instance
{
	char const *name;
	struct widget_instance *parent, *child, *next, *focused_child;
	boolean visible;
	float alpha_modifier;
	union { struct { short selected_index; } list; } parameters;
};

enum { SOUND_FORWARD, SOUND_BACK };
#define TAB_UNFOCUSED_ALPHA 0.45f
#define MATCHMAKING_ROWS 2

static struct
{
	unsigned playlists;
	short size;
	short asking, asked, added, added_row;
	boolean search_asked;
	char asked_name[P2P_PARTY_NAME_SIZE + 1], asked_id[17];
	struct matchmaking_row slots[P2P_PARTY_MAXIMUM];
	struct widget_instance *asked_from, *asked_button;
} matchmaking_screen;

static struct p2p_party_status party;
static struct p2p_mm_status search;
static int left, removed = NONE, answered = NONE, answer_result = 1, cancelled, went_back;
static boolean pending;
static char unfriended[17];
static wchar_t search_help[MATCHMAKING_TEXT_LENGTH];
static wchar_t const *const matchmaking_playlist_words[NUMBER_OF_P2P_MM_PLAYLISTS] = { L"" };
static wchar_t const *const matchmaking_size_words[NUMBER_OF_P2P_MM_SIZES] = { L"" };

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
static int request_result;
static int p2p_friend_answer(char const *id, int yes) { (void)id; (void)yes; return request_result; }
static void p2p_friend_remove(char const *id) { strcpy(unfriended, id); }
static int p2p_friend_invite_answer(int yes)
{
	answered = yes;
	pending = FALSE;
	return yes ? answer_result : P2P_FRIEND_ANSWER_DONE;
}
static int p2p_friend_invite_pending(char *name_tag, int size)
{
	snprintf(name_tag, size, "Kat#1234");
	return pending;
}
static void p2p_mm_get_status(struct p2p_mm_status *status) { *status = search; }
static void p2p_mm_cancel(void) { cancelled++; }
static void ui_widget_port_go_back(struct widget_instance *widget) { (void)widget; went_back++; }
static void visible_set(struct widget_instance *widget, boolean visible) { if (widget) widget->visible = visible; }
static void text_append(wchar_t *text, short size, wchar_t const *word) { (void)text; (void)size; (void)word; }
static void text_set_length(struct widget_instance *text_box, wchar_t const *text, short length)
{
	if (text_box && !strcmp(text_box->name, "search_help"))
		swprintf(search_help, length, L"%ls", text);
}
static void matchmaking_lines(const struct p2p_mm_status *status, const struct p2p_party_status *party, wchar_t *title,
	wchar_t *count, wchar_t *help, wchar_t *notice, short size)
{
	(void)status; (void)party;
	swprintf(title, size, L"");
	swprintf(count, size, L"");
	swprintf(notice, size, L"");
	swprintf(help, size, L"SEARCHING");
}
static void matchmaking_invite_help(char const *name_tag, wchar_t *help, short size)
{
	(void)name_tag;
	swprintf(help, size, L"INVITE Kat#1234");
}
static void matchmaking_add_words(int result, wchar_t *help, short size) { swprintf(help, size, L"WORDS %d", result); }
static void matchmaking_search_slots_show(struct widget_instance *screen, struct p2p_mm_status const *status,
	struct p2p_party_status const *party_status)
{
	(void)screen; (void)status; (void)party_status;
}
static void matchmaking_search_tracker_show(struct widget_instance *screen, struct p2p_mm_status const *status)
{
	(void)screen; (void)status;
}

#include "under_test.inc"

static struct widget_instance screen, list, rows[MATCHMAKING_ROWS + 3], bar, buttons[4];
static char const *const button_names[] = { "button_invite", "button_search", "button_yes", "button_no" };
static struct widget_instance search_screen, search_help_text, search_list, search_bar,
	search_buttons[NUMBER_OF_MATCHMAKING_SEARCH_BAR_BUTTONS];

static void search_setup(void)
{
	int index;

	search_screen = (struct widget_instance){ .name = "search_screen", .child = &search_help_text };
	search_help_text = (struct widget_instance){ .name = "search_help", .parent = &search_screen, .next = &search_list };
	search_list = (struct widget_instance){ .name = "search_list", .parent = &search_screen, .child = &search_bar };
	search_bar = (struct widget_instance){ .name = "search_button_bar", .parent = &search_list, .child = &search_buttons[0],
		.focused_child = &search_buttons[0] };
	for (index = 0; index < NUMBER_OF_MATCHMAKING_SEARCH_BAR_BUTTONS; index++)
		search_buttons[index] = (struct widget_instance){ .name = matchmaking_search_bar_names[index], .parent = &search_bar,
			.next = index + 1 < NUMBER_OF_MATCHMAKING_SEARCH_BAR_BUTTONS ? &search_buttons[index + 1] : NULL, .visible = TRUE };
	search.state = P2P_MM_LOOKING;
	pending = TRUE;
	cancelled = went_back = 0;
	search_help[0] = 0;
}

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
	party.members[0].you = party.members[0].leader = TRUE;
	strcpy(party.members[1].name, "Kat");
	strcpy(party.members[2].name, "Rook");
	left = 0;
	removed = NONE;
	answered = NONE;
	answer_result = P2P_FRIEND_ANSWER_DONE;
	request_result = P2P_FRIEND_ADD_SENT;
	unfriended[0] = 0;
	search_setup();
	memset(&matchmaking_screen, 0, sizeof(matchmaking_screen));
	matchmaking_screen.slots[1].kind = _matchmaking_row_member;
	matchmaking_screen.slots[1].index = 1;
	matchmaking_screen.slots[2].kind = _matchmaking_row_friend;
	matchmaking_screen.slots[2].index = 0;
	strcpy(matchmaking_screen.slots[2].id, "0123456789ABCDEF");
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
	CASE("friend_remove")
	{
		list.focused_child = &rows[MATCHMAKING_ROWS + 2];
		bar.focused_child = &buttons[0];
		CHECK(matchmaking_remove_ask(&rows[MATCHMAKING_ROWS + 2]), "X on a friend asks");
		CHECK(matchmaking_screen.asking == _matchmaking_ask_friend_remove, "the question is the friend's removal");
		CHECK(matchmaking_answer(&buttons[3], FALSE), "NO");
		CHECK(!unfriended[0], "NO keeps the friend");
		CHECK(matchmaking_remove_ask(&rows[MATCHMAKING_ROWS + 2]), "X asks again");
		CHECK(matchmaking_answer(&buttons[2], TRUE), "YES");
		CHECK(!strcmp(unfriended, "0123456789ABCDEF"), "the friend shown is removed: %s", unfriended);
		CHECK(removed == NONE, "no party member removed");
	}
	CASE("request_full")
	{
		list.focused_child = &rows[MATCHMAKING_ROWS + 2];
		CHECK(matchmaking_request_ask(&rows[MATCHMAKING_ROWS + 2], "0123456789ABCDEF"), "A on a request asks");
		CHECK(matchmaking_answer(&buttons[2], TRUE), "YES");
		CHECK(matchmaking_screen.added == 0, "an accepted request says nothing: %d", matchmaking_screen.added);
		request_result = P2P_FRIEND_ADD_FULL;
		list.focused_child = &rows[MATCHMAKING_ROWS + 2];
		CHECK(matchmaking_request_ask(&rows[MATCHMAKING_ROWS + 2], "0123456789ABCDEF"), "A asks again");
		CHECK(matchmaking_answer(&buttons[2], TRUE), "YES on a full list");
		CHECK(matchmaking_screen.added == P2P_FRIEND_ADD_FULL && matchmaking_screen.added_row == MATCHMAKING_ROWS + 2,
			"the full list shows on the request's row: %d %d", matchmaking_screen.added, matchmaking_screen.added_row);
	}
	CASE("search_invite")
	{
		struct widget_instance *yes = &search_buttons[_matchmaking_search_bar_yes], *no = &search_buttons[_matchmaking_search_bar_no];
		boolean closed = FALSE;

		answer_result = P2P_FRIEND_ANSWER_DONE;
		pending = TRUE;
		matchmaking_search_reply(yes, TRUE, &closed);
		CHECK(answered == TRUE && closed, "YES on the search screen joins and closes it");
		answered = NONE;
		closed = FALSE;
		pending = TRUE;
		matchmaking_search_reply(no, FALSE, &closed);
		CHECK(answered == FALSE && !closed, "NO keeps searching");
		answered = NONE;
		pending = TRUE;
		answer_result = P2P_FRIEND_ANSWER_LEAVE_FIRST;
		matchmaking_search_reply(yes, TRUE, &closed);
		CHECK(answered == TRUE && !closed, "a YES that cannot join keeps the screen");
	}
	CASE("search_update")
	{
		struct widget_instance *yes = &search_buttons[_matchmaking_search_bar_yes], *no = &search_buttons[_matchmaking_search_bar_no];

		matchmaking_search_update(&search_list);
		CHECK(!wcscmp(search_help, L"INVITE Kat#1234"), "the help line asks: %ls", search_help);
		CHECK(yes->visible && no->visible && !search_buttons[_matchmaking_search_bar_cancel].visible, "YES and NO in place of CANCEL");
		CHECK(search_bar.focused_child == no, "NO focused");
		search_bar.focused_child = yes;
		matchmaking_search_update(&search_list);
		CHECK(search_bar.focused_child == yes, "the player's move to YES holds");
		pending = FALSE;
		matchmaking_search_update(&search_list);
		CHECK(!wcscmp(search_help, L"SEARCHING") && search_buttons[_matchmaking_search_bar_cancel].visible && !no->visible,
			"the invite gone: the search's help line and CANCEL back: %ls", search_help);
		pending = TRUE;
		matchmaking_search_update(&search_list);
		CHECK(search_bar.focused_child == no, "a new invite focuses NO");
	}
	CASE("search_reply")
	{
		boolean deleted = FALSE;

		CHECK(matchmaking_search_reply(&search_buttons[_matchmaking_search_bar_no], FALSE, &deleted), "NO");
		CHECK(answered == FALSE && !deleted && !cancelled, "NO keeps searching");
		pending = TRUE;
		CHECK(matchmaking_search_reply(&search_buttons[_matchmaking_search_bar_yes], TRUE, &deleted), "YES");
		CHECK(answered == TRUE && deleted && went_back == 1, "YES joins and closes the screen");
		answered = NONE;
		deleted = FALSE;
		CHECK(matchmaking_search_reply(&search_buttons[_matchmaking_search_bar_yes], TRUE, &deleted), "YES as the invite expires");
		CHECK(answered == NONE && !deleted && went_back == 1, "nothing pending: no answer, the screen stays");
	}
	CASE("search_leave_first")
	{
		boolean deleted = FALSE;

		answer_result = P2P_FRIEND_ANSWER_LEAVE_FIRST;
		CHECK(matchmaking_search_reply(&search_buttons[_matchmaking_search_bar_yes], TRUE, &deleted), "YES in a party");
		CHECK(!deleted && !cancelled, "the screen stays, still searching");
		matchmaking_search_update(&search_list);
		CHECK(!wcscmp(search_help, L"WORDS 100"), "the party screen's LEAVE YOUR PARTY FIRST: %ls", search_help);
		pending = TRUE;
		matchmaking_search_update(&search_list);
		CHECK(!wcscmp(search_help, L"INVITE Kat#1234"), "a new invite asks: %ls", search_help);
		CHECK(matchmaking_search_reply(&search_buttons[_matchmaking_search_bar_no], FALSE, &deleted), "NO");
		matchmaking_search_update(&search_list);
		CHECK(!wcscmp(search_help, L"SEARCHING"), "the new invite cleared the words: %ls", search_help);
	}
	CASE("search_back")
	{
		boolean deleted = FALSE;

		CHECK(matchmaking_search_back(&search_screen, &deleted), "B with an invite");
		CHECK(answered == FALSE && !cancelled && !deleted, "B answers NO and keeps searching");
		CHECK(matchmaking_search_back(&search_screen, &deleted), "B again");
		CHECK(cancelled == 1 && !deleted, "B with no invite cancels the search");
	}
	CASE("invite_answer")
	{
		list.focused_child = &rows[1];
		bar.focused_child = &buttons[0];
		CHECK(matchmaking_ask(&bar, _matchmaking_ask_friend_invite), "an invite asks");
		CHECK(matchmaking_answer(&buttons[2], TRUE), "YES");
		CHECK(answered == TRUE, "YES goes to the friends layer");
		CHECK(matchmaking_screen.added == P2P_FRIEND_ADD_SENT, "joined: nothing to say");
		answer_result = P2P_FRIEND_ANSWER_LEAVE_FIRST;
		CHECK(matchmaking_ask(&bar, _matchmaking_ask_friend_invite), "another invite asks");
		CHECK(matchmaking_answer(&buttons[2], TRUE), "YES in a party");
		CHECK(matchmaking_screen.added == MATCHMAKING_LEAVE_FIRST && matchmaking_screen.added_row == 1,
			"E9: LEAVE YOUR PARTY FIRST on the row focused before: %d %d", matchmaking_screen.added, matchmaking_screen.added_row);
		matchmaking_screen.added = P2P_FRIEND_ADD_SENT;
		answer_result = P2P_FRIEND_ANSWER_FAILED;
		CHECK(matchmaking_ask(&bar, _matchmaking_ask_friend_invite), "an invite that cannot be joined asks");
		CHECK(matchmaking_answer(&buttons[2], TRUE), "YES");
		CHECK(matchmaking_screen.added == P2P_FRIEND_ADD_SENT, "a failed join is not LEAVE YOUR PARTY FIRST");
		CHECK(matchmaking_ask(&bar, _matchmaking_ask_friend_invite), "a third invite asks");
		CHECK(matchmaking_answer(&buttons[3], FALSE), "NO");
		CHECK(answered == FALSE && matchmaking_screen.added == P2P_FRIEND_ADD_SENT, "NO says nothing");
	}
	return 0;
}
