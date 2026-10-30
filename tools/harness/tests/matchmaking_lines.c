/* MATCHMAKING_LINES.C (test): the search screens' words (menu_functions.c) */

#include <wchar.h>
#include "harness.h"
#include "enums.inc"
#include "under_test.inc"

static wchar_t title[64], count[64], help[128], notice[96];

static struct p2p_mm_status queued(int can_host)
{
	struct p2p_mm_status status;

	memset(&status, 0, sizeof(status));
	status.state = P2P_MM_QUEUED;
	status.seconds = status.mark_seconds = 12;
	status.can_host = can_host;
	status.playlist = P2P_MM_SLAYER;
	status.size = P2P_MM_2V2;
	status.count = 3;
	status.needed = 4;
	return status;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	struct p2p_mm_status status;

	CASE("looking")
	{
		status = queued(1);
		status.state = P2P_MM_LOOKING;
		status.games_checked = 12;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcscmp(title, L"LOOKING FOR A GAME"), "title: %ls", title);
		CHECK(!wcscmp(help, L"Checking 12 public games..."), "help: %ls", help);
		CHECK(!count[0] && !notice[0], "no count, no notice: %ls, %ls", count, notice);
	}
	CASE("queued")
	{
		status = queued(1);
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcscmp(title, L"IN THE QUEUE"), "title: %ls", title);
		CHECK(!wcscmp(count, L"3 searching"), "count: %ls", count);
		CHECK(wcsstr(help, L"1 more for 2V2") && wcsstr(help, L"At 0:45"), "the next mark: %ls", help);
		status = queued(0);
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcsstr(help, L"2:00") && wcsstr(help, L"Your router cannot host"), "E13: strict: %ls", help);
		status = queued(-1);
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(wcsstr(help, L"Your router cannot host") != NULL, "E48: not known yet: greyed: %ls", help);
		status = queued(1);
		status.count = 1;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcscmp(count, L"1 searching"), "E5: alone: %ls", count);
		status.count = status.needed = 0;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!help[0], "no pool yet, no line about it: %ls", help);
	}
	CASE("widened")
	{
		status = queued(1);
		status.widened = 1;
		status.all_sizes_count = 5;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcscmp(count, L"5 searching, all sizes"), "E6: %ls", count);
		CHECK(wcsstr(help, L"Any size") && wcsstr(help, L"2:00"), "E6: any size, then the 2:00 mark: %ls", help);
		status.can_host = 0;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcsstr(help, L"2:00") && wcsstr(help, L"Your router cannot host"), "E13: strict: %ls", help);
	}
	CASE("hosting")
	{
		status = queued(1);
		status.state = P2P_MM_HOSTING;
		status.hosts = 1;
		status.players = 2;
		status.most_players = 4;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcscmp(title, L"SLAYER 2V2"), "title: %ls", title);
		CHECK(!wcscmp(help, L"Searchers join you."), "E5, E7: %ls", help);
		CHECK(!count[0], "the lobby carries the game's players: %ls", count);
	}
	CASE("found")
	{
		status = queued(1);
		status.state = P2P_MM_FOUND;
		status.players = 3;
		status.most_players = 4;
		status.ping = 48;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcscmp(title, L"MATCH FOUND"), "title: %ls", title);
		CHECK(!wcscmp(count, L"3 of 4 matched"), "count: %ls", count);
		CHECK(!wcscmp(help, L"Joining the game: SLAYER 2V2, 48 ms"), "help: %ls", help);
		status.ping = -1;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcscmp(help, L"Joining the game: SLAYER 2V2"), "a matchmade game, not timed: %ls", help);
		status.hosts = 1;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcscmp(help, L"Searchers join you."), "this machine hosts: %ls", help);
	}
	CASE("lobby")
	{
		wchar_t line[32];

		matchmaking_lobby_line(4, line, 32);
		CHECK(!wcscmp(line, L"Game starts in 5"), "the first message: %ls", line);
		matchmaking_lobby_line(0, line, 32);
		CHECK(!wcscmp(line, L"Game starts in 1"), "the last message: %ls", line);
		matchmaking_lobby_line(-1, line, 32);
		CHECK(!wcscmp(line, L"Waiting for players"), "no countdown, no number: %ls", line);
	}
	CASE("notices")
	{
		status = queued(1);
		status.notice = P2P_MM_NOTICE_HOST_LEFT;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcscmp(notice, L"THE HOST LEFT. BACK IN THE QUEUE, YOUR PLACE KEPT."), "host left: %ls", notice);
		status.notice = P2P_MM_NOTICE_HOST_UNREACHED;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcscmp(notice, L"COULD NOT REACH THE HOST. BACK IN THE QUEUE."), "host unreached: %ls", notice);
		status.notice = P2P_MM_NOTICE_HOST_FAILED;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcscmp(notice, L"COULD NOT HOST THE GAME. BACK IN THE QUEUE."), "host failed: %ls", notice);
		status.notice = P2P_MM_NOTICE_NONE;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!notice[0], "none: %ls", notice);
	}
	return 0;
}
