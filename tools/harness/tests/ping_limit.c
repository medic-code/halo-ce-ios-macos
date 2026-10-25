/*
PING_LIMIT.C (test)

The host's ping limit (network.host_ping_limit,
port/linux/game/network_ping_limit.c): the median of a machine's latest
round trips judged against the limit, one taken a second from the internet
play tunnel, in the lobby and a machine's first seconds in a game, and none
for a machine the tunnel does not reach or one downloading the maps.
test_ping_limit.py takes the constants, the structure and the code
(under_test.inc).
*/

#include "harness.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define P2P_PLAYER_KEY_HASH_SIZE 16

#include "config.inc"

/* the fake world: the clock, the setting, the tunnel's one peer (its address
in network byte order, its round trip), the maps downloading */
static unsigned long now_milliseconds = 100000;
static long limit_setting;
static unsigned long peer_network_address;
static int peer_ping = -1;
static boolean downloading;

#define system_milliseconds() (now_milliseconds)
#define config_integer(name) (limit_setting)

static boolean map_share_machine_downloading(
	long machine_index)
{
	return downloading;
}

static int p2p_peer_key_hash(
	unsigned long address,
	unsigned char *identifier,
	unsigned char *key_hash)
{
	memset(identifier, 0x42, 6);
	return peer_network_address && address == peer_network_address;
}

static int p2p_peer_ping(
	unsigned char const *identifier)
{
	return identifier[0] == 0x42 ? peer_ping : -1;
}

#include "under_test.inc"

/* 100.64.0.7, as the host has a peer's stand-in (host byte order) and as
the tunnel names it (network byte order) */
#define PEER_ADDRESS 0x64400007UL
#define PEER_NETWORK_ADDRESS 0x07004064UL
#define LAN_ADDRESS 0xC0A80105UL
#define LAN_NETWORK_ADDRESS 0x0501A8C0UL

static long median_of(
	short a,
	short b,
	short c,
	short d,
	short e,
	short count)
{
	short samples[6] = { a, b, c, d, e, 0 };

	return ping_limit_median(samples, count);
}

/* the machine at slot 1 checked once a second from its join, for that many
seconds: the second it was kicked (counted from 0), NONE if never */
static long kicked_after(
	unsigned long address,
	boolean lobby,
	long seconds)
{
	long second;

	network_ping_limit_machine_joined(1);
	for (second = 0; second < seconds; second++)
	{
		long ping = network_ping_limit_check(1, address, lobby, (unsigned long)second * 1000);

		now_milliseconds += 1000;
		if (ping != NONE)
			return second;
	}
	return NONE;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	peer_network_address = PEER_NETWORK_ADDRESS;
	if (!strcmp(case_name, "median"))
	{
		CHECK(median_of(80, 100, 90, 0, 0, 3) == 90, "three");
		CHECK(median_of(300, 80, 100, 90, 0, 4) == 90, "four: the lower of the middle two");
		CHECK(median_of(70, 300, 80, 100, 90, 5) == 90, "five");
		CHECK(median_of(300, 300, 0, 0, 0, 2) == NONE, "two are too few");
		CHECK(median_of(1, 2, 3, 4, 5, 6) == NONE, "more than are kept");
	}
	else if (!strcmp(case_name, "spike"))
	{
		short one_spike[5] = { 80, 80, 2000, 80, 80 };
		short three_slow[5] = { 80, 2000, 80, 2000, 2000 };

		CHECK(!ping_limit_over(150, one_spike, 5), "one slow round trip kicks no one");
		CHECK(ping_limit_over(150, three_slow, 5), "most of them slow is over");
	}
	else if (!strcmp(case_name, "threshold"))
	{
		short at[3] = { 150, 150, 150 };
		short above[3] = { 151, 151, 151 };

		CHECK(!ping_limit_over(150, at, 3), "at the limit is not over it");
		CHECK(ping_limit_over(150, above, 3), "a millisecond above is");
	}
	else if (!strcmp(case_name, "off"))
	{
		short slow[5] = { 900, 900, 900, 900, 900 };

		CHECK(!ping_limit_over(0, slow, 5), "0 is off");
		CHECK(!ping_limit_over(-5, slow, 5), "below 0 is off");
		limit_setting = 0;
		peer_ping = 900;
		CHECK(kicked_after(PEER_ADDRESS, TRUE, 30) == NONE, "off in the lobby");
	}
	else if (!strcmp(case_name, "kicked-at-third-second"))
	{
		limit_setting = 100;
		peer_ping = 200;
		CHECK(kicked_after(PEER_ADDRESS, TRUE, 30) == 2, "three samples, a second apart");
	}
	else if (!strcmp(case_name, "stays-under"))
	{
		limit_setting = 300;
		peer_ping = 200;
		CHECK(kicked_after(PEER_ADDRESS, TRUE, 60) == NONE, "200 ms under a 300 ms limit");
	}
	else if (!strcmp(case_name, "once-a-second"))
	{
		long call;

		limit_setting = 100;
		peer_ping = 200;
		network_ping_limit_machine_joined(1);
		for (call = 0; call < 50; call++)
		{
			CHECK(network_ping_limit_check(1, PEER_ADDRESS, TRUE, 0) == NONE, "call %ld within a second", call);
			now_milliseconds += 20;
		}
	}
	else if (!strcmp(case_name, "byte-order"))
	{
		limit_setting = 100;
		peer_ping = 200;
		peer_network_address = 0x64400007UL;
		CHECK(kicked_after(PEER_ADDRESS, TRUE, 10) == NONE, "the tunnel is asked in network byte order");
		peer_network_address = PEER_NETWORK_ADDRESS;
		CHECK(kicked_after(PEER_ADDRESS, TRUE, 10) == 2, "and then finds the peer");
	}
	else if (!strcmp(case_name, "lan-exempt"))
	{
		/* (a tunnel that would answer for the LAN address: it is not asked) */
		limit_setting = 100;
		peer_ping = 200;
		peer_network_address = LAN_NETWORK_ADDRESS;
		CHECK(kicked_after(LAN_ADDRESS, TRUE, 30) == NONE, "a system link machine has no limit");
	}
	else if (!strcmp(case_name, "not-timed"))
	{
		limit_setting = 100;
		peer_ping = -1;
		CHECK(kicked_after(PEER_ADDRESS, TRUE, 30) == NONE, "no round trip timed");
	}
	else if (!strcmp(case_name, "joined-in-game"))
	{
		limit_setting = 100;
		peer_ping = 200;
		CHECK(kicked_after(PEER_ADDRESS, FALSE, 30) == 2, "judged in its first seconds in a game");
	}
	else if (!strcmp(case_name, "late-in-game"))
	{
		long call;

		limit_setting = 100;
		peer_ping = 200;
		network_ping_limit_machine_joined(1);
		for (call = 0; call < 30; call++)
		{
			CHECK(network_ping_limit_check(1, PEER_ADDRESS, FALSE, 10001 + (unsigned long)call * 1000) == NONE,
				"in a game, after its first ten seconds");
			now_milliseconds += 1000;
		}
	}
	else if (!strcmp(case_name, "downloading"))
	{
		long call;

		limit_setting = 100;
		peer_ping = 200;
		network_ping_limit_machine_joined(1);
		CHECK(network_ping_limit_check(1, PEER_ADDRESS, TRUE, 0) == NONE, "one");
		now_milliseconds += 1000;
		CHECK(network_ping_limit_check(1, PEER_ADDRESS, TRUE, 1000) == NONE, "two");
		now_milliseconds += 1000;
		downloading = TRUE;
		for (call = 0; call < 30; call++)
		{
			CHECK(network_ping_limit_check(1, PEER_ADDRESS, TRUE, 2000) == NONE, "not while downloading");
			now_milliseconds += 1000;
		}
		downloading = FALSE;
		CHECK(network_ping_limit_check(1, PEER_ADDRESS, TRUE, 0) == NONE, "the samples before are let go: one");
		now_milliseconds += 1000;
		CHECK(network_ping_limit_check(1, PEER_ADDRESS, TRUE, 0) == NONE, "two");
		now_milliseconds += 1000;
		CHECK(network_ping_limit_check(1, PEER_ADDRESS, TRUE, 0) == 200, "three");
	}
	else if (!strcmp(case_name, "joined-resets"))
	{
		limit_setting = 100;
		peer_ping = 200;
		network_ping_limit_machine_joined(1);
		CHECK(network_ping_limit_check(1, PEER_ADDRESS, TRUE, 0) == NONE, "one");
		now_milliseconds += 1000;
		CHECK(network_ping_limit_check(1, PEER_ADDRESS, TRUE, 1000) == NONE, "two");
		now_milliseconds += 1000;
		network_ping_limit_machine_joined(1);
		CHECK(network_ping_limit_check(1, PEER_ADDRESS, TRUE, 0) == NONE, "a new machine at the slot: one");
		now_milliseconds += 1000;
		CHECK(network_ping_limit_check(1, PEER_ADDRESS, TRUE, 1000) == NONE, "two");
		now_milliseconds += 1000;
		CHECK(network_ping_limit_check(1, PEER_ADDRESS, TRUE, 2000) == 200, "three");
	}
	else if (!strcmp(case_name, "smoothed-start"))
	{
		long second;

		limit_setting = 150;
		network_ping_limit_machine_joined(1);
		for (second = 0; second < 30; second++)
		{
			peer_ping = second == 0 ? 400 : 80;
			CHECK(network_ping_limit_check(1, PEER_ADDRESS, TRUE, (unsigned long)second * 1000) == NONE,
				"second %ld", second);
			now_milliseconds += 1000;
		}
	}
	else
	{
		fprintf(stderr, "unknown case %s\n", case_name);
		return 2;
	}
	return 0;
}
