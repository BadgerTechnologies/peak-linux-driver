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
 *****************************************************************************/

/*****************************************************************************
 *
 * pcan_fops.h - header for struct fops only
 *
 * $Id: pcan_fops.h 1090 2016-02-02 16:03:24Z stephane $
 *
 *****************************************************************************/

#ifndef __PCAN_FOPS_H__
#define __PCAN_FOPS_H__

#include <linux/kernel.h>   /* printk() */
#include <linux/file.h>

#ifndef NO_RT
#include <rtdm/rtdm_driver.h>
#endif

#define MAX_WAIT_UNTIL_CLOSE	1000

int pcan_open_path(struct pcandev *dev, struct pcan_udata *ctx);
void pcan_release_path(struct pcandev *dev, struct pcan_udata *ctx);

struct pcandev* pcan_search_dev(int major, int minor);

int pcan_ioctl_extended_status_common(struct pcandev *dev,
					TPEXTENDEDSTATUS *pes);
int pcan_ioctl_status_common(struct pcandev *dev, TPSTATUS *ps);
int pcan_ioctl_diag_common(struct pcandev *dev, TPDIAG *pd);

#ifndef NO_RT
extern struct rtdm_device pcandev_rt;
#else
extern struct file_operations pcan_fops;
#endif

#endif
