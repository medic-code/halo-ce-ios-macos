"""A matchmade host's team rule: each party whole on one team (matchmaking.c)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure


def generated(fault=None):
    header = read('port/linux/src/p2p.h')
    enums = '\n'.join(['#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 16',
                       '#define csmemcmp memcmp',
                       '#define csmemcpy memcpy',
                       enum_with(header, 'P2P_PLAYER_KEY_HASH_SIZE = 16'),
                       enum_with(header, 'P2P_PARTY_MAXIMUM = 8'),
                       enum_with(header, 'P2P_MM_TEAM_PARTIES ='),
                       structure(header, 'p2p_mm_team_party')]) + '\n'
    source = read('port/linux/game/matchmaking.c')
    code = '\n'.join([f"#define MATCHMAKING_MOST_WAITING {constant(source, 'MATCHMAKING_MOST_WAITING')}",
                      structure(source, 'team_party'),
                      function(source, 'machine_party'),
                      function(source, 'party_team'),
                      function(source, 'matchmaking_team'),])
    if fault:
        code = mutated(code, *fault)
    return (('enums.inc', enums), ('under_test.inc', code))


CASES = ['e21_whole', 'e27_leader_hosts', 'closest_split', 'e28_short_team', 'member_team_held', 'joiner_counts',
         'seats_lapse', 'other_games', 'unknown_machine']


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('party_teams', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('labs(gap) < best_gap', '0'), 'closest_split'),
    (('labs(gap) < best_gap', 'labs(gap) > best_gap'), 'e21_whole'),
    (('table[index].reserved ||', '1 ||'), 'seats_lapse'),
    (('table[index].reserved ||', ''), 'e21_whole'),
    (('team_parties[party].team = team;', ''), 'member_team_held'),
    (('index == joining', '0'), 'joiner_counts'),
    (('matchmaking.public_game', '0'), 'other_games'),
    (('matchmaking.role != _matchmaking_hosting', '0'), 'other_games'),
    (('if (machine_index == network_game_client_get_local_machine_index())\n\t\tcsmemcpy(hash, own_key_hash, sizeof(hash));\n\telse if',
      'if'), 'e27_leader_hosts'),
    (('else if (!p2p_peer_key_hash(network_order(network_game_server_machine_address(machine_index)), identifier, hash))\n\t\treturn NONE;',
      'else\n\t\thash_of(machine_index, hash);'), 'unknown_machine'),
])
def test_negative_control(fault, case):
    status, output = run(build('party_teams', generated(fault)), case)
    assert status == CHECK_FAILED, output
