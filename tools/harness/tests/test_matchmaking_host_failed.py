"""A game that could not be hosted is said, and the search goes on (matchmaking.c)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402


def generated(fault=None):
    source = read("port/linux/game/matchmaking.c")
    header = read("port/linux/src/p2p.h")
    sound = next(line for line in source.splitlines() if line.startswith("#define MATCHMAKING_SOUND_ERROR"))
    enums = "\n".join([sound, "#define P2P_LISTING_INVITE_SIZE 77", enum_with(header, "P2P_MM_SLAYER"),
                       enum_with(header, "P2P_MM_1V1"), structure(header, "p2p_mm_decision"),
                       enum_with(source, "_matchmaking_joining")]) + "\n"
    code = function(source, "matchmaking_begin_host")
    if fault:
        code = mutated(code, *fault)
    return (("enums.inc", enums), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", ["failed", "hosted"])
def test_case(case):
    status, output = run(build("matchmaking_host_failed", generated()), case)
    assert status == 0, output


def test_negative_control():
    status, output = run(build("matchmaking_host_failed", generated(("p2p_mm_host_gave_up();", "p2p_mm_cancel();"))),
                         "failed")
    assert status == CHECK_FAILED, output
