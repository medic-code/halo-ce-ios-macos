"""The search screens' words (menu_functions.c)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["looking", "queued", "widened", "hosting", "found", "big_looking", "big_hosting", "big_found", "lobby",
         "notices"]

NEGATIVE_CONTROLS = {
    "strict-offered-2:00": (("status->can_host > 0 ? L\"%d more", "1 ? L\"%d more"), "queued"),
    "never-widened": (("if (status->widened)", "if (0)"), "widened"),
    "number-without-countdown": (("seconds >= 0", "seconds >= -1"), "lobby"),
    "last-second-lost": (("seconds >= 0", "seconds > 0"), "lobby"),
    "big-looking-as-small": (("int big = p2p_mm_size_big(status->size);", "int big = 0;"), "big_looking"),
    "big-hosting-as-small": (("int big = p2p_mm_size_big(status->size);", "int big = 0;"), "big_hosting"),
    "big-found-as-small": (("int big = p2p_mm_size_big(status->size);", "int big = 0;"), "big_found"),
    "big-queue-words": (("big && status->state == P2P_MM_QUEUED ? P2P_MM_LOOKING : ", ""), "big_looking"),
    "region-lost": (("if (status->region[0])", "if (0)"), "big_found"),
}


def generated(fault=None):
    source = read("port/linux/game/menu_functions.c")
    header = read("port/linux/src/p2p.h")
    enums = "\n".join(["#define usnprintf swprintf", "#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))",
                       "#define P2P_LISTING_REGION_SIZE 16", "#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))",
                       enum_with(header, "P2P_MM_SLAYER"), enum_with(header, "P2P_MM_1V1"),
                       enum_with(header, "P2P_MM_LOOKING"), enum_with(header, "P2P_MM_NOTICE_HOST_LEFT"),
                       structure(header, "p2p_mm_status")]) + "\n"
    words = re.findall(r"^static wchar_t const \*const matchmaking_\w+_words\[.*?\};", source, re.M | re.S)
    code = "\n".join([*words, function(source, "text_to_wide"), function(source, "region_to_wide"),
                      function(source, "matchmaking_lines"), function(source, "matchmaking_lobby_line")])
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
