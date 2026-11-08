"""The real device choice of xinput_sdl.c for controller 1's button prompts: the keyboard and mouse's or the
gamepad's, whichever had the latest new press; a moved mouse, a held stick or key, and other controllers do not."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["starts-keyboard", "gamepad-then-key", "held-stick", "held-key", "mouse", "key-swap", "pad-swap", "both-at-once",
         "other-controllers", "menus"]

# a fault in xinput_sdl.c and the cases that must catch it
NEGATIVE_CONTROLS = {
    "gamepad-held-not-pressed": (("(gamepad_held & ~gamepad_held_before)", "gamepad_held"), ["held-stick"]),
    "key-held-not-pressed": (("(held && !keyboard_held_before[index])", "held"), ["held-key"]),
    "mouse-motion-counts": (("input->mouse_wheel != 0.0f;", "input->mouse_wheel != 0.0f || input->mouse_dx != 0.0f;"),
                            ["mouse"]),
    "buttons-counted": (("(gamepad_held & ~gamepad_held_before)", "(__builtin_popcountll(gamepad_held) > __builtin_popcountll(gamepad_held_before))"),
                        ["pad-swap"]),
    "every-controller": (("controller_index != 0 || ", ""), ["other-controllers"]),
}


def generated(fault=None):
    inputs = read("port/linux/src/xinput_sdl.c")
    config = enum_with(read("port/linux/include/halo_keyboard.h"), "HALO_KEYBOARD_PROMPTS_NONE") + "\n"
    # the prompts' state as the source declares it, for a desktop build
    state = re.search(r"#ifdef HALO_ANDROID\nstatic BOOL keyboard_prompts.*?static BOOL prompts_in_menus;\n",
                      inputs, re.S)
    if not state:
        raise LookupError("the prompts' state is not found in xinput_sdl.c")
    code = state.group() + "\n".join(function(inputs, name) for name in
                                     ["keyboard_pressed", "prompt_device_update", "halo_keyboard_prompts"])
    if fault:
        code = mutated(code, *NEGATIVE_CONTROLS[fault][0])
    return (("config.inc", config), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_prompt_device(case):
    status, output = run(build("prompt_device", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [(fault, case) for fault, (_, cases) in NEGATIVE_CONTROLS.items()
                                        for case in cases])
def test_fault_detected(fault, case):
    status, output = run(build("prompt_device", generated(fault)), case)
    assert status == CHECK_FAILED, output
