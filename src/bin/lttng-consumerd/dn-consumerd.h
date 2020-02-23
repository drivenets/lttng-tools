
#include <unistd.h>
#include "common/consumer/consumer.h"


ssize_t dn_write_subbuffer(
		int outfd,
		const void *buf, size_t count,
		struct lttng_consumer_local_data *ctx);
void *dn_thread_file_rotation(void *data);
void notify_thread_dn_rotation_pipe(struct lttng_pipe *pipe);
int init_dn_write();
