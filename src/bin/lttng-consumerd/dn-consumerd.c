#define SUBBUFFER_HEADER_SIZE 84
#define MAX_HOSTNAME 64
#define TRACE_PATH "/var/log/dn/traces/"

#include <stdio.h>
#include <fcntl.h>

#include <bin/lttng-consumerd/health-consumerd.h>
#include <bin/lttng-consumerd/dn-consumerd.h>
#include "common/error.h"
#include "common/pipe.h"
#include "common/readwrite.h"
#include "common/compat/poll.h"
#include "common/hashtable/hashtable.h"
#include "lttng/constant.h"
#include "bin/lttng-sessiond/health-sessiond.h"
#include "testpoint.h"

#define MAX_FILESIZE  (1L << 23)
#define MAX_FILES  20

static struct lttng_ht *name_to_fd_ht;
static char *traces_path;


static int create_full_path(const char *filename, char *fullpath, size_t size)
{
	int ret = snprintf(fullpath, size, "%st_%s",
			traces_path, filename);
	if (ret == -1 || ret >= size)
	{
		ERR("Failed to format subdirectory from process name");
		return -1;
	}

	return 0;
}


void createfilename(const char *filename, int index, char *buf, int size)
{
	char fullpath[LTTNG_PATH_MAX];
	create_full_path(filename, fullpath, LTTNG_PATH_MAX);
	if (index > 0) {
		snprintf(buf, size, "%s.%d", fullpath, index);
	}
	else {
		strncpy(buf, fullpath, LTTNG_PATH_MAX);
	}
}

struct file_to_fd_node {
	struct lttng_ht_node_str node;
	char filename[LTTNG_PATH_MAX];
	int fd;
	uint64_t filesize;
	uint64_t max_filesize;
	uint16_t max_files;
	uint8_t is_rotating;
};


static int open_filename(const char *filename)
{
	int ret = 0;
	char fullpath[LTTNG_PATH_MAX];
	const int flags = O_WRONLY | O_CREAT | O_TRUNC;
	const mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;

	ret = create_full_path(filename, fullpath, sizeof(fullpath));
	if (ret != 0) {
		goto end;
	}

	ret = open(fullpath, flags, mode);
	if (ret < 0) {
		ERR("Failed to open file %s", fullpath);
	}

end:
	return ret;
}


static struct lttng_ht_node_str *create_new_node(const char *filename)
{
	struct file_to_fd_node *fd_struct;
	int fd = open_filename(filename);

	if (fd < 0)
	{
		return NULL;
	}

	fd_struct = zmalloc(sizeof(*fd_struct));
	if (fd_struct == NULL)
	{
		ERR("Could not malloc fd_struct");
		return NULL;
	}

	strncpy(fd_struct->filename, filename, LTTNG_PATH_MAX);
	fd_struct->fd = fd;
	fd_struct->filesize = 0;
	fd_struct->max_filesize = MAX_FILESIZE;
	fd_struct->max_files = MAX_FILES;
	fd_struct->is_rotating = 0;

	if (strncmp(filename, "wb_agent", 8) == 0)
	{
		fd_struct->max_filesize = (1L << 27);
		fd_struct->max_files = 2;
	}
	DBG("new node created %s %ldB * %d",
			filename,
			fd_struct->max_filesize,
			fd_struct->max_files);
	lttng_ht_node_init_str(&fd_struct->node, fd_struct->filename);
	return &fd_struct->node;
}


void rotate(const char *filename)
{
	int index;
	char oldname[LTTNG_PATH_MAX];
	char newname[LTTNG_PATH_MAX];
	struct lttng_ht_iter iter;
	struct lttng_ht_node_str *node_ptr;
	struct file_to_fd_node *node;

	lttng_ht_lookup(name_to_fd_ht, filename, &iter);
	node_ptr = lttng_ht_iter_get_node_str(&iter);
	if (node_ptr == NULL) {
		ERR("Rotation got to rotate non existing node %s", filename);
		return;
	}

	node = caa_container_of(node_ptr, struct file_to_fd_node, node);
	for (index=node->max_files - 2; index > -1; index--) {
		createfilename(filename, index, oldname, LTTNG_PATH_MAX);
		createfilename(filename, index+1, newname, LTTNG_PATH_MAX);
		if( access( oldname, F_OK ) != -1 ) {
			DBG("Rotating %s-->%s", oldname, newname);
			rename(oldname, newname);
		}
	}

	node_ptr = create_new_node(filename);
	if (NULL == node_ptr) {
		ERR("Failed creating node for %s while rotating", filename);
		goto derotate;

	}
	if (NULL == lttng_ht_add_replace_str(name_to_fd_ht, node_ptr)) {
		ERR("HT replace returned NULL value in %s", filename);
	}

	synchronize_rcu();
	free(node);
	return;

derotate:
	for (index=1; index<node->max_files; index++) {
		createfilename(filename, index, oldname, LTTNG_PATH_MAX);
		createfilename(filename, index - 1, newname, LTTNG_PATH_MAX);
		if( access( oldname, F_OK ) != -1 ) {
			DBG("Derotating %s-->%s", oldname, newname);
			rename(oldname, newname);
		}
	}
}


// Thread for rotating the files
void *dn_thread_file_rotation(void *data)
{
	int ret, i, pollfd;
	uint32_t revents, nb_fd;
	struct lttng_poll_event events;
	struct lttng_consumer_local_data *ctx = data;
	struct lttng_pipe *pipe = ctx->dn_rotation_pipe;
	char filename[LTTNG_PATH_MAX];

	rcu_register_thread();

	health_register(health_consumerd, HEALTH_CONSUMERD_TYPE_DN_ROTATE);

	health_code_update();

	while (1) {
		health_code_update();
		ret = lttng_poll_create(&events, 2, LTTNG_CLOEXEC); 
		if (ret < 0) {
			ERR("Could not create poll for dn rotation");
			goto dn_rotation_err;
		}

		ret = lttng_poll_add(&events, pipe->fd[0], LPOLLIN);
		if (ret < 0) {
			ERR("Could not add dnrotation pipe to poll");
			goto dn_rotation_err;
		}

restart:
		health_poll_entry();
		ret = lttng_poll_wait(&events, -1);
		DBG("DN rotation poll return from wait with %d fd(s)",
				LTTNG_POLL_GETNB(&events));
		health_poll_exit();

		if (ret < 0) {
			if (errno == EINTR) {
				ERR("Poll EINTR caught");
				goto restart;
			}
			goto dn_rotation_err;
		}

		nb_fd = ret;
		for (i = 0; i < nb_fd; i++) {
			health_code_update();

			revents = LTTNG_POLL_GETEV(&events, i);
			pollfd = LTTNG_POLL_GETFD(&events, i);

			if (pollfd == pipe->fd[0]) {
				if (revents & LPOLLIN) {
					ret = lttng_read(pipe->fd[0], filename, 
							sizeof(filename));
					if (ret < sizeof(filename)) {
						ERR("Rotation pipe read err");
						goto dn_rotation_err;
					}

					DBG("Rotating %s", filename);
					rotate(filename);
				}
			}
		}
	}

	// this code should never run
dn_rotation_err:
	health_error();
	health_unregister(health_consumerd);
	rcu_unregister_thread();
	return NULL;
}


int init_dn_write(struct lttng_consumer_local_data *ctx)
{
	int ret;
	char hostname[MAX_HOSTNAME];
	name_to_fd_ht = lttng_ht_new(16, LTTNG_HT_TYPE_STRING);
	if (!name_to_fd_ht) {
		ERR("Failed to create hash table for filename");
		return -1;
	}

	traces_path = malloc(LTTNG_PATH_MAX);
	if (!traces_path) {
		ERR("Failed to malloc array for traces path");
		return -1;
	}

	gethostname(hostname, MAX_HOSTNAME);
	ret = snprintf(traces_path, LTTNG_PATH_MAX, "%s%s/",
			TRACE_PATH, hostname);
	if (ret == -1 || ret >= LTTNG_PATH_MAX) {
		ERR("Failed to create path to logger");
		return -1;
	}
	
	ctx->dn_rotation_pipe = lttng_pipe_open(FD_CLOEXEC);
	if (!ctx->dn_rotation_pipe) {
		return -1;
	}
	// TODO make this dir (recursive mkdir)
	return 0;
}


static struct file_to_fd_node *get_fd_from_filename(char *filename)
{
	struct lttng_ht_node_str *node;
	struct lttng_ht_iter iter;

	lttng_ht_lookup(name_to_fd_ht, filename, &iter),
	node = lttng_ht_iter_get_node_str(&iter);
	if (node == NULL) {
		node = create_new_node(filename);
		if (node == NULL) {
			ERR("Could not create new node for %s", filename);
			return NULL;
		}

		lttng_ht_add_str(name_to_fd_ht, node);
	}

	return caa_container_of(node, struct file_to_fd_node, node);
}

ssize_t dn_write_subbuffer(
		const void *buf, size_t count,
		struct lttng_consumer_local_data *ctx)
{
	int ret = 0;
	int fd;
	uint64_t write_size = *(uint64_t *)(buf + 48);
	size_t i = SUBBUFFER_HEADER_SIZE;
	char filename[LTTNG_PATH_MAX];
	struct file_to_fd_node *node;
	const int write_fd = lttng_pipe_get_writefd(ctx->dn_rotation_pipe);

	// return if this a metadata stream magic == 0x75d11d57
	if (*(uint32_t *)buf == 0x75d11d57) {
		return count;
	}

	if (count <= SUBBUFFER_HEADER_SIZE) {
		return count;
	}

	write_size >>= 3; // changing from bits to bytes
	while (i < write_size)
	{
		int *msgsize;
		if (*(uint16_t *)(buf + i) == UINT16_MAX) {
			i += 8; //extended clock + another id
		}
		i += 6; // 2 - for event_id 4 for timestamp
		ret = snprintf(filename, sizeof(filename), "%s", (char *)buf + i);
		if (ret < 0 || ret >= sizeof(filename))
		{
			ERR("Could not parse filename from buffer");
			ret = -1;
			goto finish;
		}

		i += ret + 1; //eof does not count in ret
		node = get_fd_from_filename(filename);
		if (NULL == node) {
			ret = -1;
			goto finish;
		}
		fd = node->fd;
		if (fd < 0) {
			ret = -1;
			goto finish;
		}
		msgsize = (int*)(buf + i);
		i += 4;
		ret = lttng_write(fd, buf + i, *msgsize);
		if (ret > 0) {
			node->filesize += ret;
		}
		if (node->filesize > node->max_filesize && !node->is_rotating) {
			node->is_rotating = 1;
			lttng_write(write_fd, filename, sizeof(filename));
		}

		if (ret != *msgsize)
		{
			ret = -1;
			goto finish;
		}
		i += ret;

	}
	ret = count;

finish:
	return ret;
}
