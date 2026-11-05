"""MATCHMAKING's YES and NO for X's removal and LEAVE PARTY, with keys and with the pointer (menu_functions.c)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

NAMES = ("matchmaking_party_member", "sibling_index", "focus_row", "matchmaking_bar_focus", "matchmaking_refocus",
         "matchmaking_ask", "matchmaking_party_find", "matchmaking_remove_ask", "matchmaking_answer")


def generated(fault=None):
    source = read("port/linux/game/menu_functions.c")
    header = read("port/linux/src/p2p.h")
    enums = "\n".join(["#define P2P_LISTING_REGION_SIZE 16", enum_with(header, "P2P_MM_SLAYER"),
                       enum_with(header, "P2P_MM_1V1"), structure(header, "p2p_mm_choice"),
                       enum_with(header, "P2P_PARTY_MAXIMUM"), structure(header, "p2p_party_member"),
                       structure(header, "p2p_party_status"), enum_with(source, "_matchmaking_ask_none")]) + "\n"
    code = "\n".join(function(source, name) for name in NAMES)
    if fault:
        code = mutated(code, *fault)
    return (("enums.inc", enums), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", ["keys_remove", "pointer_remove", "pointer_leave"])
def test_case(case):
    status, output = run(build("matchmaking_confirm", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [
    (("list->focused_child = child;\n\tif (child)", "if (child)"), "keys_remove"),
    (("if (index < party.count)\n\t\t\tp2p_party_remove(index);", "(void)index;"), "keys_remove"),
])
def test_negative_control(fault, case):
    status, output = run(build("matchmaking_confirm", generated(fault)), case)
    assert status == CHECK_FAILED, output
