"""MATCHMAKING's YES and NO for X's removal, LEAVE PARTY and a friend's invite, with keys and with the pointer (menu_functions.c)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

NAMES = ("matchmaking_party_member", "sibling_index", "matchmaking_slot_row", "focus_row", "matchmaking_bar_focus", "matchmaking_refocus",
         "matchmaking_ask", "matchmaking_party_find", "matchmaking_remove_ask", "matchmaking_request_ask", "matchmaking_unask", "matchmaking_invite_answer",
         "matchmaking_answer", "focus_off_hidden", "matchmaking_search_bar", "matchmaking_search_update",
         "matchmaking_search_reply", "matchmaking_search_back")


def generated(fault=None):
    source = read("port/linux/game/menu_functions.c")
    header = read("port/linux/src/p2p.h")
    enums = "\n".join(["#define P2P_LISTING_REGION_SIZE 16", "#define usnprintf swprintf", "#define MATCHMAKING_TEXT_LENGTH 160",
                       "#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))", "#define TICKER_SEPARATOR L\" \"",
                       enum_with(header, "P2P_MM_SLAYER"), enum_with(header, "P2P_MM_1V1"), enum_with(header, "P2P_MM_LOOKING"),
                       structure(header, "p2p_mm_choice"), structure(header, "p2p_mm_status"), enum_with(header, "P2P_PARTY_MAXIMUM"), enum_with(header, "P2P_FRIEND_TAG_SIZE"),
                       structure(header, "p2p_party_member"),
                       structure(header, "p2p_party_status"), enum_with(header, "P2P_FRIEND_ADD_SENT"), enum_with(header, "P2P_FRIEND_ANSWER_DONE"),
                       enum_with(source, "_matchmaking_row_none"), "#define MATCHMAKING_LEAVE_FIRST 100",
                       structure(source, "matchmaking_row"), enum_with(source, "_matchmaking_ask_none"),
                       enum_with(source, "_matchmaking_search_bar_cancel"),
                       "#define MATCHMAKING_SEARCH_BAR(button) (1u << _matchmaking_search_bar_##button)",
                       re.search(r"^static char const \*const matchmaking_search_bar_names\[.*?\};", source, re.M | re.S).group(0)]) + "\n"
    code = "\n".join(function(source, name) for name in NAMES)
    if fault:
        code = mutated(code, *fault)
    return (("enums.inc", enums), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", ["keys_remove", "pointer_remove", "pointer_leave", "friend_remove", "request_full", "invite_answer",
                                  "search_invite",
                                  "search_update", "search_reply", "search_leave_first", "search_back"])
def test_case(case):
    status, output = run(build("matchmaking_confirm", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [
    (("list->focused_child = child;\n\tif (child)", "if (child)"), "keys_remove"),
    (("if (index < party.count)\n\t\t\tp2p_party_remove(index);", "(void)index;"), "keys_remove"),
    (("else if (yes && asking == _matchmaking_ask_friend_remove)", "else if (asking == _matchmaking_ask_friend_remove)"), "friend_remove"),
    (("strcpy(matchmaking_screen.asked_id, row->id);\n\t\treturn matchmaking_ask(slot, _matchmaking_ask_friend_remove);",
      "return matchmaking_ask(slot, _matchmaking_ask_friend_remove);"), "friend_remove"),
    (("\t\tmatchmaking_screen.added = MATCHMAKING_LEAVE_FIRST;\n\t\tmatchmaking_screen.added_row = row;",
      "\t\tmatchmaking_screen.added_row = row;"), "invite_answer"),
    (("if (answer == P2P_FRIEND_ANSWER_LEAVE_FIRST)", "if (answer < 0)"), "invite_answer"),
    (("if (answer == P2P_FRIEND_ANSWER_LEAVE_FIRST)", "if (answer != P2P_FRIEND_ANSWER_DONE)"),
     "invite_answer"),
    (("== P2P_FRIEND_ANSWER_DONE && yes)", "== P2P_FRIEND_ANSWER_DONE)"), "search_invite"),
    (("== P2P_FRIEND_ANSWER_DONE && yes)", "!= P2P_FRIEND_ANSWER_FAILED && yes)"), "search_invite"),
    (("invited = p2p_friend_invite_pending(invite, sizeof(invite));", "invited = FALSE;"), "search_update"),
    (("\t\tmatchmaking_bar_focus(bar, \"search_button_no\");", "\t\t(void)0;"), "search_update"),
    (("if (p2p_friend_invite_pending(invite, sizeof(invite)))\n\t\treturn matchmaking_search_reply(widget, FALSE, widget_deleted);\n", ""),
     "search_back"),
    (("if (!p2p_friend_invite_pending(invite, sizeof(invite)))\n\t\treturn TRUE;\n", ""), "search_reply"),
    (("\t\tmatchmaking_screen.added = MATCHMAKING_LEAVE_FIRST;\n\t\tmatchmaking_screen.added_row = row;", "\t\t(void)row;"),
     "search_leave_first"),
    (("\tif (invited)\n\t\tmatchmaking_screen.added = P2P_FRIEND_ADD_SENT;\n", ""), "search_leave_first"),
    (("\t\tmatchmaking_screen.added = P2P_FRIEND_ADD_FULL;\n", ""), "request_full"),
])
def test_negative_control(fault, case):
    status, output = run(build("matchmaking_confirm", generated(fault)), case)
    assert status == CHECK_FAILED, output
