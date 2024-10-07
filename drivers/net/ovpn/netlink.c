// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2024 OpenVPN, Inc.
 *
 *  Author:	Antonio Quartulli <antonio@openvpn.net>
 */

#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <net/genetlink.h>

#include <uapi/linux/ovpn.h>

#include "ovpnstruct.h"
#include "main.h"
#include "io.h"
#include "netlink.h"
#include "netlink-gen.h"

MODULE_ALIAS_GENL_FAMILY(OVPN_FAMILY_NAME);

/**
 * ovpn_get_dev_from_attrs - retrieve the netdevice a netlink message is
 *                           targeting
 * @net: network namespace where to look for the interface
 * @info: generic netlink info from the user request
 *
 * Return: the netdevice, if found, or an error otherwise
 */
static struct net_device *
ovpn_get_dev_from_attrs(struct net *net, const struct genl_info *info)
{
	struct net_device *dev;
	int ifindex;

	if (GENL_REQ_ATTR_CHECK(info, OVPN_A_IFINDEX))
		return ERR_PTR(-EINVAL);

	ifindex = nla_get_u32(info->attrs[OVPN_A_IFINDEX]);

	dev = dev_get_by_index(net, ifindex);
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "ifindex does not match any interface");
		return ERR_PTR(-ENODEV);
	}

	if (!ovpn_dev_is_valid(dev))
		goto err_put_dev;

	return dev;

err_put_dev:
	netdev_put(dev, NULL);

	NL_SET_ERR_MSG_MOD(info->extack, "specified interface is not ovpn");
	NL_SET_BAD_ATTR(info->extack, info->attrs[OVPN_A_IFINDEX]);

	return ERR_PTR(-EINVAL);
}

int ovpn_nl_pre_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
		     struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct net_device *dev = ovpn_get_dev_from_attrs(net, info);

	if (IS_ERR(dev))
		return PTR_ERR(dev);

	info->user_ptr[0] = netdev_priv(dev);

	return 0;
}

void ovpn_nl_post_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
		       struct genl_info *info)
{
	struct ovpn_struct *ovpn = info->user_ptr[0];

	if (ovpn)
		netdev_put(ovpn->dev, NULL);
}

int ovpn_nl_dev_new_doit(struct sk_buff *skb, struct genl_info *info)
{
	const char *ifname = OVPN_DEFAULT_IFNAME;
	enum ovpn_mode mode = OVPN_MODE_P2P;
	struct net_device *dev;
	struct sk_buff *msg;
	void *hdr;

	if (info->attrs[OVPN_A_IFNAME])
		ifname = nla_data(info->attrs[OVPN_A_IFNAME]);

	if (info->attrs[OVPN_A_MODE]) {
		mode = nla_get_u32(info->attrs[OVPN_A_MODE]);
		pr_debug("ovpn: setting device (%s) mode: %u\n", ifname, mode);
	}

	dev = ovpn_iface_create(ifname, mode, genl_info_net(info));
	if (IS_ERR(dev)) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "error while creating interface: %ld",
				       PTR_ERR(dev));
		return PTR_ERR(dev);
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_iput(msg, info);
	if (!hdr) {
		nlmsg_free(msg);
		return -ENOBUFS;
	}

	if (nla_put_string(msg, OVPN_A_IFNAME, dev->name)) {
		genlmsg_cancel(msg, hdr);
		nlmsg_free(msg);
		return -EMSGSIZE;
	}

	if (nla_put_u32(msg, OVPN_A_IFINDEX, dev->ifindex)) {
		genlmsg_cancel(msg, hdr);
		nlmsg_free(msg);
		return -EMSGSIZE;
	}

	genlmsg_end(msg, hdr);

	return genlmsg_reply(msg, info);
}

int ovpn_nl_dev_del_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct ovpn_struct *ovpn = info->user_ptr[0];

	rtnl_lock();
	ovpn_iface_destruct(ovpn);
	unregister_netdevice(ovpn->dev);
	netdev_put(ovpn->dev, NULL);
	rtnl_unlock();

	return 0;
}

int ovpn_nl_peer_new_doit(struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

int ovpn_nl_peer_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

int ovpn_nl_peer_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

int ovpn_nl_peer_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	return -EOPNOTSUPP;
}

int ovpn_nl_peer_del_doit(struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

int ovpn_nl_key_new_doit(struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

int ovpn_nl_key_swap_doit(struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

int ovpn_nl_key_del_doit(struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

/**
 * ovpn_nl_register - perform any needed registration in the NL subsustem
 *
 * Return: 0 on success, a negative error code otherwise
 */
int __init ovpn_nl_register(void)
{
	int ret = genl_register_family(&ovpn_nl_family);

	if (ret) {
		pr_err("ovpn: genl_register_family failed: %d\n", ret);
		return ret;
	}

	return 0;
}

/**
 * ovpn_nl_unregister - undo any module wide netlink registration
 */
void ovpn_nl_unregister(void)
{
	genl_unregister_family(&ovpn_nl_family);
}
