"""A search's notice (p2p_matchmaking.c)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["lost_host", "unreached"]

NEGATIVE_CONTROLS = {
    "cleared-by-step-1": (("state == P2P_MM_LOOKING || ", ""), "lost_host"),
    "never-cleared": (("? notice : P2P_MM_NOTICE_NONE", "? notice : notice"), "lost_host"),
}


def generated(fault=None):
    header = read("port/linux/src/p2p.h")
    code = function(read("port/linux/src/p2p_matchmaking.c"), "notice_after")
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", "\n".join([enum_with(header, "P2P_MM_LOOKING"),
                                          enum_with(header, "P2P_MM_NOTICE_HOST_LEFT"), code]) + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("matchmaking_notice", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("matchmaking_notice", generated(fault)), case)
    assert status == CHECK_FAILED, f"p2p_matchmaking.c with a fault ({control}) passed '{case}': the test cannot see it"
