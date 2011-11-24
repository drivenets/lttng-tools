/*
 * Copyright (C) 2011 - David Goulet <david.goulet@polymtl.ca>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; only version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#define _GNU_SOURCE
#include <popt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../cmd.h"
#include "../conf.h"
#include "../utils.h"

static char *opt_event_list;
static int opt_kernel;
static char *opt_channel_name;
static char *opt_session_name;
static int opt_pid_all;
static int opt_userspace;
static char *opt_cmd_name;
static int opt_disable_all;
static pid_t opt_pid;

enum {
	OPT_HELP = 1,
	OPT_USERSPACE,
};

static struct lttng_handle *handle;

static struct poptOption long_options[] = {
	/* longName, shortName, argInfo, argPtr, value, descrip, argDesc */
	{"help",           'h', POPT_ARG_NONE, 0, OPT_HELP, 0, 0},
	{"session",        's', POPT_ARG_STRING, &opt_session_name, 0, 0, 0},
	{"all-events",     'a', POPT_ARG_VAL, &opt_disable_all, 1, 0, 0},
	{"channel",        'c', POPT_ARG_STRING, &opt_channel_name, 0, 0, 0},
	{"kernel",         'k', POPT_ARG_VAL, &opt_kernel, 1, 0, 0},
	{"userspace",      'u', POPT_ARG_STRING | POPT_ARGFLAG_OPTIONAL, &opt_cmd_name, OPT_USERSPACE, 0, 0},
	{"all",            0,   POPT_ARG_VAL, &opt_pid_all, 1, 0, 0},
	{"pid",            'p', POPT_ARG_INT, &opt_pid, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0}
};

/*
 * usage
 */
static void usage(FILE *ofp)
{
	fprintf(ofp, "usage: lttng disable-event NAME[,NAME2,...] [options]\n");
	fprintf(ofp, "\n");
	fprintf(ofp, "  -h, --help               Show this help\n");
	fprintf(ofp, "  -s, --session            Apply on session name\n");
	fprintf(ofp, "  -c, --channel            Apply on this channel\n");
	fprintf(ofp, "  -a, --all-events         Disable all tracepoints\n");
	fprintf(ofp, "  -k, --kernel             Apply for the kernel tracer\n");
	fprintf(ofp, "  -u, --userspace [CMD]    Apply for the user-space tracer\n");
	fprintf(ofp, "      --all                If -u, apply on all traceable apps\n");
	fprintf(ofp, "  -p, --pid PID            If -u, apply on a specific PID\n");
	fprintf(ofp, "\n");
}

/*
 *  disable_events
 *
 *  Disabling event using the lttng API.
 */
static int disable_events(char *session_name)
{
	int err, ret = CMD_SUCCESS;
	char *event_name, *channel_name = NULL;
	struct lttng_domain dom;

	if (opt_channel_name == NULL) {
		err = asprintf(&channel_name, DEFAULT_CHANNEL_NAME);
		if (err < 0) {
			ret = CMD_FATAL;
			goto error;
		}
	} else {
		channel_name = opt_channel_name;
	}

	if (opt_kernel) {
		dom.type = LTTNG_DOMAIN_KERNEL;
	} else if (opt_pid != 0) {
		dom.type = LTTNG_DOMAIN_UST_PID;
		dom.attr.pid = opt_pid;
		DBG("PID %d set to lttng handle", opt_pid);
	} else if (opt_userspace && opt_cmd_name == NULL) {
		dom.type = LTTNG_DOMAIN_UST;
		DBG("UST global domain selected");
	} else if (opt_userspace && opt_cmd_name != NULL) {
		dom.type = LTTNG_DOMAIN_UST_EXEC_NAME;
		strncpy(dom.attr.exec_name, opt_cmd_name, NAME_MAX);
	} else {
		ERR("Please specify a tracer (--kernel or --userspace)");
		ret = CMD_NOT_IMPLEMENTED;
		goto error;
	}

	handle = lttng_create_handle(session_name, &dom);
	if (handle == NULL) {
		ret = -1;
		goto error;
	}

	if (opt_disable_all) {
		ret = lttng_disable_event(handle, NULL, channel_name);
		if (ret < 0) {
			goto error;
		}

		MSG("All %s events are disabled in channel %s",
				opt_kernel ? "kernel" : "UST", channel_name);
		goto end;
	}

	/* Strip event list */
	event_name = strtok(opt_event_list, ",");
	while (event_name != NULL) {
		/* Kernel tracer action */
		if (opt_kernel) {
			DBG("Disabling kernel event %s in channel %s",
					event_name, channel_name);
		} else if (opt_userspace) {		/* User-space tracer action */
			if (!opt_pid_all) {
				MSG("Only supporting tracing all UST processes (-u --all) for now.");
				ret = CMD_NOT_IMPLEMENTED;
				goto error;
			}
			DBG("Disabling UST event %s in channel %s",
					event_name, channel_name);
		} else {
			ERR("Please specify a tracer (--kernel or --userspace)");
			goto error;
		}

		ret = lttng_disable_event(handle, event_name, channel_name);
		if (ret < 0) {
			MSG("Unable to disable %s event %s in channel %s",
					opt_kernel ? "kernel" : "UST", event_name,
					channel_name);
		} else {
			MSG("%s event %s disabled in channel %s",
					opt_kernel ? "kernel" : "UST", event_name,
					channel_name);
		}

		/* Next event */
		event_name = strtok(NULL, ",");
	}

end:
error:
	if (opt_channel_name == NULL) {
		free(channel_name);
	}
	lttng_destroy_handle(handle);

	return ret;
}

/*
 *  cmd_disable_events
 *
 *  Disable event to trace session
 */
int cmd_disable_events(int argc, const char **argv)
{
	int opt, ret;
	static poptContext pc;
	char *session_name = NULL;

	pc = poptGetContext(NULL, argc, argv, long_options, 0);
	poptReadDefaultConfig(pc, 0);

	while ((opt = poptGetNextOpt(pc)) != -1) {
		switch (opt) {
		case OPT_HELP:
			usage(stderr);
			ret = CMD_SUCCESS;
			goto end;
		case OPT_USERSPACE:
			opt_userspace = 1;
			break;
		default:
			usage(stderr);
			ret = CMD_UNDEFINED;
			goto end;
		}
	}

	opt_event_list = (char*) poptGetArg(pc);
	if (opt_event_list == NULL && opt_disable_all == 0) {
		ERR("Missing event name(s).\n");
		usage(stderr);
		ret = CMD_SUCCESS;
		goto end;
	}

	if (!opt_session_name) {
		session_name = get_session_name();
		if (session_name == NULL) {
			ret = -1;
			goto end;
		}
	} else {
		session_name = opt_session_name;
	}

	ret = disable_events(session_name);

end:
	return ret;
}
