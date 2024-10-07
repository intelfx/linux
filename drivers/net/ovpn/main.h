/* SPDX-License-Identifier: GPL-2.0-only */
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2019-2024 OpenVPN, Inc.
 *
 *  Author:	James Yonan <james@openvpn.net>
 *		Antonio Quartulli <antonio@openvpn.net>
 */

#ifndef _NET_OVPN_MAIN_H_
#define _NET_OVPN_MAIN_H_

#define OVPN_DEFAULT_IFNAME "ovpn%d"

struct net_device *ovpn_iface_create(const char *name, enum ovpn_mode mode,
				     struct net *net);
void ovpn_iface_destruct(struct ovpn_struct *ovpn);
bool ovpn_dev_is_valid(const struct net_device *dev);

#define SKB_HEADER_LEN                                       \
	(max(sizeof(struct iphdr), sizeof(struct ipv6hdr)) + \
	 sizeof(struct udphdr) + NET_SKB_PAD)

#define OVPN_HEAD_ROOM ALIGN(16 + SKB_HEADER_LEN, 4)
#define OVPN_MAX_PADDING 16

#endif /* _NET_OVPN_MAIN_H_ */
