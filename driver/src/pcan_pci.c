/****************************************************************************ble
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
 *                Stephane Grosjean (s.grosjean@peak-system.com)    USB-PRO
 *
 * Contributions: Philipp Baer (philipp.baer@informatik.uni-ulm.de)
 *                Armin Bauer (armin.bauer@desscon.com)
 ****************************************************************************/

/****************************************************************************
 *
 * all parts to handle the interface specific parts of pcan-pci
 *
 * $Id: pcan_pci.c 1218 2016-05-25 08:02:33Z stephane $
 *
 ****************************************************************************/
/* #define DEBUG */
/* #undef DEBUG */

#include "src/pcan_common.h"	/* must always be the 1st include */

#include <linux/ioport.h>
#include <linux/pci.h>		/* all about pci */
#include <asm/io.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <asm/io.h>

#include "src/pcan_pci.h"
#include "src/pcan_sja1000.h"
#include "src/pcan_filter.h"

#ifdef PCIEC_SUPPORT
#include "src/pcan_pciec.h"
#endif

/* If defined, driver will first try to enable MSI mode with the device. On
 * any error, it will fall back into normal INTx mode.
 * If not defined, normal INTx mode will be used, as usual.
 * Undefine it if any problem related to MSI does occur.
 */
//#define PCAN_PCI_USES_MSI

/* important PITA registers */
#define PITA_ICR         0x00        // interrupt control register
#define PITA_GPIOICR     0x18        // general purpose IO interface control register
#define PITA_MISC        0x1C        // miscellaneous register

#define PEAK_PCI_VENDOR_ID	0x001C	/* the PCI device and vendor IDs */

#define PCAN_PCI_ID		0x0001  /* PCI / PCIe Slot cards */
#define PCAN_EXPRESSCARD_ID	0x0002	/* PCAN-ExpressCard */
#define PCAN_PCIE_ID		0x0003	/* PCIe Slot cards */
#define PCAN_CPCI_ID		0x0004	/* cPCI */
#define PCAN_MINIPCI_ID		0x0005	/* miniPCI */
#define PCAN_PC104PLUSQUAD_ID	0x0006	/* new PC-104 Plus Quad */
#define PCAN_PCI104E_ID		0x0007	/* PCI-104 Express */
#define PCAN_MINIPCIE_ID	0x0008	/* miniPCIe Slot cards */
#define PCAN_PCIE_OEM_ID	0x0009	/* PCAN-PCI Express OEM */
#define PCAN_EXPRESSCARD34_ID	0x000a	/* PCAN-Express Card 34 */

/* CAN-FD devices id. range start */
#define PEAK_PCICANFD_ID	0x0010

#define PCAN_PCIEFD10_ID	PEAK_PCICANFD_ID
#define PCAN_PCIEFD_ID		(PEAK_PCICANFD_ID + PCAN_PCIE_ID)

#define PCI_CONFIG_PORT_SIZE	0x1000  /* size of the config io-memory */
#define PCI_PORT_SIZE		0x0400  /* size of a channel io-memory */

#ifdef LINUX_26
#define pci_find_device(v, d, x) pci_get_device(v, d, x)
#endif

#define VERSION_REG1		0x40
#define VERSION_REG2		0x44
#define VERSION_REG2_MASK	0xfff
#define VERSION_REG2_MSI	0x110

/*
 * GLOBALS
 */
#ifdef UDEV_SUPPORT
static const struct pci_device_id pcan_pci_tbl[] = {
	{PEAK_PCI_VENDOR_ID, PCAN_PCI_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0},
	{PEAK_PCI_VENDOR_ID, PCAN_PCIE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0},
	{PEAK_PCI_VENDOR_ID, PCAN_CPCI_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0},
	{PEAK_PCI_VENDOR_ID, PCAN_MINIPCI_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0},
	{PEAK_PCI_VENDOR_ID, PCAN_PC104PLUSQUAD_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0},
	{PEAK_PCI_VENDOR_ID, PCAN_PCI104E_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0},
	{PEAK_PCI_VENDOR_ID, PCAN_MINIPCIE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0},
#ifdef PCIEC_SUPPORT
	{PEAK_PCI_VENDOR_ID, PCAN_EXPRESSCARD_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0},
	{PEAK_PCI_VENDOR_ID, PCAN_EXPRESSCARD34_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0},
#endif
#ifdef PCAN_PCIE_OEM_ID
	{PEAK_PCI_VENDOR_ID, PCAN_PCIE_OEM_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0},
#endif

	/* CAN-FD devices */
	{ PCI_DEVICE(PEAK_PCI_VENDOR_ID, PEAK_PCICANFD_ID) },
	{ PCI_DEVICE(PEAK_PCI_VENDOR_ID, PCAN_PCIEFD_ID) },

	{0, }
};

MODULE_DEVICE_TABLE(pci, pcan_pci_tbl);
#endif /* UDEV_SUPPORT */

static const char *pcan_pci_adapter_name[] = {
	[PCAN_PCI_ID] = "PCAN-PCI",
	[PCAN_PCIE_ID] = "PCAN-PCI Express",
	[PCAN_CPCI_ID] = "PCAN-cPCI",
	[PCAN_MINIPCI_ID] = "PCAN-miniPCI",
	[PCAN_PC104PLUSQUAD_ID] = "PCAN-PC/104-Plus Quad",
	[PCAN_PCI104E_ID] = "PCAN-PCI/104-Express",
	[PCAN_MINIPCIE_ID] = "PCAN-miniPCIe",
#ifdef PCIEC_SUPPORT
	[PCAN_EXPRESSCARD_ID] = "PCAN-ExpressCard",
	[PCAN_EXPRESSCARD34_ID] = "PCAN-ExpressCard 34",
#endif
#ifdef PCAN_PCIE_OEM_ID
	[PCAN_PCIE_OEM_ID] = "PCAN-PCI Express OEM",
#endif

	/* CAN-FD devices */
	[PCAN_PCIEFD10_ID] = "PCAN-PCIe FD (proto)",
	[PCAN_PCIEFD_ID] = "PCAN-PCIe FD",
};

/* non-SJA1000 PCI devices probing is external */
extern void pcan_pci_ucan_remove(struct pci_dev *dev);
extern int pcan_pci_ucan_probe(struct pci_dev *dev, u16 sub_system_id,
			       const char *adapter_name, int can_count);

/* ugly (but historical) global count of ALL the pci channel devices */
int _pci_devices = 0;

/* count of SJA1000 PCI devices */
static int pcan_pci_sja1000_adapters = 0;

/* count of SJA1000 channels devices */
static int pcan_pci_sja1000_devices = 0;

static void pcan_pci_unregister_driver(struct pci_driver *p_pci_drv);

/* read a register */
static u8 pcan_pci_readreg(struct pcandev *dev, u8 port)
{
	u32 lPort = port << 2;
	return readb(dev->port.pci.pvVirtPort + lPort);
}

/* write a register */
static void pcan_pci_writereg(struct pcandev *dev, u8 port, u8 data)
{
	u32 lPort = port << 2;
	writeb(data, dev->port.pci.pvVirtPort + lPort);
}

static const u16 pita_icr_masks[] = { 0x0002, 0x0001, 0x0040, 0x0080 };

/* select and clear in Pita stored interrupt */
void pcan_pci_clear_stored_interrupt(struct pcandev *dev)
{
	u16 pita_icr_mask = pita_icr_masks[dev->nChannel];
	u16 pita_icr_low = readw(dev->port.pci.pvVirtConfigPort + PITA_ICR);

	if (pita_icr_low & pita_icr_mask) {
		writew(pita_icr_mask,
				dev->port.pci.pvVirtConfigPort + PITA_ICR);
#if 0//ndef PCAN_PCI_USES_MSI
		/* PCIe: in order to be sure that the PCIe message
		 * "deassert INTx" has been received, issue a dummy read
		 * instruction next.
		 *
		 * (useless with MSI) */
		readw(dev->port.pci.pvVirtConfigPort + PITA_ICR);
#endif
	}
}

/* enable interrupt again */
void pcan_pci_enable_interrupt(struct pcandev *dev)
{
	u16 pita_icr_mask = pita_icr_masks[dev->nChannel];
	u16 pita_icr_high =
		readw(dev->port.pci.pvVirtConfigPort + PITA_ICR + 2);

	DPRINTK(KERN_DEBUG "%s: %s(%u): PITA ICR=%04Xh\n",
			DEVICE_NAME, __func__,
			dev->nMinor, pita_icr_high);

	pita_icr_high |= pita_icr_mask;
	writew(pita_icr_high, dev->port.pci.pvVirtConfigPort + PITA_ICR + 2);

	dev->wInitStep++;
}

static void pcan_pci_free_irq(struct pcandev *dev, struct pcan_udata *dev_priv)
{
	/* disable interrupt */
	u16 pita_icr_mask = pita_icr_masks[dev->nChannel];
	u16 pita_icr_high =
			readw(dev->port.pci.pvVirtConfigPort + PITA_ICR + 2);

	DPRINTK(KERN_DEBUG "%s: %s(%u): PITA ICR=%04Xh\n",
			DEVICE_NAME, __func__,
			dev->nMinor, pita_icr_high);

	pita_icr_high &= ~pita_icr_mask;
	writew(pita_icr_high, dev->port.pci.pvVirtConfigPort + PITA_ICR + 2);

	/* read it again, to wait for write command to complete */
	//readw(dev->port.pci.pvVirtConfigPort + PITA_ICR + 2);
#ifdef NO_RT
	free_irq(dev->wIrq, dev);
#else
	rtdm_irq_free(&dev->irq_handle);
#endif

	dev->wInitStep = 5;
}

/* release and probe */
static int pcan_pci_cleanup(struct pcandev *dev)
{
	if (pcan_pci_sja1000_devices <= 0) {
		pr_info("%s(): ABNORMAL pcan_pci_sja1000_devices=%d ! "
			"Nothing done next\n",
			__func__, pcan_pci_sja1000_devices);
		return 0;
	}

	DPRINTK(KERN_DEBUG
		"%s: %s(): _pci_devices=%d sja100 devices=%d wInitStep=%u\n",
		DEVICE_NAME, __func__, _pci_devices, pcan_pci_sja1000_devices,
		dev->wInitStep);

	switch (dev->wInitStep) {
	case 6:
		pcan_pci_free_irq(dev, NULL);
	case 5:
#ifdef PCIEC_SUPPORT
		pcan_pciec_delete_card(dev);
#endif
	case 4:
		iounmap(dev->port.pci.pvVirtPort);
	case 3:
		release_mem_region(dev->dwPort, PCI_PORT_SIZE);
	case 2:
		/* SGr note: DON'T call  pcan_dev_remove_from_list(dev) here
		 * because it uses the same mutex than
		 * pcan_pci_sja1000_remove() */
		if (dev->nChannel == 0)
			iounmap(dev->port.pci.pvVirtConfigPort);
	case 1:
		if (dev->nChannel == 0)
			release_mem_region(dev->port.pci.dwConfigPort,
							PCI_CONFIG_PORT_SIZE);
	case 0:
		dev->filter = pcan_delete_filter_chain(dev->filter);

		_pci_devices--;
		pcan_pci_sja1000_devices--;
#ifdef UDEV_SUPPORT
#ifndef PCAN_PCI_EVENT_DRIVEN
		if (!pcan_pci_sja1000_devices)
			pcan_pci_unregister_driver(&pcan_drv.pci_drv);
#endif
#endif
	}

	return 0;
}

/* interface depended open and close */
static int pcan_pci_open(struct pcandev *dev)
{
#ifdef DEBUG
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif
	dev->ucActivityState = ACTIVITY_IDLE;
	return 0;
}

static int pcan_pci_release(struct pcandev *dev)
{
#ifdef DEBUG
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif

	dev->ucActivityState = ACTIVITY_INITIALIZED;
	return 0;
}

#ifdef PCAN_PCI_USES_MSI
static int pcan_pci_enable_msi(struct pci_dev *pci_dev, struct pcan_msi *msi)
{
	int err = -EINVAL;

	int nvec = msi->msi_requested;

	DPRINTK(KERN_DEBUG
		"%s: %s():msi_requested=%d msi_enabled=%d msix_enabled=%d\n",
		DEVICE_NAME, __func__,
		msi->msi_requested,
		pci_dev->msi_enabled, pci_dev->msix_enabled);

	/*
	 * should test before: some config runs with msi aloread enabled for
	 * PCI device. This test removes ugly WARNING in logs for 3.0.0.
	 */
	if (pci_dev_msi_enabled(pci_dev))
		return 0;
#ifdef DEBUG
	{
		int pos = pci_find_capability(pci_dev, PCI_CAP_ID_MSI);

		if (!pos) {
			pr_warn("%s: pci_find_capability() failure\n",
				DEVICE_NAME);
		} else {
			u16 msgctl;
			int maxvec;

			pci_read_config_word(pci_dev,
						pos + PCI_MSI_FLAGS, &msgctl);
			maxvec = 1 << ((msgctl & PCI_MSI_FLAGS_QMASK) >> 1);
			pr_info("%s: maxvec=%d\n", DEVICE_NAME, maxvec);
		}
	}
#endif

	while (1) {
		err = pci_enable_msi_block(pci_dev, nvec);

#ifdef DEBUG
		pr_info("%s: pci_enable_msi_block(%d) status=%d\n",
			DEVICE_NAME, nvec, err);
#endif
		if (!err) {
			/* disable PCI INTx for device */
			pci_intx(pci_dev, 0);
			msi->msi_assigned = nvec;
			break;
		}

		if (err < 0) {
			pr_warn("%s: enabling MSI mode err=%d\n",
				DEVICE_NAME, err);
			break;
		}

		nvec = err;
	}

	return err;
}
#endif /* PCAN_PCI_USES_MSI */

#ifndef NO_RT
/* RT version of the IRQ handler */
static int pcan_pci_irqhandler(rtdm_irq_t *irq_context)
{
	struct pcandev *dev = rtdm_irq_get_arg(irq_context, struct pcandev);

#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
static irqreturn_t pcan_pci_irqhandler(int irq, void *arg, struct pt_regs *regs)
{
	struct pcandev *dev = (struct pcandev *)arg;
#else
static irqreturn_t pcan_pci_irqhandler(int irq, void *arg)
{
	struct pcandev *dev = (struct pcandev *)arg;
#endif
	int err;

	err = pcan_sja1000_irqhandler(dev);
	pcan_pci_clear_stored_interrupt(dev);

	return PCAN_IRQ_RETVAL(err);
}

static int pcan_pci_req_irq(struct pcandev *dev, struct pcan_udata *dev_priv)
{
	int err;

#ifdef NO_RT
	err = request_irq(dev->wIrq, pcan_pci_irqhandler,
				IRQF_SHARED, DEVICE_NAME, dev);
#else
	err = rtdm_irq_request(&dev->irq_handle,
				dev->wIrq,
				pcan_pci_irqhandler,
				RTDM_IRQTYPE_SHARED | RTDM_IRQTYPE_EDGE,
				dev_priv->context->device->proc_name, dev);
#endif

	if (err) {
		pr_err("%s: failed requesting PCI IRQ %u (err %d)\n",
			DEVICE_NAME, dev->wIrq, err);
		return err;
	}

	pcan_pci_enable_interrupt(dev);

	return 0;
}

static int pcan_pci_channel_init(struct pcandev *dev, struct pci_dev *pciDev,
				u32 dwConfigPort, u32 dwPort, u16 wIrq,
				struct pcandev *mastr_dev)
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,0,0)
	int err;
#endif
	DPRINTK(KERN_DEBUG "%s: %s(), irq=%d _pci_devices = %d\n",
		DEVICE_NAME, __func__, wIrq, _pci_devices);

	/* obsolete - will be removed soon */
	dev->props.ucMasterDevice = CHANNEL_MASTER;

	/* set this before any instructions, fill struct pcandev, part 1 */
	dev->wInitStep   = 0;
	dev->readreg     = pcan_pci_readreg;
	dev->writereg    = pcan_pci_writereg;
#ifndef PCIEC_SUPPORT
	dev->cleanup     = pcan_pci_cleanup;
#endif
	dev->req_irq     = pcan_pci_req_irq;
	dev->free_irq    = pcan_pci_free_irq;
	dev->open        = pcan_pci_open;
	dev->release     = pcan_pci_release;
	dev->nMajor      = pcan_drv.nMajor;
	dev->nMinor      = PCAN_PCI_MINOR_BASE + _pci_devices;
	dev->filter      = NULL;

	/* fill struct pcandev, part 1 */
	dev->port.pci.dwConfigPort = dwConfigPort;
	dev->dwPort = dwPort;
	dev->wIrq = wIrq;

	/* reject illegal combination */
	if (!dwPort || !wIrq) {
		pr_info("%s: %s(): illegal combination dwPort=%d wIrq=%d\n",
			DEVICE_NAME, __func__, dwPort, wIrq);
		return -EINVAL;
	}

	dev->filter = pcan_create_filter_chain();

	/* do it only if the device is channel master,
	 * and channel 0 is it always
	 */
	if (!dev->nChannel) {
		u32 v1;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,0,0)
		err = check_mem_region(dev->port.pci.dwConfigPort,
							PCI_CONFIG_PORT_SIZE);
		if (err) {
			pr_info("%s: %s(@%d) check_mem_region(%d, %d) err=%d\n",
				DEVICE_NAME, __func__, __LINE__,
				dev->port.pci.dwConfigPort,
				PCI_CONFIG_PORT_SIZE, err);
			return -EBUSY;
		}
#endif
		request_mem_region(dev->port.pci.dwConfigPort,
						PCI_CONFIG_PORT_SIZE,
						DEVICE_NAME);

		dev->wInitStep = 1;

		dev->port.pci.pvVirtConfigPort =
				ioremap(dwConfigPort, PCI_CONFIG_PORT_SIZE);
		if (dev->port.pci.pvVirtConfigPort == NULL) {
			pr_info("%s: %s(@%d) ioremap(%d, %d) failure\n",
				DEVICE_NAME, __func__, __LINE__,
				dwConfigPort, PCI_CONFIG_PORT_SIZE);
			return -ENODEV;
		}

		dev->wInitStep = 2;

		/* configuration of the PCI chip, part 2: */

		/* set GPIO control register */
		writew(0x0005,
			dev->port.pci.pvVirtConfigPort + PITA_GPIOICR + 2);

		/* enable all channels */
		writeb(0x00, dev->port.pci.pvVirtConfigPort + PITA_GPIOICR);

		/* toggle reset */
		writeb(0x05, dev->port.pci.pvVirtConfigPort + PITA_MISC + 3);
		mdelay(5);

		/* leave parport mux mode */
		writeb(0x04, dev->port.pci.pvVirtConfigPort + PITA_MISC + 3);
		wmb();

		v1 = readl(dev->port.pci.pvVirtConfigPort + VERSION_REG1);
		if (v1) {

			/* this is an FPGA equipped board */
			u32 v2 = readl(dev->port.pci.pvVirtConfigPort +
								VERSION_REG2);
			
			dev->adapter->hw_ver_major = (v2 & 0x0000f000) >> 12;
			dev->adapter->hw_ver_minor = (v2 & 0x00000f00) >> 8;
			dev->adapter->hw_ver_subminor = (v2 & 0x000000f0) >> 4;

#ifdef PCAN_PCI_USES_MSI
			/* read MSI ability of the board */
			if (((v2 >> 4) & VERSION_REG2_MASK) >=
							VERSION_REG2_MSI) {
				dev->port.pci.msi.msi_requested = 4;
				dev->port.pci.msi.msi_assigned = 0;
				pcan_pci_enable_msi(pciDev, &dev->port.pci.msi);
				dev->wIrq = pciDev->irq;
			}
#endif
		}

	} else {

		dev->port.pci.pvVirtConfigPort =
				mastr_dev->port.pci.pvVirtConfigPort;

#ifdef PCAN_PCI_USES_MSI
		/* adjust MSI/INTA irq from master device IRQ value */
		dev->wIrq = mastr_dev->wIrq +
			(dev->nChannel * \
				mastr_dev->port.pci.msi.msi_assigned) /
					mastr_dev->port.pci.msi.msi_requested;
#endif
	}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,0,0)
	err = check_mem_region(dev->dwPort, PCI_PORT_SIZE);
	if (err) {
		pr_info("%s: %s(@%d) check_mem_region(%d, %d) err=%d\n",
			DEVICE_NAME, __func__, __LINE__, dev->dwPort,
			PCI_PORT_SIZE, err);

		return -EBUSY;
	}
#endif
	request_mem_region(dev->dwPort, PCI_PORT_SIZE, DEVICE_NAME);

	dev->wInitStep = 3;

	dev->port.pci.pvVirtPort = ioremap(dwPort, PCI_PORT_SIZE);

	if (dev->port.pci.pvVirtPort == NULL) {
		pr_info("%s: %s(@%d) ioremap(%d, %d) failure\n",
			DEVICE_NAME, __func__, __LINE__, dwPort, PCI_PORT_SIZE);

		return -ENODEV;
	}

	dev->wInitStep = 4;

	_pci_devices++;
	dev->wInitStep = 5;

	pr_info("%s: pci device minor %d found\n", DEVICE_NAME, dev->nMinor);

	return 0;
}

/*
 * create one pci based devices from peak
 * - this may be one of multiple from a card
 */
static int create_one_pci_device(struct pci_dev *pciDev, int nChannel,
					struct pcandev *mastr_dev,
					struct pcandev **dev,
					struct pcan_adapter *adapter)
{
	struct pcandev *local_dev = NULL;
	int err = 0;

	DPRINTK(KERN_DEBUG "%s: %s(nChannel=%d)\n",
		DEVICE_NAME, __func__, nChannel);

#ifdef DEBUG
	for (err = 0; err < DEVICE_COUNT_RESOURCE; err++) {
		if (pciDev->resource[err].name)
			printk(KERN_INFO
				"resource[%d]: name=\"%s\" start=%d (%xh) "
				"end=%d (%xh) flags=%08xh\n",
				err,
				pciDev->resource[err].name,
				(int )pciDev->resource[err].start,
				(int )pciDev->resource[err].start,
				(int )pciDev->resource[err].end,
				(int )pciDev->resource[err].end,
				(int )pciDev->resource[err].flags);
	}
	err = 0;
#endif
	/* make the first device on board */
	local_dev = pcan_malloc(sizeof(struct pcandev), GFP_KERNEL);
	if (!local_dev) {
		err = -ENOMEM;
		goto fail;
	}

	pcan_soft_init(local_dev, "pci", HW_PCI);

	local_dev->nChannel = nChannel;
	local_dev->adapter = adapter;

	local_dev->device_open = sja1000_open;
	local_dev->device_write = sja1000_write;
	local_dev->device_release = sja1000_release;
	local_dev->port.pci.pciDev = NULL;

	local_dev->props.ucExternalClock = 1;

#ifdef PCIEC_SUPPORT
	/* card pointer must be NULL for all but PCAN-Expresscard */
	local_dev->port.pci.card = NULL;
#endif

	err = pcan_pci_channel_init(local_dev, pciDev,
		(u32)pciDev->resource[0].start, /* dwConfigPort */
		(u32)pciDev->resource[1].start + nChannel * 0x400, /* dwPort */
		(u16)pciDev->irq,
		mastr_dev);

	if (!err)
		err = sja1000_probe(local_dev);

	if (err) {
#ifndef PCIEC_SUPPORT
		/* Thanks Hardi! */
		local_dev->cleanup(local_dev);
#endif
		pcan_free(local_dev);
		*dev = NULL;
		goto fail;
	}

	local_dev->ucPhysicallyInstalled = 1;
	local_dev->port.pci.pciDev = pciDev;

#ifdef PCIEC_SUPPORT
	/* we have a card with i2c controlled blinking LED */
	if ((pciDev->device == PCAN_EXPRESSCARD_ID) ||
		(pciDev->device == PCAN_EXPRESSCARD34_ID)) {

		/* master channel */
		if (local_dev->nChannel == 0)
			local_dev->port.pci.card =
				pcan_pciec_create_card(pciDev, local_dev);
		else
			local_dev->port.pci.card =
				pcan_pciec_locate_card(pciDev, local_dev);
	}
#endif

	/* add this device to the list */
	pcan_add_device_in_list(local_dev);
	*dev = local_dev;

	return 0;

fail:
	pr_err("%s: %s(nChannel=%d) discarded - %d\n",
				DEVICE_NAME, __func__, nChannel, err);

	return err;
}

/* move to event driven creation of devices, not for kernels 2.4.x */
static int pcan_pci_sja1000_probe(struct pci_dev *pciDev, u16 wSubSysID,
				  const char *adapter_name, int can_count)
{
	struct pcan_adapter *adapter;
	struct pcandev *dev, *mastr_dev;
	int err, i;

	DPRINTK(KERN_DEBUG "%s: %s(%p)\n", DEVICE_NAME, __func__, pciDev);


	/* configure the PCI chip, part 1 */
	err = pci_write_config_word(pciDev, PCI_COMMAND, 2);
	if (err)
		goto fail;

	err = pci_write_config_word(pciDev, 0x44, 0);
	if (err)
		goto fail;
	wmb();

	adapter = pcan_alloc_adapter(adapter_name, pcan_pci_sja1000_adapters,
					can_count);
	mastr_dev = NULL;
	for (i = 0; i < can_count; i++) {
		err = create_one_pci_device(pciDev, i, mastr_dev,
							&dev, adapter);
		if (err)
			goto fail;

		if (!mastr_dev)
			mastr_dev = dev;
	}

	pcan_pci_sja1000_devices += can_count;
	pcan_pci_sja1000_adapters++;

	return 0;

fail:
#ifdef PCAN_PCI_USES_MSI
	pci_disable_msi(pciDev);
#endif

	return err;
}


static int pcan_pci_probe(struct pci_dev *dev, const struct pci_device_id *ent)
{
	u16 sub_system_id;
	int err, can_count;

	DPRINTK(KERN_DEBUG "%s: %s(%p:id=%0xh)\n",
			DEVICE_NAME, __func__, dev, ent->device);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10)
	err = pci_enable_device(dev);
	if (err)
		goto fail;
#endif

	err = pci_read_config_word(dev, PCI_SUBSYSTEM_ID, &sub_system_id);
	if (err)
		goto fail_disable_pci;

	/* number of CAN channels depends on the sub-system id */
	if (sub_system_id < 0x0004)
		can_count = 1;
	else if (sub_system_id < 0x0010)
		can_count = 2;
	else if (sub_system_id < 0x0012)
		can_count = 3;
	else
		can_count = 4;

	/* consider that devid >= 0x10 => CAN-FD devices */
	if (ent->device >= PEAK_PCICANFD_ID) {
		err = pcan_pci_ucan_probe(dev, sub_system_id,
				pcan_pci_adapter_name[dev->device],
				can_count);
	} else {
		err = pcan_pci_sja1000_probe(dev, sub_system_id,
				pcan_pci_adapter_name[dev->device],
				can_count);
	}

	if (err)
		goto fail_disable_pci;

	return 0;

fail_disable_pci:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 10)
	pci_disable_device(dev);
fail:
#endif
	return err;
}

static int pcan_pci_register_driver(struct pci_driver *pci_drv)
{
#ifdef DEBUG
	pr_info("%s: %s(%p)\n", DEVICE_NAME, __func__, pci_drv);
#endif
	pci_drv->name = DEVICE_NAME;
	pci_drv->id_table = pcan_pci_tbl;

	return pci_register_driver(pci_drv);
}

static void pcan_pci_unregister_driver(struct pci_driver *pci_drv)
{
#ifdef DEBUG
	pr_info("%s: %s(%p)\n", DEVICE_NAME, __func__, pci_drv);
#endif
	pci_unregister_driver(pci_drv);
}

#ifdef PCAN_PCI_EVENT_DRIVEN

static void pcan_pci_sja1000_remove(struct pci_dev *pciDev)
{
	struct pcandev *dev;
	struct pcan_adapter *adapter = NULL;
	struct list_head *pos;
	struct list_head *n;

	DPRINTK(KERN_DEBUG "%s: %s(%p)\n", DEVICE_NAME, __func__, pciDev);

	pcan_mutex_lock(&pcan_drv.devices_lock);

	list_for_each_prev_safe(pos, n, &pcan_drv.devices) {
		dev = list_entry(pos, struct pcandev, list);
		if ((dev->wType == HW_PCI) &&
					(dev->port.pci.pciDev == pciDev)) {

			pcan_pci_cleanup(dev);
			list_del(&dev->list);
			pcan_drv.wDeviceCount--;

#if 1
			/* SGR Note: because of pcan_free(dev), is this really
			 * useful? */
#else
			/* TODO: a much better hack to address plugging out
			 * while a path to the device is open
			 */
			dev->ucPhysicallyInstalled = 0;
#endif
			/* free all device allocated memory */
			if (!adapter)
				adapter = dev->adapter;
			pcan_free(dev);
		}
	}

	pcan_mutex_unlock(&pcan_drv.devices_lock);

	if (adapter)
		pcan_free(adapter);

#ifdef PCAN_PCI_USES_MSI
	pci_disable_msi(pciDev);
#endif
}

static void pcan_pci_remove(struct pci_dev *dev)
{
	DPRINTK(KERN_DEBUG "%s: %s(%p:id=%0xh)\n",
			DEVICE_NAME, __func__, dev, dev->device);

	if (dev->device >= PEAK_PCICANFD_ID) {
		pcan_pci_ucan_remove(dev);
	} else {
		pcan_pci_sja1000_remove(dev);
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10)
	pci_disable_device(dev);
#endif
}

int pcan_pci_init(void)
{
	DPRINTK(KERN_DEBUG "%s: %s()\n", DEVICE_NAME, __func__);

	pcan_drv.pci_drv.probe = pcan_pci_probe;
	pcan_drv.pci_drv.remove = pcan_pci_remove;

	return pcan_pci_register_driver(&pcan_drv.pci_drv);
}

void pcan_pci_deinit(void)
{
	DPRINTK(KERN_DEBUG "%s: %s()\n", DEVICE_NAME, __func__);
	pcan_pci_unregister_driver(&pcan_drv.pci_drv);
}

#else /* PCAN_PCI_EVENT_DRIVEN */

#ifdef LINUX_26
static inline int pci_present(void)
{
#ifdef CONFIG_PCI
	return 1;
#else
	return 0;
#endif
}
#endif

/* search all pci based devices from peak */
int pcan_search_and_create_pci_devices(void)
{
	const int n = sizeof(pcan_pci_tbl) / sizeof(pcan_pci_tbl[0]) - 1;
	int err = 0;
	int i;

	/* search pci devices */
	DPRINTK(KERN_DEBUG "%s: %s()\n", DEVICE_NAME, __func__);

	if (!pci_present())
		return 0;

	/* for each device id... */
	for (i = 0; i < n; i++) {
		struct pci_dev *from = NULL;
		struct pci_dev *pciDev;

		/* ...loop looking for all the same adapters */
		do {
			pciDev = pci_find_device(pcan_pci_tbl[i].vendor,
						 pcan_pci_tbl[i].device,
						 from);

			/* no (more) device found with that device id.:
			 * break the current device loop to search for any
			 * other PEAK devices...  */
			if (!pciDev) {
				DPRINTK(KERN_DEBUG "%s: %s(): i=%d (%04x.%04x) "
					"pciDev=NULL from=%p\n",
					DEVICE_NAME, __func__, i,
					pcan_pci_tbl[i].vendor,
					pcan_pci_tbl[i].device,
					from);
				break;
			}

			/* a PCI device with PCAN_PCI_VENDOR_ID and
			 * PCAN_PCI_DEVICE_ID was found */
			from = pciDev;

			/* create all corresponding channel devices */
			err = pcan_pci_probe(pciDev, pcan_pci_tbl + i);

		} while (!err);
	}

	DPRINTK(KERN_DEBUG "%s: %s() status=%d\n", DEVICE_NAME, __func__, err);

#ifdef UDEV_SUPPORT
	/* register only if at least one SJA1000 channel has been found */
	if (pcan_pci_sja1000_devices > 0)
		pcan_pci_register_driver(&pcan_drv.pci_drv);
#endif

	return err;
}
#endif /* PCAN_PCI_EVENT_DRIVEN */
