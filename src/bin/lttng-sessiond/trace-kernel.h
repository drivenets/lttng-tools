/*
 * Copyright (C) 2011 - David Goulet <david.goulet@polymtl.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _LTT_TRACE_KERNEL_H
#define _LTT_TRACE_KERNEL_H

#include <urcu/list.h>

#include <lttng/lttng.h>
#include <common/lttng-kernel.h>
#include <common/lttng-kernel-old.h>
#include <common/defaults.h>

#include "consumer.h"
#include "tracker.h"

/* Kernel event list */
struct ltt_kernel_event_list {
	struct cds_list_head head;
};

/* Channel stream list */
struct ltt_kernel_stream_list {
	struct cds_list_head head;
};

/* Channel list */
struct ltt_kernel_channel_list {
	struct cds_list_head head;
};

struct ltt_kernel_context {
	struct lttng_kernel_context ctx;
	struct cds_list_head list;
	/* Indicates whether or not the context is in a list. */
	bool in_list;
};

/* Kernel event */
struct ltt_kernel_event {
	int fd;
	int enabled;
	enum lttng_event_type type;
	struct lttng_kernel_event *event;
	struct cds_list_head list;
	char *filter_expression;
	struct lttng_filter_bytecode *filter;
	struct lttng_userspace_probe_location *userspace_probe_location;
};

/* Kernel channel */
struct ltt_kernel_channel {
	int fd;
	uint64_t key; /* Key to reference this channel with the consumer. */
	int enabled;
	unsigned int stream_count;
	unsigned int event_count;
	bool published_to_notification_thread;
	struct cds_list_head ctx_list;
	struct lttng_channel *channel;
	struct ltt_kernel_event_list events_list;
	struct ltt_kernel_stream_list stream_list;
	struct cds_list_head list;
	/* Session pointer which has a reference to this object. */
	struct ltt_kernel_session *session;
	bool sent_to_consumer;
};

/* Metadata */
struct ltt_kernel_metadata {
	int fd;
	uint64_t key; /* Key to reference this channel with the consumer. */
	struct lttng_channel *conf;
};

/* Channel stream */
struct ltt_kernel_stream {
	int fd;
	int state;
	int cpu;
	bool sent_to_consumer;
	/* Format is %s_%d respectively channel name and CPU number. */
	char name[DEFAULT_STREAM_NAME_LEN];
	uint64_t tracefile_size;
	uint64_t tracefile_count;
	struct cds_list_head list;
};

/* Kernel session */
struct ltt_kernel_session {
	int fd;
	int metadata_stream_fd;
	int consumer_fds_sent;
	unsigned int channel_count;
	unsigned int stream_count_global;
	struct ltt_kernel_metadata *metadata;
	struct ltt_kernel_channel_list channel_list;
	/* UID/GID of the user owning the session */
	uid_t uid;
	gid_t gid;
	struct consumer_output *consumer;
	/* Tracing session id */
	uint64_t id;
	/* Session is active or not meaning it has been started or stopped. */
	unsigned int active:1;
	/* Tell or not if the session has to output the traces. */
	unsigned int output_traces;
	unsigned int snapshot_mode;
	unsigned int has_non_default_channel;
	/* Current trace chunk of the ltt_session. */
	struct lttng_trace_chunk *current_trace_chunk;
	/* Tracker lists */
	struct lttng_tracker_list *tracker_list_pid;
	struct lttng_tracker_list *tracker_list_vpid;
	struct lttng_tracker_list *tracker_list_uid;
	struct lttng_tracker_list *tracker_list_vuid;
	struct lttng_tracker_list *tracker_list_gid;
	struct lttng_tracker_list *tracker_list_vgid;
};

/*
 * Lookup functions. NULL is returned if not found.
 */
struct ltt_kernel_event *trace_kernel_get_event_by_name(
		char *name, struct ltt_kernel_channel *channel,
		enum lttng_event_type type);
struct ltt_kernel_event *trace_kernel_find_event(
		char *name, struct ltt_kernel_channel *channel,
		enum lttng_event_type type,
		struct lttng_filter_bytecode *filter);
struct ltt_kernel_channel *trace_kernel_get_channel_by_name(
		const char *name, struct ltt_kernel_session *session);

/*
 * Create functions malloc() the data structure.
 */
struct ltt_kernel_session *trace_kernel_create_session(void);
struct ltt_kernel_channel *trace_kernel_create_channel(
		struct lttng_channel *chan);
enum lttng_error_code trace_kernel_create_event(struct lttng_event *ev,
		char *filter_expression, struct lttng_filter_bytecode *filter,
		struct ltt_kernel_event **kernel_event);
struct ltt_kernel_metadata *trace_kernel_create_metadata(void);
struct ltt_kernel_stream *trace_kernel_create_stream(const char *name,
		unsigned int count);
struct ltt_kernel_context *trace_kernel_create_context(
		struct lttng_kernel_context *ctx);
struct ltt_kernel_context *trace_kernel_copy_context(
		struct ltt_kernel_context *ctx);

/*
 * Destroy functions free() the data structure and remove from linked list if
 * it's applies.
 */
void trace_kernel_destroy_session(struct ltt_kernel_session *session);
void trace_kernel_destroy_metadata(struct ltt_kernel_metadata *metadata);
void trace_kernel_destroy_channel(struct ltt_kernel_channel *channel);
void trace_kernel_destroy_event(struct ltt_kernel_event *event);
void trace_kernel_destroy_stream(struct ltt_kernel_stream *stream);
void trace_kernel_destroy_context(struct ltt_kernel_context *ctx);
void trace_kernel_free_session(struct ltt_kernel_session *session);

#endif /* _LTT_TRACE_KERNEL_H */
