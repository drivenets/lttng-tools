
#include <unistd.h>
#include "common/consumer/consumer.h"


ssize_t dn_write_subbuffer(
		const void *buf, size_t count,
		struct lttng_consumer_local_data *ctx);
void *dn_thread_file_rotation(void *data);
int init_dn_write();
