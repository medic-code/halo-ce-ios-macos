"""Step 1's pick of a public game for a matchmaking search (p2p_lobby.c)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure


def generated(fault=None):
    header = read('port/linux/src/p2p.h')
    version = constant(read('port/linux/include/halo_port_limits.h'), 'HALO_PORT_NETWORK_VERSION')
    enums = '\n'.join([f'#define HALO_PORT_NETWORK_VERSION {version}',
                       enum_with(header, 'P2P_LISTING_NAME_SIZE = 32'),
                       enum_with(header, 'P2P_LOBBY_PICK_PING = 200'),
                       structure(header, 'p2p_listing'),
                       structure(header, 'p2p_lobby_wanted')]) + '\n'
    source = read('port/linux/src/p2p_lobby.c')
    matchmaking = read('port/linux/src/p2p_matchmaking.c')
    code = '\n'.join([function(source, 'pick_fits'), function(source, 'p2p_lobby_pick'),
                      function(matchmaking, 'game_has_room')])
    if fault:
        code = mutated(code, *fault)
    return (('enums.inc', enums), ('under_test.inc', code))


CASES = ['e1_fits', 'e2_most_players', 'e3_passed_over', 'e51_e56_dedicated', 'e31_big', 'e46_region_first',
         'e52_e57_region_only', 'e53_e58_no_region', 'e55_empty_dedicated', 'e54_no_masters', 'small_unchanged',
         'e28_room', 'public_room', 'in_progress']


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('matchmaking_pick', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('game->player_count > games[best].player_count', 'game->player_count < games[best].player_count'),
     'e2_most_players'),
    (('game->ping >= P2P_LOBBY_PICK_PING', 'game->ping > P2P_LOBBY_PICK_PING'), 'e3_passed_over'),
    (('if (game->dedicated)', 'if (0)'), 'e51_e56_dedicated'),
    (('!game->has_teams != !wanted->teams', '0'), 'e3_passed_over'),
    (('!wanted->region[0] || !strcasecmp(game->region, wanted->region)', '1'), 'e52_e57_region_only'),
    (('preferred > best_preferred', 'preferred < best_preferred'), 'e53_e58_no_region'),
    (('!game->player_count && !(wanted->big && game->dedicated)', '!game->player_count'), 'e55_empty_dedicated'),
    (('2 * wanted->party - 1', 'wanted->party'), 'public_room'),
    (('game->has_teams ? 2 * wanted->party - 1 : wanted->party', '2 * wanted->party - 1'), 'public_room'),
    (('wanted->party > 1 &&', '0 &&'), 'public_room'),
    (('game->in_progress ||', ''), 'in_progress'),
    (('wanted->party > 1 &&', 'wanted->party &&'), 'in_progress'),
    (('(red < blue ? red : blue) + party', 'blue + party'), 'e28_room'),
    (('(red < blue ? red : blue) + party', 'red + party'), 'e28_room'),
    (('!teamed ||', '1 ||'), 'e28_room'),
    (('if (teamed && party > 1 && !red && players)', 'if (party > 1 && !red && players)'), 'e28_room'),
    (('if (teamed && party > 1 && !red && players)', 'if (teamed && !red && players)'), 'e28_room'),
    (('if (teamed && party > 1 && !red && players)', 'if (teamed && party > 1 && !red)'), 'e28_room'),
    (('players + party <= most_players', '1'), 'e28_room'),
])
def test_negative_control(fault, case):
    status, output = run(build('matchmaking_pick', generated(fault)), case)
    assert status == CHECK_FAILED, output
