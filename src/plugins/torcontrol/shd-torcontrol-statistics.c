/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>

#include "shd-torcontrol.h"
#include "shd-torcontrol-statistics.h"

struct _TorControlStatistics {
	ShadowLogFunc log;
	enum torcontrolstatistic_state currentState;
	enum torcontrolstatistic_state nextState;

	GString* targetHostname;
	in_addr_t targetIP;
	in_port_t targetPort;
	gint targetSockd;
};


/*
 * setting up and registering with the ControlPort
 */

static gboolean _torcontrolstatistics_manageState(TorControlStatistics* tstats) {

	beginmanage: switch (tstats->currentState) {

	case TCS_SEND_AUTHENTICATE: {
		/* authenticate with the control port */
		if (torControl_authenticate(tstats->targetSockd, "password") > 0) {
			/* idle until we receive the response, then move to next state */
			tstats->currentState = TCS_IDLE;
			tstats->nextState = TCS_RECV_AUTHENTICATE;
		}
		break;
	}

	case TCS_RECV_AUTHENTICATE: {
		tstats->currentState = TCS_SEND_SETEVENTS;
		goto beginmanage;
		break;
	}

	case TCS_SEND_SETEVENTS: {
		/* send list of events to listen on */
		if (torControl_setevents(tstats->targetSockd, "CIRC STREAM ORCONN BW STREAM_BW")
				> 0) {
			/* idle until we receive the response, then move to next state */
			tstats->currentState = TCS_IDLE;
			tstats->nextState = TCS_RECV_SETEVENTS;
		}
		break;
	}

	case TCS_RECV_SETEVENTS: {
		/* all done */
		tstats->currentState = TCS_IDLE;
		tstats->nextState = TCS_IDLE;
		goto beginmanage;
		break;
	}

	case TCS_IDLE: {
		if (tstats->nextState == TCS_IDLE) {
			return TRUE;
		}
		break;
	}

	default:
		break;
	}

	return FALSE;
}

static void _torcontrolstatistics_handleResponseEvent(
		TorControlStatistics* tstats, GList *reply, gpointer userData) {
	TorControl_ReplyLine *replyLine = g_list_first(reply)->data;

	switch (TORCTL_CODE_TYPE(replyLine->code)) {
	case TORCTL_REPLY_ERROR: {
		tstats->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "[%d] ERROR: %s",
				replyLine->code, replyLine->body);
		break;
	}

	case TORCTL_REPLY_SUCCESS: {
		tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "[%d] SUCCESS: %s",
				replyLine->code, replyLine->body);
		tstats->currentState = tstats->nextState;
		_torcontrolstatistics_manageState(tstats);
		break;
	}

	default:
		break;
	}
}

/*
 * handling the asynchronous events from control port
 */

static void _torcontrolstatistics_handleORConnEvent(
		TorControlStatistics* tstats, gint code, gchar* line, gint connID, gchar *target, gint status,
		gint reason, gint numCircuits) {
	tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
			"%s:%i %s", tstats->targetHostname->str, tstats->targetPort, line);
}

static void _torcontrolstatistics_handleCircEvent(TorControlStatistics* tstats,
		gint code, gchar* line, gint circID, GString* path, gint status, gint buildFlags,
		gint purpose, gint reason, GDateTime* createTime) {
	tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
			"%s:%i %s", tstats->targetHostname->str, tstats->targetPort, line);
}

static void _torcontrolstatistics_handleStreamEvent(
		TorControlStatistics* tstats, gint code, gchar* line, gint streamID, gint circID,
		in_addr_t targetIP, in_port_t targetPort, gint status, gint reason,
		gint remoteReason, gchar *source, in_addr_t sourceIP,
		in_port_t sourcePort, gint purpose) {
	tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
			"%s:%i %s", tstats->targetHostname->str, tstats->targetPort, line);
}

static void _torcontrolstatistics_handleBWEvent(TorControlStatistics* tstats,
		gint code, gchar* line, gint bytesRead, gint bytesWritten) {
	tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
			"%s:%i %s", tstats->targetHostname->str, tstats->targetPort, line);
}

static void _torcontrolstatistics_handleExtendedBWEvent(TorControlStatistics* tstats,
		gint code, gchar* line, gchar* type, gint id, gint bytesRead, gint bytesWritten) {
	tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
			"%s:%i %s", tstats->targetHostname->str, tstats->targetPort, line);
}

static void _torcontrolstatistics_handleCellStatsEvent(TorControlStatistics* tstats,
		gint code, gchar* line, gint circID, gint nextHopCircID, gint prevHopCircID,
		gint appProcessed, gint appTotalWaitMillis, double appMeanQueueLength,
		gint exitProcessed, gint exitTotalWaitMillis, double exitMeanQueueLength) {
	tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
			"%s:%i %s", tstats->targetHostname->str, tstats->targetPort, line);
}

/*
 * module setup and teardown
 */

static void _torcontrolstatistics_free(TorControlStatistics* tstats) {
	g_assert(tstats);

	g_string_free(tstats->targetHostname, TRUE);

	g_free(tstats);
}

TorControlStatistics* torcontrolstatistics_new(ShadowLogFunc logFunc,
		gchar* hostname, in_addr_t ip, in_port_t port, gint sockd, gchar **args,
		TorControl_EventHandlers *handlers) {
	g_assert(handlers);

	handlers->initialize = (TorControlInitialize) _torcontrolstatistics_manageState;
	handlers->free = (TorControlFree) _torcontrolstatistics_free;
	handlers->circEvent = (TorControlCircEventFunc) _torcontrolstatistics_handleCircEvent;
	handlers->streamEvent = (TorControlStreamEventFunc) _torcontrolstatistics_handleStreamEvent;
	handlers->orconnEvent = (TorControlORConnEventFunc) _torcontrolstatistics_handleORConnEvent;
	handlers->bwEvent = (TorControlBWEventFunc) _torcontrolstatistics_handleBWEvent;
	handlers->extendedBWEvent = (TorControlExtendedBWEventFunc) _torcontrolstatistics_handleExtendedBWEvent;
	handlers->cellStatsEvent = (TorControlCellStatsEventFunc) _torcontrolstatistics_handleCellStatsEvent;
	handlers->responseEvent = (TorControlResponseFunc) _torcontrolstatistics_handleResponseEvent;

	TorControlStatistics* tstats = g_new0(TorControlStatistics, 1);

	tstats->log = logFunc;

	tstats->targetHostname = g_string_new(hostname);
	tstats->targetIP = ip;
	tstats->targetPort = port;
	tstats->targetSockd = sockd;

	tstats->currentState = TCS_SEND_AUTHENTICATE;

	return tstats;
}
