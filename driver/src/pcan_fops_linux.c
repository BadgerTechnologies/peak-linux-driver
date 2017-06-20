/*****************************************************************************
 * Copyright (C) 2001-2007  PEAK System-Technik GmbH
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
 * Maintainer(s): Stephane Grosjean (s.grosjean@peak-system.com)
 *
 * Major contributions by:
 *                Klaus Hitschler (klaus.hitschler@gmx.de)
 *                Edouard Tisserant (edouard.tisserant@lolitech.fr) XENOMAI
 *                Laurent Bessard   (laurent.bessard@lolitech.fr)   XENOMAI
 *                Oliver Hartkopp   (oliver.hartkopp@volkswagen.de) socketCAN
 *
 * Contributions: Marcel Offermans (marcel.offermans@luminis.nl)
 *                Arno (a.vdlaan@hccnet.nl)
 *                John Privitera (JohnPrivitera@dciautomation.com)
 *****************************************************************************/

/* #define DEBUG */
/* #undef DEBUG */

/*****************************************************************************
 *
 * pcan_fops_linux.c - all file operation functions, exports only struct fops
 *
 * $Id: pcan_fops_linux.c $
 *
 *****************************************************************************/

#if 1
/* 2015-06-16 (SGr note):
 * seems that this function is useless...
 * Keep it a while, just for historical reason...
 */
#else
/*
 * wait until write fifo is empty, max time in msec
 */
void wait_for_empty_fifo(struct pcandev *dev, u32 mTime)
{
	u32 dwStart = get_mtime();

	/* not need to wait for anything if device not plugged! */
	if (!dev->ucPhysicallyInstalled)
		return;

	while (!atomic_read(&dev->hw_is_ready_to_send) &&
				((get_mtime() - dwStart) < mTime))
		schedule();

	/* force it */
	atomic_set(&dev->hw_is_ready_to_send, 1);
}
#endif

/* is called when the path is opened */
static int pcan_open(struct inode *inode, struct file *filep)
{
	struct pcandev *dev;
	struct pcan_udata *dev_priv;
	int _major = MAJOR(inode->i_rdev);
	int _minor = minor(inode->i_rdev);
	int err;

	DPRINTK(KERN_DEBUG "%s: %s(), major/minor = %d/%d\n",
			               DEVICE_NAME, __func__, _major, _minor);

	dev = pcan_search_dev(_major, _minor);
	if (!dev)
		return -ENODEV;

	/* create file object */
	dev_priv = pcan_malloc(sizeof(struct pcan_udata), GFP_KERNEL);
	if (!dev_priv) {
		pr_err("%s: %s(): memory allocation failed!\n",
				DEVICE_NAME, __func__);
		return -ENOMEM;
	}

	/* fill file object and init read and write method buffers */
	dev_priv->dev = dev;
	dev_priv->open_flags = filep->f_flags;
	dev_priv->filep = filep;

	if (filep->f_mode & FMODE_READ) {
		dev_priv->nReadRest = 0;
		dev_priv->nTotalReadCount = 0;
		dev_priv->pcReadPointer = dev_priv->pcReadBuffer;
	}

	if (filep->f_mode & FMODE_WRITE) {
		dev_priv->nWriteCount = 0;
		dev_priv->pcWritePointer = dev_priv->pcWriteBuffer;
	}

	filep->private_data = (void *)dev_priv;

	err = pcan_open_path(dev, dev_priv);
	if (err)
		pcan_free(dev_priv);

	return err;
}

static int pcan_release(struct inode *inode, struct file *filep)
{
	struct pcan_udata *dev_priv = (struct pcan_udata *)filep->private_data;

	DPRINTK(KERN_DEBUG "%s: %s()\n", DEVICE_NAME, __func__);

	/* free the associated irq and allocated memory */
	if (dev_priv) {
		if (dev_priv->dev)
			pcan_release_path(dev_priv->dev, dev_priv);

		pcan_free(dev_priv);
	}
	return 0;
}

/*
 * is called at user ioctl() with cmd = PCAN_INIT
 */
static int pcan_ioctl_init(struct pcandev *dev, TPCANInit __user *pi)
{
	TPCANInit init;
	struct pcanfd_init init_fd;
	int err;

#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif

	err = copy_from_user(&init, pi, sizeof(init));
	if (err) {
		pr_err("%s: %s(): copy_from_user() failure\n",
				DEVICE_NAME, __func__);
		return -EFAULT;
	}

	return pcanfd_ioctl_set_init(dev, pcan_init_to_fd(&init_fd, &init));
}

/*
 * is called at user ioctl() with cmd = PCAN_WRITE_MSG
 */
static int pcan_ioctl_write(struct pcandev *dev, TPCANMsg __user *usr,
						struct pcan_udata *dev_priv)
{
	struct pcanfd_msg cf;
	TPCANMsg msg;
	int err;

#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif

	/* get from user space */
	if (copy_from_user(&msg, usr, sizeof(msg))) {
		pr_err("%s: %s(): copy_from_user() failure\n",
				DEVICE_NAME, __func__);
		err = -EFAULT;
		goto fail;
	}

	/* do some minimal (but mandatory!) check */
	if (msg.LEN > 8) {
		pr_err("%s: trying to send msg %xh  with invalid data len %d\n",
				DEVICE_NAME, msg.ID, msg.LEN);
		err = -EINVAL;
		goto fail;
	}

	/* convert old-style TPCANMsg into new-style struct pcanfd_msg */
	err = pcanfd_ioctl_send_msg(dev, pcan_msg_to_fd(&cf, &msg), dev_priv);
	if (err)
		goto fail;

	return 0;

fail:
#ifdef DEBUG
        pr_err("%s: failed to write CAN frame (err %d)\n", DEVICE_NAME, err);
#endif
	return err;
}

/*
 * is called at user ioctl() with cmd = PCAN_READ_MSG
 */
static int pcan_ioctl_read(struct pcandev *dev, TPCANRdMsg __user *usr,
						struct pcan_udata *dev_priv)
{
	struct pcanfd_msg msgfd;
	TPCANRdMsg msg;
	int err;

#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif

	do {
		err = pcanfd_ioctl_recv_msg(dev, &msgfd, dev_priv);
		if (err)
			return err;

		if (pcan_is_fd(&msgfd)) {
			pr_err("%s: CAN-FD frame discarded "
				"(CAN 2.0 application)\n", DEVICE_NAME);
			err = -EINVAL;
		}
	} while (err);

	if (copy_to_user(usr, pcan_fd_to_msg(&msg, &msgfd), sizeof(*usr)))
		err = -EFAULT;

	return err;
}

/*
 * is called at user ioctl() with cmd = PCAN_GET_STATUS
 */
static int pcan_ioctl_status(struct pcandev *dev, TPSTATUS __user *status)
{
	TPSTATUS local;
	int err = 0;

#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif

	pcan_ioctl_status_common(dev, &local);

	if (copy_to_user(status, &local, sizeof(local))) {
		err = -EFAULT;
		goto fail;
	}

	dev->wCANStatus = 0;
	dev->nLastError = 0;

fail:
	return err;
}

/*
 * is called at user ioctl() with cmd = PCAN_GET_EXT_STATUS
 */
int pcan_ioctl_extended_status(struct pcandev *dev,
						TPEXTENDEDSTATUS __user *status)
{
	TPEXTENDEDSTATUS local;
	int err = 0;

#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif

	pcan_ioctl_extended_status_common(dev, &local);

	if (copy_to_user(status, &local, sizeof(local))) {
		err = -EFAULT;
		goto fail;
	}

	dev->wCANStatus = 0;
	dev->nLastError = 0;

fail:
	return err;
}

/*
 * is called at user ioctl() with cmd = PCAN_DIAG
 */
static int pcan_ioctl_diag(struct pcandev *dev, TPDIAG __user *diag)
{
	TPDIAG local;
	int err = 0;

#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif

	pcan_ioctl_diag_common(dev, &local);

	if (copy_to_user(diag, &local, sizeof(local)))
		err = -EFAULT;

	return err;
}

/*
 * get BTR0BTR1 init values
 */
static int pcan_ioctl_BTR0BTR1(struct pcandev *dev, TPBTR0BTR1 __user *BTR0BTR1)
{
	TPBTR0BTR1 local;
	int err = 0;

#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif

	if (copy_from_user(&local, BTR0BTR1, sizeof(local))) {
		err = -EFAULT;
		goto fail;
	}

	/* this does not influence hardware settings, only BTR0BTR1 values
	 * are calculated */
	local.wBTR0BTR1 = sja1000_bitrate(local.dwBitRate);
	if (!local.wBTR0BTR1) {
		err = -EFAULT;
		goto fail;
	}

	if (copy_to_user(BTR0BTR1, &local, sizeof(*BTR0BTR1)))
		err = -EFAULT;

fail:
	return err;
}

/*
 * add a message filter_element into the filter chain or delete all
 * filter_elements
 */
static int pcan_ioctl_msg_filter(struct pcandev *dev,
						TPMSGFILTER __user *filter)
{
	TPMSGFILTER local_filter;

#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif

	/* filter == NULL -> delete the filter_elements in the chain */
	if (!filter) {
		pcan_delete_filter_all(dev->filter);
		return 0;
	}

	if (copy_from_user(&local_filter, filter, sizeof(local_filter)))
		return -EFAULT;

	return pcan_add_filter(dev->filter, local_filter.FromID,
				local_filter.ToID, local_filter.MSGTYPE);
}

/*
 * set or get extra parameters from the devices
 */
static int pcan_ioctl_extra_parameters(struct pcandev *dev,
						TPEXTRAPARAMS __user *params)
{
	TPEXTRAPARAMS local;
	int err = 0;

#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif

	if (copy_from_user(&local, params, sizeof(local))) {
		pr_err("%s: %s(): copy_from_user() failure\n",
				DEVICE_NAME, __func__);
		err = -EFAULT;
		goto fail;
	}

	if (!dev->device_params) {
		pr_err("%s: %s(): NULL device_params address\n",
				DEVICE_NAME, __func__);
		err = -EINVAL;
		goto fail;
	}

	err = dev->device_params(dev, &local);
	if (err)
		goto fail;

	if (copy_to_user(params, &local, sizeof(*params)))
		err = -EFAULT;

fail:
	return err;
}

/*
 * is called at user ioctl() call
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
int pcan_ioctl(struct inode *inode,
		struct file *filep, unsigned int cmd, unsigned long arg)
#else
long pcan_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
#endif
{
	struct pcan_udata *dev_priv = (struct pcan_udata *)filep->private_data;
	struct pcandev *dev = dev_priv->dev;
	struct pcanfd_init fdi;
	struct pcanfd_state fds;
	struct pcanfd_msg msgfd;
	struct pcanfd_msgs msgfdl, *pl;
	struct pcanfd_msgs __user * upl;
	int err, l;

#ifdef DEBUG
	pr_info("%s: %s(CAN%u, cmd=%u)\n",
		DEVICE_NAME, __func__, dev->nChannel+1, cmd);
#endif

	/* check whether this device is always linked. */
	if (!pcan_is_device_in_list(dev))
		return -ENODEV;

	/* if the device is plugged out */
	if (!dev->ucPhysicallyInstalled)
		return -ENODEV;

	switch (cmd) {
	case PCAN_INIT:
		err = pcan_ioctl_init(dev, (TPCANInit __user *)arg);
		break;
	case PCAN_READ_MSG:
		/* support blocking and nonblocking IO */
		err = pcan_ioctl_read(dev, (TPCANRdMsg __user *)arg, dev_priv);
		break;
	case PCAN_WRITE_MSG:
		/* support blocking and nonblocking IO */
		err = pcan_ioctl_write(dev, (TPCANMsg __user *)arg, dev_priv);
		break;
	case PCAN_GET_STATUS:
		err = pcan_ioctl_status(dev, (TPSTATUS __user *)arg);
		break;
	case PCAN_GET_EXT_STATUS:
		err = pcan_ioctl_extended_status(dev,
						(TPEXTENDEDSTATUS __user *)arg);
		break;
	case PCAN_DIAG:
		err = pcan_ioctl_diag(dev, (TPDIAG __user *)arg);
		break;
	case PCAN_BTR0BTR1:
		err = pcan_ioctl_BTR0BTR1(dev, (TPBTR0BTR1 __user *)arg);
		break;
	case PCAN_MSG_FILTER:
		err = pcan_ioctl_msg_filter(dev, (TPMSGFILTER *)arg);
		break;
	case PCAN_EXTRA_PARAMS:
		err = pcan_ioctl_extra_parameters(dev, (TPEXTRAPARAMS *)arg);
		break;

	/* CAN-FD new API */
	case PCANFD_SET_INIT:
		err = copy_from_user(&fdi, (struct pcanfd_init __user *)arg,
				     sizeof(fdi));
		if (err)
			return -EFAULT;

		err = pcanfd_ioctl_set_init(dev, &fdi);
		break;

	case PCANFD_GET_INIT:
		err = pcanfd_ioctl_get_init(dev, &fdi);
		if (err)
			break;

		err = copy_to_user((struct pcanfd_init __user *)arg, &fdi,
				   sizeof(fdi));
		if (err)
			return -EFAULT;
		break;

	case PCANFD_GET_STATE:
		err = pcanfd_ioctl_get_state(dev, &fds);
		if (err)
			break;

		err = copy_to_user((struct pcanfd_state __user *)arg, &fds,
				   sizeof(fds));
		if (err)
			return -EFAULT;

		break;

#ifdef PCANFD_ADD_FILTER
	case PCANFD_ADD_FILTER:
		if (arg) {
			struct pcanfd_msg_filter mf;

			err = copy_from_user(&mf,
					(struct pcanfd_msg_filter __user *)arg,
					sizeof(mf));
			if (err)
				return -EFAULT;

			err = pcanfd_ioctl_add_filter(dev, &mf);
		} else {
			err = pcanfd_ioctl_add_filter(dev, NULL);
		}
		break;
#endif

	case PCANFD_ADD_FILTERS:
		if (arg) {
			struct pcanfd_msg_filters mfl, *pfl;
			struct pcanfd_msg_filters  __user *upfl;

			upfl = (struct pcanfd_msg_filters __user *)arg;
			err = copy_from_user(&mfl, upfl, sizeof(mfl));
			if (err) {
				pr_err("%s: %s(): copy_from_user() failure\n",
					DEVICE_NAME, __func__);
				return -EFAULT;
			}

			if (!mfl.count)
				return 0;

			l = sizeof(mfl) +
				mfl.count * sizeof(struct pcanfd_msg_filter);
			pfl = pcan_malloc(l, GFP_KERNEL);
			if (!pfl) {
				pr_err("%s: failed to alloc filter list\n",
						DEVICE_NAME);
				return -ENOMEM;
			}

			if (copy_from_user(pfl, upfl, l)) {
				pcan_free(pfl);
				pr_err("%s: %s(): copy_from_user() failure\n",
					DEVICE_NAME, __func__);
				return -EFAULT;
			}

			err = pcanfd_ioctl_add_filters(dev, pfl);

			pcan_free(pfl);
		} else {
			err = pcanfd_ioctl_add_filters(dev, NULL);
		}
		break;

	case PCANFD_GET_FILTERS:
		if (arg) {
			struct pcanfd_msg_filters mfl, *pfl;
			struct pcanfd_msg_filters  __user *upfl;

			upfl = (struct pcanfd_msg_filters __user *)arg;
			err = copy_from_user(&mfl, upfl, sizeof(mfl));
			if (err)
				return -EFAULT;

			if (!mfl.count)
				return 0;

			l = sizeof(mfl) +
				mfl.count * sizeof(struct pcanfd_msg_filter);
			pfl = pcan_malloc(l, GFP_KERNEL);
			if (!pfl) {
				pr_err("%s: failed to alloc filter list\n",
						DEVICE_NAME);
				return -ENOMEM;
			}

			pfl->count = mfl.count;
			err = pcanfd_ioctl_get_filters(dev, pfl);

			/* copy the count and the filter received */
			l = sizeof(mfl) +
				pfl->count * sizeof(struct pcanfd_msg_filter);

			if (copy_to_user(upfl, pfl, l)) {
				pr_err("%s: %s(): copy_to_user() failure\n",
						DEVICE_NAME, __func__);
				err = -EFAULT;
			}

			pcan_free(pfl);
		} else {
			err = pcanfd_ioctl_get_filters(dev, NULL);
		}
		break;

	case PCANFD_SEND_MSG:
		err = copy_from_user(&msgfd, (struct pcanfd_msg __user *)arg,
				     sizeof(msgfd));
		if (err)
			return -EFAULT;

		err = pcanfd_ioctl_send_msg(dev, &msgfd, dev_priv);
		break;

	case PCANFD_RECV_MSG:
		err = pcanfd_ioctl_recv_msg(dev, &msgfd, dev_priv);
		if (err)
			break;

		err = copy_to_user((struct pcanfd_msg __user *)arg, &msgfd,
				   sizeof(msgfd));
		if (err)
			return -EFAULT;
		break;

	case PCANFD_SEND_MSGS:
		upl = (struct pcanfd_msgs __user *)arg;
		err = copy_from_user(&msgfdl, upl, sizeof(msgfdl));
		if (err) {
			pr_err("%s: %s(): copy_from_user() failure\n",
				DEVICE_NAME, __func__);
			return -EFAULT;
		}

		/* ok. Nothing to send. So nothing done. Perfect. */
		if (!msgfdl.count)
			return 0;

		l = sizeof(msgfdl) + msgfdl.count * sizeof(msgfd);
		pl = pcan_malloc(l, GFP_KERNEL);
		if (!pl) {
			pr_err("%s: %s(): failed to alloc msgs list\n",
				DEVICE_NAME, __func__);
			return -ENOMEM;
		}

		if (copy_from_user(pl, upl, l)) {
			pr_err("%s: %s(): copy_from_user() failure\n",
				DEVICE_NAME, __func__);
			pcan_free(pl);
			return -EFAULT;
		}

		err = pcanfd_ioctl_send_msgs(dev, pl, dev_priv);

		/* copy the count of msgs really sent (= pl->count) */
		if (copy_to_user(upl, pl, sizeof(msgfdl))) {
			pr_err("%s: %s(): copy_from_user() failure\n",
				DEVICE_NAME, __func__);
			err = -EFAULT;
		}

#if 0
		/* next, copy the msgs that have not been sent (if any) */
		else {

			/* get the count of msgs *NOT* sent */
			l = (msgfdl.count - pl->count) * sizeof(msgfd);

			/* move them in front of the list */
			if (copy_to_user(upl->list, pl->list + pl->count, l))
				err = -EFAULT;
		}
#endif
		pcan_free(pl);

		break;

	case PCANFD_RECV_MSGS:
		upl = (struct pcanfd_msgs __user *)arg;
		err = copy_from_user(&msgfdl, upl, sizeof(msgfdl));
		if (err) {
			pr_err("%s: %s(): copy_from_user() failure\n",
				DEVICE_NAME, __func__);
			return -EFAULT;
		}

		/* ok! no room for saving rcvd msgs!? Thus, nothing returned */
		if (!msgfdl.count)
			return 0;

		l = sizeof(msgfdl) + msgfdl.count * sizeof(msgfd);
		pl = pcan_malloc(l, GFP_KERNEL);
		if (!pl) {
			pr_err("%s: failed to alloc msgs list\n", DEVICE_NAME);
			return -ENOMEM;
		}

		pl->count = msgfdl.count;
		err = pcanfd_ioctl_recv_msgs(dev, pl, dev_priv);

		/* copy the count and the msgs received */
		l = sizeof(msgfdl) + pl->count * sizeof(msgfd);
		if (copy_to_user(upl, pl, l)) {
			pr_err("%s: %s(): copy_to_user() failure\n",
				DEVICE_NAME, __func__);
			err = -EFAULT;
		}

		pcan_free(pl);

		break;

	default:
		pr_err("%s: %s(cmd=%u): unsupported cmd\n",
			DEVICE_NAME, __func__, cmd);
		err = -ENOTTY;
		break;
	}

#ifdef DEBUG
	pr_info("%s: %s(CAN%u, cmd=%u): returns %d\n",
		DEVICE_NAME, __func__, dev->nChannel+1, cmd, err);
#endif

	return err;
}

/*
 * is called when read from the path
 */
static ssize_t pcan_read(struct file *filep, char *buf, size_t count,
								loff_t *f_pos)
{
	int err;
	int len = 0;
	struct pcanfd_msg f;
	struct pcan_udata *dev_priv = (struct pcan_udata *)filep->private_data;
	struct pcandev *dev = dev_priv->dev;

#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif

	/* check whether this device is always linked. */
	if (!pcan_is_device_in_list(dev))
		return -ENODEV;

	/* if the device is plugged out */
	if (!dev->ucPhysicallyInstalled)
		return -ENODEV;

	if (dev_priv->nReadRest <= 0) {

		err = pcanfd_ioctl_recv_msg(dev, &f, dev_priv);
		if (err)
			return err;

		dev_priv->nReadRest =
			pcan_make_output(dev_priv->pcReadBuffer, &f);
		dev_priv->pcReadPointer = dev_priv->pcReadBuffer;
	}

	/* give the data to the user */
	if (count > dev_priv->nReadRest) {
		/* put all data to user */
		len = dev_priv->nReadRest;
		dev_priv->nReadRest = 0;
		if (copy_to_user(buf, dev_priv->pcReadPointer, len)) {
			pr_err("%s: %s(): copy_to_user() failure\n",
				DEVICE_NAME, __func__);
			return -EFAULT;
		}
		dev_priv->pcReadPointer = dev_priv->pcReadBuffer;
	} else {
		/* put only partial data to user */
		len = count;
		dev_priv->nReadRest -= count;
		if (copy_to_user(buf, dev_priv->pcReadPointer, len)) {
			pr_err("%s: %s(): copy_to_user() failure\n",
				DEVICE_NAME, __func__);
			return -EFAULT;
		}
		dev_priv->pcReadPointer =
				(u8 *)((u8*)dev_priv->pcReadPointer + len);
	}

	*f_pos += len;
	dev_priv->nTotalReadCount += len;

	return len;
}

static int pcan_write_line(struct file *filep, u8 *ptr, size_t count)
{
	struct pcan_udata *dev_priv = (struct pcan_udata *)filep->private_data;
	struct pcandev *dev = dev_priv->dev;
	u32 amount = (u32)(dev_priv->pcWritePointer - ptr - 1);
	u32 offset = (u32)(ptr - dev_priv->pcWriteBuffer + 1);
	int err;

	if ((amount > WRITEBUFFER_SIZE) || (offset > WRITEBUFFER_SIZE)) {
#ifdef __LP64__
#warning "Compiling for __LP64__"
#endif
		printk(KERN_ERR "%s: %s() fault: %zu %u, %u: \n",
			DEVICE_NAME, __func__, count, amount, offset);
		return -EFAULT;
	}

	if (pcan_parse_input_idle(dev_priv->pcWriteBuffer)) {
		struct pcanfd_msg msgfd;

		if (pcan_parse_input_message(dev_priv->pcWriteBuffer, &msgfd)) {
			struct pcanfd_init fdi;

			err = pcan_parse_input_init(dev_priv->pcWriteBuffer,
									&fdi);
			if (err)
				return err;
#if 0
			DPRINTK(KERN_DEBUG
				"%s: ***** Init 0x%04x 0x%02x 0x%02x\n",
				DEVICE_NAME, Init.wBTR0BTR1, Init.ucCANMsgType,
				Init.ucListenOnly);
#endif
			/* init the associated chip and the fifos again
			 * with new parameters
			 */
			err = pcanfd_ioctl_set_init(dev, &fdi);
			if (err)
				return err;
		} else {
#if 0 // ------- print out message, begin -----------
			int i = 0;

			DPRINTK(KERN_DEBUG "%s: *** 0x%08x 0x%02x %d . ",
				DEVICE_NAME, f.id, f.flags, f.data_len);

			while (i++ < f.data_len)
				DPRINTK(KERN_DEBUG "0x%02x ", f.data[i]);

			DPRINTK(KERN_DEBUG " ***\n");
#endif // ------- print out message, end ------------

			err = pcanfd_ioctl_send_msg(dev, &msgfd, dev_priv);
			if (err)
				if (err != -ENODATA)
					return err;
		}
	}

	/* move rest of amount data in buffer offset steps to left */
	memmove(dev_priv->pcWriteBuffer, ptr + 1, amount);
	dev_priv->pcWritePointer -= offset;

	return 0;
}

static ssize_t pcan_write(struct file *filep, const char *buf, size_t count,
								loff_t *f_pos)
{
	struct pcan_udata *dev_priv = (struct pcan_udata *)filep->private_data;
	struct pcandev *dev = dev_priv->dev;
	int err = 0;
	u32 dwRest;
	u8 *ptr;

#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif

	/* check whether this device is always linked. */
	if (!pcan_is_device_in_list(dev))
		return -ENODEV;

	/* if the device is plugged out	*/
	if (!dev->ucPhysicallyInstalled)
		return -ENODEV;

	/* calculate remaining buffer space */
	dwRest = WRITEBUFFER_SIZE -
		(dev_priv->pcWritePointer - dev_priv->pcWriteBuffer); /* nRest > 0! */
	count  = (count > dwRest) ? dwRest : count;

	if (copy_from_user(dev_priv->pcWritePointer, buf, count)) {
		pr_err("%s: %s(): copy_from_user() failure\n",
				DEVICE_NAME, __func__);
		return -EFAULT;
	}

	/* adjust working pointer to end */
	dev_priv->pcWritePointer += count;

	/* iterate search blocks ending with '\n' */
	while (1) {

		/* search first '\n' from begin of buffer */
		ptr = dev_priv->pcWriteBuffer;
		while ((*ptr != '\n') && (ptr < dev_priv->pcWritePointer))
			ptr++;

		/* parse input when a CR was found */
		if ((*ptr == '\n') && (ptr < dev_priv->pcWritePointer)) {

			err = pcan_write_line(filep, ptr, count);
			if (err)
				return err;
		} else
			break; /* no CR found */
	}

	if (dev_priv->pcWritePointer >=
				(dev_priv->pcWriteBuffer + WRITEBUFFER_SIZE)) {
		/* reject all */
		dev_priv->pcWritePointer = dev_priv->pcWriteBuffer;
		return -EFAULT;
	}

	return count;
}

/*
 * is called at poll or select
 */
static unsigned int pcan_poll(struct file *filep, poll_table *wait)
{
	struct pcan_udata *dev_priv = (struct pcan_udata *)filep->private_data;
	struct pcandev *dev = dev_priv->dev;
	unsigned int mask = 0;

#if 0//def DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif
	pcan_mutex_lock(&dev->mutex);

	/* if the device is plugged out	*/
	if (dev->ucPhysicallyInstalled) {

		poll_wait(filep, &dev->in_event, wait);
		poll_wait(filep, &dev->out_event, wait);

		/* return on ops that could be performed without blocking */
		if (!pcan_fifo_empty(&dev->readFifo))
			mask |= POLLIN | POLLRDNORM;

		if (!pcan_fifo_full(&dev->writeFifo))
			mask |= POLLOUT | POLLWRNORM;
	}

	pcan_mutex_unlock(&dev->mutex);

	return mask;
}

/*
 * this structure is used in init_module(void)
 */
struct file_operations pcan_fops = {
	/*
	 * marrs:  added owner, which is used to implement a use count that
	 *         disallows rmmod calls when the driver is still in use (as
	 *         suggested by Duncan Sands on the linux-kernel mailinglist)
	 */
	owner:      THIS_MODULE,
	open:       pcan_open,
	release:    pcan_release,
	read:       pcan_read,
	write:      pcan_write,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
	ioctl:      pcan_ioctl,
#else
	unlocked_ioctl: pcan_ioctl,
#endif
	poll:       pcan_poll,
};
