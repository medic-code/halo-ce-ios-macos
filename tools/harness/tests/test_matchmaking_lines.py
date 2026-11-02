"""The search screens' words (menu_functions.c)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["looking", "queued", "widened", "hosting", "found", "big_looking", "big_hosting", "big_found", "lobby",
         "notices", "party_sizes"]

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
    "region-lost": (("if (status->region[0])", "if (0)"), "big_found"),
    "size-not-skipped": (("for (tries = 0; tries < NUMBER_OF_P2P_MM_SIZES && !mm_party_size_allowed(party, size); tries++)",
                          "for (tries = 0; tries < 0; tries++)"), "party_sizes"),
    "skip-only-goes-up": (("step = to == (from + NUMBER_OF_P2P_MM_SIZES - 1) % NUMBER_OF_P2P_MM_SIZES ?", "step = 0 ?"),
                          "party_sizes"),
    "fallback-ignores-the-party": (("while (after < P2P_MM_FFA_6 && !mm_party_size_allowed(party, after))",
                                    "while (0)"), "party_sizes"),
    "forced-size-saved": (("after != before && mm_party_size_allowed(party, before)", "after != before"), "party_sizes"),
    "unchanged-size-saved": (("after != before && mm_party_size_allowed(party, before)", "mm_party_size_allowed(party, before)"),
                             "party_sizes"),
}


def generated(fault=None):
    source = read("port/linux/game/menu_functions.c")
    header = read("port/linux/src/p2p.h")
    search = read("port/linux/src/p2p_matchmaking.c")
    enums = "\n".join(["#define usnprintf swprintf", "#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))",
                       "#define MM_MAXIMUM_PARTY P2P_PARTY_MAXIMUM", "#define P2P_LISTING_REGION_SIZE 16",
                       "#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))",
                       enum_with(header, "P2P_MM_SLAYER"), enum_with(header, "P2P_MM_1V1"),
                       enum_with(header, "P2P_MM_LOOKING"), enum_with(header, "P2P_MM_NOTICE_HOST_LEFT"),
                       enum_with(header, "P2P_PARTY_MAXIMUM"), structure(header, "p2p_mm_status")]) + "\n"
    words = re.findall(r"^static wchar_t const \*const matchmaking_\w+_words\[.*?\};", source, re.M | re.S)
    code = "\n".join([*(function(search, name) for name in ("p2p_mm_size_players", "mm_party_size_allowed",
                                                            "mm_playlist_has_size")),
                      *words, function(source, "text_to_wide"), function(source, "region_to_wide"),
                      *(function(source, name) for name in (
                          "matchmaking_party_size", "matchmaking_size_saved", "matchmaking_size_after",
                          "matchmaking_lines", "matchmaking_lobby_line"))])
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
