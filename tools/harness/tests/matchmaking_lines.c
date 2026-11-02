/* MATCHMAKING_LINES.C (test): the search screens' words (menu_functions.c) */

#include <wchar.h>
#include "harness.h"
#include "enums.inc"
#define p2p_mm_size_big(size) ((size) >= P2P_MM_BIG_TEAM)
#include "under_test.inc"

static wchar_t title[96], count[96], help[96], notice[96];

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
		CHECK(!wcscmp(help, L"Joining the game:\r\nSLAYER 2V2, 48 ms"), "help: %ls", help);
		status.ping = -1;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcscmp(help, L"Joining the game:\r\nSLAYER 2V2"), "a matchmade game, not timed: %ls", help);
		status.hosts = 1;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcscmp(help, L"Searchers join you."), "this machine hosts: %ls", help);
	}
	CASE("big_looking")
	{
		status = queued(1);
		status.state = P2P_MM_LOOKING;
		status.size = P2P_MM_BIG_TEAM;
		matchmaking_lines(&status, title, count, help, notice, 96);
		CHECK(!wcscmp(title, L"LOOKING FOR A GAME"), "title: %ls", title);
		CHECK(!wcscmp(help, L"Looking for the best big game near you.\r\nIf none fits, you host one."), "help: %ls", help);
		status.state = P2P_MM_QUEUED;
		status.can_host = 0;
		matchmaking_lines(&status, title, count, help, notice, 96);
		CHECK(!wcscmp(title, L"LOOKING FOR A GAME") && !count[0], "a big size has no queue: %ls, %ls", title, count);
		CHECK(wcsstr(help, L"Your router cannot host") && !wcsstr(help, L"you host one"), "strict: %ls", help);
	}
	CASE("big_hosting")
	{
		wchar_t line[32];

		status = queued(1);
		status.state = P2P_MM_HOSTING;
		status.hosts = 1;
		status.size = P2P_MM_BIG_FFA;
		matchmaking_lines(&status, title, count, help, notice, 96);
		CHECK(!wcscmp(title, L"SLAYER BIG FFA"), "title: %ls", title);
		CHECK(!wcscmp(help, L"START NOW starts it"), "help: %ls", help);
		matchmaking_lobby_line(-1, TRUE, 2, 128, line, 32);
		CHECK(!wcscmp(line, L"2 of 128 players"), "the lobby: %ls", line);
		matchmaking_lobby_line(2, TRUE, 2, 128, line, 32);
		CHECK(!wcscmp(line, L"Game starts in 3"), "after START NOW: %ls", line);
	}
	CASE("big_found")
	{
		status = queued(1);
		status.state = P2P_MM_FOUND;
		status.size = P2P_MM_BIG_TEAM;
		status.players = 9;
		status.most_players = 16;
		status.ping = -1;
		strcpy(status.region, "eu");
		matchmaking_lines(&status, title, count, help, notice, 96);
		CHECK(!wcscmp(title, L"MATCH FOUND") && !count[0], "no pool to count: %ls, %ls", title, count);
		CHECK(wcsstr(help, L"SLAYER BIG TEAM\r\n9 of 16 players, EU") != NULL, "a dedicated server's region: %ls", help);
		status.region[0] = 0;
		status.players = 47;
		status.most_players = 128;
		status.ping = 48;
		matchmaking_lines(&status, title, count, help, notice, 96);
		CHECK(wcsstr(help, L"SLAYER BIG TEAM\r\n47 of 128 players, 48 ms") != NULL, "a player's game: %ls", help);
	}
	CASE("lobby")
	{
		wchar_t line[32];

		matchmaking_lobby_line(4, FALSE, 0, 0, line, 32);
		CHECK(!wcscmp(line, L"Game starts in 5"), "the first message: %ls", line);
		matchmaking_lobby_line(0, FALSE, 0, 0, line, 32);
		CHECK(!wcscmp(line, L"Game starts in 1"), "the last message: %ls", line);
		matchmaking_lobby_line(-1, FALSE, 0, 0, line, 32);
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
		status.notice = P2P_MM_NOTICE_TOO_BIG;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcscmp(notice, L"THE PARTY IS TOO BIG FOR THAT GAME. PICK A BIGGER GAME."), "too big: %ls", notice);
		status.notice = P2P_MM_NOTICE_NONE;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!notice[0], "none: %ls", notice);
	}
	return 0;
}
