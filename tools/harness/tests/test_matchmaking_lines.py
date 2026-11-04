"""The search screens' words (menu_functions.c)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["looking", "queued", "widened", "hosting", "found", "big_looking", "big_hosting", "big_found", "lobby",
         "notices", "member_view", "member_queue", "member_found", "party_sizes", "party_panel", "party_notices"]

NEGATIVE_CONTROLS = {
    "strict-offered-2:00": (("status->can_host > 0 ? L\"%d more", "1 ? L\"%d more"), "queued"),
    "full-before-pool": (("if (status->count <= 0)\n\t\t\t\tbreak;\n", ""), "queued"),
    "never-widened": (("if (status->widened)", "if (0)"), "widened"),
    "number-without-countdown": (("seconds >= 0", "seconds >= -1"), "lobby"),
    "last-second-lost": (("seconds >= 0", "seconds > 0"), "lobby"),
    "big-looking-as-small": (("int big = p2p_mm_size_big(status->size);", "int big = 0;"), "big_looking"),
    "big-hosting-as-small": (("int big = p2p_mm_size_big(status->size);", "int big = 0;"), "big_hosting"),
    "big-found-as-small": (("int big = p2p_mm_size_big(status->size);", "int big = 0;"), "big_found"),
    "big-queue-words": (("big && status->state == P2P_MM_QUEUED ? P2P_MM_LOOKING : ", ""), "big_looking"),
    "member-zero-of-four": (("if (status->most_players)\n\t\t\tusnprintf(count", "if (1)\n\t\t\tusnprintf(count"),
                            "member_found"),
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
    "waiting-for-nobody": (("if (leader[0] && view", "if (view"), "party_panel"),
    "big-party-not-skipped": (("while (first < P2P_MM_BIG_TEAM", "while (first < P2P_MM_FFA_6"), "party_panel"),
    "moved-member-lost": (("if (index < party->count && !strcmp(party->members[index].name, name))\n\t\treturn index;\n", ""),
                          "party_panel"),
    "no-standing-x": (("view->asking ? L\"\" : L\"X\"", "view->asking ? L\"\" : L\"\""), "party_panel"),
    "x-while-asking": (("matchmaking_party_member(party) || view->asking ?", "matchmaking_party_member(party) ?"), "party_panel"),
}


def generated(fault=None):
    source = read("port/linux/game/menu_functions.c")
    header = read("port/linux/src/p2p.h")
    search = read("port/linux/src/p2p_matchmaking.c")
    enums = "\n".join(["#define usnprintf swprintf", "#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))", "#define NONE (-1)",
                       "#define MM_MAXIMUM_PARTY P2P_PARTY_MAXIMUM", "#define P2P_LISTING_REGION_SIZE 16",
                       "#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))",
                       enum_with(header, "P2P_MM_SLAYER"), enum_with(header, "P2P_MM_1V1"),
                       enum_with(header, "P2P_MM_LOOKING"), enum_with(header, "P2P_MM_NOTICE_HOST_LEFT"),
                       enum_with(header, "P2P_PARTY_MAXIMUM"), enum_with(header, "P2P_PARTY_NOTICE_NONE"),
                       structure(header, "p2p_mm_choice"), structure(header, "p2p_mm_status"),
                       structure(header, "p2p_party_member"), structure(header, "p2p_party_status"),
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
                          "matchmaking_party_name_utf8", "matchmaking_party_mark", "matchmaking_party_words",
                          "matchmaking_party_notice", "matchmaking_lines", "matchmaking_lobby_line"))])
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
