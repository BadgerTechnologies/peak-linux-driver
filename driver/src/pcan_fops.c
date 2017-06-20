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
 * Maintainer(s): Klaus Hitschler (klaus.hitschler@gmx.de)
 *
 * Major contributions by:
 *                Edouard Tisserant (edouard.tisserant@lolitech.fr) XENOMAI
 *                Laurent Bessard   (laurent.bessard@lolitech.fr)   XENOMAI
 *                Oliver Hartkopp   (oliver.hartkopp@volkswagen.de) socketCAN
 *
 * Contributions: Marcel Offermans (marcel.offermans@luminis.nl)
 *                Arno (a.vdlaan@hccnet.nl)
 *                John Privitera (JohnPrivitera@dciautomation.com)
 *****************************************************************************/

/*****************************************************************************
 *
 * pcan_fops.c - all file operation functions, exports only struct fops
 *
 * $Id: pcan_fops.c 1237 2016-06-01 09:48:13Z stephane $
 *
 *****************************************************************************/
/* #define DEBUG */
/* #undef DEBUG */

#include "src/pcan_common.h"	/* must always be the 1st include */

#include <linux/kernel.h>   // DPRINTK()
#include <linux/slab.h>     // pcan_malloc()
#include <linux/fs.h>       // everything...
#include <linux/errno.h>    // error codes
#include <linux/types.h>    // size_t
#include <linux/proc_fs.h>  // proc
#include <linux/fcntl.h>    // O_ACCMODE
#include <linux/pci.h>      // all about pci
#include <linux/capability.h> // all about restrictions
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
#include <asm/system.h>     // cli(), *_flags
#endif
#include <asm/uaccess.h>    // copy_...
#include <linux/delay.h>    // mdelay()
#include <linux/poll.h>     // poll() and select()

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,13)
#include <linux/moduleparam.h>
#endif

#include "src/pcan_main.h"
#include "src/pcan_pci.h"
#include "src/pcan_isa.h"
#include "src/pcan_dongle.h"
#include "src/pcan_sja1000.h"
#include "src/pcan_fifo.h"
#include "src/pcan_fops.h"
#include "src/pcan_parse.h"
#include "src/pcan_usb.h"
#include "src/pcan_filter.h"

#include "src/pcanfd_core.h"

#ifndef MODULE_LICENSE
#define MODULE_LICENSE(x)
#endif
#ifndef MODULE_VERSION
#define MODULE_VERSION(x)
#endif

MODULE_AUTHOR("s.grosjean@peak-system.com");
MODULE_AUTHOR("klaus.hitschler@gmx.de");
MODULE_DESCRIPTION("Driver for PEAK-System CAN interfaces");
MODULE_VERSION(CURRENT_RELEASE);
MODULE_SUPPORTED_DEVICE("PCAN-ISA, PCAN-PC/104, PCAN-Dongle, PCAN-PCI(e), PCAN-ExpressCard, PCAN-PCCard, PCAN-USB (compilation dependent)");
MODULE_LICENSE("GPL");

#if defined(module_param_array) && LINUX_VERSION_CODE > KERNEL_VERSION(2,6,13)
extern char *type[8];
extern ushort io[8];
extern char irq[8];
extern ushort btr0btr1;
extern char *bitrate;
extern char *dbitrate;
extern char *assign;

module_param_array(type, charp, NULL, 0444);
module_param_array(io, ushort, NULL, 0444);
module_param_array(irq, byte,  NULL, 0444);
module_param(btr0btr1, ushort, 0444);
module_param(bitrate, charp, 0444);
module_param(dbitrate, charp, 0444);
module_param(assign, charp, 0444);
#else
MODULE_PARM(type, "0-8s");
MODULE_PARM(io, "0-8h");
MODULE_PARM(irq, "0-8b");
MODULE_PARM(btr0btr1, "h");
MODULE_PARM(bitrate, "s");
MODULE_PARM(dbitrate, "s");
MODULE_PARM(assign, "s");
#endif

MODULE_PARM_DESC(type, "type of PCAN interface (isa, sp, epp)");
MODULE_PARM_DESC(io, "io-port address for either PCAN-ISA, PC/104 or Dongle");
MODULE_PARM_DESC(irq, "interrupt number for either PCAN-ISA, PC/104 or Dongle");
MODULE_PARM_DESC(btr0btr1, "initial bitrate (BTR0BTR1 format) for all channels");
MODULE_PARM_DESC(bitrate, "initial nominal bitrate for all channels");
MODULE_PARM_DESC(dbitrate, "initial data bitrate for all CAN-FD channels");
MODULE_PARM_DESC(assign, "assignment for netdevice names to CAN devices");

#if defined(LINUX_24)
EXPORT_NO_SYMBOLS;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,18) || LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define minor(x)	MINOR(x)
#endif

/* opens a data path with a pcan device.
 * This function is called by:
 * - pcan_open()
 * - pcan_open_rt()
 * - pcan_netdev_open()
 */
int pcan_open_path(struct pcandev *dev, struct pcan_udata *irq_arg)
{
	int err = 0;

	pcan_mutex_lock(&dev->mutex);

#ifdef DEBUG
	pr_info("%s: %s(): minor=%d, opened path=%d\n",
		DEVICE_NAME, __func__, dev->nMinor, dev->nOpenPaths);
#endif
	/* only the 1st open to this device makes a default init  */
	if (dev->nOpenPaths) {
		dev->nOpenPaths++;
		goto lbl_unlock_exit;
	}

	err = pcanfd_dev_reset(dev);
	if (err)
		goto lbl_unlock_exit;

	/* open the interface special parts */
	if (dev->open) {
		err = dev->open(dev);
		if (err) {
			pr_err("%s: can't open interface specific!\n",
				DEVICE_NAME);
			goto lbl_unlock_exit;
		}
	}

	/* special handling: probe here only for dongle devices,
	 * because connect after init is possible */
	switch (dev->wType) {

	case HW_DONGLE_SJA:
	case HW_DONGLE_SJA_EPP:

		/* no usb here, generic sja1000 call for dongle */
		err = sja1000_probe(dev);
		if (err) {
			pr_err("%s: %s-dongle minor %d (io=0x%04x, irq=%d) "
				"not found (err %d)\n",
				DEVICE_NAME, dev->type, dev->nMinor,
				dev->dwPort, dev->wIrq, err);
			dev->release(dev);
			goto lbl_unlock_exit;
		}
		break;
	}

	/* initialize here the sync mechanism between ISR and fifo */
	pcan_event_init(&dev->in_event, 0);
	pcan_event_init(&dev->out_event, 0);

	/* install irq */
	if (dev->req_irq) {
		err = dev->req_irq(dev, irq_arg);
		if (err) {
			pr_err("%s: can't request irq from device (err %d)\n",
				DEVICE_NAME, err);
			goto lbl_unlock_exit;
		}
	}

	dev->opened_index = dev->adapter->opened_count++;

	/* inc nOpenPath BEFORE calling _open() because some devices (USB for
	 * ex) may start sending notifications (interrupt based) before
	 * returning from the function. */
	dev->nOpenPaths++;
	pcan_mutex_unlock(&dev->mutex);

	/* open the device itself */
	err = pcanfd_dev_open(dev, &dev->init_settings);
	if (!err)
		return 0;

	pcan_mutex_lock(&dev->mutex);

	dev->nOpenPaths--;
	pr_err("%s: can't open device hardware itself (err %d)!\n",
		DEVICE_NAME, err);

lbl_unlock_exit:
	pcan_mutex_unlock(&dev->mutex);

	return err;
}

/* find the pcandev according to given major,minor numbers
 * returns NULL pointer in the case of no success
 */
struct pcandev* pcan_search_dev(int major, int minor)
{
	struct pcandev *dev = (struct pcandev *)NULL;
	struct list_head *ptr;

#ifdef DEBUG
	pr_info("%s: %s(): major,minor=%d,%d\n",
		DEVICE_NAME, __func__, major, minor);
#endif

#ifdef PCAN_USB_PCAN_SYSFS
#ifndef PCAN_USB_DONT_REGISTER_DEV
	/* translate USB devices into pcan devices */
	if ((minor >= PCAN_USB_MINOR_BASE) && (minor < PCCARD_MINOR_BASE)) {
		major = USB_MAJOR;
		minor -= PCAN_USB_MINOR_BASE;
	}
#endif
#endif
	pcan_mutex_lock(&pcan_drv.devices_lock);

	if (list_empty(&pcan_drv.devices)) {
		pcan_mutex_unlock(&pcan_drv.devices_lock);
		DPRINTK(KERN_DEBUG "%s: no devices to select from!\n",
			DEVICE_NAME);
		return NULL;
	}

	/* loop through my devices */
	for (ptr = pcan_drv.devices.next; ptr != &pcan_drv.devices;
							ptr = ptr->next) {
		dev = (struct pcandev *)ptr;

		if (dev->nMajor == major)
			if (dev->nMinor == minor)
				break;
	}

	pcan_mutex_unlock(&pcan_drv.devices_lock);

	if (ptr == &pcan_drv.devices) {
		printk(KERN_DEBUG "%s: didn't find any pcan devices (%d,%d)\n",
			DEVICE_NAME, major, minor);
		return NULL;
	}

	return dev;
}

/* is called by pcan_release() and pcan_netdev_close() */
void pcan_release_path(struct pcandev *dev, struct pcan_udata *irq_arg)
{
	int err;

	pcan_mutex_lock(&dev->mutex);

#ifdef DEBUG
	pr_info("%s: %s(): minor=%d, path=%d\n",
		DEVICE_NAME, __func__, dev->nMinor, dev->nOpenPaths);
#endif
	/* if it's the last release: init the chip for non-intrusive operation*/
	if (dev->nOpenPaths > 1) {
		dev->nOpenPaths--;

	} else if (dev->nOpenPaths == 1)  {

		/* tell the world that the device is being closed now */
		dev->nOpenPaths = 0;

		pcan_mutex_unlock(&dev->mutex);

		if (pcan_task_can_wait()) {
#ifdef DEBUG
			pr_info("%s: preparing to wait: "
				"ucPhysicallyInstalled=%u fifo_empty=%u "
				"tx_engine_state=%d to=%d\n",
				DEVICE_NAME, dev->ucPhysicallyInstalled,
				pcan_fifo_empty(&dev->writeFifo),
#ifdef PCAN_USES_OLD_TX_ENGINE_STATE
				atomic_read(&dev->tx_engine_state),
#else
				dev->locked_tx_engine_state,
#endif
				MAX_WAIT_UNTIL_CLOSE);
#endif

			/* Now, wait for the tx fifo to be empty and for the 
			 * tx engine of the hardware to finish... */
			err = pcan_event_wait_timeout(dev->out_event,
					!dev->ucPhysicallyInstalled ||
					(pcan_fifo_empty(&dev->writeFifo) &&
#ifdef PCAN_USES_OLD_TX_ENGINE_STATE
					atomic_read(&dev->tx_engine_state) !=
#else
					dev->locked_tx_engine_state !=
#endif
							TX_ENGINE_STARTED),
					MAX_WAIT_UNTIL_CLOSE);

#ifdef DEBUG
			pr_info("%s: finished waiting: "
				"ucPhysicallyInstalled=%u fifo_empty=%u "
				"tx_engine_state=%d err=%d\n",
				DEVICE_NAME, dev->ucPhysicallyInstalled,
				pcan_fifo_empty(&dev->writeFifo),
#ifdef PCAN_USES_OLD_TX_ENGINE_STATE
				atomic_read(&dev->tx_engine_state),
#else
				dev->locked_tx_engine_state,
#endif
				err);
#endif
		}

		/* release the device itself */
		if (dev->device_release)
			dev->device_release(dev);

		dev->flags &= ~PCAN_DEV_OPENED;
		pcan_set_bus_state(dev, PCANFD_UNKNOWN);

		if (dev->release)
			dev->release(dev);

		/* release the device irq */
		if (dev->free_irq)
			dev->free_irq(dev, irq_arg);

		if (dev->adapter->opened_count > 0)
			dev->adapter->opened_count--;

		/* destroy useless syncs */
		pcan_event_free(&dev->in_event);
		pcan_event_free(&dev->out_event);

#ifdef PCAN_USES_OLD_TX_ENGINE_STATE
		atomic_set(&dev->tx_engine_state, TX_ENGINE_CLOSED);
#else
		dev->locked_tx_engine_state = TX_ENGINE_CLOSED;
#endif

		return;
	}

	pcan_mutex_unlock(&dev->mutex);
}

/* is called at user ioctl() with cmd = PCAN_GET_STATUS */
int pcan_ioctl_status_common(struct pcandev *dev, TPSTATUS *local)
{
	local->wErrorFlag = dev->wCANStatus;

	/* get infos for friends of polling operation */
	if (pcan_fifo_empty(&dev->readFifo))
		local->wErrorFlag |= CAN_ERR_QRCVEMPTY;

	if (pcan_fifo_full(&dev->writeFifo))
		local->wErrorFlag |= CAN_ERR_QXMTFULL;

	local->nLastError = dev->nLastError;

	return 0;
}

/* is called at user ioctl() with cmd = PCAN_GET_EXT_STATUS */
int pcan_ioctl_extended_status_common(struct pcandev *dev,
					TPEXTENDEDSTATUS *local)
{
	local->wErrorFlag = dev->wCANStatus;

	local->nPendingReads = dev->readFifo.nStored;

	/* get infos for friends of polling operation */
	if (pcan_fifo_empty(&dev->readFifo))
		local->wErrorFlag |= CAN_ERR_QRCVEMPTY;

	local->nPendingWrites = dev->writeFifo.nStored;

	if (pcan_fifo_full(&dev->writeFifo))
		local->wErrorFlag |= CAN_ERR_QXMTFULL;

	local->nLastError = dev->nLastError;

	return 0;
}

/* is called at user ioctl() with cmd = PCAN_DIAG */
int pcan_ioctl_diag_common(struct pcandev *dev, TPDIAG *local)
{
	local->wType = dev->wType;

	switch (dev->wType) {
	case HW_USB:
	case HW_USB_FD:
	case HW_USB_PRO:
	case HW_USB_PRO_FD:
#ifdef USB_SUPPORT 
		local->dwBase = pcan_usb_get_if(dev)->dwSerialNumber;
		local->wIrqLevel = dev->port.usb.ucHardcodedDevNr;
#endif
		break;
	default:
		local->dwBase = dev->dwPort;
		local->wIrqLevel = dev->wIrq;
		break;
	}

	local->dwReadCounter = dev->readFifo.dwTotal;
	local->dwWriteCounter = dev->writeFifo.dwTotal;
	local->dwIRQcounter = dev->dwInterruptCounter;
	local->dwErrorCounter = dev->dwErrorCounter;
	local->wErrorFlag = dev->wCANStatus;

	/* get infos for friends of polling operation */
	if (pcan_fifo_empty(&dev->readFifo))
		local->wErrorFlag |= CAN_ERR_QRCVEMPTY;

	if (pcan_fifo_full(&dev->writeFifo))
		local->wErrorFlag |= CAN_ERR_QXMTFULL;

	local->nLastError = dev->nLastError;
	local->nOpenPaths = dev->nOpenPaths;

	strncpy(local->szVersionString,
			pcan_drv.szVersionString, VERSIONSTRING_LEN);
	return 0;
}

/*
 * Inculde system specific entry points:
 */
#ifdef NO_RT
#include "pcan_fops_linux.c"
#else
#include "pcan_fops_rt.c"
#endif
