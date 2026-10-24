"""The real button of coop_spectate.c's lines: jump's key, in capitals, for the keyboard's player; A for the others."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

# a fault in coop_spectate.c and the cases that must catch it
NEGATIVE_CONTROLS = {
    "always-a": (("halo_keyboard_prompts(local_player_index) != HALO_KEYBOARD_PROMPTS_NONE", "FALSE"), ["keyboard"]),
    "always-key": (("halo_keyboard_prompts(local_player_index) != HALO_KEYBOARD_PROMPTS_NONE", "TRUE"),
                   ["controller"]),
}


def generated(fault=None):
    keyboard = read("port/linux/include/halo_keyboard.h")
    config = enum_with(keyboard, "HALO_KEYBOARD_MOVE_FORWARD") + "\n" + \
        enum_with(keyboard, "HALO_KEYBOARD_PROMPTS_NONE") + "\n"
    code = function(read("port/linux/game/coop_spectate.c"), "next_button_name")
    if fault:
        code = mutated(code, *NEGATIVE_CONTROLS[fault][0])
    return (("config.inc", config), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", ["keyboard", "controller"])
def test_coop_prompt_lines(case):
    status, output = run(build("coop_prompt_lines", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [(fault, case) for fault, (_, cases) in NEGATIVE_CONTROLS.items()
                                        for case in cases])
def test_fault_detected(fault, case):
    status, output = run(build("coop_prompt_lines", generated(fault)), case)
    assert status == CHECK_FAILED, output
