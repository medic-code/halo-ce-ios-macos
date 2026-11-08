/* P2P_FRIENDS.C: the friends list, its key and its requests over the brokers */

#include "platform.h"
#include "port_config.h"
#include "p2p_internal.h"
#include "posix.h"
#include "posix_server.h"
#include "server_mode.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef HALO_WINDOWS
#include <fcntl.h>
#include <unistd.h>
#endif

static const char tag_label[] = "hceu-friend-tag";
static const char id_label[] = "hceu-friend-id";
static const char record_label[] = "hceu-friend-1";
static const char request_label[] = "hceu-friend-r";
static const char tag_alphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
static const unsigned char zeros[P2P_KEY_SIZE];

enum
{
	FRIEND_INBOX_BYTES = 8,
	FRIEND_FORMAT = 1,
	FRIEND_NONCE_SIZE = 16,
	FRIEND_PLAIN_SIZE = P2P_KEY_SIZE + 8 + FRIEND_NONCE_SIZE + P2P_SIGNATURE_SIZE,
	FRIEND_SEALED_SIZE = 2 + P2P_KEY_SIZE + P2P_SEAL_OVERHEAD,
	FRIEND_RECORD_MAXIMUM = 320,
	FRIEND_NAME_CHARACTERS = 11,
	FRIEND_BODY_MAXIMUM = P2P_PARTY_LINK_SIZE,
	FRIEND_TOPIC_SIZE = 64,
	FRIEND_OPENED_MAXIMUM = 32,
	FRIEND_REFUSED_MAXIMUM = 16,
	FRIEND_OUTBOX_SIZE = 8,
	FRIEND_FRESH_SECONDS = 300,
	FRIEND_DAY_SECONDS = 86400,
	FRIEND_REQUEST_DAYS = 7,
	FRIEND_REQUEST_SECONDS = FRIEND_REQUEST_DAYS * FRIEND_DAY_SECONDS,
	FRIEND_CARD_SECONDS = 2592000,
	FRIEND_FORGOTTEN_DAYS = 60,
	FRIEND_HERE_SECONDS = 60,
	FRIEND_LOOKUP_SECONDS = 3,
	FRIEND_OFFLINE_SECONDS = 90,
	FRIEND_PRESENCE_SIZE = 3,
};

enum
{
	FRIEND_CARD = 1,
	FRIEND_REQUEST,
	FRIEND_ACCEPT,
	FRIEND_HELLO,
	FRIEND_HERE,
	FRIEND_BYE,
	FRIEND_INVITE,
};

enum
{
	FRIEND_DROP_EMPTY = -1,
	FRIEND_DROP_LONG = -2,
	FRIEND_DROP_FORMAT = -3,
	FRIEND_DROP_SHORT = -4,
	FRIEND_DROP_KIND = -5,
	FRIEND_DROP_FLOOD = -6,
	FRIEND_DROP_KEY = -7,
	FRIEND_DROP_SEAL = -8,
	FRIEND_DROP_SIGNATURE = -9,
	FRIEND_DROP_AGE = -10,
	FRIEND_DROP_NAME = -11,
	FRIEND_DROP_SELF = -12,
	FRIEND_DROP_TOPIC = -13,
	FRIEND_DROP_REPLAY = -14,
	FRIEND_DROP_STRANGER = -15,
	FRIEND_DROP_REFUSED = -16,
	FRIEND_DROP_NONCE = -17,
	FRIEND_DROP_BODY = -18,
	FRIEND_DROP_STATE = -19,
	FRIEND_DROP_BUSY = -20,
	FRIEND_DROP_TAG = -21,
	FRIEND_DROP_REASONS = 22,
};

struct friend_record
{
	int kind;
	unsigned char sender[P2P_KEY_SIZE];
	unsigned long long sequence;
	unsigned char nonce[FRIEND_NONCE_SIZE];
	int body_size;
	unsigned char body[FRIEND_RECORD_MAXIMUM];
};

struct friend
{
	unsigned char key[P2P_KEY_SIZE];
	char id[2 * FRIEND_INBOX_BYTES + 1];
	char inbox[2 * FRIEND_INBOX_BYTES + 1];
	int sent;
	unsigned char nonce[FRIEND_NONCE_SIZE];
	unsigned long long last_sequence;
	long last_day;
	char name_tag[P2P_FRIEND_NAME_TAG_SIZE];
	int number, clock_logged, due, request_size;
	int heard, state, presence;
	unsigned request_taken, presence_taken;
	unsigned long heard_time;
	unsigned char request[FRIEND_RECORD_MAXIMUM];
};

struct friend_request
{
	unsigned char key[P2P_KEY_SIZE];
	char id[2 * FRIEND_INBOX_BYTES + 1];
	char inbox[2 * FRIEND_INBOX_BYTES + 1];
	char name_tag[P2P_FRIEND_NAME_TAG_SIZE];
	unsigned char nonce[FRIEND_NONCE_SIZE];
	unsigned long long sequence;
};

static struct
{
	int loaded, on, key_unmade, unsaved;
	unsigned char seed[P2P_SEED_SIZE];
	unsigned char public_key[P2P_KEY_SIZE];
	unsigned char x25519_secret[P2P_KEY_SIZE];
	unsigned char x25519_public[P2P_KEY_SIZE];
	char inbox[2 * FRIEND_INBOX_BYTES + 1];
	char tag[P2P_FRIEND_TAG_SIZE + 1];
	char id[2 * FRIEND_INBOX_BYTES + 1];
	char list_path[1100];
	char requests_topic[FRIEND_TOPIC_SIZE];
	char inbox_topic[FRIEND_TOPIC_SIZE];
	int card_due, card_size;
	unsigned card_taken;
	unsigned char card[FRIEND_RECORD_MAXIMUM];
	char look_topic[FRIEND_TOPIC_SIZE], look_tag[P2P_FRIEND_TAG_SIZE + 1];
	unsigned long look_time;
	int look_count, lookup_result;
	unsigned char look_keys[2][P2P_KEY_SIZE];
	char look_names[2][P2P_PARTY_NAME_SIZE + 1];
	int topics_set, connected;
	unsigned up;
	int state, state_changed, save_due;
	unsigned long here_time;
	unsigned long long sequence;
	int count, numbers;
	struct friend list[P2P_FRIENDS_MAXIMUM];
	int request_count;
	struct friend_request requests[P2P_FRIEND_REQUESTS_SHOWN];
	int invite_pending;
	unsigned char invite_key[P2P_KEY_SIZE];
	char invite_link[P2P_PARTY_LINK_SIZE];
	int removed_count;
	unsigned char removed[P2P_FRIENDS_MAXIMUM][P2P_KEY_SIZE];
	int refused_count;
	unsigned char refused[FRIEND_REFUSED_MAXIMUM][P2P_KEY_SIZE];
	int opened, flood_logged;
	unsigned char seen[FRIEND_OPENED_MAXIMUM][8];
	int drops[FRIEND_DROP_REASONS];
	long drops_logged;
	int out_count;
	struct
	{
		char topic[FRIEND_TOPIC_SIZE];
		int size, kept;
		unsigned taken;
		unsigned char bytes[FRIEND_RECORD_MAXIMUM];
	} out[FRIEND_OUTBOX_SIZE];
} friends;

static void friend_hash(const char *label, int label_size, const unsigned char *public_key, unsigned char *digest)
{
	unsigned char message[(sizeof(tag_label) > sizeof(id_label) ? sizeof(tag_label) : sizeof(id_label)) + P2P_KEY_SIZE];

	memcpy(message, label, (size_t)label_size);
	memcpy(message + label_size, public_key, P2P_KEY_SIZE);
	p2p_sha256(message, label_size + P2P_KEY_SIZE, digest);
}

static void friend_tag(const unsigned char *public_key, char *tag)
{
	unsigned char digest[P2P_SHA256_SIZE];
	int index;

	friend_hash(tag_label, (int)sizeof(tag_label) - 1, public_key, digest);
	for (index = 0; index < P2P_FRIEND_TAG_SIZE; index++)
	{
		int bit = 5 * index;
		unsigned group = (unsigned)(digest[bit / 8] << 8 | digest[bit / 8 + 1]);

		tag[index] = tag_alphabet[(group >> (11 - bit % 8)) & 31];
	}
	tag[P2P_FRIEND_TAG_SIZE] = 0;
}

static void friend_id(const unsigned char *public_key, char *id)
{
	unsigned char digest[P2P_SHA256_SIZE];

	friend_hash(id_label, (int)sizeof(id_label) - 1, public_key, digest);
	p2p_hex(digest, FRIEND_INBOX_BYTES, id);
}

static void friend_card_topic(const char *tag, const char *id, char *topic)
{
	unsigned char digest[P2P_SHA256_SIZE];
	char hash[2 * FRIEND_INBOX_BYTES + 1];

	p2p_sha256((const unsigned char *)tag, (int)strlen(tag), digest);
	p2p_hex(digest, FRIEND_INBOX_BYTES, hash);
	snprintf(topic, FRIEND_TOPIC_SIZE, P2P_FRIEND_TOPIC_PREFIX "t/%s/%s", hash, id);
}

static void friend_inbox_topic(const char *inbox, char *topic)
{
	snprintf(topic, FRIEND_TOPIC_SIZE, P2P_FRIEND_TOPIC_PREFIX "p/%s", inbox);
}

static int key_parse(const char *text)
{
	unsigned char seed[P2P_SEED_SIZE], inbox[FRIEND_INBOX_BYTES];
	char line[2 * P2P_SEED_SIZE + 1];
	int found = 0;

	while (*text && found < 2)
	{
		const char *end = text + strcspn(text, "\n");
		int length = (int)(end - text);

		while (length && (text[length - 1] == '\r' || text[length - 1] == ' '))
			length--;
		if (length && *text != '#')
		{
			if (length != (found ? 2 * FRIEND_INBOX_BYTES : 2 * P2P_SEED_SIZE))
				return 0;
			memcpy(line, text, (size_t)length);
			line[length] = 0;
			if (!(found ? p2p_unhex(line, inbox, FRIEND_INBOX_BYTES) : p2p_unhex(line, seed, P2P_SEED_SIZE)))
				return 0;
			found++;
		}
		text = *end ? end + 1 : end;
	}
	if (found < 2)
		return 0;
	memcpy(friends.seed, seed, sizeof(seed));
	p2p_hex(inbox, FRIEND_INBOX_BYTES, friends.inbox);
	memset(seed, 0, sizeof(seed));
	memset(line, 0, sizeof(line));
	return 1;
}

static int file_write(const char *path, const char *text, int replace)
{
	size_t size = strlen(text);
#ifdef HALO_WINDOWS
	FILE *file = fopen(path, replace ? "wb" : "wbx");
	int written;

	if (!file)
		return 0;
	written = fwrite(text, 1, size, file) == size;
	if (fclose(file) != 0 || !written)
		return remove(path), 0;
	return 1;
#else
	size_t written = 0;
	int descriptor = open(path, O_CREAT | (replace ? O_TRUNC : O_EXCL) | O_WRONLY, 0600);

	if (descriptor < 0)
		return 0;
	while (written < size)
	{
		long count = (long)write(descriptor, text + written, size - written);

		if (count <= 0)
			break;
		written += (size_t)count;
	}
	if (close(descriptor) != 0 || written != size)
		return remove(path), 0;
	return 1;
#endif
}

static int key_make(const char *path)
{
	unsigned char inbox[FRIEND_INBOX_BYTES];
	char seed_hex[2 * P2P_SEED_SIZE + 1];
	char text[400];
	int made;

	posix_random_bytes(friends.seed, sizeof(friends.seed));
	posix_random_bytes(inbox, sizeof(inbox));
	p2p_hex(friends.seed, P2P_SEED_SIZE, seed_hex);
	p2p_hex(inbox, FRIEND_INBOX_BYTES, friends.inbox);
	snprintf(text, sizeof(text),
		"# OpenCE friend key: your tag comes from it. Keep it secret; a copy cannot be revoked. friends.txt is your list.\n"
		"%s\n%s\n",
		seed_hex, friends.inbox);
	made = file_write(path, text, 0);
	memset(seed_hex, 0, sizeof(seed_hex));
	memset(text, 0, sizeof(text));
	if (!made)
		memset(friends.seed, 0, sizeof(friends.seed));
	return made;
}

static long friend_today(void)
{
	return (long)(time(NULL) / FRIEND_DAY_SECONDS);
}

static unsigned long long friend_now_ms(void)
{
	return (unsigned long long)time(NULL) * 1000;
}

static int friend_name_ok(const unsigned char *name, int size)
{
	static const unsigned least[4] = { 0, 0x80, 0x800, 0x10000 };
	int at = 0, characters = 0;

	if (size < 1 || size > P2P_PARTY_NAME_SIZE)
		return 0;
	while (at < size)
	{
		unsigned code = name[at++];
		int extra = code < 0x80 ? 0 : code >= 0xF0 ? 3 : code >= 0xE0 ? 2 : code >= 0xC0 ? 1 : -1, more;

		if (code < 0x20 || code == 0x7f || extra < 0 || code > 0xF4 || at + extra > size)
			return 0;
		code &= extra ? 0x3Fu >> extra : 0x7Fu;
		for (more = 0; more < extra; more++)
		{
			if ((name[at] & 0xC0) != 0x80)
				return 0;
			code = code << 6 | (name[at++] & 0x3Fu);
		}
		if (code < least[extra] || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
			return 0;
		characters++;
	}
	return characters <= FRIEND_NAME_CHARACTERS;
}

static void friend_name_tag(const unsigned char *key, const unsigned char *name, int size, char *name_tag)
{
	char tag[P2P_FRIEND_TAG_SIZE + 1];

	friend_tag(key, tag);
	snprintf(name_tag, P2P_FRIEND_NAME_TAG_SIZE, "%.*s#%s", size, (const char *)name, tag);
}

static int friend_pair_topic(const unsigned char *to_key, const unsigned char *other_key, char *topic)
{
	unsigned char other[P2P_KEY_SIZE], message[sizeof(request_label) - 1 + 2 * P2P_KEY_SIZE];
	unsigned char digest[P2P_SHA256_SIZE];
	char id[2 * FRIEND_INBOX_BYTES + 1], hash[2 * FRIEND_INBOX_BYTES + 1];
	int size = (int)sizeof(request_label) - 1;

	if (!p2p_ed25519_to_x25519(other_key, other))
		return 0;
	memcpy(message, request_label, (size_t)size);
	memcpy(message + size, to_key, P2P_KEY_SIZE);
	p2p_x25519(message + size + P2P_KEY_SIZE, friends.x25519_secret, other);
	p2p_sha256(message, (int)sizeof(message), digest);
	memset(message, 0, sizeof(message));
	p2p_hex(digest, FRIEND_INBOX_BYTES, hash);
	friend_id(to_key, id);
	snprintf(topic, FRIEND_TOPIC_SIZE, P2P_FRIEND_TOPIC_PREFIX "%s/r/%s", id, hash);
	return 1;
}

static int friend_signed(int kind, const unsigned char *sender, const unsigned char *recipient,
	unsigned long long sequence, const unsigned char *nonce, const unsigned char *body, int body_size,
	unsigned char *message)
{
	int size = (int)sizeof(record_label) - 1, index;

	memcpy(message, record_label, (size_t)size);
	message[size++] = FRIEND_FORMAT;
	message[size++] = (unsigned char)kind;
	memcpy(message + size, sender, P2P_KEY_SIZE);
	size += P2P_KEY_SIZE;
	memcpy(message + size, recipient, P2P_KEY_SIZE);
	size += P2P_KEY_SIZE;
	for (index = 0; index < 8; index++)
		message[size++] = (unsigned char)(sequence >> (56 - 8 * index));
	memcpy(message + size, nonce, FRIEND_NONCE_SIZE);
	size += FRIEND_NONCE_SIZE;
	memcpy(message + size, body, (size_t)body_size);
	return size + body_size;
}

static int friend_sign(int kind, const unsigned char *to_key, const unsigned char *nonce, const void *body,
	int body_size, unsigned char *plain)
{
	unsigned char message[sizeof(record_label) + 2 + 2 * P2P_KEY_SIZE + 8 + FRIEND_NONCE_SIZE + FRIEND_BODY_MAXIMUM];
	unsigned long long now = friend_now_ms();
	int size, tail = 8 + FRIEND_NONCE_SIZE + body_size;

	friends.sequence = now > friends.sequence ? now : friends.sequence + 1;
	size = friend_signed(kind, friends.public_key, to_key, friends.sequence, nonce, body, body_size, message);
	memcpy(plain, friends.public_key, P2P_KEY_SIZE);
	memcpy(plain + P2P_KEY_SIZE, message + size - tail, (size_t)tail);
	p2p_ed25519_sign(friends.seed, friends.public_key, message, size, plain + P2P_KEY_SIZE + tail);
	return FRIEND_PLAIN_SIZE + body_size;
}

static int friend_seal_key(const unsigned char *shared, const unsigned char *one_time, const unsigned char *recipient,
	unsigned char *key)
{
	unsigned char message[sizeof(record_label) - 1 + 3 * P2P_KEY_SIZE];
	int size = (int)sizeof(record_label) - 1;

	if (p2p_equal(shared, zeros, P2P_KEY_SIZE))
		return 0;
	memcpy(message, record_label, (size_t)size);
	memcpy(message + size, shared, P2P_KEY_SIZE);
	memcpy(message + size + P2P_KEY_SIZE, one_time, P2P_KEY_SIZE);
	memcpy(message + size + 2 * P2P_KEY_SIZE, recipient, P2P_KEY_SIZE);
	p2p_sha256(message, (int)sizeof(message), key);
	memset(message, 0, sizeof(message));
	return 1;
}

static int friend_seal(int kind, const unsigned char *to_key, const unsigned char *plain, int plain_size,
	unsigned char *bytes)
{
	unsigned char recipient[P2P_KEY_SIZE], secret[P2P_KEY_SIZE], shared[P2P_KEY_SIZE], key[P2P_SHA256_SIZE];
	int made;

	bytes[0] = FRIEND_FORMAT;
	bytes[1] = (unsigned char)kind;
	if (kind == FRIEND_CARD)
	{
		memcpy(bytes + 2, plain, (size_t)plain_size);
		return 2 + plain_size;
	}
	if (!p2p_ed25519_to_x25519(to_key, recipient))
		return 0;
	posix_random_bytes(secret, sizeof(secret));
	p2p_x25519(bytes + 2, secret, NULL);
	p2p_x25519(shared, secret, recipient);
	made = friend_seal_key(shared, bytes + 2, recipient, key);
	memset(secret, 0, sizeof(secret));
	memset(shared, 0, sizeof(shared));
	if (!made)
		return 0;
	return 2 + P2P_KEY_SIZE + p2p_seal(key, plain, plain_size, bytes + 2 + P2P_KEY_SIZE);
}

static int friend_record_write(int kind, const unsigned char *to_key, const unsigned char *nonce, const void *body,
	int body_size, unsigned char *bytes)
{
	unsigned char plain[FRIEND_PLAIN_SIZE + FRIEND_BODY_MAXIMUM];

	return friend_seal(kind, to_key, plain, friend_sign(kind, to_key, nonce, body, body_size, plain), bytes);
}

static int friend_record_kind(const unsigned char *bytes, int size)
{
	int kind;

	if (!size)
		return FRIEND_DROP_EMPTY;
	if (size > FRIEND_RECORD_MAXIMUM)
		return FRIEND_DROP_LONG;
	if (bytes[0] != FRIEND_FORMAT)
		return FRIEND_DROP_FORMAT;
	if (size < 2)
		return FRIEND_DROP_SHORT;
	kind = bytes[1];
	if (kind < FRIEND_CARD || kind > FRIEND_INVITE)
		return FRIEND_DROP_KIND;
	if (size < (kind == FRIEND_CARD ? 2 : FRIEND_SEALED_SIZE) + FRIEND_PLAIN_SIZE)
		return FRIEND_DROP_SHORT;
	return kind;
}

static int friend_record_read(const unsigned char *bytes, int size, unsigned long long now_ms,
	struct friend_record *record)
{
	unsigned char plain[FRIEND_RECORD_MAXIMUM];
	unsigned char message[sizeof(record_label) + 2 + 2 * P2P_KEY_SIZE + FRIEND_RECORD_MAXIMUM];
	const unsigned char *opened = bytes + 2;
	unsigned long long kept;
	int kind = friend_record_kind(bytes, size), plain_size = size - 2, index, message_size;

	memset(record->sender, 0, P2P_KEY_SIZE);
	if (kind < 0)
		return kind;
	if (kind != FRIEND_CARD)
	{
		unsigned char shared[P2P_KEY_SIZE], key[P2P_SHA256_SIZE];
		int made;

		p2p_x25519(shared, friends.x25519_secret, bytes + 2);
		made = friend_seal_key(shared, bytes + 2, friends.x25519_public, key);
		memset(shared, 0, sizeof(shared));
		if (!made)
			return FRIEND_DROP_KEY;
		plain_size = p2p_open(key, bytes + 2 + P2P_KEY_SIZE, size - 2 - P2P_KEY_SIZE, plain);
		if (plain_size < FRIEND_PLAIN_SIZE)
			return FRIEND_DROP_SEAL;
		opened = plain;
	}
	record->kind = kind;
	record->sequence = 0;
	for (index = 0; index < 8; index++)
		record->sequence = record->sequence << 8 | opened[P2P_KEY_SIZE + index];
	memcpy(record->nonce, opened + P2P_KEY_SIZE + 8, FRIEND_NONCE_SIZE);
	record->body_size = plain_size - FRIEND_PLAIN_SIZE;
	memcpy(record->body, opened + P2P_KEY_SIZE + 8 + FRIEND_NONCE_SIZE, (size_t)record->body_size);
	message_size = friend_signed(kind, opened, kind == FRIEND_CARD ? zeros : friends.public_key, record->sequence,
		record->nonce, record->body, record->body_size, message);
	if (!p2p_ed25519_verify(opened, message, message_size, opened + plain_size - P2P_SIGNATURE_SIZE))
		return FRIEND_DROP_SIGNATURE;
	memcpy(record->sender, opened, P2P_KEY_SIZE);
	kept = kind == FRIEND_CARD ? FRIEND_CARD_SECONDS : kind == FRIEND_REQUEST || kind == FRIEND_ACCEPT ?
		FRIEND_REQUEST_SECONDS : FRIEND_FRESH_SECONDS;
	if (record->sequence > now_ms + FRIEND_FRESH_SECONDS * 1000ULL || record->sequence + kept * 1000 < now_ms)
		return FRIEND_DROP_AGE;
	if (((kind == FRIEND_REQUEST || kind == FRIEND_ACCEPT) && (record->body_size <= FRIEND_INBOX_BYTES ||
		!friend_name_ok(record->body + FRIEND_INBOX_BYTES, record->body_size - FRIEND_INBOX_BYTES))) ||
		(kind == FRIEND_CARD && !friend_name_ok(record->body, record->body_size)))
	{
		return FRIEND_DROP_NAME;
	}
	return kind;
}

static struct friend *friend_find(const unsigned char *key)
{
	int index;

	for (index = 0; index < friends.count; index++)
	{
		if (!memcmp(friends.list[index].key, key, P2P_KEY_SIZE))
			return &friends.list[index];
	}
	return NULL;
}

enum
{
	_line_unreadable,
	_line_repeated,
	_line_past_most,
	_line_unheard,
	NUMBER_OF_LINE_DROPS,
};

static int friend_line_read(const char *line, int length, struct friend *friend)
{
	char copy[400], key[80], inbox[40], state[16], nonce[80];
	unsigned char bytes[FRIEND_INBOX_BYTES];
	const char *name_tag, *mark;
	unsigned long long sequence;
	long day;
	int used = 0;

	if (length >= (int)sizeof(copy))
		return 0;
	memcpy(copy, line, (size_t)length);
	copy[length] = 0;
	if (sscanf(copy, "%79s %39s %15s %79s %llu %ld %n", key, inbox, state, nonce, &sequence, &day, &used) != 6 ||
		!used)
	{
		return 0;
	}
	memset(friend, 0, sizeof(*friend));
	friend->sent = !strcmp(state, "sent");
	name_tag = copy + used;
	mark = strrchr(name_tag, '#');
	if (!p2p_unhex(key, friend->key, P2P_KEY_SIZE) || (!friend->sent && strcmp(state, "friend")) ||
		(strcmp(inbox, "-") && !p2p_unhex(inbox, bytes, FRIEND_INBOX_BYTES)) ||
		(friend->sent ? !p2p_unhex(nonce, friend->nonce, FRIEND_NONCE_SIZE) : strcmp(nonce, "-")) || !mark ||
		!friend_name_ok((const unsigned char *)name_tag, (int)(mark - name_tag)))
	{
		return 0;
	}
	friend_name_tag(friend->key, (const unsigned char *)name_tag, (int)(mark - name_tag), friend->name_tag);
	if (strcmp(friend->name_tag, name_tag))
		return 0;
	friend_id(friend->key, friend->id);
	if (strcmp(inbox, "-"))
		p2p_hex(bytes, FRIEND_INBOX_BYTES, friend->inbox);
	friend->last_sequence = sequence;
	friend->last_day = day;
	return 1;
}

static int friends_parse(const char *text, struct friend *list, int *drops)
{
	long today = friend_today();
	int count = 0;

	while (*text)
	{
		const char *end = text + strcspn(text, "\n");
		int length = (int)(end - text), index;
		struct friend friend;

		if (length && text[length - 1] == '\r')
			length--;
		if (length)
		{
			if (!friend_line_read(text, length, &friend))
				drops[_line_unreadable]++;
			else if (today - friend.last_day > (friend.sent ? FRIEND_REQUEST_DAYS : FRIEND_FORGOTTEN_DAYS))
				drops[_line_unheard]++;
			else
			{
				for (index = 0; index < count && memcmp(list[index].key, friend.key, P2P_KEY_SIZE); index++)
					;
				if (index < count)
					drops[_line_repeated]++;
				else if (count == P2P_FRIENDS_MAXIMUM)
					drops[_line_past_most]++;
				else
					list[count++] = friend;
			}
		}
		text = *end ? end + 1 : end;
	}
	return count;
}

static int friends_load(const char *path)
{
	char other[1100];
	int drops[NUMBER_OF_LINE_DROPS] = { 0 };
	size_t size = 0;
	char *text;
	int index;

	snprintf(other, sizeof(other), "%s.tmp", path);
	remove(other);
	friends.count = 0;
	text = config_file_read(path, &size);
	if (!text)
		return 0;
	friends.count = friends_parse(text, friends.list, drops);
	for (index = 0; index < friends.count; index++)
		friends.list[index].number = ++friends.numbers;
	if (drops[_line_unreadable] + drops[_line_repeated] + drops[_line_past_most] + drops[_line_unheard])
	{
		platform_log("friends: friends.txt: %d unreadable, %d repeated, %d past %d, %d unheard lines left out",
			drops[_line_unreadable], drops[_line_repeated], drops[_line_past_most], P2P_FRIENDS_MAXIMUM,
			drops[_line_unheard]);
		snprintf(other, sizeof(other), "%s.bad", path);
		remove(other);
		file_write(other, text, 0);
	}
	free(text);
	return 1;
}

static int friend_removed(const unsigned char *key)
{
	int index;

	for (index = 0; index < friends.removed_count; index++)
		if (p2p_equal(key, friends.removed[index], P2P_KEY_SIZE))
			return 1;
	return 0;
}

static void friends_merge(const char *path)
{
	static struct friend saved[P2P_FRIENDS_MAXIMUM];
	int drops[NUMBER_OF_LINE_DROPS] = { 0 };
	size_t size = 0;
	char *text = config_file_read(path, &size);
	int count, index;

	if (!text)
		return;
	count = friends_parse(text, saved, drops);
	free(text);
	for (index = 0; index < count; index++)
	{
		struct friend *friend = friend_find(saved[index].key);

		if (friend_removed(saved[index].key))
			continue;
		if (!friend)
		{
			if (friends.count == P2P_FRIENDS_MAXIMUM)
				continue;
			friend = &friends.list[friends.count++];
			*friend = saved[index];
			friend->number = ++friends.numbers;
			continue;
		}
		if (friend->sent && !saved[index].sent)
		{
			friend->sent = 0;
			memset(friend->nonce, 0, sizeof(friend->nonce));
			strcpy(friend->inbox, saved[index].inbox);
			strcpy(friend->name_tag, saved[index].name_tag);
		}
		if (saved[index].last_sequence > friend->last_sequence)
			friend->last_sequence = saved[index].last_sequence;
		if (saved[index].last_day > friend->last_day)
			friend->last_day = saved[index].last_day;
	}
}

static int friends_save(const char *path)
{
	char temporary[1100], key[2 * P2P_KEY_SIZE + 1], nonce[2 * FRIEND_NONCE_SIZE + 1];
	char *text = malloc((size_t)P2P_FRIENDS_MAXIMUM * 256 + 1);
	size_t size = 0;
	int index, saved;

	if (!text)
		return 0;
	friends_merge(path);
	text[0] = 0;
	for (index = 0; index < friends.count; index++)
	{
		const struct friend *friend = &friends.list[index];

		p2p_hex(friend->key, P2P_KEY_SIZE, key);
		p2p_hex(friend->nonce, FRIEND_NONCE_SIZE, nonce);
		size += (size_t)snprintf(text + size, 256, "%s %s %s %s %llu %ld %s\n", key,
			friend->inbox[0] ? friend->inbox : "-", friend->sent ? "sent" : "friend", friend->sent ? nonce : "-",
			friend->last_sequence, friend->last_day, friend->name_tag);
	}
	snprintf(temporary, sizeof(temporary), "%s.tmp", path);
	remove(temporary);
	saved = file_write(temporary, text, 1);
	free(text);
	/* (Windows' rename does not replace a file; Android and web have no posix_server.c) */
#if HALO_SERVER_BUILD
	if (saved && !posix_server_rename(temporary, path))
#else
	if (saved && rename(temporary, path) != 0)
#endif
		saved = 0;
	if (!saved)
	{
		remove(temporary);
		platform_log("friends: friends.txt could not be saved");
	}
	friends.unsaved = !saved;
	return saved;
}

static void friends_keys(void)
{
	p2p_ed25519_public(friends.seed, friends.public_key, friends.x25519_secret);
	p2p_ed25519_to_x25519(friends.public_key, friends.x25519_public);
	friend_tag(friends.public_key, friends.tag);
	friend_id(friends.public_key, friends.id);
	snprintf(friends.requests_topic, sizeof(friends.requests_topic), P2P_FRIEND_TOPIC_PREFIX "%s/r/+", friends.id);
	friend_inbox_topic(friends.inbox, friends.inbox_topic);
}

/* under p2p_lock; a friend.key that is there and cannot be read is left as it is and friends stay off */
static void friends_key_load(void)
{
	char path[1100];
	size_t size = 0;
	char *text;

	if (friends.loaded)
		return;
	friends.loaded = 1;
	friends.lookup_result = P2P_FRIEND_ADD_LOOKING;
	if (!config_boolean("network.friends"))
		return;
	snprintf(path, sizeof(path), "%s/friend.key", platform_save_root());
	text = config_file_read(path, &size);
	if (text)
	{
		friends.on = key_parse(text);
		memset(text, 0, size);
		free(text);
	}
	else if (key_make(path))
	{
		friends.on = 1;
		platform_log("friends: new key");
	}
	else
	{
		friends.key_unmade = 1;
		platform_log("friends: friend.key could not be created");
		return;
	}
	if (!friends.on)
	{
		platform_log("friends: friend.key unreadable");
		return;
	}
	friends_keys();
	snprintf(friends.list_path, sizeof(friends.list_path), "%s/friends.txt", platform_save_root());
	friends_load(friends.list_path);
}

static void friend_out(const char *topic, const unsigned char *bytes, int size, int kept)
{
	if (friends.out_count == FRIEND_OUTBOX_SIZE || (!size && kept >= 0))
		return;
	snprintf(friends.out[friends.out_count].topic, FRIEND_TOPIC_SIZE, "%s", topic);
	if (size)
		memcpy(friends.out[friends.out_count].bytes, bytes, (size_t)size);
	friends.out[friends.out_count].size = size;
	friends.out[friends.out_count].kept = kept;
	friends.out[friends.out_count].taken = 0;
	friends.out_count++;
}

static int own_name_ok(void)
{
	const char *name = p2p_party_name();

	return friend_name_ok((const unsigned char *)name, (int)strlen(name));
}

static int friend_body(unsigned char *body)
{
	const char *name = p2p_party_name();
	int size = (int)strlen(name);

	p2p_unhex(friends.inbox, body, FRIEND_INBOX_BYTES);
	memcpy(body + FRIEND_INBOX_BYTES, name, (size_t)size);
	return FRIEND_INBOX_BYTES + size;
}

static void friend_accept(const unsigned char *key, const unsigned char *nonce)
{
	unsigned char body[FRIEND_BODY_MAXIMUM], bytes[FRIEND_RECORD_MAXIMUM];
	char topic[FRIEND_TOPIC_SIZE];
	int size = friend_record_write(FRIEND_ACCEPT, key, nonce, body, friend_body(body), bytes);

	if (size && friend_pair_topic(key, key, topic))
		friend_out(topic, bytes, size, FRIEND_REQUEST_SECONDS);
}

static void friend_invite_drop(const unsigned char *key)
{
	if (friends.invite_pending && p2p_equal(friends.invite_key, key, P2P_KEY_SIZE))
		friends.invite_pending = 0;
}

static int friend_link_ok(const unsigned char *link, int size)
{
	int index;

	for (index = 0; index < size; index++)
	{
		if (link[index] < 0x21 || link[index] > 0x7e)
			return 0;
	}
	return size > 0 && size < P2P_PARTY_LINK_SIZE;
}

static void friend_offline(struct friend *friend)
{
	friend->heard = friend->presence = 0;
	friend->state = P2P_FRIEND_OFFLINE;
	friend_invite_drop(friend->key);
}

static void friend_presence_due(struct friend *friend, int kind)
{
	friend->presence = kind;
	friend->presence_taken = 0;
}

static int friend_silent(const struct friend *friend)
{
	return (unsigned int)(p2p_now() - friend->heard_time) >= FRIEND_OFFLINE_SECONDS * 1000u;
}

static void friend_presence_heard(struct friend *friend, const struct friend_record *record)
{
	if (record->kind == FRIEND_BYE)
	{
		friend_offline(friend);
		return;
	}
	if ((record->kind == FRIEND_HELLO || !friend->heard) && friend->presence != FRIEND_HELLO)
		friend_presence_due(friend, FRIEND_HERE);
	friend->heard = 1;
	friend->heard_time = p2p_now();
	friend->state = (record->body[1] << 8 | record->body[2]) == HALO_PORT_NETWORK_VERSION ? record->body[0] :
		P2P_FRIEND_UPDATE;
}

static int friend_card_heard(const char *topic, const unsigned char *payload, int size)
{
	struct friend_record record;
	char tag[P2P_FRIEND_TAG_SIZE + 1], id[2 * FRIEND_INBOX_BYTES + 1], expected[FRIEND_TOPIC_SIZE];
	int kind, index;

	if (!friends.look_topic[0])
		return FRIEND_DROP_STATE;
	kind = friend_record_read(payload, size, friend_now_ms(), &record);
	if (kind < 0)
		return kind;
	friend_tag(record.sender, tag);
	if (strcmp(tag, friends.look_tag))
		return FRIEND_DROP_TAG;
	friend_id(record.sender, id);
	friend_card_topic(tag, id, expected);
	if (strcmp(expected, topic))
		return FRIEND_DROP_TOPIC;
	for (index = 0; index < friends.look_count; index++)
	{
		if (p2p_equal(friends.look_keys[index], record.sender, P2P_KEY_SIZE))
			return kind;
	}
	if (friends.look_count < 2)
	{
		memcpy(friends.look_keys[friends.look_count], record.sender, P2P_KEY_SIZE);
		memcpy(friends.look_names[friends.look_count], record.body, (size_t)record.body_size);
		friends.look_names[friends.look_count++][record.body_size] = 0;
	}
	return kind;
}

static int friend_request_find(const unsigned char *key)
{
	int index;

	for (index = 0; index < friends.request_count && memcmp(friends.requests[index].key, key, P2P_KEY_SIZE); index++)
		;
	return index;
}

static void friend_request_drop(int index)
{
	memmove(&friends.requests[index], &friends.requests[index + 1],
		sizeof(friends.requests[0]) * (size_t)(--friends.request_count - index));
}

static int friend_heard(const char *topic, const unsigned char *payload, int size)
{
	struct friend_record record;
	struct friend_request *request;
	struct friend *friend;
	char expected[FRIEND_TOPIC_SIZE];
	unsigned char digest[P2P_SHA256_SIZE];
	unsigned long long now = friend_now_ms();
	int requests = !strncmp(topic, friends.requests_topic, strlen(friends.requests_topic) - 1);
	int cards = !strncmp(topic, P2P_FRIEND_TOPIC_PREFIX "t/", sizeof(P2P_FRIEND_TOPIC_PREFIX "t/") - 1);
	int kind = friend_record_kind(payload, size), index;

	if (kind < 0)
		return kind;
	if (requests != (kind == FRIEND_REQUEST || kind == FRIEND_ACCEPT) || cards != (kind == FRIEND_CARD))
		return FRIEND_DROP_KIND;
	if (cards)
		return friend_card_heard(topic, payload, size);
	if (requests)
	{
		p2p_sha256(payload, size, digest);
		for (index = 0; index < friends.opened; index++)
		{
			if (!memcmp(friends.seen[index], digest, sizeof(friends.seen[index])))
				return FRIEND_DROP_REPLAY;
		}
		if (friends.opened == FRIEND_OPENED_MAXIMUM)
		{
			if (!friends.flood_logged++)
				platform_log("friends: over %d request records this session: the rest are dropped",
					FRIEND_OPENED_MAXIMUM);
			return FRIEND_DROP_FLOOD;
		}
		memcpy(friends.seen[friends.opened++], digest, sizeof(friends.seen[0]));
	}
	kind = friend_record_read(payload, size, now, &record);
	friend = friend_find(record.sender);
	if (kind == FRIEND_DROP_AGE && friend && !friend->sent && !friend->clock_logged++)
		platform_log("friends: friend %d clock off by %lld s", friend->number,
			((long long)record.sequence - (long long)now) / 1000);
	if (kind < 0)
		return kind;
	if (p2p_equal(record.sender, friends.public_key, P2P_KEY_SIZE))
		return FRIEND_DROP_SELF;
	if (requests && (!friend_pair_topic(friends.public_key, record.sender, expected) || strcmp(expected, topic)))
		return FRIEND_DROP_TOPIC;
	if (friend && record.sequence <= friend->last_sequence)
		return FRIEND_DROP_REPLAY;
	if (kind == FRIEND_ACCEPT)
	{
		if (!friend || !friend->sent || !p2p_equal(friend->nonce, record.nonce, FRIEND_NONCE_SIZE))
			return FRIEND_DROP_NONCE;
		friend->sent = 0;
		memset(friend->nonce, 0, sizeof(friend->nonce));
		p2p_hex(record.body, FRIEND_INBOX_BYTES, friend->inbox);
		friend_name_tag(record.sender, record.body + FRIEND_INBOX_BYTES, record.body_size - FRIEND_INBOX_BYTES,
			friend->name_tag);
		friend->last_sequence = record.sequence;
		friend->last_day = friend_today();
		friend_presence_due(friend, FRIEND_HELLO);
		friends_save(friends.list_path);
		index = friend_request_find(record.sender);
		if (index < friends.request_count)
			friend_request_drop(index);
		friend_out(topic, NULL, 0, -1);
		return kind;
	}
	if (kind != FRIEND_REQUEST)
	{
		if (!friend || friend->sent)
			return FRIEND_DROP_STRANGER;
		if (((kind == FRIEND_HELLO || kind == FRIEND_HERE) && (record.body_size != FRIEND_PRESENCE_SIZE ||
			record.body[0] < P2P_FRIEND_MENUS || record.body[0] > P2P_FRIEND_IN_GAME)) ||
			(kind == FRIEND_INVITE && !friend_link_ok(record.body, record.body_size)))
		{
			return FRIEND_DROP_BODY;
		}
		friend->last_sequence = record.sequence;
		if (friend->last_day != friend_today())
		{
			friend->last_day = friend_today();
			friends.save_due = 1;
		}
		if (kind == FRIEND_INVITE)
		{
			if (friends.state != P2P_FRIEND_MATCHMAKING)
				return FRIEND_DROP_STATE;
			if (friends.invite_pending && !p2p_equal(friends.invite_key, record.sender, P2P_KEY_SIZE))
				return FRIEND_DROP_BUSY;
			friends.invite_pending = 1;
			memcpy(friends.invite_key, record.sender, P2P_KEY_SIZE);
			memcpy(friends.invite_link, record.body, (size_t)record.body_size);
			friends.invite_link[record.body_size] = 0;
		}
		if (kind == FRIEND_HELLO || kind == FRIEND_HERE || kind == FRIEND_BYE)
			friend_presence_heard(friend, &record);
		return kind;
	}
	if (friend && !friend->sent)
	{
		friend->last_sequence = record.sequence;
		if (own_name_ok())
		{
			friend_out(topic, NULL, 0, -1);
			friend_accept(record.sender, record.nonce);
		}
		return kind;
	}
	for (index = 0; index < friends.refused_count; index++)
	{
		if (!memcmp(friends.refused[index], record.sender, P2P_KEY_SIZE))
			return FRIEND_DROP_REFUSED;
	}
	index = friend_request_find(record.sender);
	if (index < friends.request_count && record.sequence <= friends.requests[index].sequence)
		return FRIEND_DROP_REPLAY;
	if (index == friends.request_count && index == P2P_FRIEND_REQUESTS_SHOWN)
		index = 0;
	if (index < friends.request_count)
		memmove(&friends.requests[index], &friends.requests[index + 1],
			sizeof(friends.requests[0]) * (size_t)(friends.request_count - index - 1));
	else
		friends.request_count++;
	request = &friends.requests[friends.request_count - 1];
	memcpy(request->key, record.sender, P2P_KEY_SIZE);
	friend_id(record.sender, request->id);
	p2p_hex(record.body, FRIEND_INBOX_BYTES, request->inbox);
	friend_name_tag(record.sender, record.body + FRIEND_INBOX_BYTES, record.body_size - FRIEND_INBOX_BYTES,
		request->name_tag);
	memcpy(request->nonce, record.nonce, FRIEND_NONCE_SIZE);
	request->sequence = record.sequence;
	return kind;
}

void p2p_friends_received(const char *topic, const unsigned char *payload, int size)
{
	int kind;

	if (!friends.on)
		return;
	kind = friend_heard(topic, payload, size);
	if (kind < 0)
		friends.drops[-kind]++;
}

static int friend_request_publish(struct friend *friend)
{
	unsigned char body[FRIEND_BODY_MAXIMUM];
	char topic[FRIEND_TOPIC_SIZE];

	if (!friend_pair_topic(friend->key, friend->key, topic) || (!friend->request_size && !own_name_ok()))
		return 0;
	if (!friend->request_size)
		friend->request_size = friend_record_write(FRIEND_REQUEST, friend->key, friend->nonce, body, friend_body(body),
			friend->request);
	return friend->request_size ?
		p2p_signal_friend_publish(topic, friend->request, friend->request_size, FRIEND_REQUEST_SECONDS,
		&friend->request_taken) : 0;
}

static int friend_card_publish(void)
{
	char topic[FRIEND_TOPIC_SIZE];

	if (!friends.card_size)
	{
		const char *name = p2p_party_name();

		if (!own_name_ok())
			return 0;
		friends.card_size = friend_record_write(FRIEND_CARD, zeros, zeros, name, (int)strlen(name), friends.card);
	}
	friend_card_topic(friends.tag, friends.id, topic);
	return friends.card_size ? p2p_signal_friend_publish(topic, friends.card, friends.card_size, FRIEND_CARD_SECONDS,
		&friends.card_taken) : 0;
}

static int friend_add(const unsigned char *key, const char *name);

static void friend_lookup_end(void)
{
	friends.lookup_result = friends.look_count == 2 ? P2P_FRIEND_ADD_TWO_PLAYERS : friends.look_count ?
		friend_add(friends.look_keys[0], friends.look_names[0]) :
		p2p_signal_wildcards() ? P2P_FRIEND_ADD_NO_PLAYER : P2P_FRIEND_ADD_NO_SERVER;
	friends.look_topic[0] = 0;
	friends.topics_set = 0;
}

int p2p_friends_wanted(void)
{
	if (halo_server_mode())
		return 0;
	friends_key_load();
	return friends.on;
}

static int friend_presence_record(const struct friend *friend, char *topic, unsigned char *bytes)
{
	unsigned char body[FRIEND_PRESENCE_SIZE];

	body[0] = (unsigned char)(friends.state ? friends.state : P2P_FRIEND_MENUS);
	body[1] = (unsigned char)(HALO_PORT_NETWORK_VERSION >> 8);
	body[2] = (unsigned char)HALO_PORT_NETWORK_VERSION;
	friend_inbox_topic(friend->inbox, topic);
	return friend_record_write(friend->presence, friend->key, zeros, body, FRIEND_PRESENCE_SIZE, bytes);
}

static int friend_presence_publish(struct friend *friend)
{
	unsigned char bytes[FRIEND_RECORD_MAXIMUM];
	char topic[FRIEND_TOPIC_SIZE];
	int size = friend_presence_record(friend, topic, bytes);

	return !size || p2p_signal_friend_publish(topic, bytes, size, 0, &friend->presence_taken) >= friends.connected;
}

static void friends_expire(void)
{
	int index;

	for (index = 0; index < friends.count; index++)
	{
		struct friend *friend = &friends.list[index];

		if (friend->heard && friend_silent(friend))
			friend_offline(friend);
	}
}

enum { FRIEND_DROPS_LOG_SECONDS = 3600 };

static void friends_drops_log(void)
{
	static const char *const reasons[FRIEND_DROP_REASONS] = {
		[-FRIEND_DROP_EMPTY] = "empty",
		[-FRIEND_DROP_LONG] = "long",
		[-FRIEND_DROP_FORMAT] = "format",
		[-FRIEND_DROP_SHORT] = "short",
		[-FRIEND_DROP_KIND] = "kind",
		[-FRIEND_DROP_FLOOD] = "flood",
		[-FRIEND_DROP_KEY] = "key",
		[-FRIEND_DROP_SEAL] = "seal",
		[-FRIEND_DROP_SIGNATURE] = "signature",
		[-FRIEND_DROP_AGE] = "age",
		[-FRIEND_DROP_NAME] = "name",
		[-FRIEND_DROP_SELF] = "self",
		[-FRIEND_DROP_TOPIC] = "topic",
		[-FRIEND_DROP_REPLAY] = "replay",
		[-FRIEND_DROP_STRANGER] = "stranger",
		[-FRIEND_DROP_REFUSED] = "refused",
		[-FRIEND_DROP_NONCE] = "nonce",
		[-FRIEND_DROP_BODY] = "body",
		[-FRIEND_DROP_STATE] = "state",
		[-FRIEND_DROP_BUSY] = "busy",
		[-FRIEND_DROP_TAG] = "tag",
	};
	char text[512];
	int reason, length = 0, total = 0;

	for (reason = 1; reason < FRIEND_DROP_REASONS; reason++)
	{
		if (friends.drops[reason])
		{
			length += snprintf(text + length, sizeof(text) - (size_t)length, "%s%d %s", total ? ", " : "",
				friends.drops[reason], reasons[reason]);
			total += friends.drops[reason];
			friends.drops[reason] = 0;
		}
	}
	if (total)
	{
		friends.drops_logged = (long)time(NULL);
		platform_log("friends: %d record%s dropped: %s", total, total == 1 ? "" : "s", text);
	}
}

void p2p_friends_quit(void)
{
	unsigned char bytes[FRIEND_RECORD_MAXIMUM];
	char topic[FRIEND_TOPIC_SIZE];
	int index, size;

	pthread_mutex_lock(&p2p_lock);
	for (index = 0; friends.on && index < friends.count; index++)
	{
		struct friend *friend = &friends.list[index];

		if (friend->heard)
		{
			friend->presence = FRIEND_BYE;
			size = friend_presence_record(friend, topic, bytes);
			if (size)
				p2p_signal_friend_goodbye(topic, bytes, size);
			friend->heard = friend->presence = 0;
		}
	}
	p2p_signal_flush();
	friends_drops_log();
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_friends_update(void)
{
	long today;
	unsigned up, fresh;
	int connected, index;

	if (!p2p_friends_wanted())
		return;
	if ((long)time(NULL) - friends.drops_logged >= FRIEND_DROPS_LOG_SECONDS)
		friends_drops_log();
	friends_expire();
	if (friends.look_topic[0] && (unsigned int)(p2p_now() - friends.look_time) >= FRIEND_LOOKUP_SECONDS * 1000u)
		friend_lookup_end();
	if (!friends.topics_set)
	{
		p2p_signal_friend_topics(friends.requests_topic, friends.inbox_topic, friends.look_topic);
		friends.topics_set = 1;
	}
	connected = p2p_signal_connected();
	up = p2p_signal_up();
	fresh = up & ~friends.up;
	if (fresh)
	{
		int hellos = 0;

		for (index = 0; index < friends.count; index++)
		{
			struct friend *friend = &friends.list[index];

			friend->presence_taken &= ~fresh;
			friend->request_taken &= ~fresh;
			friend->due = friend->sent;
			if (!friend->sent && friend->inbox[0])
			{
				friend->presence = FRIEND_HELLO;
				hellos++;
			}
		}
		for (index = 0; index < friends.out_count; index++)
			friends.out[index].taken &= ~fresh;
		friends.card_taken &= ~fresh;
		friends.card_size = 0;
		friends.card_due = 1;
		if (!friends.up)
			friends.here_time = p2p_now();
		platform_log("friends: %d broker%s, %d hello", connected, connected == 1 ? "" : "s", hellos);
	}
	friends.connected = connected;
	friends.up = up;
	if (!connected)
		return;
	today = friend_today();
	for (index = 0; index < friends.count; index++)
	{
		struct friend *friend = &friends.list[index];

		if (friend->due && (!friend->sent || today - friend->last_day > FRIEND_REQUEST_DAYS ||
			friend_request_publish(friend) >= connected))
		{
			friend->due = 0;
		}
	}
	for (index = 0; index < friends.out_count;)
	{
		if (p2p_signal_friend_publish(friends.out[index].topic, friends.out[index].bytes, friends.out[index].size,
			friends.out[index].kept, &friends.out[index].taken) >= connected)
		{
			memmove(&friends.out[index], &friends.out[index + 1],
				sizeof(friends.out[0]) * (size_t)(--friends.out_count - index));
		}
		else
		{
			index++;
		}
	}
	if (friends.state_changed || (unsigned int)(p2p_now() - friends.here_time) >= FRIEND_HERE_SECONDS * 1000u)
	{
		friends.state_changed = 0;
		friends.here_time = p2p_now();
		for (index = 0; index < friends.count; index++)
		{
			if (friends.list[index].heard && friends.list[index].presence != FRIEND_HELLO)
				friend_presence_due(&friends.list[index], FRIEND_HERE);
		}
	}
	for (index = 0; index < friends.count; index++)
	{
		if (friends.list[index].presence && !friend_presence_publish(&friends.list[index]))
			break;
		friends.list[index].presence = 0;
	}
	if (friends.card_due && friend_card_publish() >= connected)
		friends.card_due = 0;
	if (friends.save_due)
	{
		friends.save_due = 0;
		friends_save(friends.list_path);
	}
}

void p2p_friends_key(unsigned char *key)
{
	friends_key_load();
	if (friends.on)
		memcpy(key, friends.public_key, P2P_KEY_SIZE);
	else
		memset(key, 0, P2P_KEY_SIZE);
}

void p2p_friends_mark(struct p2p_party_status *status)
{
	int index;

	for (index = 0; index < status->count; index++)
	{
		const struct friend *friend = friend_find(status->members[index].friend_key);

		status->members[index].friend = friend ? (friend->sent ? 2 : 1) : 0;
	}
}

static int friend_name_before(const char *left, const char *right)
{
	for (; *left && tolower((unsigned char)*left) == tolower((unsigned char)*right); left++, right++)
		;
	return tolower((unsigned char)*left) < tolower((unsigned char)*right);
}

void p2p_friends_get_status(struct p2p_friends_status *status)
{
	struct p2p_party_status party;
	const char *name;
	int index, member, place, heard;

	memset(status, 0, sizeof(*status));
	p2p_party_get_status(&party);
	pthread_mutex_lock(&p2p_lock);
	friends_key_load();
	status->on = friends.on;
	status->key_unmade = friends.key_unmade;
	status->unsaved = friends.unsaved;
	name = p2p_party_name();
	if (friends.on && name[0])
		snprintf(status->me, sizeof(status->me), "%s#%s", name, friends.tag);
	status->reached = friends.connected;
	status->looking = friends.look_topic[0] != 0;
	status->lookup_result = friends.lookup_result;
	for (heard = 1; heard >= 0; heard--)
	{
		int first = status->row_count;

		for (index = 0; index < friends.count; index++)
		{
			const struct friend *friend = &friends.list[index];

			status->sent += heard && friend->sent;
			for (member = 0; member < party.count &&
				!p2p_equal(party.members[member].friend_key, friend->key, P2P_KEY_SIZE); member++)
				;
			if (friend->sent || !friend->heard != !heard || member < party.count)
				continue;
			for (place = status->row_count; place > first && friend_name_before(friend->name_tag,
				status->rows[place - 1].name_tag); place--)
				status->rows[place] = status->rows[place - 1];
			strcpy(status->rows[place].id, friend->id);
			strcpy(status->rows[place].name_tag, friend->name_tag);
			status->rows[place].state = heard ? friend->state : P2P_FRIEND_OFFLINE;
			status->row_count++;
		}
		if (heard)
			status->online_count = status->row_count;
	}
	status->request_count = friends.request_count;
	for (index = 0; index < friends.request_count; index++)
	{
		const struct friend_request *request = &friends.requests[friends.request_count - 1 - index];

		strcpy(status->requests[index].id, request->id);
		strcpy(status->requests[index].name_tag, request->name_tag);
	}
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_friends_set_state(int state)
{
	pthread_mutex_lock(&p2p_lock);
	if (state != friends.state)
	{
		friends.state = state;
		friends.state_changed = 1;
		if (state != P2P_FRIEND_MATCHMAKING)
			friends.invite_pending = 0;
	}
	pthread_mutex_unlock(&p2p_lock);
}

static int friend_answer(const struct friend_request *request, int yes);

/* under p2p_lock */
static int friend_add(const unsigned char *key, const char *name)
{
	unsigned char x25519_public[P2P_KEY_SIZE];
	struct friend *friend;
	int size = (int)strlen(name), index, result;

	friends_key_load();
	if (!friends.on || !p2p_signal_connected() || !own_name_ok())
		return P2P_FRIEND_ADD_NO_SERVER;
	if (p2p_equal(key, zeros, P2P_KEY_SIZE) || !p2p_ed25519_to_x25519(key, x25519_public) ||
		p2p_equal(key, friends.public_key, P2P_KEY_SIZE) || !friend_name_ok((const unsigned char *)name, size))
	{
		return P2P_FRIEND_ADD_NO_PLAYER;
	}
	index = friend_request_find(key);
	if (index < friends.request_count)
	{
		result = friend_answer(&friends.requests[index], 1);
		if (result == P2P_FRIEND_ADD_SENT)
			friend_request_drop(index);
		return result;
	}
	friend = friend_find(key);
	if (friend)
		return friend->sent ? P2P_FRIEND_ADD_SENT : P2P_FRIEND_ADD_FRIEND;
	if (friends.count == P2P_FRIENDS_MAXIMUM)
		return P2P_FRIEND_ADD_FULL;
	friend = &friends.list[friends.count++];
	memset(friend, 0, sizeof(*friend));
	memcpy(friend->key, key, P2P_KEY_SIZE);
	friend_id(key, friend->id);
	friend->sent = 1;
	posix_random_bytes(friend->nonce, FRIEND_NONCE_SIZE);
	friend->last_day = friend_today();
	friend_name_tag(key, (const unsigned char *)name, size, friend->name_tag);
	friend->number = ++friends.numbers;
	friend->due = 1;
	friends_save(friends.list_path);
	return P2P_FRIEND_ADD_SENT;
}

int p2p_friend_add_member(int member)
{
	struct p2p_party_status party;
	int result = P2P_FRIEND_ADD_NO_PLAYER;

	p2p_party_get_status(&party);
	pthread_mutex_lock(&p2p_lock);
	if (member >= 0 && member < party.count && !party.members[member].you)
		result = friend_add(party.members[member].friend_key, party.members[member].name);
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

int p2p_friend_add_tag(const char *typed)
{
	char tag[P2P_FRIEND_TAG_SIZE + 1];
	int index, result = P2P_FRIEND_ADD_LOOKING;

	if (strlen(typed) != P2P_FRIEND_TAG_SIZE)
		return P2P_FRIEND_ADD_NO_PLAYER;
	for (index = 0; index < P2P_FRIEND_TAG_SIZE; index++)
	{
		char character = typed[index] >= 'a' && typed[index] <= 'z' ? (char)(typed[index] - 'a' + 'A') : typed[index];

		if (!character || !strchr(tag_alphabet, character))
			return P2P_FRIEND_ADD_NO_PLAYER;
		tag[index] = character;
	}
	tag[P2P_FRIEND_TAG_SIZE] = 0;
	pthread_mutex_lock(&p2p_lock);
	friends_key_load();
	if (!friends.on || !p2p_signal_wildcards() || !own_name_ok())
	{
		result = P2P_FRIEND_ADD_NO_SERVER;
	}
	else if (!strcmp(tag, friends.tag))
	{
		result = P2P_FRIEND_ADD_SELF;
	}
	else if (!friends.look_topic[0] || strcmp(tag, friends.look_tag))
	{
		strcpy(friends.look_tag, tag);
		friend_card_topic(tag, "+", friends.look_topic);
		friends.look_time = p2p_now();
		friends.look_count = 0;
		friends.topics_set = 0;
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

/* under p2p_lock */
static int friend_answer(const struct friend_request *request, int yes)
{
	char topic[FRIEND_TOPIC_SIZE];
	struct friend *friend = friend_find(request->key);

	if (yes && !own_name_ok())
		return P2P_FRIEND_ADD_NO_SERVER;
	if (yes && !friend && friends.count == P2P_FRIENDS_MAXIMUM)
		return P2P_FRIEND_ADD_FULL;
	if (friend_pair_topic(friends.public_key, request->key, topic))
		friend_out(topic, NULL, 0, -1);
	if (!yes)
	{
		if (friends.refused_count < FRIEND_REFUSED_MAXIMUM)
			memcpy(friends.refused[friends.refused_count++], request->key, P2P_KEY_SIZE);
		return P2P_FRIEND_ADD_SENT;
	}
	if (!friend)
	{
		friend = &friends.list[friends.count++];
		memset(friend, 0, sizeof(*friend));
		memcpy(friend->key, request->key, P2P_KEY_SIZE);
		strcpy(friend->id, request->id);
		friend->number = ++friends.numbers;
	}
	friend->sent = 0;
	memset(friend->nonce, 0, sizeof(friend->nonce));
	strcpy(friend->inbox, request->inbox);
	strcpy(friend->name_tag, request->name_tag);
	friend->last_sequence = request->sequence;
	friend->last_day = friend_today();
	friend_presence_due(friend, FRIEND_HELLO);
	friends_save(friends.list_path);
	friend_accept(request->key, request->nonce);
	return P2P_FRIEND_ADD_SENT;
}

int p2p_friend_answer(const char *id, int yes)
{
	int index, result = P2P_FRIEND_ADD_NO_PLAYER;

	pthread_mutex_lock(&p2p_lock);
	for (index = 0; index < friends.request_count; index++)
	{
		if (!strcmp(friends.requests[index].id, id))
		{
			result = friend_answer(&friends.requests[index], yes);
			if (result == P2P_FRIEND_ADD_SENT)
				friend_request_drop(index);
			break;
		}
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

/* under p2p_lock */
static int friend_by_id(const char *id)
{
	int index;

	for (index = 0; index < friends.count; index++)
	{
		if (!strcmp(friends.list[index].id, id))
			return index;
	}
	return -1;
}

int p2p_friend_invite(const char *id)
{
	unsigned char key[P2P_KEY_SIZE], bytes[FRIEND_RECORD_MAXIMUM];
	char link[P2P_PARTY_LINK_SIZE], topic[FRIEND_TOPIC_SIZE];
	int index, found = 0, size;

	pthread_mutex_lock(&p2p_lock);
	index = friends.on ? friend_by_id(id) : -1;
	if (index >= 0 && friends.list[index].heard && !friends.list[index].sent &&
		friends.list[index].state == P2P_FRIEND_MATCHMAKING && friends.out_count < FRIEND_OUTBOX_SIZE)
	{
		memcpy(key, friends.list[index].key, P2P_KEY_SIZE);
		friend_inbox_topic(friends.list[index].inbox, topic);
		found = 1;
	}
	pthread_mutex_unlock(&p2p_lock);
	if (!found || !p2p_party_invite_quiet(link, sizeof(link)))
		return 0;
	pthread_mutex_lock(&p2p_lock);
	size = friend_find(key) && friends.out_count < FRIEND_OUTBOX_SIZE ?
		friend_record_write(FRIEND_INVITE, key, zeros, link, (int)strlen(link), bytes) : 0;
	if (size)
		friend_out(topic, bytes, size, 0);
	pthread_mutex_unlock(&p2p_lock);
	return size > 0;
}

void p2p_friend_remove(const char *id)
{
	int index;

	pthread_mutex_lock(&p2p_lock);
	index = friend_by_id(id);
	if (index >= 0)
	{
		friend_invite_drop(friends.list[index].key);
		if (friends.removed_count == P2P_FRIENDS_MAXIMUM)
			memmove(friends.removed[0], friends.removed[1], sizeof(friends.removed) - sizeof(friends.removed[0]));
		else
			friends.removed_count++;
		memcpy(friends.removed[friends.removed_count - 1], friends.list[index].key, P2P_KEY_SIZE);
		memmove(&friends.list[index], &friends.list[index + 1],
			sizeof(friends.list[0]) * (size_t)(--friends.count - index));
		if (friends_save(friends.list_path))
			friends.removed_count = 0;
	}
	pthread_mutex_unlock(&p2p_lock);
}

int p2p_friend_invite_pending(char *name_tag, int size)
{
	const struct friend *friend;
	int found;

	pthread_mutex_lock(&p2p_lock);
	friend = friends.invite_pending ? friend_find(friends.invite_key) : NULL;
	found = friend != NULL;
	if (found)
		snprintf(name_tag, (size_t)size, "%s", friend->name_tag);
	pthread_mutex_unlock(&p2p_lock);
	return found;
}

int p2p_friend_invite_answer(int yes)
{
	char link[P2P_PARTY_LINK_SIZE];
	struct p2p_party_status party;
	int pending, can_join;

	pthread_mutex_lock(&p2p_lock);
	pending = friends.invite_pending;
	memcpy(link, friends.invite_link, sizeof(link));
	friends.invite_pending = 0;
	can_join = pending && yes && p2p_party_can_join(link);
	pthread_mutex_unlock(&p2p_lock);
	if (!pending || !yes)
		return P2P_FRIEND_ANSWER_DONE;
	if (!can_join)
		return P2P_FRIEND_ANSWER_LEAVE_FIRST;
	p2p_party_get_status(&party);
	if (party.search_state != P2P_MM_OFF)
		p2p_mm_cancel();
	return p2p_join_invite(link) ? P2P_FRIEND_ANSWER_DONE : P2P_FRIEND_ANSWER_FAILED;
}
