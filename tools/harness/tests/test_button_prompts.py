"""The real button_prompts.c and the key names of xinput_sdl.c: for the keyboard's player a message's icon is the
key bound to its action (the action's name when none is), a button the key of the action the layout puts on it, and
in the menus the key that presses it; for anyone else, the icon."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["controller-unchanged", "other-players", "action-keys", "rebound", "unbound", "buttons-in-game", "menus",
         "custom-icons"]

# a fault in button_prompts.c or xinput_sdl.c and the cases that must catch it
NEGATIVE_CONTROLS = {
    "crouch-zoom-swapped": (("HALO_KEYBOARD_CROUCH,\n\tHALO_KEYBOARD_ZOOM,\n\tHALO_KEYBOARD_JUMP,",
                             "HALO_KEYBOARD_ZOOM,\n\tHALO_KEYBOARD_CROUCH,\n\tHALO_KEYBOARD_JUMP,"), ["action-keys"]),
    "keys-for-everyone": (("prompts == HALO_KEYBOARD_PROMPTS_NONE || ", ""), ["controller-unchanged"]),
    "no-menu-keys": (("prompts == HALO_KEYBOARD_PROMPTS_MENUS &&", "FALSE &&"), ["menus"]),
    "no-action-name": (("\tif (!name[0])\n\t{\n\t\tchar *character;", "\tif (FALSE)\n\t{\n\t\tchar *character;"),
                       ["unbound"]),
}

PROMPTS = "port/linux/game/button_prompts.c"


def generated(fault=None):
    keyboard = read("port/linux/include/halo_keyboard.h")
    inputs = read("port/linux/src/xinput_sdl.c")
    settings = re.search(r"static const char \*const binding_settings\[[^\]]*\] =\n\{.*?\n\};\n", inputs, re.S)
    if not settings:
        raise LookupError("binding_settings is not found in xinput_sdl.c")
    config = "\n".join([
        enum_with(keyboard, "HALO_KEYBOARD_MOVE_FORWARD"),
        enum_with(keyboard, "HALO_KEYBOARD_PROMPTS_NONE"),
        structure(read("source/input/input_abstraction.h"), "game_input_preferences"),
        re.search(r"#define MAXIMUM_BINDINGS \d+", inputs).group(),
        settings.group(),
    ]) + "\n"
    prompts = read(PROMPTS)
    code = function(inputs, "halo_keyboard_action_key") + "\n" + prompts[prompts.index("/* ---------- constants */"):]
    if fault:
        code = mutated(code, *NEGATIVE_CONTROLS[fault][0])
    return (("config.inc", config), ("under_test.inc", code + "\n"))


def icon_members(source, first):
    """an icon enum's members from its first, without the prefix"""
    enum = enum_with(source, first)
    return [name.split("_icon_", 1)[1] for name in re.findall(r"\b(\w*_icon_\w+)\s*,", enum)]


def test_icons_in_the_games_order():
    ours = icon_members(read(PROMPTS), "_prompt_icon_a_button")
    hud = icon_members(read("source/interface/hud_messaging.c"), "_icon_a_button,\n\t_icon_b_button")
    assert hud[:len(ours)] == ours
    widget = read("source/interface/ui_widget.c")
    names = re.findall(r'L"([\w-]+)"', widget[widget.index("static wchar_t const *icon_names"):][:1200])
    assert [name.replace("-", "_") for name in names[:len(ours)]] == ours


def test_controls_in_the_games_order():
    prompts = read(PROMPTS)
    ours = re.findall(r"HALO_KEYBOARD_\w+", prompts[prompts.index("control_actions[] ="):][:600])[:12]
    abstraction = read("source/input/input_abstraction.c")
    table = abstraction[abstraction.index("keyboard_game_controls[NUMBER_OF_GAME_CONTROLS] ="):][:600]
    theirs = re.findall(r"(HALO_KEYBOARD_\w+|-1)", table)[:12]
    assert [ours[i] for i, name in enumerate(theirs) if name != "-1"] == [name for name in theirs if name != "-1"]
    assert [ours[i] for i, name in enumerate(theirs) if name == "-1"] == ["HALO_KEYBOARD_PAUSE",
                                                                          "HALO_KEYBOARD_SCOREBOARD"]


@pytest.mark.parametrize("case", CASES)
def test_button_prompts(case):
    status, output = run(build("button_prompts", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [(fault, case) for fault, (_, cases) in NEGATIVE_CONTROLS.items()
                                        for case in cases])
def test_fault_detected(fault, case):
    status, output = run(build("button_prompts", generated(fault)), case)
    assert status == CHECK_FAILED, output
