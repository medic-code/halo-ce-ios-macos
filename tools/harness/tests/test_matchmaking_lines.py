"""The search screens' words (menu_functions.c)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["looking", "queued", "unreached", "hosting", "hosted_panel", "found", "big_looking", "big_hosting", "big_found",
         "lobby", "notices", "member_view", "member_queue", "member_found", "party_sizes", "party_panel", "party_notices", "friends_line", "friend_rows",
         "friend_layout", "friend_words", "add_friend"]

NEGATIVE_CONTROLS = {
    "invite-bar-ignored": (("if (invited)\n\t\treturn MATCHMAKING_SEARCH_BAR(yes)", "if (0)\n\t\treturn MATCHMAKING_SEARCH_BAR(yes)"), "member_queue"),
    "number-without-countdown": (("seconds >= 0", "seconds >= -1"), "lobby"),
    "last-second-lost": (("seconds >= 0", "seconds > 0"), "lobby"),
    "big-looking-as-small": (("int big = p2p_mm_size_big(status->size);", "int big = 0;"), "big_looking"),
    "big-hosting-as-small": (("int big = p2p_mm_size_big(status->size);", "int big = 0;"), "big_hosting"),
    "big-found-as-small": (("int big = p2p_mm_size_big(status->size);", "int big = 0;"), "big_found"),
    "big-queue-words": (("big && status->state == P2P_MM_QUEUED ? P2P_MM_LOOKING : ", ""), "big_looking"),
    "region-lost": (("if (status->region[0])", "if (0)"), "big_found"),
    "member-seen-as-leader": (("return party->count && !party->members[0].you;", "return 0;"), "member_view"),
    "member-queue-as-leader": (("return party->count && !party->members[0].you;", "return 0;"), "member_queue"),
    "leader-seen-as-member": (("party->count && !party->members[0].you", "party->count"), "party_panel"),
    "size-not-skipped": (("for (tries = 0; tries < NUMBER_OF_P2P_MM_SIZES && !mm_party_size_allowed(party, size); tries++)",
                          "for (tries = 0; tries < 0; tries++)"), "party_sizes"),
    "skip-only-goes-up": (("step = to == (from + NUMBER_OF_P2P_MM_SIZES - 1) % NUMBER_OF_P2P_MM_SIZES ?", "step = 0 ?"),
                          "party_sizes"),
    "fallback-ignores-the-party": (("while (after < P2P_MM_FFA_6 && !mm_party_size_allowed(party, after))",
                                    "while (0)"), "party_sizes"),
    "forced-size-saved": (("after != before && mm_party_size_allowed(party, before)", "after != before"), "party_sizes"),
    "unchanged-size-saved": (("after != before && mm_party_size_allowed(party, before)", "mm_party_size_allowed(party, before)"),
                             "party_sizes"),
    "waiting-for-nobody": (("if (leader[0])\n\t\t\tusnprintf(line", "if (1)\n\t\t\tusnprintf(line"), "party_panel"),
    "big-party-not-skipped": (("while (first < P2P_MM_BIG_TEAM", "while (first < P2P_MM_FFA_6"), "party_panel"),
    "moved-member-lost": (("if (index < party->count && !strcmp(party->members[index].name, name))\n\t\treturn index;\n", ""),
                          "party_panel"),
    "no-standing-x": (("return matchmaking_party_member(party) || view->asking ? L\"\" : L\"X\";",
                        "return matchmaking_party_member(party) || view->asking ? L\"\" : L\"\";"), "friend_rows"),
    "x-while-asking": (("return matchmaking_party_member(party) || view->asking ?", "return matchmaking_party_member(party) ?"),
                       "friend_rows"),
    "friends-off-always-shown": (("else if (enabled && !friends->on)", "else if (!friends->on)"), "friends_line"),
    "unreadable-key-says-you-are": (("if (friends->on && friends->me[0])", "if (friends->me[0])"), "friends_line"),
    "unsaved-unsaid": (("if (friends->on && friends->unsaved)", "if (0)"), "friends_line"),
    "unmade-key-says-unreadable": (("friends->key_unmade ?", "0 ?"), "friends_line"),
    "focused-addable-says-remove": (("addable ? L\"A ADD FRIEND\" :\n\t\t\tmatchmaking_party_member(party) ? L\"\" : L\"X REMOVE\"", "matchmaking_party_member(party) ? L\"\" : L\"X REMOVE\""), "friend_rows"),
    "own-tag-unsaid": (("case P2P_FRIEND_ADD_SELF:", "case 98:"), "add_friend"),
    "add-friend-for-a-friend": (("!party->members[index].friend &&", ""), "friend_rows"),
    "add-friend-without-a-key": (("memcmp(party->members[index].friend_key, no_key, sizeof(no_key))", "1"), "friend_rows"),
    "add-friend-while-searching": (("!matchmaking_rows_locked(party) &&\n\t\tmatchmaking_party_addable", "matchmaking_party_addable"),
                                   "friend_rows"),
    "requests-over-three": (("MIN(P2P_FRIEND_REQUESTS_SHOWN, P2P_PARTY_MAXIMUM - MAX(party->count, 1))", "P2P_PARTY_MAXIMUM - MAX(party->count, 1)"), "friend_rows"),
    "requests-over-the-rows": (("MIN(P2P_FRIEND_REQUESTS_SHOWN, P2P_PARTY_MAXIMUM - MAX(party->count, 1))", "P2P_FRIEND_REQUESTS_SHOWN"), "friend_rows"),
    "request-row-no-focus-cue": (("focused ? L\"A ANSWER\" : L\"REQUEST\"", "L\"REQUEST\""), "friend_rows"),
    "request-help-without-answer": ((" A: answer YES or NO.", ""), "friend_rows"),
    "add-friend-with-friends-off": (("return friends_on && index >= 0", "return index >= 0"), "friend_rows"),
    "member-requests-hidden": (("if (party->search_state != P2P_MM_OFF)\n\t\treturn 0;", "if (matchmaking_rows_locked(party))\n\t\treturn 0;"), "friend_rows"),
    "no-server-unsaid": (("case P2P_FRIEND_ADD_NO_SERVER:", "case 99:"), "friend_rows"),
    "friends-over-a-search": (("!matchmaking_rows_locked(party) && party->count < P2P_PARTY_MAXIMUM", "party->count < P2P_PARTY_MAXIMUM"), "friend_layout"),
    "friends-in-a-full-party": (("!matchmaking_rows_locked(party) && party->count < P2P_PARTY_MAXIMUM", "!matchmaking_rows_locked(party)"), "friend_layout"),
    "friends-off-listed": (("return friends->on && !matchmaking_rows_locked(party)", "return !matchmaking_rows_locked(party)"), "friend_layout"),
    "add-friend-button-missing": (("if (adding)\n\t\treturn MATCHMAKING_BAR(add_friend)", "if (0)\n\t\treturn MATCHMAKING_BAR(add_friend)"), "add_friend"),
    "bare-tag-unnamed": (("size_t length, out = 0;", "size_t length, out = 1;"), "add_friend"),
    "tag-off-keyboard": (("*at > 0x7E", "*at > 0xFFFF"), "add_friend"),
    "tag-untrimmed": (("tag[length - 1] == L' '", "0"), "add_friend"),
    "tag-after-the-name": (("tag = at + 1;", "tag = at;"), "add_friend"),
    "lookup-unsaid": (("case P2P_FRIEND_ADD_LOOKING:", "case 98:"), "add_friend"),
    "request-sent-unsaid": (("case MATCHMAKING_REQUEST_SENT:", "case 97:"), "add_friend"),
    "heading-without-rows": (("if (requests || online)", "if (1)"), "friend_layout"),
    "friend-id-lost": (("strcpy(rows[total].id, friends->rows[index].id);", ""), "friend_layout"),
    "heading-on-the-bottom-slot": (("(scroll < last && rows[scroll + P2P_PARTY_MAXIMUM - 1].kind == _matchmaking_row_heading)", "0"), "friend_layout"),
    "heading-on-the-top-slot": (("(scroll > 0 && rows[scroll].kind == _matchmaking_row_heading) ||", ""), "friend_layout"),
    "scroll-past-the-end": (("PIN(scroll + step, 0, last)", "MAX(scroll + step, 0)"), "friend_layout"),
    "scroll-flickers-at-seven": (("for (turn = 0; turn < 2; turn++, direction = (short)-direction)", "for (turn = 0; turn < 1; turn++, direction = (short)-direction)"), "friend_layout"),
    "scroll-never-steps": (("scroll = (short)PIN(scroll + step, 0, last);", "scroll = (short)PIN(scroll, 0, last);"), "friend_layout"),
    "heading-passed-the-wrong-way": (("short direction = step < 0 ? -1 : 1, turn;", "short direction = 1, turn;"), "friend_layout"),
    "menus-friend-invitable": (("words = L\"IN MENUS\";", "words = L\"INVITE\";"), "friend_words"),
    "game-friend-invitable": (("words = L\"IN A GAME\";", "words = L\"INVITE\";"), "friend_words"),
    "update-friend-invitable": (("words = L\"UPDATE\";", "words = L\"INVITE\";"), "friend_words"),
    "offline-friend-invitable": (("words = L\"OFFLINE\";", "words = L\"INVITE\";"), "friend_words"),
    "invited-mark-lost": (("invited ? L\"INVITED\" : ", ""), "friend_words"),
    "friend-focus-cue-lost": (("focused ? L\"A INVITE\" : ", ""), "friend_words"),
    "friend-name-with-tag": (("matchmaking_tagless(friend_row->name_tag, name);", "mods_wide_text(friend_row->name_tag, name, P2P_PARTY_NAME_SIZE + 1);"), "friend_words"),
    "busy-friend-help-offers-invite": (("usnprintf(help, size - 1, friend_row->state == P2P_FRIEND_MATCHMAKING ?", "usnprintf(help, size - 1, 1 ?"), "friend_words"),
    "invite-prompt-one-line": (("%ls\\r\\ninvites you to a party", "%ls invites you to a party"), "friend_words"),
    "invite-prompt-full-stop": (("invites you to a party\"", "invites you to a party.\""), "friend_words"),
    "leave-first-unsaid": (("case MATCHMAKING_LEAVE_FIRST:", "case 99:"), "friend_words"),
    "removal-asks-nothing": (("from your friends?", "from your friends."), "friend_words"),
}


def generated(fault=None):
    source = read("port/linux/game/menu_functions.c")
    header = read("port/linux/src/p2p.h")
    search = read("port/linux/src/p2p_matchmaking.c")
    enums = "\n".join(["#define usnprintf swprintf", "#define ustrlen wcslen", "#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))", "#define NONE (-1)", "#define MIN(a, b) ((a) < (b) ? (a) : (b))", "#define MAX(a, b) ((a) > (b) ? (a) : (b))",
                       "#define MM_MAXIMUM_PARTY P2P_PARTY_MAXIMUM", "#define P2P_LISTING_REGION_SIZE 16",
                       "#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))",
                       enum_with(header, "P2P_MM_SLAYER"), enum_with(header, "P2P_MM_1V1"),
                       enum_with(header, "P2P_MM_LOOKING"), enum_with(header, "P2P_MM_NOTICE_HOST_LEFT"),
                       enum_with(header, "P2P_PARTY_MAXIMUM"), enum_with(header, "P2P_PARTY_NOTICE_NONE"),
                       structure(header, "p2p_mm_choice"), structure(header, "p2p_mm_status"),
                       structure(header, "p2p_party_member"), structure(header, "p2p_party_status"),
                       enum_with(header, "P2P_FRIEND_TAG_SIZE"), enum_with(header, "P2P_FRIEND_ADD_SENT"), structure(header, "p2p_friend_row"),
                       structure(header, "p2p_friends_status"),
                       enum_with(header, "P2P_FRIEND_OFFLINE"), enum_with(source, "_matchmaking_row_none"),
                       "#define MATCHMAKING_ROW_LIMIT (P2P_PARTY_MAXIMUM + 1 + P2P_FRIEND_REQUESTS_SHOWN + P2P_FRIENDS_MAXIMUM)",
                       "#define MATCHMAKING_LEAVE_FIRST 100", "#define MATCHMAKING_NO_TAG 101", "#define MATCHMAKING_REQUEST_SENT 102", structure(source, "matchmaking_row"),
                       enum_with(source, "_matchmaking_ask_none"), enum_with(source, "_matchmaking_bar_defaults"),
                       enum_with(source, "_matchmaking_search_bar_widen"),
                       "#define MATCHMAKING_BAR(button) (1u << _matchmaking_bar_##button)",
                       "#define MATCHMAKING_SEARCH_BAR(button) (1u << _matchmaking_search_bar_##button)",
                       structure(source, "matchmaking_party_view")]) + "\n"
    words = re.findall(r"^static wchar_t const \*const matchmaking_\w+_words\[.*?\};", source, re.M | re.S)
    code = "\n".join([*(function(search, name) for name in ("p2p_mm_size_players", "mm_party_size_allowed",
                                                            "mm_playlist_has_size")),
                      function(source, "mods_wide_text"), *words, function(source, "text_to_wide"),
                      function(source, "region_to_wide"),
                      *(function(source, name) for name in (
                          "matchmaking_party_member", "matchmaking_party_size", "matchmaking_size_saved", "matchmaking_size_after",
                          "matchmaking_party_find", "matchmaking_party_bar", "matchmaking_search_bar",
                          "matchmaking_party_name_utf8", "matchmaking_rows_locked", "matchmaking_party_addable",
                          "matchmaking_requests_shown", "matchmaking_adding", "matchmaking_rows_layout", "matchmaking_scroll_bad",
                          "matchmaking_scroll", "matchmaking_tagless", "matchmaking_friend_row", "matchmaking_friend_help",
                          "matchmaking_invite_help", "matchmaking_friend_remove_help", "matchmaking_request_row", "matchmaking_request_help",
                          "matchmaking_tag_typed", "matchmaking_add_words", "matchmaking_party_mark", "matchmaking_party_words",
                          "matchmaking_party_notice", "matchmaking_friends_line", "matchmaking_lines", "matchmaking_lobby_line"))])
    if fault:
        code = mutated(code, *fault)
    return (("enums.inc", enums), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("matchmaking_lines", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("matchmaking_lines", generated(fault)), case)
    assert status == CHECK_FAILED, f"menu_functions.c with a fault ({control}) passed '{case}': the test cannot see it"
