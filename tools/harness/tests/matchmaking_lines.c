/* MATCHMAKING_LINES.C (test): the search screens' words (menu_functions.c) */

#include <wchar.h>
#include "harness.h"
#include "enums.inc"
static unsigned long now_ms = 1500;
static unsigned long system_milliseconds(void) { return now_ms; }
#include "under_test.inc"

static wchar_t title[64], count[64], help[128], notice[96];

static struct p2p_mm_status queued(int can_host)
{
	struct p2p_mm_status status;

	memset(&status, 0, sizeof(status));
	status.state = P2P_MM_QUEUED;
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
		CHECK(!wcscmp(title, L"SEARCHING..."), "title: %ls", title);
		CHECK(!wcscmp(count, L"3 searching"), "count: %ls", count);
		CHECK(!wcscmp(help, L"1 more for 2V2."), "the players still needed: %ls", help);
		status = queued(0);
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(wcsstr(help, L"Your router cannot host") != NULL, "E13: strict: %ls", help);
		status = queued(-1);
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(wcsstr(help, L"Checking your router...") != NULL, "E48: not known yet: %ls", help);
		status = queued(1);
		status.count = 1;
		matchmaking_lines(&status, title, count, help, notice, 64);
		CHECK(!wcscmp(count, L"1 searching"), "E5: alone: %ls", count);
	}
	CASE("dots")
	{
		static wchar_t const *const frames[] = { L"SEARCHING   ", L"SEARCHING.  ", L"SEARCHING.. ", L"SEARCHING...",
			L"SEARCHING   " };
		int frame;

		status = queued(1);
		for (frame = 0; frame < 5; frame++)
		{
			now_ms = 500ul * (unsigned long)frame + 100;
			matchmaking_lines(&status, title, count, help, notice, 64);
			CHECK(!wcscmp(title, frames[frame]), "0 to 3 dots, a step each half second, the word still: %ls", title);
		}
		now_ms = 1500;
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
		CHECK(!wcscmp(help, L"Searchers join you."), "a pool's host: %ls", help);
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
