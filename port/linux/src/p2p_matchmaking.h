/* state lives in struct mm_search, never globals: tools/p2p_matchmaking_check.c runs several machines in one program */

#ifndef __HALO_LINUX_P2P_MATCHMAKING_H
#define __HALO_LINUX_P2P_MATCHMAKING_H

#include "p2p_internal.h"
#include "p2p.h"

enum
{
	MM_FORMAT = 1,
	MM_FORMAT_PARTY = 2,
	MM_KIND_SEARCHING = 1,
	MM_KIND_HOSTING,
	MM_KIND_CANCEL,
	MM_FLAG_CAN_HOST = 1,
	MM_FLAG_UNDER_WAY = 2,
	MM_FLAG_HELD = 4,
	MM_MAXIMUM_POOL = 8,
	MM_MAXIMUM_PARTY = P2P_PARTY_MAXIMUM,
	MM_MAXIMUM_UNITS = 256,
	/* records after the first */
	MM_REFRESHES_COUNTED = 2,
	MM_REFRESHES_PINGED = 6,
	MM_SEALED_TOKEN_SIZE = P2P_TOKEN_SIZE + P2P_SEAL_OVERHEAD,
	MM_HEADER_SIZE = 2 + 1 + 2 + 1 + 4 + 1 + 1 + 1 + 1 + P2P_KEY_SIZE,
	MM_MAXIMUM_HOSTING_SIZE = MM_HEADER_SIZE + 3 + MM_MAXIMUM_POOL * (P2P_KEY_HASH_SIZE + MM_SEALED_TOKEN_SIZE) +
		P2P_TOKEN_SIZE + P2P_SIGNATURE_SIZE,
	MM_MEMBER_SIZE = P2P_KEY_SIZE + P2P_SIGNATURE_SIZE,
	MM_MAXIMUM_PARTY_SIZE = MM_HEADER_SIZE + 1 + (MM_MAXIMUM_PARTY - 1) * MM_MEMBER_SIZE + P2P_SIGNATURE_SIZE,
	MM_SEALED_INVITE_SIZE = P2P_LINK_SIZE + P2P_SEAL_OVERHEAD,
	MM_PARTY_SEARCH_SIZE = 7,
	MM_MAXIMUM_LEADER_SIZE = 1 + P2P_KEY_SIZE + 4 + P2P_PARTY_NAME_SIZE + 1 + (MM_MAXIMUM_PARTY - 1) * P2P_KEY_HASH_SIZE +
		MM_PARTY_SEARCH_SIZE + 2 + (MM_MAXIMUM_PARTY - 1) * MM_SEALED_INVITE_SIZE + P2P_SIGNATURE_SIZE + P2P_SEAL_OVERHEAD,
	MM_MAXIMUM_SEARCH_SIZE = MM_MAXIMUM_HOSTING_SIZE > MM_MAXIMUM_PARTY_SIZE ? MM_MAXIMUM_HOSTING_SIZE :
		MM_MAXIMUM_PARTY_SIZE,
	MM_MAXIMUM_RECORD_SIZE = MM_MAXIMUM_SEARCH_SIZE > MM_MAXIMUM_LEADER_SIZE ? MM_MAXIMUM_SEARCH_SIZE :
		MM_MAXIMUM_LEADER_SIZE,
	/* tenths of a second */
	MM_INTERVAL = 30,
	MM_MINIMUM_INTERVAL = 10,
	MM_RECORDS_PER_SECOND = 4,
};

struct mm_record
{
	int kind;
	unsigned short network_version;
	/* rises with each record of a run's key */
	unsigned long sequence;
	/* tenths of a second until the next */
	int interval;
	int can_host, under_way, held;
	int playlist, size;
	unsigned char key[P2P_KEY_SIZE];
	/* the hash of the key's X25519 form: an invite's host part */
	unsigned char key_hash[P2P_KEY_HASH_SIZE];
	int players, most_players;
	int pool_count;
	unsigned char pool[MM_MAXIMUM_POOL][P2P_KEY_HASH_SIZE];
	unsigned char sealed[MM_MAXIMUM_POOL][MM_SEALED_TOKEN_SIZE];
	unsigned char token[P2P_TOKEN_SIZE];
	int party;
	unsigned char member_keys[MM_MAXIMUM_PARTY - 1][P2P_KEY_SIZE];
	unsigned char member_signatures[MM_MAXIMUM_PARTY - 1][P2P_SIGNATURE_SIZE];
};

void mm_topic(int playlist, int size, char *topic, int topic_size);
void mm_party_topic(const unsigned char *secret, char *topic, int topic_size);
/* tenths of a second */
int mm_interval(int queues);

int mm_topic_listed(const char *const *topics, int count, const char *topic);

int mm_record_write(const struct mm_record *record, const unsigned char *seed, unsigned char *bytes, int size);
/* checks the signatures: 1 only if whole, signed by its key, and by every member it names */
int mm_record_read(const unsigned char *bytes, int size, struct mm_record *record);
void mm_party_sign(const unsigned char *seed, const unsigned char *leader_key, unsigned char *signature);
int mm_party_signed(const unsigned char *key, const unsigned char *leader_key, const unsigned char *signature);
int mm_party_size_allowed(int party, int size);
int mm_seal_token(const unsigned char *seed, const unsigned char *member_key, const unsigned char *token,
	unsigned char *sealed);
int mm_open_token(const unsigned char *seed, const unsigned char *host_key, const unsigned char *sealed,
	unsigned char *token);

struct mm_unit
{
	unsigned char key[P2P_KEY_SIZE];
	unsigned char key_hash[P2P_KEY_HASH_SIZE];
	int can_host;
	int failed;
	int players;
};

int mm_pool(const struct mm_unit *units, int count, int players, int teamed, int *pool);
int mm_elect(const struct mm_unit *units, const int *pool, int pool_count);

enum
{
	/* milliseconds */
	MM_SETTLE = 4000,
	MM_HOLD = 20000,
	MM_HOST_WAIT = P2P_MM_HOST_WAIT_SECONDS * 1000,
	MM_WIDEN = P2P_MM_WIDEN_SECONDS * 1000,
	MM_HOST = P2P_MM_HOST_SECONDS * 1000,
	MM_EXPIRY_INTERVALS = 3,
	MM_RUNNING_INTERVAL = 25000,
	MM_LOOK_TIME = P2P_MM_LOOK_SECONDS * 1000,
	MM_JOIN_BACKOFF = 30000,
	MM_MAXIMUM_OUT = 32,
};

enum
{
	MM_FAILED_NONE,
	MM_FAILED_GAME_UNJOINABLE,
	/* an elected host, until it is heard hosting */
	MM_FAILED_PASSED_OVER
};

struct mm_peer
{
	int used;
	int playlist, size;
	struct mm_record record;
	/* this machine's milliseconds, not the sender's clock */
	unsigned long heard;
	int refreshes;
	int failed;
};

struct mm_out
{
	char topic[P2P_MM_TOPIC_SIZE];
	unsigned char bytes[MM_MAXIMUM_RECORD_SIZE];
	int size;
};

struct mm_search_party
{
	/* players, the leader among them */
	int count;
	unsigned char keys[MM_MAXIMUM_PARTY - 1][P2P_KEY_SIZE];
	unsigned char signatures[MM_MAXIMUM_PARTY - 1][P2P_SIGNATURE_SIZE];
	int member_hosts;
	/* a member: the leader's search decides, and this machine publishes none of its own */
	int following;
	unsigned char leader[P2P_KEY_SIZE], leader_hash[P2P_KEY_HASH_SIZE];
};

struct mm_search
{
	unsigned char seed[P2P_SEED_SIZE], key[P2P_KEY_SIZE], key_hash[P2P_KEY_HASH_SIZE];
	int state;
	struct p2p_mm_choice choice;
	/* Unix seconds, never sent */
	unsigned long started;
	unsigned long marks_from;
	unsigned long join_after;
	/* 1, 0 (strict router) or -1 (STUN has not answered yet) */
	int can_host;
	int widened, host_now;
	unsigned long sequence;
	int queue_count;
	int queues[P2P_MM_MAXIMUM_TOPICS][2];
	int next_queue;
	unsigned long next_record;
	struct mm_peer peers[MM_MAXIMUM_UNITS];
	unsigned char pool[MM_MAXIMUM_POOL][P2P_KEY_HASH_SIZE];
	int pool_count, pool_playlist, pool_size;
	unsigned long pool_since;
	int hosting_playlist, hosting_size, players, most_players, under_way;
	unsigned char token[P2P_TOKEN_SIZE];
	unsigned long held_until, next_hosting_record;
	unsigned char joining[P2P_KEY_HASH_SIZE];
	unsigned long joining_since;
	int joining_players, joining_most_players, joining_ping;
	char joining_region[P2P_LISTING_REGION_SIZE + 1];
	struct p2p_mm_decision decision;
	int decided;
	int games_checked;
	unsigned char picked[P2P_IDENTIFIER_SIZE];
	int picked_set;
	struct mm_search_party party;
	/* joined: the search is off, and the party still follows it */
	int in_game;
	int left_game;
	unsigned char failed_game[P2P_KEY_HASH_SIZE];
};

void mm_wanted(const struct mm_search *search, int queue, struct p2p_lobby_wanted *wanted);
int mm_counted(const struct mm_peer *peer);

void mm_start(struct mm_search *search, const unsigned char *seed, const struct p2p_mm_choice *choice,
	unsigned long now, unsigned long unix_now);
/* the record must already be verified (mm_record_read) */
void mm_heard(struct mm_search *search, const struct mm_record *record, unsigned long now);
int mm_step(struct mm_search *search, unsigned long now, struct mm_out *out, int out_size);
void mm_hosted(struct mm_search *search, const unsigned char *token, int players, int most_players, int under_way,
	unsigned long now);
void mm_joined(struct mm_search *search, int ok, unsigned long now);
void mm_widen_now(struct mm_search *search);
void mm_host_now(struct mm_search *search);
int mm_cancel(struct mm_search *search, struct mm_out *out, int out_size);
void mm_game_lost(struct mm_search *search, unsigned long now);
void mm_host_gave_up(struct mm_search *search, unsigned long now);
void mm_set_party(struct mm_search *search, const struct mm_search_party *party, unsigned long now);
void mm_follow(struct mm_search *search, const unsigned char *seed, const struct p2p_mm_choice *choice,
	const unsigned char *leader, int players, unsigned long now, unsigned long unix_now);
void mm_follow_game(struct mm_search *search, const char *invite, int playlist, int size, int public_game,
	unsigned long now);
void mm_game_left(struct mm_search *search, unsigned long now);
/* a party member not in a game hosts none: its last record is old, and a game of its that failed is over */
void mm_forget_game(struct mm_search *search, const unsigned char *key);
void mm_invite_text(const unsigned char *key_hash, const unsigned char *token, char *text);

enum { MM_PARTY_NONE, MM_PARTY_LEADER, MM_PARTY_MEMBER };

struct mm_party_member
{
	unsigned char key[P2P_KEY_SIZE], hash[P2P_KEY_HASH_SIZE];
	char name[P2P_PARTY_NAME_SIZE + 1];
	unsigned long sequence, heard;
	unsigned char signature[P2P_SIGNATURE_SIZE];
	int signature_heard;
	int searching, in_game, can_host, left_number;
};

struct mm_party_gone
{
	unsigned char hash[P2P_KEY_HASH_SIZE];
	unsigned long sequence;
};

struct mm_party_search
{
	/* rises with each SEARCH; left: with each game the party gives up */
	int number, game, left;
	int searching, widened, playlists, size, host;
	int has_game, game_playlist, game_size, public_game;
	/* the party grew too big for the search: it stopped, or goes on in its game without the friend */
	int outgrown;
	char invite[P2P_LINK_SIZE];
};

struct mm_party
{
	unsigned char seed[P2P_SEED_SIZE], key[P2P_KEY_SIZE], hash[P2P_KEY_HASH_SIZE], seal[P2P_SHA256_SIZE];
	char name[P2P_PARTY_NAME_SIZE + 1];
	int role;
	/* the leader first */
	struct mm_party_member members[P2P_PARTY_MAXIMUM];
	int count;
	char topic[P2P_PARTY_TOPIC_SIZE], link[P2P_PARTY_LINK_SIZE];
	unsigned long sequence, sent;
	struct mm_party_gone gone[P2P_PARTY_MAXIMUM];
	int gone_count;
	int notice;
	char notice_name[P2P_PARTY_NAME_SIZE + 1];
	struct mm_party_search search;
	/* a member's: the leader's record has been heard; the numbers it had then or has since acted on */
	int search_heard, left_number, followed_game, left_seen;
	int in_game, can_host;
	/* the leader's: since when members have had to follow */
	unsigned long follow_since;
	int follow_done;
	/* a member's: hosting while the leader is in no game */
	unsigned long waiting_since;
	int waiting_for_leader;
};

struct mm_party_record
{
	unsigned char plain[MM_MAXIMUM_RECORD_SIZE];
	int size, kind;
	const unsigned char *key, *body;
	unsigned char leader[P2P_KEY_SIZE];
	unsigned long sequence;
	unsigned char seal[P2P_SHA256_SIZE];
};

void mm_put_long(unsigned char *bytes, unsigned long value);
unsigned long mm_get_long(const unsigned char *bytes);
void mm_party_make(struct mm_party *party, const unsigned char *seed, const unsigned char *secret, unsigned long now);
int mm_party_link_held(const char *text);
int mm_party_can_join(const struct mm_party *party, const unsigned char *seed, const char *text);
int mm_party_join(struct mm_party *party, const unsigned char *seed, const char *text, unsigned long now);
/* each returns the size of a record to publish on the topic held before the call, 0 if none */
int mm_party_step(struct mm_party *party, unsigned long now, unsigned char *bytes);
int mm_party_leave(struct mm_party *party, unsigned char *bytes);
int mm_party_remove(struct mm_party *party, int index, const unsigned char *secret, unsigned long now, unsigned char *bytes);
/* opens a record and screens copies from other brokers, before any signature check; 1 if mm_party_verify should see it */
int mm_party_read(struct mm_party *party, const unsigned char *payload, int size, struct mm_party_record *record);
int mm_party_verify(const struct mm_party_record *record);
void mm_party_heard(struct mm_party *party, const struct mm_party_record *record, unsigned long now);
void mm_party_status(const struct mm_party *party, struct p2p_party_status *status);
void mm_party_search_begin(struct mm_party *party, struct mm_search *search, unsigned long now);
void mm_party_search_left(struct mm_party *party, unsigned long now);
void mm_party_search(struct mm_party *party, struct mm_search *search, unsigned long now, unsigned long unix_now);

struct mm_search *p2p_mm_local(void);
int p2p_party_is_member(void);
int p2p_party_too_big(void);
void p2p_party_search_begin(struct mm_search *search);
void p2p_party_search_left(void);

#endif
