"""MATCH FOUND shows however fast the join (matchmaking.c)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

FAULT = ("if (matchmaking.seconds < MATCHMAKING_FOUND_SECONDS)\n\t\t\treturn;\n", "")


def generated(fault=None):
    source = read("port/linux/game/matchmaking.c")
    defines = [line for line in source.splitlines()
               if line.startswith(("#define MATCHMAKING_JOIN_SECONDS", "#define MATCHMAKING_FOUND_SECONDS"))]
    enums = "\n".join([*defines, enum_with(read("port/linux/src/p2p.h"), "P2P_JOIN_FAILED"),
                       enum_with(source, "_matchmaking_joining"), enum_with(source, "_client_state_searching")]) + "\n"
    code = function(source, "matchmaking_update_join")
    if fault:
        code = mutated(code, *fault)
    return (("enums.inc", enums), ("under_test.inc", code + "\n"))


def test_fast_join():
    status, output = run(build("matchmaking_found_hold", generated()), "fast_join")
    assert status == 0, output


def test_negative_control():
    status, output = run(build("matchmaking_found_hold", generated(FAULT)), "fast_join")
    assert status == CHECK_FAILED, output
