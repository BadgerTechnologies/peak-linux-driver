/*****************************************************************************
 * Copyright (C) 2001-2010  PEAK System-Technik GmbH
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
 *****************************************************************************/

/*****************************************************************************
 *
 * all parts to handle device interface specific for pcan-pci
 *
 * $Id: pcan_pci.h 936 2015-09-10 13:31:56Z stephane $
 *
 *****************************************************************************/

#ifndef __PCAN_PCI_H__
#define __PCAN_PCI_H__

#include <src/pcan_main.h>

#if 1 /* def PCIEC_SUPPORT */
/* History: in previous versions of pcan, supporting or not PCIEC was leading
 * to two ways of registering PCI devices...
 * From v8.0, PCI CAN devices creation is always event driven... */
#if 1//def NO_RT
/* v7.x RT: PCIEC_SUPPORT was NOT supported, thus PCAN_PCI_EVENT_DRIVEN was'nt
 * defined... */
#define PCAN_PCI_EVENT_DRIVEN
#endif
#endif

#ifdef PCAN_PCI_EVENT_DRIVEN
int pcan_pci_init(void);
void pcan_pci_deinit(void);
#else
int pcan_search_and_create_pci_devices(void);
#endif

void pcan_pci_clear_stored_interrupt(struct pcandev *dev);
void pcan_pci_enable_interrupt(struct pcandev *dev);

#endif
