/*
 * CAN-FD extension to PEAK-System CAN products.
 *
 * Copyright (C) 2015 Stephane Grosjean <s.grosjean@peak-system.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
/* #define DEBUG */
/*#undef DEBUG*/

#include "src/pcanfd_core.h"
#include "src/pcan_filter.h"

#ifdef DEBUG
#define DEBUG_WAIT_RD
#define DEBUG_WAIT_WR
#else
//#define DEBUG_WAIT_RD
//#define DEBUG_WAIT_WR
#endif

/* Timeout set to task waiting for room in the tx queue.
 * 0 means infinite.
 * != 0 implies that the wait might end with -ETIMEDOUT. */
//#define PCANFD_TIMEOUT_WAIT_FOR_WR	100
#define PCANFD_TIMEOUT_WAIT_FOR_WR	0

extern u16 btr0btr1;
extern u32 pcan_def_dbitrate;

/*
 * Compute bitrate according to bittiming spec and Clock frequency
 */
static int pcan_bittiming_to_bitrate(u32 clk_Hz, struct pcan_bittiming *pbt)
{
#ifdef DEBUG
	pr_info("%s: %s(): brp=%u bitrate=%u bps\n", DEVICE_NAME, __func__,
			pbt->brp, pbt->bitrate);
#endif

	/* avoid div by 0 */
	if (pbt->brp) {
		pbt->sjw = 1; /* ??? */
		pbt->sample_point = (PCAN_SAMPT_SCALE *
			(1 + pbt->tseg1)) / (1 + pbt->tseg1 + pbt->tseg2);
		pbt->bitrate = pcan_get_bps(clk_Hz, pbt);
		return 0;
	}

	return -EINVAL;
}

int pcan_bittiming_normalize(struct pcan_bittiming *pbt,
			u32 clock_Hz, const struct pcan_bittiming_caps *caps)
{
	int err = -EINVAL;

#ifdef DEBUG
	pr_info("%s: %s(clk=%u): brp=%u bitrate=%u bps\n",
		DEVICE_NAME, __func__, clock_Hz, pbt->brp, pbt->bitrate);
#endif

	/* always trust bitrate first */
	if (pbt->bitrate) {
		err = pcan_bitrate_to_bittiming(pbt, caps, clock_Hz);

	/* if brp valid, use these for computing the bitrate field */
	} else if (pbt->brp) {
		err = pcan_bittiming_to_bitrate(clock_Hz, pbt);

#ifdef DEBUG
	/* else, if any of them is valid, it's an error! */
	} else {
		pr_info("%s: invalid bittiming specs: unable to normalize\n",
							DEVICE_NAME);
#endif
	}

	return err;
}

/*
 * Convert 8MHz SJA1000 BTR0BTR1 16-bits value into a generic bittiming
 * representation
 */
struct pcan_bittiming *pcan_btr0btr1_to_bittiming(struct pcan_bittiming *pbt,
						  u16 btr0btr1)
{
#ifdef DEBUG
	pr_info("%s: %s(BTR0BTR1=%04xh)\n", DEVICE_NAME, __func__, btr0btr1);
#endif
	pbt->sjw = 1 + ((btr0btr1 & 0xc000) >> 14);
	pbt->brp = 1 + ((btr0btr1 & 0x3f00) >> 8);
	pbt->tsam = (btr0btr1 & 0x0080) >> 7;
	pbt->tseg2 = 1 + ((btr0btr1 & 0x0070) >> 4);
	pbt->tseg1 = 1 + (btr0btr1 & 0x000f);
	pbt->bitrate = 0;

	pcan_bittiming_to_bitrate(8*MHz, pbt);

	return pbt;
}

/* Convert old CAN 2.0 init object into new-style CAN-FD init object */
struct pcanfd_init *pcan_init_to_fd(struct pcanfd_init *pfdi,
				    const TPCANInit *pi)
{
#ifdef DEBUG
	pr_info("%s: %s(pi[MsgType=%u ListenOnly=%u BTR0BTR1=%04xh])\n",
			DEVICE_NAME, __func__,
			pi->ucCANMsgType, pi->ucListenOnly, pi->wBTR0BTR1);
#endif

#if 0
	/* DON'T memset('\0') the struct pcanfd_init since it may already 
	 * contain data (or other CANFD specific values). Caller HAS TO
	 * initialize the struct pcanfd_init by himself! */
#else
	memset(pfdi, '\0', sizeof(*pfdi));
#endif
	if (!(pi->ucCANMsgType & MSGTYPE_EXTENDED))
		pfdi->flags |= PCANFD_INIT_STD_MSG_ONLY;

	if (pi->ucListenOnly)
		pfdi->flags |= PCANFD_INIT_LISTEN_ONLY;

	if (pi->wBTR0BTR1) {
		pcan_btr0btr1_to_bittiming(&pfdi->nominal, pi->wBTR0BTR1);
		pcan_bittiming_to_bitrate(8*MHz, &pfdi->nominal);
	}

#ifdef DEBUG
	pr_info("%s: %s(): bitrate=%u bps\n", DEVICE_NAME, __func__,
			pfdi->nominal.bitrate);
#endif
	return pfdi;
}

/* Convert CAN-FD init object into old-style CAN 2.0 init object. */
TPCANInit *pcan_fd_to_init(TPCANInit *pi, struct pcanfd_init *pfdi)
{
#ifdef DEBUG
	pr_info("%s: %s(pfdi[flags=%08xh bitrate=%u brp=%u])\n",
			DEVICE_NAME, __func__, pfdi->flags,
			pfdi->nominal.bitrate, pfdi->nominal.brp);
#endif
	pi->ucCANMsgType =
		(pfdi->flags & PCANFD_INIT_STD_MSG_ONLY) ? 0 : MSGTYPE_EXTENDED;
	pi->ucListenOnly = !!(pfdi->flags & PCANFD_INIT_LISTEN_ONLY);

	/* this is OK if nominal.bitrate is not 0 */
	if (pfdi->nominal.bitrate)
		pi->wBTR0BTR1 = sja1000_bitrate(pfdi->nominal.bitrate);

	/* otherwise, consider brp,... with the init clock first */
	else if (!pcan_bittiming_to_bitrate(pfdi->clock_Hz, &pfdi->nominal)) {
		/* and convert this bitrate in SJA1000 BTR0BTR1 */
		pi->wBTR0BTR1 = sja1000_bitrate(pfdi->nominal.bitrate);

	} else {
		pi->wBTR0BTR1 = 0;

		pr_err("%s: invalid bittiming specs: BTR0BTR1 set to 0\n",
							DEVICE_NAME);
	}

	memset(&pfdi->data, '\0', sizeof(struct pcan_bittiming));

	return pi;
}

/* Convert old-style TPCANMsg CAN 2.0 type into the new one
 *
 * WARNING: msg->LEN field MUST be <= 64
 */
struct pcanfd_msg *pcan_msg_to_fd(struct pcanfd_msg *pf,
				    const TPCANMsg *msg)
{
	pf->type = PCANFD_TYPE_CAN20_MSG;	/* obviously */
	pf->id = msg->ID;
	pf->flags = msg->MSGTYPE;
	pf->data_len = msg->LEN;
	memcpy(pf->data, msg->DATA, msg->LEN);

	return pf;
}

/* Convert CAN 2.0 frame into old-style TPCANRdMsg type
 *
 * Warning: it's caller's responsibility to check whether pf->data_len is <= 8
 */
TPCANRdMsg *pcan_fd_to_msg(TPCANRdMsg *msg, const struct pcanfd_msg *pf)
{
	switch (pf->type) {

	case PCANFD_TYPE_STATUS:
		msg->Msg.ID = pf->id;
		msg->Msg.MSGTYPE = MSGTYPE_STATUS;
		msg->Msg.LEN = 4;

		memset(msg->Msg.DATA, CAN_ERR_OK, msg->Msg.LEN);

		switch (pf->id) {
		case PCANFD_ERROR_WARNING:
			msg->Msg.DATA[3] |= CAN_ERR_BUSLIGHT;
			break;
		case PCANFD_ERROR_PASSIVE:
			msg->Msg.DATA[3] |= CAN_ERR_BUSHEAVY;
			break;
		case PCANFD_ERROR_BUSOFF:
			msg->Msg.DATA[3] |= CAN_ERR_BUSOFF;
			break;
		case PCANFD_RX_EMPTY:
			msg->Msg.DATA[3] |= CAN_ERR_QRCVEMPTY;
			break;
		case PCANFD_RX_OVERFLOW:
			msg->Msg.DATA[3] |= CAN_ERR_OVERRUN;
			break;
		case PCANFD_TX_OVERFLOW:
			msg->Msg.DATA[3] |= CAN_ERR_QXMTFULL;
			break;

		default:
		case PCANFD_TX_EMPTY:
			msg->Msg.DATA[3] |= CAN_ERR_RESOURCE;

		case PCANFD_ERROR_ACTIVE:
			break;
		}
		break;

	case PCANFD_TYPE_CAN20_MSG:
		msg->Msg.ID = pf->id;
		msg->Msg.MSGTYPE = (BYTE )(pf->flags & 0xff);
		msg->Msg.LEN = pf->data_len;

		/* Warning: pf->data_len MUST be <= 8 */
		memcpy(&msg->Msg.DATA, pf->data, pf->data_len);
		break;

	default:
		return NULL;
	}

	/* TODO: should check whether PCANFD_TIMESTAMP is always set */
	if (pf->flags & PCANFD_TIMESTAMP) {
		msg->dwTime = pf->timestamp.tv_sec * 1000;
		msg->dwTime += pf->timestamp.tv_usec / 1000;
		msg->wUsec = pf->timestamp.tv_usec % 1000;
	}

	return msg;
}

/* reset fifo queues and counters of a pcan device and release it.
 * WARNING: caller should normally wait for the output fifo to be empty
 *          before calling pcanfd_dev_reset()
 */
int pcanfd_dev_reset(struct pcandev *dev)
{
	int err;

	/* flush fifo contents */
	err = pcan_fifo_reset(&dev->writeFifo);
	if (err)
		goto reset_fail;

	err = pcan_fifo_reset(&dev->readFifo);
	if (err)
		goto reset_fail;

	/* release the device (if it has been opened) */
	if (dev->nOpenPaths && dev->device_release)
		dev->device_release(dev);

	dev->flags &= ~PCAN_DEV_OPENED;

#ifdef PCAN_USES_OLD_TX_ENGINE_STATE
	atomic_set(&dev->tx_engine_state, TX_ENGINE_CLOSED);
#else
	dev->locked_tx_engine_state = TX_ENGINE_CLOSED;
#endif

	/* New: reset these counters too */
	dev->dwErrorCounter = 0;
	dev->dwInterruptCounter = 0;

#ifdef DEBUG
	pr_info(DEVICE_NAME ": %s CAN%u rx=%u tx=%u\n",
			dev->adapter->name, dev->nChannel+1,
			dev->rx_frames_counter, dev->tx_frames_counter);
#endif
	dev->rx_frames_counter = 0;
	dev->tx_frames_counter = 0;

	//dev->wCANStatus = CAN_ERR_OK;
	pcan_set_bus_state(dev, PCANFD_UNKNOWN);

reset_fail:
	return err;
}

int pcanfd_dev_open(struct pcandev *dev, struct pcanfd_init *pfdi)
{
	int err;

	/* check init settings */
	if (!pfdi->clock_Hz)
		pfdi->clock_Hz = dev->sysclock_Hz;

	err = pcan_bittiming_normalize(&pfdi->nominal,
					pfdi->clock_Hz, dev->bittiming_caps);
	if (err) {
		pfdi->nominal = dev->def_init_settings.nominal;
#ifdef DEBUG
		pr_err(DEVICE_NAME ": %s CAN%u using default bittiming\n",
		       dev->adapter->name, dev->nChannel+1);
#endif
		//return err;
	}

	/* sanitize */
	if (!(dev->flags & PCAN_DEV_BUSLOAD_RDY))
		pfdi->flags &= ~PCANFD_INIT_BUS_LOAD_INFO;

	if (!dev->device_open_fd) {
		TPCANInit init;

		pfdi->flags &= ~(PCANFD_INIT_FD|PCANFD_INIT_FD_NON_ISO);

		pcan_fd_to_init(&init, pfdi);
		if (!init.wBTR0BTR1) {
			init.wBTR0BTR1 = sja1000_bitrate(dev->def_init_settings.nominal.bitrate);
#ifdef DEBUG
			pr_err(DEVICE_NAME
				": %s CAN%u using default BTR0BTR1\n",
				dev->adapter->name, dev->nChannel+1);
#endif
			//return -EINVAL;
		}

		/* Note: all of these devices don't allow to change their clock
		 * settings */
		pfdi->clock_Hz = dev->sysclock_Hz;
#ifdef DEBUG
		pr_info("%s[%d]: time=%u.%06us: opening CAN%u with clock=%u Hz "
			"bitrate=%u bps BTR0BTR1=%04Xh (flags=%08xh)\n",
			DEVICE_NAME, dev->nMinor,
			(u32 )dev->init_timestamp.tv_sec,
			(u32 )dev->init_timestamp.tv_usec,
			dev->nChannel+1,
			pfdi->clock_Hz,
			pfdi->nominal.bitrate, init.wBTR0BTR1,
			pfdi->flags);
		pr_info("%s[%d]: nominal [brp=%d tseg1=%d tseg2=%d sjw=%d]\n",
			DEVICE_NAME, dev->nMinor, pfdi->nominal.brp,
			pfdi->nominal.tseg1, pfdi->nominal.tseg2,
			pfdi->nominal.sjw);
#endif

		/* device is not CAN-FD capable: forward to (old) CAN 2.0 API */
		err = dev->device_open(dev, init.wBTR0BTR1,
						init.ucCANMsgType,
						init.ucListenOnly);
	} else {

		err = pcan_bittiming_normalize(&pfdi->nominal,
					pfdi->clock_Hz, dev->bittiming_caps);
		if (err) {
			pfdi->nominal = dev->def_init_settings.nominal;
#ifdef DEBUG
			pr_err(DEVICE_NAME
				": %s CAN%u using default bittiming\n",
				dev->adapter->name, dev->nChannel+1);
#endif
			//return err;
		}

		if (pfdi->flags & PCANFD_INIT_FD) {

			err = pcan_bittiming_normalize(&pfdi->data,
					pfdi->clock_Hz, dev->dbittiming_caps);

			if (err) {
				pfdi->data = dev->def_init_settings.data;
#ifdef DEBUG
				pr_err(DEVICE_NAME ": %s CAN%u using default "
					"data bittiming\n",
					dev->adapter->name, dev->nChannel+1);
#endif
				//return err;
			}

			/* For CAN FD the data bitrate has to be >= the
			 * nominal bitrate */
			if (pfdi->data.bitrate < pfdi->nominal.bitrate) {
				pr_err(DEVICE_NAME ": %s CAN%u data bitrate "
					"(%u bps) should be greater than "
					"nominal bitrate (%u bps)\n",
					dev->adapter->name, dev->nChannel+1,
					pfdi->data.bitrate,
					pfdi->nominal.bitrate);

				return -EINVAL;
			}
		}

#ifdef DEBUG
		pr_info("%s[%d]: time=%u.%06us: opening CANFD%u with "
			"clock=%u Hz bitrate=%u bps dbitrate=%u bps "
			"(flags=%08xh)\n",
			DEVICE_NAME, dev->nMinor,
			(u32 )dev->init_timestamp.tv_sec,
			(u32 )dev->init_timestamp.tv_usec,
			dev->nChannel+1,
			pfdi->clock_Hz,
			pfdi->nominal.bitrate, pfdi->data.bitrate,
			pfdi->flags);
		pr_info("%s[%d]: nominal [brp=%d tseg1=%d tseg2=%d sjw=%d]\n",
			DEVICE_NAME, dev->nMinor, pfdi->nominal.brp,
			pfdi->nominal.tseg1, pfdi->nominal.tseg2,
			pfdi->nominal.sjw);
		pr_info("%s[%d]: data [brp=%d tseg1=%d tseg2=%d sjw=%d]\n",
			DEVICE_NAME, dev->nMinor, pfdi->data.brp,
			pfdi->data.tseg1, pfdi->data.tseg2,
			pfdi->data.sjw);
#endif
		err = dev->device_open_fd(dev, pfdi);
	}

	if (err)
		return err;

	dev->flags |= PCAN_DEV_OPENED;
	pcan_gettimeofday(&dev->init_timestamp);

	/* remember the init settings for further usage */
	dev->init_settings = *pfdi;

	/* default tx engine state: ready to start! */
#ifdef PCAN_USES_OLD_TX_ENGINE_STATE
	if (atomic_read(&dev->tx_engine_state) == TX_ENGINE_CLOSED)
		atomic_set(&dev->tx_engine_state, TX_ENGINE_STOPPED);
#else
	if (dev->locked_tx_engine_state == TX_ENGINE_CLOSED)
		dev->locked_tx_engine_state = TX_ENGINE_STOPPED;
#endif
	return 0;
}

int pcanfd_ioctl_set_init(struct pcandev *dev, struct pcanfd_init *pfdi)
{
	int err;

#ifdef DEBUG
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif

	err = pcanfd_dev_reset(dev);
	if (err)
		return err;

	return pcanfd_dev_open(dev, pfdi);
}

int pcanfd_ioctl_get_init(struct pcandev *dev, struct pcanfd_init *pfdi)
{
#ifdef DEBUG
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif

	memcpy(pfdi, &dev->init_settings, sizeof(*pfdi));

	return 0;
}

/* add a message filter_element into the filter chain or delete all
 * filter_elements
 */
int pcanfd_ioctl_add_filter(struct pcandev *dev, struct pcanfd_msg_filter *pf)
{
#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif

	/* filter == NULL -> delete the filter_elements in the chain */
	if (!pf) {
		pcan_delete_filter_all(dev->filter);
		return 0;
	}

	return pcan_add_filter(dev->filter,
		               pf->id_from, pf->id_to, pf->msg_flags);
}

/* add several message filter_element into the filter chain.
 */
int pcanfd_ioctl_add_filters(struct pcandev *dev,
						struct pcanfd_msg_filters *pfl)
{
#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif

	/* filter == NULL -> delete the filter_elements in the chain */
	if (!pfl) {
		pcan_delete_filter_all(dev->filter);
		return 0;
	}

	return pcan_add_filters(dev->filter, pfl->list, pfl->count);
}

/* get several message filter_element from the filter chain.
 */
int pcanfd_ioctl_get_filters(struct pcandev *dev,
						struct pcanfd_msg_filters *pfl)
{
	int err;

#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif

	/* filter == NULL -> return the current nb of filters in the chain */
	if (!pfl)
		return pcan_get_filters_count(dev->filter);

	err = pcan_get_filters(dev->filter, pfl->list, pfl->count);
	if (err < 0) {
		pfl->count = 0;
		return err;
	}

	pfl->count = err;
	return 0;
}

int pcanfd_ioctl_get_state(struct pcandev *dev, struct pcanfd_state *pfds)
{
#ifdef DEBUG
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif

	pfds->ver_major = PCAN_VERSION_MAJOR;
	pfds->ver_minor = PCAN_VERSION_MINOR;
	pfds->ver_subminor = PCAN_VERSION_SUBMINOR;

	pfds->tv_init = dev->init_timestamp;

	pfds->bus_state = dev->bus_state;
	pfds->device_id = dev->device_alt_num;

	pfds->open_counter = dev->nOpenPaths;
	pfds->filters_counter = pcan_get_filters_count(dev->filter);

	pfds->hw_type = dev->wType;
	pfds->channel_number = dev->nChannel;

	pfds->can_status = dev->wCANStatus;
	pfds->bus_load = dev->bus_load;

	pfds->tx_max_msgs = dev->writeFifo.nCount;
	pfds->tx_pending_msgs = dev->writeFifo.nStored;

	pfds->rx_max_msgs = dev->readFifo.nCount;
	pfds->rx_pending_msgs = dev->readFifo.nStored;
	pfds->tx_error_counter = dev->tx_error_counter;
	pfds->rx_error_counter = dev->rx_error_counter;
	pfds->tx_frames_counter = dev->tx_frames_counter;
	pfds->rx_frames_counter = dev->rx_frames_counter;

	pfds->host_time_ns = dev->time_sync.tv_ns;
	pfds->hw_time_ns = dev->time_sync.ts_us * 1000;

	return 0;
}

static int pcanfd_recv_msg(struct pcandev *dev, struct pcanfd_msg *pf,
		                                        struct pcan_udata *ctx)
{
	int err;

	do {
		/* if the device has been plugged out while waiting,
		 * or if any task is closing it */
		if (!dev->ucPhysicallyInstalled || !dev->nOpenPaths) {
			err = -ENODEV;
			break;
		}

		/* get data from fifo */
		err = pcan_fifo_get(&dev->readFifo, pf);
		if (err >= 0) {
			dev->wCANStatus &= ~CAN_ERR_OVERRUN;
#ifdef DEBUG_WAIT_RD
			pr_info("%s: %s(%u): still %u items in Rx queue\n",
				DEVICE_NAME, __func__, __LINE__,
				dev->readFifo.nStored);
#endif
			err = 0;
			break;
		}

		/* support nonblocking read if requested */
		if (ctx->open_flags & O_NONBLOCK) {
			err = -EAGAIN;
			break;
		}

		/* check whether the task is able to wait:
		 * Linux: always!
		 * RT: depends on the RT context of the running task */
		if (!pcan_task_can_wait()) {
			pr_info("%s: %s(%u): ABNORMAL task unable to wait!\n",
				DEVICE_NAME, __func__, __LINE__);
			err = -EAGAIN;
			break;
		}

		/* sleep until some msg is available. */
#ifdef DEBUG_WAIT_RD
		pr_info("%s: %s(%u): waiting for some msgs to read...\n",
			DEVICE_NAME, __func__, __LINE__);
#endif

		/* wait for some msg in the Rx queue.
		 *
		 * Note: ^C may occur while waiting. In RT, preemption can 
		 * schedule another task that might call close() while we're
		 * always waiting here.
		 * - If the event is destroyed by some other task, the below
		 *   call fails with err=-EIDRM(43).
		 * - if some other task deletes this wating task, this tasks
		 *   is first unblocked, thus err=-EINTR(4).*/
		err = pcan_event_wait(dev->in_event,
					!dev->ucPhysicallyInstalled ||
					!pcan_fifo_empty(&dev->readFifo));

#ifdef DEBUG_WAIT_RD
		pr_info("%s: end of waiting for rx fifo not empty: err=%d\n",
			DEVICE_NAME, err);
#endif

	} while (err >= 0);

	/* Note: ERESTARTSYS == 512 */
	return (err == -ERESTARTSYS) ? -EINTR : err;
}

int __pcan_dev_start_writing(struct pcandev *dev, struct pcan_udata *ctx)
{
	int err;

#ifdef DEBUG
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif
	err = dev->device_write(dev, ctx);
	return (err == -ENODATA) ? 0 : err;
}

static int pcanfd_send_msg(struct pcandev *dev, struct pcanfd_msg *pf,
					struct pcan_udata *ctx)
{
	int err;

#ifdef DEBUG
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif

	switch (pf->type) {

	case PCANFD_TYPE_CANFD_MSG:

		/* accept such messages for CAN-FD capable devices only */
		if ((dev->init_settings.flags & PCANFD_INIT_FD) &&
				(pf->data_len <= PCANFD_MAXDATALEN))
			break;

	case PCANFD_TYPE_CAN20_MSG:
		if (pf->data_len <= 8)
			break;
	default:
		pr_err("%s: trying to send invalid msg (type=%xh len=%d)\n",
				DEVICE_NAME, pf->type, pf->data_len);
		return -EBADMSG;
	}

	/* filter extended data if initialized to standard only
	 * SGR note: no need to wait for doing such test... */
	if (!(dev->bExtended) && ((pf->flags & PCANFD_MSG_EXT) ||
							(pf->id > 2047))) {

		pr_err("%s: trying to send ext msg %xh while not setup for\n",
				DEVICE_NAME, pf->id);
		return -EINVAL;
	}

	do {
		/* if the device has been plugged out while waiting,
		 * or if any task is closing it */
		if (!dev->ucPhysicallyInstalled || !dev->nOpenPaths) {
			err = -ENODEV;
			break;
		}

		/* no need to write in case of BUS_OFF */
		if (dev->bus_state == PCANFD_ERROR_BUSOFF) {
			err = -ENETDOWN;
			break;
		}

		/* put data into fifo */
		err = pcan_fifo_put(&dev->writeFifo, pf);
		if (err >= 0) {
			dev->wCANStatus &= ~CAN_ERR_XMTFULL;
#ifdef DEBUG
			pr_info("%s: %s(%u): still %u free items in Tx queue\n",
				DEVICE_NAME, __func__, __LINE__,
				dev->writeFifo.nCount - dev->writeFifo.nStored);
#endif
			err = 0;
			break;
		}

		/* remember the status */
		dev->wCANStatus |= CAN_ERR_XMTFULL;

		/* support nonblocking write if requested */
		if ((ctx->open_flags & O_NONBLOCK) || !pcan_task_can_wait()) {
			err = -EAGAIN;
			break;
		}

		/* sleep until space is available. */
#ifdef DEBUG_WAIT_WR
		pr_info(DEVICE_NAME
			": %s CAN%u waiting %u ms. for some free space "
			"to write...\n",
			dev->adapter->name, dev->nChannel+1,
			PCANFD_TIMEOUT_WAIT_FOR_WR);
#endif

		/* wait up to 100 ms. for some room in the Tx queue.
		 *
		 * some logs:
		 *
[ 7977.396005] pcan: pcanfd_send_msg(359): waiting for some free space to write...
...
[ 7977.400974] pcan: CAN1 lnk=1 signaling writing task...
...
[ 7977.400977] pcan: end of waiting for tx fifo not full: err=0
		 *
		 * Note: ^C may occur while waiting. In RT, preemption can 
		 * schedule another task that might call close() while we're
		 * always waiting here.
		 * - If the event is destroyed by some other task, the below
		 *   call fails with err=-EIDRM(43).
		 * - if some other task deletes this waiting task, this tasks
		 *   is first unblocked, thus err=-EINTR(4). */
		err = pcan_event_wait_timeout(dev->out_event,
					!dev->ucPhysicallyInstalled ||
					!pcan_fifo_full(&dev->writeFifo),
					PCANFD_TIMEOUT_WAIT_FOR_WR);

#ifdef DEBUG_WAIT_WR
		pr_info("%s: end of waiting for tx fifo not full: err=%d\n",
			DEVICE_NAME, err);
#endif

	} while (err >= 0);

	return (err == -ERESTARTSYS) ? -EINTR : err;
}

int pcanfd_ioctl_send_msg(struct pcandev *dev, struct pcanfd_msg *pf,
					struct pcan_udata *ctx)
{
	int err;

#ifdef PCAN_USES_OLD_TX_ENGINE_STATE
	err = pcanfd_send_msg(dev, pf, ctx);
	if (err)
		return err;

	/* if we just put the 1st message (=the fifo was empty), we can start
	 * writing on hardware if it is ready for doing this. */
	if (atomic_read(&dev->tx_engine_state) == TX_ENGINE_STOPPED)

		/* if can device ready to send, start writing */
		return __pcan_dev_start_writing(dev, ctx);

	return 0;
#else
	pcan_lock_irqsave_ctxt lck_ctx;

	err = pcanfd_send_msg(dev, pf, ctx);
	if (err)
		return err;

	pcan_lock_get_irqsave(&dev->isr_lock, lck_ctx);

	/* if we just put the 1st message (=the fifo was empty), we can start
	 * writing on hardware if it is ready for doing this. */
	if (dev->locked_tx_engine_state == TX_ENGINE_STOPPED) {

		//pr_info(DEVICE_NAME ": [%u] TX_ENGINE_STOPPED => start writing\n", task_pid_nr(current));

		/* if can device ready to send, start writing */
		err = __pcan_dev_start_writing(dev, ctx);
	}

	pcan_lock_put_irqrestore(&dev->isr_lock, lck_ctx);

	return err;
#endif
}

int pcanfd_ioctl_send_msgs(struct pcandev *dev, struct pcanfd_msgs *pl,
					struct pcan_udata *ctx)
{
	struct pcanfd_msg *pm;
	int err = 0, n = pl->count;

#ifdef DEBUG
	pr_info("%s: %s(count=%u)\n", DEVICE_NAME, __func__, n);
#endif

	pl->count = 0;

	pm = pl->list;
	for( ; pl->count < n; pl->count++) {
		err = pcanfd_send_msg(dev, pm, ctx);
		if (err)
			break;
		pm++;
	}

#ifdef DEBUG
	pr_info("%s: %s(count=%u): sent %u msgs\n",
			DEVICE_NAME, __func__, n, pl->count);
#endif

	/* if at least ONE message has been enqueued */
	if (pl->count) {

		/* if we just put the 1st messages (=the fifo was empty),
		 * we can start writing on hardware if it is ready for doing
		 * this. */
#ifdef PCAN_USES_OLD_TX_ENGINE_STATE
		if (atomic_read(&dev->tx_engine_state) == TX_ENGINE_STOPPED)

			/* if can device ready to send, start writing */
			return __pcan_dev_start_writing(dev, ctx);

		err = 0;
#else
		pcan_lock_irqsave_ctxt lck_ctx;

		err = 0;

		pcan_lock_get_irqsave(&dev->isr_lock, lck_ctx);

		if (dev->locked_tx_engine_state == TX_ENGINE_STOPPED)

			/* if can device ready to send, start writing */
			err = __pcan_dev_start_writing(dev, ctx);

		pcan_lock_put_irqrestore(&dev->isr_lock, lck_ctx);
#endif
	}

	return err;
}

int pcanfd_ioctl_send_msgs_nolock(struct pcandev *dev, struct pcanfd_msgs *pl,
					struct pcan_udata *ctx)
{
	struct pcanfd_msg *pm;
	int err = 0, n = pl->count;

#ifdef DEBUG
	pr_info("%s: %s(count=%u)\n", DEVICE_NAME, __func__, n);
#endif

	pl->count = 0;

	pm = pl->list;
	for( ; pl->count < n; pl->count++) {
		err = pcanfd_send_msg(dev, pm, ctx);
		if (err)
			break;
		pm++;
	}

#ifdef DEBUG
	pr_info("%s: %s(count=%u): sent %u msgs\n",
			DEVICE_NAME, __func__, n, pl->count);
#endif

	/* if at least ONE message has been enqueued */
	if (pl->count) {

		/* if we just put the 1st messages (=the fifo was empty),
		 * we can start writing on hardware if it is ready for doing
		 * this. */
		err = 0;

		if (dev->locked_tx_engine_state == TX_ENGINE_STOPPED)

			/* if can device ready to send, start writing */
			err = __pcan_dev_start_writing(dev, ctx);
	}

	return err;
}

int pcanfd_ioctl_recv_msg(struct pcandev *dev, struct pcanfd_msg *pmsgfd,
					struct pcan_udata *ctx)
{
	int err;

#ifdef DEBUG
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif

	err = pcanfd_recv_msg(dev, pmsgfd, ctx);

#ifdef DEBUG
	pr_info("%s: %s(): returns %d\n", DEVICE_NAME, __func__, err);
#endif
	return err;
}

int pcanfd_ioctl_recv_msgs(struct pcandev *dev, struct pcanfd_msgs *pl,
					struct pcan_udata *ctx)
{
	struct pcanfd_msg *pm;
	int err = 0, n = pl->count, saved_flags = ctx->open_flags;

#ifdef DEBUG
	pr_info("%s: %s(count=%u)\n", DEVICE_NAME, __func__, n);
#endif

	pm = pl->list;

	for (pl->count = 0; pl->count < n; pl->count++) {
		err = pcanfd_recv_msg(dev, pm, ctx);
		if (err)
			break;

		/* the task won't block anymore since at least one msg has been
		 * read. */
		ctx->open_flags |= O_NONBLOCK;

#if 0
		pl->count++;

		/* consider that reading messages that aren't CAN frames are
		 * enough important to break the messages list loop... */
		if (pm->type == PCANFD_TYPE_STATUS)
			break;
#endif
		pm++;
	}

	/* restore original flags asap */
	ctx->open_flags = saved_flags;

#ifdef DEBUG
	pr_info("%s: %s(count=%u): got %u msgs (err %d)\n",
			DEVICE_NAME, __func__, n, pl->count, err);
#endif
	return (pl->count > 0) ? 0 : err;
}
