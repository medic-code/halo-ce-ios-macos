"""The server browser's FILTERS panel (menu_functions.c's section of that
name): its rows' values read from their settings, shown, kept, written and
applied to the lobby's filter, each row's help, and SHOW MISSING MAPS."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, mutated, read, run, structure


def generated(fault=None):
    source = read('port/linux/game/menu_functions.c')
    lobby = read('port/linux/src/p2p.h')
    code = re.search(r"^/\* ---- the server browser's FILTERS.*?(?=^/\* the row of the browser's chosen game)",
                     source, re.M | re.S).group(0)
    if fault:
        code = mutated(code, *fault)
    return (('lobby.inc', structure(lobby, 'p2p_lobby_filter') + '\n' + enum_with(lobby, 'P2P_LOBBY_TEAMS_ANY')),
            ('under_test.inc', code))


@pytest.mark.parametrize('case', ['load', 'apply', 'show', 'save', 'shown', 'help', 'maps'])
def test_case(case):
    status, output = run(build('browser_filters', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('filter.hide_empty = !filter_values[_filter_empty];', 'filter.hide_empty = filter_values[_filter_empty];'),
     'apply'),
    (('atoi(filter_pings[PIN(filter_values[_filter_ping], 0, NUMBEROF(filter_pings) - 2)])',
      'filter_values[_filter_ping]'), 'apply'),
    (('			config_default(filter_rows[row].setting, text, sizeof(text));', ';'), 'load'),
    (('		config_text(filter_rows[row].setting, text, sizeof(text));', ';'), 'load'),
    (('{ "game_type_spinner", "network.browser_game_type", filter_engines, 5, FALSE },',
      '{ "game_type_spinner", "network.browser_game_type", filter_teams, 5, FALSE },'), 'load'),
    (('value == filter_values[index] || ', ''), 'save'),
    (('		if (!config_write(filter_rows[index].setting, filter_rows[index].values[value]))\n'
      '			platform_log("menus: could not write %s to config.toml", filter_rows[index].setting);\n', ''),
     'save'),
    (('filter.teams = filter_values[_filter_teams];', ';'), 'apply'),
    (('	list->visible = !shown;', ';'), 'show'),
    (('spinner->parameters.list.selected_index = filter_values[index];', ';'), 'show'),
    (('	menu->focused_child = menu->child;', ';'), 'show'),
    (('screen->focused_child = shown ? panel : list;', ';'), 'show'),
    (('		filter_values[index] = value;', ';'), 'save'),
    (('	browser_filter_apply();\n	lobby_browser.first = 0;', '	lobby_browser.first = 0;'), 'save'),
    (('	lobby_browser.first = 0;', ';'), 'save'),
    (('multiplayer.mode == _multiplayer_mode_server_browser && ', ''), 'shown'),
    (('(filter_rows[index].help_each_value ? spinner->parameters.list.selected_index : 0)',
      'spinner->parameters.list.selected_index'), 'help'),
    (('filter_teams, 6, TRUE },', 'filter_teams, 5, TRUE },'), 'help'),
    (('	if (filter_values[_filter_missing_maps])', '	if (0)'), 'maps'),
    (('strlen(map) < P2P_LISTING_MAP_SIZE && ', ''), 'maps'),
    ((' && campaign_level_of(map) == NONE', ''), 'maps'),
    (('missing = missing && !custom_edition_map_file_present(map);', ';'), 'maps'),
    (('			return browser_maps[index].missing;', '			break;'), 'maps'),
])
def test_negative_control(fault, case):
    status, output = run(build('browser_filters', generated(fault)), case)
    assert status == CHECK_FAILED, output
