/*
 * Copyright (C) 2011 - David Goulet <david.goulet@polymtl.ca>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; only version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <string.h>
#include <unistd.h>

#include <lttng/lttng.h>
#include <lttng-sessiond-comm.h>
#include <lttngerr.h>

#include "channel.h"
#include "hashtable.h"
#include "kernel.h"
#include "ust-ctl.h"
#include "utils.h"

/*
 * Return allocated channel attributes.
 */
struct lttng_channel *channel_new_default_attr(int dom)
{
	struct lttng_channel *chan;

	chan = zmalloc(sizeof(struct lttng_channel));
	if (chan == NULL) {
		perror("zmalloc channel init");
		goto error_alloc;
	}

	if (snprintf(chan->name, sizeof(chan->name), "%s",
				DEFAULT_CHANNEL_NAME) < 0) {
		perror("snprintf default channel name");
		goto error;
	}

	chan->attr.overwrite = DEFAULT_CHANNEL_OVERWRITE;
	chan->attr.switch_timer_interval = DEFAULT_CHANNEL_SWITCH_TIMER;
	chan->attr.read_timer_interval = DEFAULT_CHANNEL_READ_TIMER;

	switch (dom) {
	case LTTNG_DOMAIN_KERNEL:
		chan->attr.subbuf_size = DEFAULT_KERNEL_CHANNEL_SUBBUF_SIZE;
		chan->attr.num_subbuf = DEFAULT_KERNEL_CHANNEL_SUBBUF_NUM;
		chan->attr.output = DEFAULT_KERNEL_CHANNEL_OUTPUT;
		break;
	case LTTNG_DOMAIN_UST:
	case LTTNG_DOMAIN_UST_PID:
		chan->attr.subbuf_size = DEFAULT_UST_CHANNEL_SUBBUF_SIZE;
		chan->attr.num_subbuf = DEFAULT_UST_CHANNEL_SUBBUF_NUM;
		chan->attr.output = DEFAULT_UST_CHANNEL_OUTPUT;
		break;
	default:
		goto error;	/* Not implemented */
	}

	return chan;

error:
	free(chan);
error_alloc:
	return NULL;
}

/*
 * Disable kernel channel of the kernel session.
 */
int channel_kernel_disable(struct ltt_kernel_session *ksession,
		char *channel_name)
{
	int ret;
	struct ltt_kernel_channel *kchan;

	kchan = trace_kernel_get_channel_by_name(channel_name, ksession);
	if (kchan == NULL) {
		ret = LTTCOMM_KERN_CHAN_NOT_FOUND;
		goto error;
	} else if (kchan->enabled == 1) {
		ret = kernel_disable_channel(kchan);
		if (ret < 0) {
			if (ret != EEXIST) {
				ret = LTTCOMM_KERN_CHAN_DISABLE_FAIL;
			}
			goto error;
		}
	}

	ret = LTTCOMM_OK;

error:
	return ret;
}

/*
 * Enable kernel channel of the kernel session.
 */
int channel_kernel_enable(struct ltt_kernel_session *ksession,
		struct ltt_kernel_channel *kchan)
{
	int ret;

	if (kchan->enabled == 0) {
		ret = kernel_enable_channel(kchan);
		if (ret < 0) {
			ret = LTTCOMM_KERN_CHAN_ENABLE_FAIL;
			goto error;
		}
	}

	ret = LTTCOMM_OK;

error:
	return ret;
}

/*
 * Create kernel channel of the kernel session and notify kernel thread.
 */
int channel_kernel_create(struct ltt_kernel_session *ksession,
		struct lttng_channel *attr, int kernel_pipe)
{
	int ret;
	struct lttng_channel *defattr = NULL;

	/* Creating channel attributes if needed */
	if (attr == NULL) {
		defattr = channel_new_default_attr(LTTNG_DOMAIN_KERNEL);
		if (defattr == NULL) {
			ret = LTTCOMM_FATAL;
			goto error;
		}
		attr = defattr;
	}

	/* Channel not found, creating it */
	ret = kernel_create_channel(ksession, attr, ksession->trace_path);
	if (ret < 0) {
		ret = LTTCOMM_KERN_CHAN_FAIL;
		goto error;
	}

	/* Notify kernel thread that there is a new channel */
	ret = notify_thread_pipe(kernel_pipe);
	if (ret < 0) {
		ret = LTTCOMM_FATAL;
		goto error;
	}

	ret = LTTCOMM_OK;
error:
	free(defattr);
	return ret;
}
