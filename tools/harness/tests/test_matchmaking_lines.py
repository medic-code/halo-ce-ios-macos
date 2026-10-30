"""The search screens' words (menu_functions.c)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["looking", "queued", "widened", "hosting", "found", "notices"]

NEGATIVE_CONTROLS = {
    "strict-offered-2:00": (("status->can_host > 0 ? L\"%d more", "1 ? L\"%d more"), "queued"),
    "full-before-pool": (("if (status->count <= 0)\n\t\t\t\tbreak;\n", ""), "queued"),
    "never-widened": (("if (status->widened)", "if (0)"), "widened"),
}


def generated(fault=None):
    source = read("port/linux/game/menu_functions.c")
    header = read("port/linux/src/p2p.h")
    enums = "\n".join(["#define usnprintf swprintf", "#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))",
                       enum_with(header, "P2P_MM_SLAYER"), enum_with(header, "P2P_MM_1V1"),
                       enum_with(header, "P2P_MM_LOOKING"), enum_with(header, "P2P_MM_NOTICE_HOST_LEFT"),
                       structure(header, "p2p_mm_status")]) + "\n"
    words = re.findall(r"^static wchar_t const \*const matchmaking_\w+_words\[.*?\};", source, re.M | re.S)
    code = "\n".join([*words, function(source, "matchmaking_lines")])
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
