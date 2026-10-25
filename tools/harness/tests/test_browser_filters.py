"""The server browser's FILTERS panel (menu_functions.c's section of that
name): shown and hidden, OK, and each row's help."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run


def generated(fault=None):
    source = read('port/linux/game/menu_functions.c')
    code = re.search(r"^/\* ---- the server browser's FILTERS.*?(?=^/\* the row of the browser's chosen game)",
                     source, re.M | re.S).group(0)
    if fault:
        code = mutated(code, *fault)
    return (('under_test.inc', code),)


@pytest.mark.parametrize('case', ['show', 'save', 'shown', 'help'])
def test_case(case):
    status, output = run(build('browser_filters', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('	list->visible = !shown;', ';'), 'show'),
    (('	menu->focused_child = menu->child;', ';'), 'show'),
    (('screen->focused_child = shown ? panel : list;', ';'), 'show'),
    (('	browser_filters_show(screen_of(widget), FALSE);', ';'), 'save'),
    (('multiplayer.mode == _multiplayer_mode_server_browser && ', ''), 'shown'),
    (('(filter_rows[index].help_each_value ? spinner->parameters.list.selected_index : 0)',
      'spinner->parameters.list.selected_index'), 'help'),
    (('{ "game_options_spinner", 6, TRUE },', '{ "game_options_spinner", 5, TRUE },'), 'help'),
])
def test_negative_control(fault, case):
    status, output = run(build('browser_filters', generated(fault)), case)
    assert status == CHECK_FAILED, output
