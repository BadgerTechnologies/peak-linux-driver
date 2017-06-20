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
 *                Stephane Grosjean (s.grosjean@peak-system.com)    USB-PRO
 *                     
 * Contributions: Marcel Offermans (marcel.offermans@luminis.nl)
 *                Philipp Baer     (philipp.baer@informatik.uni-ulm.de)
 *                Garth Zeglin     (garthz@ri.cmu.edu)
 *                Harald Koenig    (H.Koenig@science-computing.de)
 *****************************************************************************/

/*****************************************************************************
 *
 * pcan_main_rt.c - the starting point of the driver,
 *               init and cleanup and proc interface
 *
 * $Id: pcan_main_rt.c $
 *
 *****************************************************************************/

#define DEV_REGISTER		rt_dev_register
#define DEV_UNREGISTER		rt_dev_unregister
#define REMOVE_DEV_LIST		rt_remove_dev_list

/* list of the RT device objects */
static struct list_head rt_device_list;

static int rt_dev_register(void)
{
	struct list_head *pos;
	struct pcandev *dev;
	struct rtdm_device *rtdmdev;
	struct rt_device *rt_dev;
	int result = 0;

#ifdef DEBUG
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif

	pcan_mutex_lock(&pcan_drv.devices_lock);

	list_for_each(pos, &pcan_drv.devices) {
		dev = list_entry(pos, struct pcandev, list);

		rtdmdev = (struct rtdm_device *)pcan_malloc(sizeof(*rtdmdev),
							GFP_KERNEL);
		if (!rtdmdev)
			return -ENOMEM;

		rt_dev = (struct rt_device *)pcan_malloc(sizeof(*rt_dev),
							GFP_KERNEL);
		if (!rt_dev) {
			pcan_free(rtdmdev);
			return -ENOMEM;
		}

		memcpy(rtdmdev, &pcandev_rt, sizeof(struct rtdm_device));
		rtdmdev->device_id = MKDEV(dev->nMajor,dev->nMinor);
		snprintf(rtdmdev->device_name, RTDM_MAX_DEVNAME_LEN,
							"pcan%d", dev->nMinor);
		rtdmdev->proc_name = rtdmdev->device_name;
		result = rtdm_dev_register(rtdmdev);

		if (!result) {
#ifdef DEBUG
			pr_info("%s: %s(): RTDM device \"pcan%d\" registered\n",
				DEVICE_NAME, __func__, dev->nMinor);
#endif
			rt_dev->device = rtdmdev;
			list_add_tail(&rt_dev->list, &rt_device_list);
		} else {

			pr_err("%s: rtdm_dev_register() failure (err %d)\n",
				DEVICE_NAME, result);

			pcan_free(rtdmdev);
			pcan_free(rt_dev);
			return result;
		}

		/* create sysfs device node */
		pcan_sysfs_dev_node_create(dev);
	}

	pcan_mutex_unlock(&pcan_drv.devices_lock);

	return result;
}

void rt_dev_unregister(void)
{
	struct list_head *pos;
	struct pcandev *dev;
	struct rt_device *rt_dev;

#ifdef DEBUG
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif

	pcan_mutex_lock(&pcan_drv.devices_lock);

	/* remove sysfs entries */
	list_for_each(pos, &pcan_drv.devices) {
		dev = list_entry(pos, struct pcandev, list);
		pcan_sysfs_dev_node_destroy(dev);
	}

	pcan_mutex_unlock(&pcan_drv.devices_lock);

	/* unregister all registered devices */
	list_for_each(pos, &rt_device_list) {

		rt_dev = list_entry(pos, struct rt_device, list);

		rtdm_dev_unregister(rt_dev->device, 1000);
	}
}

void rt_remove_dev_list(void)
{
	struct rt_device *rt_dev;
	struct list_head *pos, *n;

#ifdef DEBUG
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif

	/* cycle through the list of devices and remove them */
	list_for_each_safe(pos, n, &rt_device_list) {
		rt_dev = list_entry(pos, struct rt_device, list);

		/* free all device allocted memory */
		pcan_free(rt_dev->device);
		list_del(&rt_dev->list);

		pcan_free(rt_dev);
	}

	/* remove all pcan devices */
	remove_dev_list();
}
