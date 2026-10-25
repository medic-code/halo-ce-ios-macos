"""Social Games' pick (menu_functions.c's social_games_pick): the public game
that fits the playlists chosen best."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure


def generated(fault=None):
    source = read('port/linux/game/menu_functions.c')
    lobby = read('port/linux/src/p2p.h')
    declarations = re.search(r"^#define SOCIAL_GAMES_PING_LIMIT.*?^\};", source, re.M | re.S).group(0)
    code = declarations + '\n\n' + function(source, 'social_games_pick')
    if fault:
        code = mutated(code, *fault)
    return (('listing.inc', enum_with(lobby, 'P2P_LISTING_NAME_SIZE') + '\n' + structure(lobby, 'p2p_listing')),
            ('under_test.inc', code))


@pytest.mark.parametrize('case', ['playlists', 'skipped', 'order', 'limit', 'none'])
def test_case(case):
    status, output = run(build('social_games', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('!playlists->engines[game->engine_type] ||', ''), 'playlists'),
    (('game->has_teams ? playlists->team_games : playlists->free_for_all',
      'game->has_teams ? playlists->free_for_all : playlists->team_games'), 'playlists'),
    ((' || !game->open', ''), 'skipped'),
    (('game->player_count >= game->maximum_player_count || ', ''), 'skipped'),
    ((' || game->locked', ''), 'skipped'),
    ((' || game->failed', ''), 'skipped'),
    (('if (game->ping < 0)', 'if (0)'), 'skipped'),
    (('game->player_count > games[best].player_count', 'game->player_count < games[best].player_count'), 'order'),
    (('game->ping < games[best].ping', 'game->ping > games[best].ping'), 'order'),
    (('game->ping < games[best].ping', 'game->ping <= games[best].ping'), 'order'),
    (('if (game->ping >= ping_limit)', 'if (game->ping > ping_limit)'), 'limit'),
    (('if (game->ping >= ping_limit)', 'if (0)'), 'limit'),
    (('(*untimed)++;', ';'), 'none'),
    (('(*fitting)++;', ';'), 'order'),
])
def test_negative_control(fault, case):
    status, output = run(build('social_games', generated(fault)), case)
    assert status == CHECK_FAILED, output
