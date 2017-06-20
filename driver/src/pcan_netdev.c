/*****************************************************************************
 * Copyright (C) 2006-2007  PEAK System-Technik GmbH
 *
 * linux@peak-system.com
 * www.peak-system.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Maintainer(s): Klaus Hitschler (klaus.hitschler@gmx.de)
 * Contributions: Oliver Hartkopp (oliver.hartkopp@volkswagen.de)
 *****************************************************************************/

/*****************************************************************************
 *
 * pcan_netdev.c - CAN network device support functions
 *
 * $Id: pcan_netdev.c 1178 2016-03-24 15:17:28Z stephane $
 *
 * For CAN netdevice / socketcan specific questions please check the
 * Mailing List <socketcan-users@lists.berlios.de>
 * Project homepage http://developer.berlios.de/projects/socketcan
 *
 *****************************************************************************/
/*#define DEBUG*/
/*#undef DEBUG*/

#include "src/pcan_common.h"
#include <linux/sched.h>
#include <linux/skbuff.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0)
#include <linux/can/skb.h>
#endif
#include "src/pcan_main.h"
#include "src/pcan_fops.h"  /* pcan_open_path(), pcan_release_path() */
#include "src/pcan_fifo.h"
#include "src/pcan_netdev.h"

#define CAN_NETDEV_NAME		"can%d"

/* if defined, fix "Kernel NULL pointer dereference" when creating "canx"
 * interface under high busload conditions (should be defined) */
#define BUG_FIX_NULL_NETDEV

#if 0
/* private data structure for netdevice
 * "struct can_priv" is defined since 2.6.31 in include/linux/net/can/dev.h
 * => rename our struct can_priv into struct pcan_priv and set kernel
 *    can_priv as first member */
struct pcan_priv {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 23)
	struct net_device_stats		stats;	/* standard netdev statistics */

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
	struct can_priv			can;	/* must be the 1st one */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
	/* consider playing with bitrates only for 3.6+ and CAN-FD */
	struct can_bittiming_const	bt_const;
	struct can_bittiming_const	dbt_const;
#endif
#endif
	struct pcandev *		dev;	/* back ref to PCAN device */
};
#else
#define pcan_priv	pcan_udata
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
/* Note: Kernel 3.6 is the first one in which CAN-FD has been added.
 * Code below has been imported from linux-3.6/include/linux/can.h */
#define CAN_MAX_DLEN		8

#elif LINUX_VERSION_CODE < KERNEL_VERSION(3,18,0)
static inline bool can_is_canfd_skb(const struct sk_buff *skb)
{
	return skb->len == CANFD_MTU;
}
#endif

extern int __pcan_dev_start_writing(struct pcandev *dev,
						struct pcan_udata *ctx);

/* AF_CAN netdevice: open device */
static int pcan_netdev_open(struct net_device *dev)
{
	struct pcan_priv *priv = netdev_priv(dev);
	struct pcandev *pdev = priv->dev;

	DPRINTK(KERN_DEBUG "%s: %s %s\n", DEVICE_NAME, __func__, dev->name);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
	memset(&pdev->init_settings.nominal, '\0',
					sizeof(struct pcan_bittiming));
	pdev->init_settings.nominal.bitrate = priv->can.bittiming.bitrate;
	if (priv->can.ctrlmode & CAN_CTRLMODE_FD) {
		pdev->init_settings.flags |= PCANFD_INIT_FD;
		memset(&pdev->init_settings.data, '\0',
					sizeof(struct pcan_bittiming));
		pdev->init_settings.data.bitrate =
			priv->can.data_bittiming.bitrate;
	}
#endif

	if (pcan_open_path(pdev, priv))
		return -ENODEV;

	netif_start_queue(dev);

	return 0;
}

/* AF_CAN netdevice: close device */
static int pcan_netdev_close(struct net_device *dev)
{
	struct pcan_priv *priv = netdev_priv(dev);
	struct pcandev *pdev = priv->dev;

	DPRINTK(KERN_DEBUG "%s: %s %s\n", DEVICE_NAME, __func__, dev->name);

	pcan_release_path(pdev, priv);

	netif_stop_queue(dev);

	return 0;
}

/* AF_CAN netdevice: get statistics for device */
struct net_device_stats *pcan_netdev_get_stats(struct net_device *dev)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
	struct pcan_priv *priv = netdev_priv(dev);

	/* TODO: read statistics from chip */
	return &priv->stats;
#else
	return &dev->stats;
#endif
}

/* AF_CAN netdevice: transmit handler for device */
static int pcan_netdev_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct pcan_priv *priv = netdev_priv(dev);
	struct pcandev *pdev = priv->dev;
	struct net_device_stats *stats = pcan_netdev_get_stats(dev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
	struct canfd_frame *cf = (struct canfd_frame*)skb->data;
#else
	struct can_frame *cf = (struct can_frame*)skb->data;
#endif
	struct pcanfd_msg f;
	int err;

	DPRINTK(KERN_DEBUG "%s: %s %s\n", DEVICE_NAME, __func__, dev->name);

	/* if the device is plugged out */
	if (!pdev->ucPhysicallyInstalled) {
		stats->tx_dropped++;
		goto free_out;
	}

	/* convert SocketCAN CAN frame to PCAN FIFO compatible format */
	f.type = PCANFD_TYPE_CAN20_MSG;
	f.flags = PCANFD_MSG_STD;

	if (cf->can_id & CAN_RTR_FLAG)
		f.flags |= PCANFD_MSG_RTR;
	if (cf->can_id & CAN_EFF_FLAG)
		f.flags |= PCANFD_MSG_EXT;
	f.id = cf->can_id & CAN_ERR_MASK;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
	if (can_is_canfd_skb(skb)) {

		f.type = PCANFD_TYPE_CANFD_MSG;

		if (cf->flags & CANFD_ESI)
			f.flags |= PCANFD_MSG_ESI;
		if (cf->flags & CANFD_BRS)
			f.flags |= PCANFD_MSG_BRS;
	}

	f.data_len = cf->len;
#else
	f.data_len = cf->can_dlc;
#endif

	memcpy(f.data, cf->data, f.data_len);

	/* put data into fifo */
	err = pcan_fifo_put(&pdev->writeFifo, &f);
	if (err < 0) {
		/* stop netdev queue when PCAN FIFO is full */
		stats->tx_fifo_errors++; /* just for informational purposes */
		netif_stop_queue(dev);

		stats->tx_dropped++;
		goto free_out;
	}

	/* if we just put the 1st message (=the fifo was empty), we can start
	 * writing on hardware if it is ready for doing this. */
#ifdef PCAN_USES_OLD_TX_ENGINE_STATE
	if (atomic_read(&pdev->tx_engine_state) == TX_ENGINE_STOPPED)
#else
	if (pdev->locked_tx_engine_state == TX_ENGINE_STOPPED)
#endif
		/* if can device ready to send, start writing */
		__pcan_dev_start_writing(pdev, NULL);

	stats->tx_packets++;
	stats->tx_bytes += f.data_len;

	dev->trans_start = jiffies;

free_out:
	dev_kfree_skb(skb);

	return 0;
}

/* AF_CAN netdevice: receive function (put can_frame to netdev queue) */
int pcan_netdev_rx(struct pcandev *dev, struct pcanfd_msg *pf)
{
	struct net_device *ndev = dev->netdev;
	struct net_device_stats *stats;
	struct sk_buff *skb;
	u8 *prx_cnt, *ptx_cnt;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0)
	struct can_frame cf, *pcf = &cf;
#else
	struct pcan_priv *priv = netdev_priv(ndev);
	struct canfd_frame cf, *pcf = &cf;
#endif
	int ld, lf = sizeof(cf);

	DPRINTK(KERN_DEBUG "%s: %s(%s)\n", DEVICE_NAME, __func__, ndev->name);

#ifdef BUG_FIX_NULL_NETDEV
	/* under high busload condtions, interrupts may occur before everything
	 * has been completed.  */
	if (!ndev)
		return 0;
#endif

	switch (pf->type) {

	case PCANFD_TYPE_NOP:
		return 0;

	case PCANFD_TYPE_CANFD_MSG:
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0)
		/* Kernels < 3.6 don't know anything about CAN-FD... */
		return -EINVAL;
#else
		if (!(priv->can.ctrlmode & CAN_CTRLMODE_FD)) {
			pr_err("%s: CANFD frame discarded (%s not CAN-FD)\n",
					DEVICE_NAME, ndev->name);
			return 0;
		}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
		skb = dev_alloc_skb(lf);
#else
		/* handle CAN-FD when kernel is ok for this */
		skb = alloc_canfd_skb(ndev, &pcf);
#endif
#endif
		break;

	default:
	case PCANFD_TYPE_CAN20_MSG:
		lf = sizeof(struct can_frame);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
		skb = dev_alloc_skb(lf);
#else
		/* using 3.9+ version enables to forget this:
		 *
		 * can_skb_reserve(skb);
		 * can_skb_prv(skb)->ifindex = ndev->ifindex;
		 *
		 * which is mandatory when using dev_alloc_skb() for Kernels
		 * 3.9+
		 */
		skb = alloc_can_skb(ndev, (struct can_frame **)&pcf);
#endif
		break;
	}

	if (!skb)
		return -ENOMEM;

#if 0
	/* Currently the driver only supports timestamp setting at host arrival
	 * time. Therefore the netdev support can used the timestamp provided
	 * by netif_rx() which is set when there is no given timestamp (and
	 * when network timestamps are not disabled by default in the host).
	 * So we just use the mechanics like any other network device does... */
#else
	/* use hw timestamp if given */
	if (pf->flags & PCANFD_TIMESTAMP) {
		struct skb_shared_hwtstamps *hwts = skb_hwtstamps(skb);
		hwts->hwtstamp = timeval_to_ktime(pf->timestamp);

		skb->tstamp = hwts->hwtstamp;
	}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
	skb->dev = ndev;
	skb->protocol = htons(ETH_P_CAN);
	skb->pkt_type = PACKET_BROADCAST;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
#endif

	memset(pcf, '\0', lf);

	switch (pf->type) {

	case PCANFD_TYPE_STATUS:

		/* use device counters instead of data bytes saved into pf->data
		 * because these counters are copied into pf->data[] just 
		 * before being pushed into chardev rx fifo. Thus, pf->data[]
		 * don't contain any rx/tx err counters! */
		prx_cnt = &dev->rx_error_counter;
		ptx_cnt = &dev->tx_error_counter;

		pcf->can_id |= CAN_ERR_FLAG;
		ld = CAN_ERR_DLC;

		switch (pf->id) {
		case PCANFD_ERROR_BUSOFF:
			pcf->can_id |= CAN_ERR_BUSOFF_NETDEV;
			break;

		case PCANFD_ERROR_PASSIVE:
			pcf->can_id |= CAN_ERR_CRTL;
			if (*prx_cnt > 127)
				pcf->data[1] |= CAN_ERR_CRTL_RX_PASSIVE;
			if (*ptx_cnt > 127)
				pcf->data[1] |= CAN_ERR_CRTL_TX_PASSIVE;
			break;
		case PCANFD_ERROR_WARNING:
			pcf->can_id |= CAN_ERR_CRTL;
			if (*prx_cnt > 96)
				pcf->data[1] |= CAN_ERR_CRTL_RX_WARNING;
			if (*ptx_cnt > 96)
				pcf->data[1] |= CAN_ERR_CRTL_TX_WARNING;
			break;

		case PCANFD_RX_OVERFLOW:
			if (pf->flags & PCANFD_ERROR_PROTOCOL) {
				pcf->can_id |= CAN_ERR_PROT;
				pcf->data[2] |= CAN_ERR_PROT_OVERLOAD;
			} else {
				pcf->can_id |= CAN_ERR_CRTL;
				pcf->data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;
			}
			break;
		case PCANFD_TX_OVERFLOW:
			pcf->can_id |= CAN_ERR_CRTL;
			pcf->data[1] |= CAN_ERR_CRTL_TX_OVERFLOW;
			break;
		}
		break;

	case PCANFD_TYPE_CAN20_MSG:
		pcf->can_id = pf->id & CAN_ERR_MASK;
		if (pf->flags & PCANFD_MSG_RTR)
			pcf->can_id |= CAN_RTR_FLAG;
		if (pf->flags & PCANFD_MSG_EXT)
			pcf->can_id |= CAN_EFF_FLAG;

		ld = pf->data_len;
		if (ld > CAN_MAX_DLEN)
			ld = CAN_MAX_DLEN;

		memcpy(pcf->data, pf->data, ld);
		break;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
	case PCANFD_TYPE_CANFD_MSG:
		if (pf->flags & PCANFD_MSG_ESI)
			pcf->flags |= CANFD_ESI;
		if (pf->flags & PCANFD_MSG_BRS)
			pcf->flags |= CANFD_BRS;

		pcf->can_id = pf->id & CAN_ERR_MASK;
		if (pf->flags & PCANFD_MSG_RTR)
			pcf->can_id |= CAN_RTR_FLAG;
		if (pf->flags & PCANFD_MSG_EXT)
			pcf->can_id |= CAN_EFF_FLAG;

		ld = pf->data_len;
		if (ld > CANFD_MAX_DLEN)
			ld = CANFD_MAX_DLEN;

		memcpy(pcf->data, pf->data, ld);
		break;
#endif
	default:
		pr_err("%s: %s() unsupported pcan msg type %d\n",
				DEVICE_NAME, __func__, pf->type);
		return -EINVAL;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0)
	pcf->can_dlc = ld;
#else
	pcf->len = ld;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
	/* if the frame has been allocated using dev_alloc_skb(), MUST
	 * copy the local frame object into the skb data */
	memcpy(skb_put(skb, lf), &cf, lf);

#elif LINUX_VERSION_CODE == KERNEL_VERSION(4,1,0) \
   || LINUX_VERSION_CODE == KERNEL_VERSION(4,1,1)
	/* mandatory for Kernels 4.1.[01] */
	__net_timestamp(skb);
#endif

	netif_rx(skb);

	ndev->last_rx = jiffies;

	stats = pcan_netdev_get_stats(ndev);
	stats->rx_packets++;
	stats->rx_bytes += ld;

	return 1;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)
static int pcan_netdev_change_mtu(struct net_device *netdev, int new_mtu)
{
#ifdef DEBUG
	pr_info("%s:%s(new_mtu=%d) old_mtu=%d\n",
			DEVICE_NAME, __func__, new_mtu, netdev->mtu);
#endif
	/* Do not allow changing the MTU while running */
	if (netdev->flags & IFF_UP)
		return -EBUSY;

	/* allow change of MTU according to the CANFD ability of the device */
	if (new_mtu != CAN_MTU) {

		struct pcan_priv *priv = netdev_priv(netdev);

		if (!(priv->can.ctrlmode_supported & CAN_CTRLMODE_FD))
			return -EINVAL;

		if (new_mtu != CANFD_MTU)
			return -EINVAL;
	}

	netdev->mtu = new_mtu;
	return 0;
}
#endif
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,28)
static const struct net_device_ops pcan_netdev_ops = {
	.ndo_open	= pcan_netdev_open,
	.ndo_start_xmit	= pcan_netdev_tx,
	.ndo_stop	= pcan_netdev_close,
	.ndo_get_stats	= pcan_netdev_get_stats,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)
	.ndo_change_mtu = pcan_netdev_change_mtu,
#else
	.ndo_change_mtu = can_change_mtu,
#endif
#endif
};
#endif

/* AF_CAN netdevice: initialize data structure */
static void pcan_netdev_init(struct net_device *dev)
{
#if 0
	/* Fill in the the fields of the device structure with AF_CAN generic
	 * values */
	if (!dev)
		return;
#endif
	/* should be superfluos as struct netdevice is zeroed at malloc time */
#if 0
	dev->change_mtu           = NULL;
	dev->set_mac_address      = NULL;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	dev->hard_header          = NULL;
	dev->rebuild_header       = NULL;
	dev->hard_header_cache    = NULL;
	dev->header_cache_update  = NULL;
	dev->hard_header_parse    = NULL;
#else
	dev->header_ops           = NULL;
#endif
#endif

	dev->type = ARPHRD_CAN;
	dev->hard_header_len = 0;
	dev->mtu = sizeof(struct can_frame);
	dev->addr_len = 0;
	dev->tx_queue_len = 10;

	dev->flags = IFF_NOARP;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,28)
	dev->netdev_ops  = &pcan_netdev_ops;
#else
	dev->open = pcan_netdev_open;
	dev->stop = pcan_netdev_close;
	dev->hard_start_xmit = pcan_netdev_tx;
	dev->get_stats = pcan_netdev_get_stats;
#endif
}

static void pcan_check_ifname(char *name)
{
	/* check wanted assigned 'name' against existing device names */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	if (__dev_get_by_name(name)) {
#else
	if (__dev_get_by_name(&init_net, name)) {
#endif
		printk(KERN_INFO "%s: assigned netdevice %s already exists\n",
				DEVICE_NAME, name);

		*name = 0; /* mark for auto assignment */
	}
}

/* AF_CAN netdevice: try to reassign netdev name according to user needs */
void pcan_netdev_create_name(char *name, struct pcandev *pdev)
{
	extern char *assign; /* module param: assignment for netdevice names */
	int minor = pdev->nMinor;
	char *pa = assign;

	DPRINTK(KERN_DEBUG
		"%s: %s(): minor=%d major=%d (usb major=%d) assign=\"%s\"\n",
		DEVICE_NAME, __func__, minor, pdev->nMajor, USB_MAJOR, assign);

	if (!assign) /* auto assignment */
		return;

#ifdef USB_SUPPORT
#ifdef CONFIG_USB_DYNAMIC_MINORS
#ifndef PCAN_USB_DONT_REGISTER_DEV
	if (pdev->nMajor == USB_MAJOR)
		minor += PCAN_USB_MINOR_BASE;
#endif
#endif
#endif

	if (!strncmp(pa, "devid", 5)) {

		/* if device defines an alternate number, use it instead of
		 * its minor */
		if (pdev->flags & PCAN_DEV_USES_ALT_NUM) {
			snprintf(name, IFNAMSIZ-1, CAN_NETDEV_NAME,
					(int )pdev->device_alt_num);
			pcan_check_ifname(name);
			if (*name)
				return;
		}

		pa += 5;
		if (*pa++ != ',')
			return;
	}

	if (!strncmp(pa, "peak", 4)) {

		/* assign=peak
		 * easy: /dev/pcanXX -> canXX */
		snprintf(name, IFNAMSIZ-1, CAN_NETDEV_NAME, minor);

	} else {

		/* e.g. assign=pcan32:can1,pcan41:can2 */
		int peaknum, netnum;
		char *ptr = pa;

		while (ptr < (pa + strlen(pa))) {
			/* search first 'p' from pcanXX */
			ptr = strchr(ptr, 'p');
			if (!ptr)
				return; /* no match => quit */

			if (sscanf(ptr, DEVICE_NAME "%d:can%d", &peaknum,
								&netnum) != 2) {
				printk(KERN_INFO
					"%s: bad parameter format in netdevice "
					"assignment.\n", DEVICE_NAME);
				return; /* bad parameter format => quit */
			}

			if (peaknum == minor) {
				snprintf(name, IFNAMSIZ-1, CAN_NETDEV_NAME,
									netnum);
				break; /* done */
			}
			ptr++; /* search for next 'p' */
		}
	}

	if (*name)
		pcan_check_ifname(name);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
static struct can_bittiming_const *
	pcan_netdev_convert_bt_caps(struct can_bittiming_const *pconst,
				    const struct pcan_bittiming_caps *pcaps)
{
	if (!pcaps)
		return NULL;

	memset(pconst, '\0', sizeof(*pconst));

	strncpy(pconst->name, DEVICE_NAME, sizeof(pconst->name));
	pconst->tseg1_min = pcaps->min_tseg1;
	pconst->tseg1_max = pcaps->max_tseg1;
	pconst->tseg2_min = pcaps->min_tseg2;
	pconst->tseg2_max = pcaps->max_tseg2;
	pconst->sjw_max = pcaps->max_sjw;
	pconst->brp_min = pcaps->min_brp;
	pconst->brp_max = pcaps->max_brp;
	pconst->brp_inc = pcaps->inc_brp;

	return pconst;
}
#endif

/* AF_CAN netdevice: register network device */
int pcan_netdev_register(struct pcandev *pdev)
{
	struct net_device *ndev;
	struct pcan_priv *priv;
	char name[IFNAMSIZ] = {0};
	char *can_type = "CAN";

	pcan_netdev_create_name(name, pdev);

	if (!name[0]) {
		/* use the default: autoassignment */
		strncpy(name, CAN_NETDEV_NAME, IFNAMSIZ-1);
	}

#ifdef LINUX_26

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
	ndev = alloc_netdev(sizeof(struct pcan_priv), name, pcan_netdev_init);
#else
	ndev = alloc_netdev(sizeof(struct pcan_priv), name, NET_NAME_UNKNOWN,
			pcan_netdev_init);
#endif

	if (!ndev) {
		printk(KERN_ERR "%s: out of memory\n", DEVICE_NAME);
		return 1;
	}

	priv = netdev_priv(ndev);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
	/* registering via register_candv() enables to play with bitrates too */
	priv->can.bittiming_const =
		pcan_netdev_convert_bt_caps(&priv->bt_const,
					    pdev->bittiming_caps);
	priv->can.data_bittiming_const =
		pcan_netdev_convert_bt_caps(&priv->dbt_const,
					    pdev->dbittiming_caps);

	priv->can.clock.freq = pdev->sysclock_Hz /
		pdev->bittiming_caps->intern_prescaler;

	/* setup default bitrate now */
	priv->can.bittiming.bitrate = pdev->init_settings.nominal.bitrate;

	/* need to register as candev for CAN-FD support */
	if (register_candev(ndev)) {
#else
	if (register_netdev(ndev)) {
#endif
		printk(KERN_INFO "%s: Failed registering netdevice\n",
			DEVICE_NAME);
		free_netdev(ndev);
		return 1;
	}
#else
	ndev = (struct net_device*)pcan_malloc(sizeof(struct net_device),
					   GFP_KERNEL);
	if (!ndev) {
		printk(KERN_ERR "%s: out of memory\n", DEVICE_NAME);
		return 1;
	}

	memset(ndev, 0, sizeof(struct net_device));

	priv = (struct pcan_priv*)pcan_malloc(sizeof(struct pcan_priv), GFP_KERNEL);
	if (!priv) {
		printk(KERN_ERR "%s: out of memory\n", DEVICE_NAME);
		pcan_free(ndev);
		return 1;
	}

	memset(priv, 0, sizeof(struct pcan_priv));
	ndev->priv = priv;

	/* fill net_device structure */
	pcan_netdev_init(ndev);

	strncpy(ndev->name, name, IFNAMSIZ-1); /* name the device */
	SET_MODULE_OWNER(ndev);

	if (register_netdev(ndev)) {
		printk(KERN_INFO "%s: Failed registering netdevice\n",
			DEVICE_NAME);
		pcan_free(priv);
		pcan_free(ndev);
		return 1;
	}
#endif

	/* Make references between pcan device and netdevice */
	priv->dev = pdev;
	pdev->netdev = ndev;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
	/* if an open_fd entry point is defined, then the device is CAN-FD */
	if (pdev->device_open_fd) {
		priv->can.ctrlmode_supported |= CAN_CTRLMODE_FD;
		can_type = "CAN-FD";
	}
#endif

	printk(KERN_INFO
	       "%s: registered %s netdevice %s for %s hw (%d,%d)\n",
	       DEVICE_NAME, can_type,
	       ndev->name, pdev->type, pdev->nMajor, pdev->nMinor);

	return 0;
}

/* AF_CAN netdevice: unregister network device */
int pcan_netdev_unregister(struct pcandev *pdev)
{
	struct net_device *ndev = pdev->netdev;
	struct pcan_priv *priv;

	if (!ndev)
		return 1;

	DPRINTK(KERN_DEBUG "%s: %s %s\n", DEVICE_NAME, __func__, ndev->name);

	/* mark as unregistered to be sure not to loop here again */
	pdev->netdev = NULL;

	priv = netdev_priv(ndev);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
	unregister_netdev(ndev);
#else
	unregister_candev(ndev);
#endif

#ifndef LINUX_26
	if (priv)
		pcan_free(priv);
#endif

	return 0;
}
