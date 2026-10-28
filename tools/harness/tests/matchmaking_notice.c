/* MATCHMAKING_NOTICE.C (test): a search's notice (p2p_matchmaking.c) */

#include "harness.h"
#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	int notice;

	CASE("lost_host")
	{
		notice = notice_after(P2P_MM_NOTICE_HOST_LEFT, P2P_MM_LOOKING);
		CHECK(notice == P2P_MM_NOTICE_HOST_LEFT, "step 1 again: kept");
		notice = notice_after(notice, P2P_MM_QUEUED);
		CHECK(notice == P2P_MM_NOTICE_HOST_LEFT, "in the queue: kept");
		notice = notice_after(notice, P2P_MM_FOUND);
		CHECK(notice == P2P_MM_NOTICE_NONE, "a match found: cleared");
		CHECK(notice_after(notice, P2P_MM_QUEUED) == P2P_MM_NOTICE_NONE, "back in the queue: not back");
	}
	CASE("unreached")
	{
		CHECK(notice_after(P2P_MM_NOTICE_HOST_UNREACHED, P2P_MM_QUEUED) == P2P_MM_NOTICE_HOST_UNREACHED, "queued: kept");
		CHECK(notice_after(P2P_MM_NOTICE_HOST_UNREACHED, P2P_MM_HOSTING) == P2P_MM_NOTICE_NONE, "hosting: cleared");
		CHECK(notice_after(P2P_MM_NOTICE_HOST_UNREACHED, P2P_MM_OFF) == P2P_MM_NOTICE_NONE, "the search's end: cleared");
	}
	return 0;
}
