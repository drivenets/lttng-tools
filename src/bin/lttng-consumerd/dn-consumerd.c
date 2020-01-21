#define SUBBUFFER_HEADER_SIZE 84
#define MAX_HOSTNAME 64
#define TRACE_PATH "/var/log/dn/traces/"

#include <stdio.h>
#include <fcntl.h>

#include "bin/lttng-consumerd/dn-consumerd.h"
#include "common/error.h"
#include "common/readwrite.h"
#include "common/hashtable/hashtable.h"
#include "lttng/constant.h"


static struct lttng_ht *name_to_fd_ht;
static char *traces_path;

struct file_to_fd_node {
	struct lttng_ht_node_str node;
	char filename[LTTNG_PATH_MAX];
	int fd;
};


int init_dn_write()
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

	// TODO make this dir (recursive mkdir)
	return 0;
}


static int create_full_path(const char *filename, char *fullpath, size_t size)
{
	int ret = snprintf(fullpath, size, "%s%s",
			traces_path, filename);
	if (ret == -1 || ret >= size)
	{
		ERR("Failed to format subdirectory from process name");
		return -1;
	}

	return 0;
}


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


static struct lttng_ht_node_str *create_new_node(char *filename)
{
	struct file_to_fd_node *fd_struct;
	struct lttng_ht_node_str *node;
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
	node = &fd_struct->node;
	lttng_ht_node_init_str(node, fd_struct->filename);
	lttng_ht_add_str(name_to_fd_ht, node);
	return node;
}


static int get_fd_from_filename(char *filename)
{
	struct file_to_fd_node *fd_struct;
	struct lttng_ht_node_str *node;
	struct lttng_ht_iter iter;

	lttng_ht_lookup(name_to_fd_ht, filename, &iter),
	node = lttng_ht_iter_get_node_str(&iter);
	if (node == NULL) {
		node = create_new_node(filename);
		if (node == NULL) {
			ERR("Could not create new node for %s", filename);
			return -1;
		}
	}

	fd_struct = caa_container_of(node, struct file_to_fd_node, node);
	return fd_struct->fd;
}

ssize_t dn_write_subbuffer(const void *buf, size_t count)
{
	int ret = 0;
	int fd;
	uint64_t write_size = *(uint64_t *)(buf + 48);
	size_t i = SUBBUFFER_HEADER_SIZE;
	char filename[LTTNG_PATH_MAX];
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
		fd = get_fd_from_filename(filename);
		if (fd < 0)
		{
			ret = -1;
			goto finish;
		}
		msgsize = (int*)(buf + i);
		i += 4;
		ret = lttng_write(fd, buf + i, *msgsize);
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
