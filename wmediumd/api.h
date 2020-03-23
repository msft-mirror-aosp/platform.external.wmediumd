/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _WMEDIUMD_API_H
#define _WMEDIUMD_API_H

enum wmediumd_message {
	/* invalid message */
	WMEDIUMD_MSG_INVALID,

	/* ACK, returned for each message for synchronisation */
	WMEDIUMD_MSG_ACK,

	/*
	 * Register/unregister for frames, this may be a pure control
	 * socket which doesn't want to see frames.
	 */
	WMEDIUMD_MSG_REGISTER,
	WMEDIUMD_MSG_UNREGISTER,

	/*
	 * netlink message, the data is the entire netlink message,
	 * this is used to communicate frame TX/RX in the familiar
	 * netlink format, to avoid having a special format
	 */
	WMEDIUMD_MSG_NETLINK,
};

struct wmediumd_message_header {
	/* type of message - see enum wmediumd_message */
	uint32_t type;
	/* data length */
	uint32_t data_len;

	/* variable-length data according to the message type */
	uint8_t data[];
};

#endif /* _WMEDIUMD_API_H */
