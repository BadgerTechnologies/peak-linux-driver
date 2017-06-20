/*
 * Copyright (C) 2012 Stephane Grosjean <s.grosjean@peak-system.com>
 *
 * PEAK-System uCAN CAN-FD PCI adapters driver.
 *
 * Copyright (C) 2015-2016  PEAK System-Technik GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
/*#define DEBUG*/
/*#undef DEBUG*/

#include "src/pcan_common.h"
#include "src/pcan_main.h"

#include "src/pcanfd_ucan.h"
#include "src/pcan_filter.h"

#if 0//ndef PCIEC_SUPPORT
/* there isn't any objective reason... but see in pcan_pci.c the comment above
 * pcan_pci_probe()...
 **/
#error "PCIEC_SUPPORT MUST be set for uCAN PCI devices"
#endif

#ifdef DEBUG
#define DEBUG_IRQ
#define DEBUG_RXDMA
#define DEBUG_WRITE
#define DEBUG_CMD
#define DEBUG_MSI
#define DEBUG_DMA
#define DEBUG_FREE_DMA
#else
//#define DEBUG_IRQ_TX
//#define DEBUG_IRQ_RX
//#define DEBUG_IRQ_SPURIOUS
//#define DEBUG_IRQ_LOST	/* works better w/ MSI (shared INTA is noisy) */
//#define DEBUG_RXDMA		/* dumping Rx DMA area generates loss of IRQ */
//#define DEBUG_WRITE
//#define DEBUG_CMD
//#define DEBUG_MSI
#ifdef CONFIG_64BIT
#define DEBUG_DMA
#endif

#endif

#ifdef DEBUG_IRQ
#define DEBUG_IRQ_TX
#define DEBUG_IRQ_RX
#define DEBUG_IRQ_SPURIOUS
#define DEBUG_IRQ_LOST
#endif

//#define UCAN_STANDALONE_PCI_DRIVER

/* if defined, DMA buffers are allocated each time device is opened. 
 * if not defined, DMA buffers are allocated once, when the driver is loaded.
 * MUST be undefined for 64-bits Linux! */
//#define UCAN_PCI_OPEN_ALLOC_DMA

/* if defined, 64-bits commands MUST be atomically written on the bus */
#define UCAN_64BITS_CMD_MUST_BE_ATOMIC

#ifdef NO_RT
/* if defined, MSI is tried */
#define UCAN_PCI_ENABLE_MSI
//#define UCAN_PCI_ENABLE_MSIX
#endif

#ifdef UCAN_PCI_ENABLE_MSI
/* if defined, try to run with less than 1 MSI per channel */
#define UCAN_PCI_SHARE_MSI
#endif

/* if defined, entering in ISR locks access to uCAN */
/* #define UCAN_LOCK_ENTIRE_ISR */
#ifndef PCAN_USES_OLD_TX_ENGINE_STATE
/* isr_lock is used by the new tx engine state */
#undef UCAN_LOCK_ENTIRE_ISR
#endif

/* if defined, non-coherent memory is use instead of coherent memory */
/* #define UCAN_USES_NON_COHERENT_DMA */

/* if defined, Tx path is handled out of the ISR but from thread context.
 * Should not be defined for now, since tests show that Tx path may lock on
 * CAN2 and CAN3 when threaded irq is set, while everyting works fine if not
 * defined. */
/* #define UCAN_PCI_USE_THREADED_IRQ */

/* define the period in ms of a debug timer */
/* #define DEBUG_TIMER_PERIOD_MS		(1000) */

#define DRV_NAME			"pcanfd-pci"

#define UCAN_PCI_BAR0_SIZE		(64*1024)
#define UCAN_PCI_RX_DMA_SIZE		(4*1024)
#define UCAN_PCI_TX_DMA_SIZE		(4*1024)

#define UCAN_PCI_TX_PAGE_SIZE		(2*1024)

/* System Control Registers */
#define UCAN_PCI_REG_SYS_CTL_SET	0x0000	/* set bits */
#define UCAN_PCI_REG_SYS_CTL_CLR	0x0004	/* clear bits */

/* System Control Registers Bits */
#define UCAN_SYS_CTL_TS_RST		0x00000001
#define UCAN_SYS_CTL_CLK_EN		0x00000002

/* Version info registers */
#define UCAN_PCI_REG_VER1		0x0040
#define UCAN_PCI_REG_VER2		0x0044

/* uCAN core addresses */
#define UCAN_PCI_CANx_ADDR(i)		((i+1) * 0x1000)

/* uCAN core registers */
#define UCAN_PCI_REG_MISC		0x0000	/* Misc. control */
#define UCAN_PCI_REG_CLK_SEL		0x0008	/* Clock selector */
#define UCAN_PCI_REG_CMD_PORT_L		0x0010	/* 64-bits command port */
#define UCAN_PCI_REG_CMD_PORT_H		0x0014
#define UCAN_PCI_REG_TX_REQ_ACC		0x0020	/* Tx request accumulator */
#define UCAN_PCI_REG_TX_CTL_SET		0x0030	/* Tx control set register */
#define UCAN_PCI_REG_TX_CTL_CLR		0x0038	/* Tx control clear register */
#define UCAN_PCI_REG_TX_DMA_ADDR_L	0x0040	/* 64-bits addr for Tx DMA */
#define UCAN_PCI_REG_TX_DMA_ADDR_H	0x0044
#define UCAN_PCI_REG_RX_CTL_SET		0x0050	/* Rx control set register */
#define UCAN_PCI_REG_RX_CTL_CLR		0x0058	/* Rx control clear register */
#define UCAN_PCI_REG_RX_CTL_WRT		0x0060	/* Rx control write register */
#define UCAN_PCI_REG_RX_CTL_ACK		0x0068	/* Rx control ACK register */
#define UCAN_PCI_REG_RX_DMA_ADDR_L	0x0070	/* 64-bits addr for Rx DMA */
#define UCAN_PCI_REG_RX_DMA_ADDR_H	0x0074

/* uCAN misc register bits */
#define UCAN_MISC_TS_RST		0x00000001	/* timestamp cnt reset*/

/* uCAN core Clock SELector Source & DIVider */
#define UCAN_CLK_SEL_DIV_MASK		0x00000007
#define UCAN_CLK_SEL_DIV_60MHZ		0x00000000	/* SRC=240MHz only */
#define UCAN_CLK_SEL_DIV_40MHZ		0x00000001	/* SRC=240MHz only */
#define UCAN_CLK_SEL_DIV_30MHZ		0x00000002	/* SRC=240MHz only */
#define UCAN_CLK_SEL_DIV_24MHZ		0x00000003	/* SRC=240MHz only */
#define UCAN_CLK_SEL_DIV_20MHZ		0x00000004	/* SRC=240MHz only */

#define UCAN_CLK_SEL_SRC_MASK		0x00000008	/* 0=80MHz, 1=240MHz */
#define UCAN_CLK_SEL_SRC_240MHZ		0x00000008
#define UCAN_CLK_SEL_SRC_80MHZ		(~UCAN_CLK_SEL_SRC_240MHZ & \
							UCAN_CLK_SEL_SRC_MASK)

#define UCAN_CLK_SEL_20MHZ	(UCAN_CLK_SEL_SRC_240MHZ|UCAN_CLK_SEL_DIV_20MHZ)
#define UCAN_CLK_SEL_24MHZ	(UCAN_CLK_SEL_SRC_240MHZ|UCAN_CLK_SEL_DIV_24MHZ)
#define UCAN_CLK_SEL_30MHZ	(UCAN_CLK_SEL_SRC_240MHZ|UCAN_CLK_SEL_DIV_30MHZ)
#define UCAN_CLK_SEL_40MHZ	(UCAN_CLK_SEL_SRC_240MHZ|UCAN_CLK_SEL_DIV_40MHZ)
#define UCAN_CLK_SEL_60MHZ	(UCAN_CLK_SEL_SRC_240MHZ|UCAN_CLK_SEL_DIV_60MHZ)
#define UCAN_CLK_SEL_80MHZ	(UCAN_CLK_SEL_SRC_80MHZ)

/* uCAN RX/Tx control register bits */
#define UCAN_CTL_UNC_BIT		0x00010000	/* Uncached DMA mem */
#define UCAN_CTL_RST_BIT		0x00020000	/* reset DMA action */
#define UCAN_CTL_IEN_BIT		0x00040000	/* IRQ enable */

/* default values for Count and Time Limit in Rx IRQ:
 * Count Limit is the maximum number of msgs saved in Rx DMA per IRQ, while
 * Time Limit is the delay (in 100 x µs) after which an IRQ is raised, even if
 * CL is not reached.
 *
 * CL  TL  Notes on 1xframe/ms CAN bus load (500k) on a single uCAN
 *  x   1  1 Rx msg
 *  5  10  2 (sometimes 3) Rx msgs BUT MSI sharing looses IRQ
 * 16  10  Windows driver values lost 1 irq at the beginning
 * 16  16  looses some irq on high bus load with shared MSIs
 * */
#define UCAN_CTL_IRQ_CL_DEF	16	/* Rx msg max nb per IRQ in Rx DMA */
#define UCAN_CTL_IRQ_TL_DEF	10	/* Time before IRQ if < CL (x 100 µs) */

#define UCAN_OPTIONS_SET	(UCAN_OPTION_ERROR|UCAN_OPTION_BUSLOAD)

/* Tx anticipation window (link logical address should be aligned on 2K
 * boundary) */
#define UCAN_PCI_LNK_COUNT	(UCAN_PCI_TX_DMA_SIZE / UCAN_PCI_TX_PAGE_SIZE)

#define UCAN_MSG_LNK_TX		0x1001	/* Tx msgs link */

struct ucan_pci_tx_link {
	__le16		size;
	__le16		type;
	__le32		laddr_lo;
	__le32		laddr_hi;
} __attribute__((packed, aligned(4)));

struct ucan_pci_rx_dma {
	struct ucan_pci_irq_status irq_status;	/* 32-bits */
	__le32	sys_time_low;
	__le32	sys_time_high;
	struct ucan_rx_msg msg[0];
} __attribute__((packed, aligned(4)));

extern int _pci_devices;

/* the PEAK uCAN PCI adapter object */
struct ucan_adapter {
	struct pcan_adapter adapter;	/* always first element */
	struct pci_dev *pci_dev;
	void __iomem *bar0_addr;
#ifdef UCAN_64BITS_CMD_MUST_BE_ATOMIC
	pcan_lock_t ucan_lock;
#endif
#ifdef UCAN_PCI_ENABLE_MSI
	int msi_count;
	int msi_step;
#ifdef UCAN_PCI_ENABLE_MSIX
	struct msix_entry msix_entries[4];
#endif
#endif

#ifdef DEBUG_IRQ_LOST
	unsigned int irq_pass;
	unsigned int irq_none;
	unsigned int lnk_set[4];
	unsigned int lnk_irq[4];
#endif

	struct pcandev pcan_dev[0];
};

static int ucan_pci_devices = 0;
static int ucan_pci_adapters = 0;

#ifdef UCAN_STANDALONE_PCI_DRIVER

#define PEAK_PCI_VENDOR_ID		0x001c
#define PEAK_PCIEFD10_ID		0x0010
#define PEAK_PCIEFD_ID			0x0013

static const struct pci_device_id ucan_pci_table[] = {
	{ PCI_DEVICE(PEAK_PCI_VENDOR_ID, PEAK_PCIEFD10_ID) },
	{ PCI_DEVICE(PEAK_PCI_VENDOR_ID, PEAK_PCIEFD_ID) },
	{}
};

MODULE_DEVICE_TABLE(pci, ucan_pci_table);
#endif

static void pcan_pci_ucan_hw_remove(struct pci_dev *dev);

#ifdef UCAN_USES_NON_COHERENT_DMA
#define ucan_dma_alloc(a, b, c, d)	dma_alloc_noncoherent(a, b, c, d)
#define ucan_dma_free(a, b, c, d)	dma_free_noncoherent(a, b, c, d)
#else
#define ucan_dma_alloc(a, b, c, d)	dma_alloc_coherent(a, b, c, d)
#define ucan_dma_free(a, b, c, d)	dma_free_coherent(a, b, c, d)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)
/* Note: these functions have been imported from the Kernel in which they have
 * been added. Since they're used only here, they are defined as static.
 */
#ifdef UCAN_PCI_ENABLE_MSI
/* Note: there's an issue between mainline kernels 3.13 and Ubuntu's 3.13:
 * pci_enable_msi_range() *ISNOT* part of mainline 3.13 Kernels since it has
 * been introduced in 3.14 *BUT* Ubutnu's Kernel 3.13.11 includes this function:
 *
 * $ grep LINUX_VERSION_CODE /usr/src/linux-headers-3.13.0-77-generic/include/generated/uapi/linux/version.h
 * #define LINUX_VERSION_CODE 199947
 *
 * (that is, Kernel v3.13.1 while:)
 *
 * $ uname -r
 * 3.13.0-77-generic
 *
 * $ grep pci_enable_msi_range /usr/src/linux-headers-3.13.0-77-generic/include/linux/pci.h
 * static inline int pci_enable_msi_range(struct pci_dev *dev, int minvec,
 * int pci_enable_msi_range(struct pci_dev *dev, int minvec, int maxvec);
 *
 * FIX: driver's Makfile greps local Kernel headers to decide by its own!
 * (-DFIX_PCIENABLEMSIRANGE)
 */
#ifdef FIX_PCIENABLEMSIRANGE
#define pci_enable_msi_range	ucan_pci_enable_msi_range
#endif

/**
 * pci_enable_msi_range - configure device's MSI capability structure
 * @dev: device to configure
 * @minvec: minimal number of interrupts to configure
 * @maxvec: maximum number of interrupts to configure
 *
 * This function tries to allocate a maximum possible number of interrupts in a
 * range between @minvec and @maxvec. It returns a negative errno if an error
 * occurs. If it succeeds, it returns the actual number of interrupts allocated
 * and updates the @dev's irq member to the lowest new interrupt number;
 * the other interrupt numbers allocated to this device are consecutive.
 **/
static int pci_enable_msi_range(struct pci_dev *dev, int minvec, int maxvec)
{
	int nvec = maxvec;
	int rc;

	if (maxvec < minvec)
		return -ERANGE;

	do {
		rc = pci_enable_msi_block(dev, nvec);
		if (rc < 0) {
			return rc;
		} else if (rc > 0) {
			if (rc < minvec)
				return -ENOSPC;
			nvec = rc;
		}
	} while (rc);

	return nvec;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34)
static inline int dma_set_coherent_mask(struct device *dev, u64 mask)
{
	if (!dma_supported(dev, mask))
		return -EIO;
	dev->coherent_dma_mask = mask;
	return 0;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
static inline int dma_set_mask_and_coherent(struct device *dev, u64 mask)
{
	int rc = dma_set_mask(dev, mask);
	if (rc == 0)
		dma_set_coherent_mask(dev, mask);
	return rc;
}
#endif /* KERNEL_VERSION(3,13,0) */
#endif /* KERNEL_VERSION(3,14,0) */

#ifdef DEBUG
static void ucan_pci_sys_dump(struct ucan_adapter *ucan_adapter,
				const char *prompt, int offset, int len)
{
	int i;

	if (len <= 0)
		len = 0x4c;
	pr_info(DEVICE_NAME ": %s SYS block [%u..%u] @%p:\n",
		prompt ? prompt : "", offset, offset+len,
		ucan_adapter->bar0_addr);
	for (i = 0; i <= len; ) {
		if (!(i % 16))
			pr_info(DEVICE_NAME ": %03x: ", offset + i);
		printk("%02x ", readb(ucan_adapter->bar0_addr + i));
		if (!(++i % 16))
			printk("\n");
	}
	if (i % 16)
		printk("\n");
}

static void ucan_pci_dev_dump(struct pcandev *dev, const char *prompt,
				int offset, int len)
{
	int i;

	if (len <= 0)
		len = 0x7c;
	pr_info(DEVICE_NAME ": %s uCAN%u block [%u..%u] @%p:\n",
		prompt ? prompt : "",
		dev->nChannel+1, offset, offset+len,
		dev->port.pci.pvVirtPort);
	for (i = 0; i <= len; ) {
		if (!(i % 16))
			pr_info(DEVICE_NAME ": %03x: ", offset + i);
		printk("%02x ", readb(dev->port.pci.pvVirtPort + offset + i));
		if (!(++i % 16))
			printk("\n");
	}
	if (i % 16)
		printk("\n");
}
#endif

#if defined(DEBUG_RXDMA) || defined(DEBUG_IRQ_RX)
static void ucan_pci_rx_dma_dump(struct pcandev *dev, const char *prompt,
				 int offset, int len)
{
	int i;

	if (!len)
		return;
	if (len < 0)
		len = 48; /* enough to get the channel */
	pr_info(DEVICE_NAME ": %s uCAN%u Rx DMA [%u..%u[ @%p:\n",
		prompt ? prompt : "",
		dev->nChannel+1, offset, offset+len,
		dev->port.pci.rx_dma_vaddr);
	for (i = 0; i < len; ) {
		if (!(i % 16))
			pr_info(DEVICE_NAME ": %03x: ", offset + i);

		printk("%02x ",
			*(u8 *)(dev->port.pci.rx_dma_vaddr + offset + i));

		if (!(++i % 16))
			printk("\n");
	}
	if (i % 16)
		printk("\n");
}
#endif

#ifdef DEBUG_WRITE
static void ucan_pci_tx_dma_dump(struct pcandev *dev, const char *prompt,
				 int offset, int len)
{
	struct ucan_pci_page *page =
			dev->port.pci.tx_pages + dev->port.pci.tx_page_index;
	int i;

	if (!len)
		return;
	if (len < 0)
		len = 128;
	pr_info(DEVICE_NAME ": %s uCAN%u Tx DMA page #%u (%p) [%u..%u[:\n",
		prompt ? prompt : "",
		dev->nChannel+1, dev->port.pci.tx_page_index, page->vbase,
		offset, offset+len);
	for (i = 0; i < len; ) {
		if (!(i % 16))
			pr_info(DEVICE_NAME ": %03x: ", offset + i);
		printk("%02x ", *(u8 *)(page->vbase + offset + i));
		if (!(++i % 16))
			printk("\n");
	}
	if (i % 16)
		printk("\n");
}
#endif

/* read a 32 bits value from SYS block register */
static inline u32 ucan_sys_readl(struct ucan_adapter *ucan_adapter, u16 addr)
{
	return readl(ucan_adapter->bar0_addr + addr);
}

/* write a 32 bits value into a SYS block register */
static inline void ucan_sys_writel(struct ucan_adapter *ucan_adapter,
				   u32 v, u16 addr)
{
#ifdef DEBUG
	pr_info(DEVICE_NAME ": %s(): %08x -> REG[%03x]\n", __func__, v, addr);
#endif
	writel(v, ucan_adapter->bar0_addr + addr);
}

/* read a 32 bits value from uCANx block register */
static inline u32 ucan_x_readl(struct pcandev *dev, u16 addr)
{
	return readl(dev->port.pci.pvVirtPort + addr);
}

/* write a 32 bits value into a uCANx block register */
static inline void ucan_x_writel(struct pcandev *dev, u32 v, u16 addr)
{
#ifdef DEBUG
	pr_info(DEVICE_NAME ": %s(): %08x -> CAN%u_REG[%03x]\n",
		__func__, v, dev->nChannel+1, addr);
#endif
	writel(v, dev->port.pci.pvVirtPort + addr);
}

/*
 * static int ucan_pci_send_cmd(struct pcandev *dev)
 */
static int ucan_pci_send_cmd(struct pcandev *dev)
{
#ifdef UCAN_64BITS_CMD_MUST_BE_ATOMIC
	struct ucan_adapter *ucan = pci_get_drvdata(dev->port.pci.pciDev);
	pcan_lock_irqsave_ctxt flags;

	/* to be sure that the 64-bits command is atomic */
	pcan_lock_get_irqsave(&ucan->ucan_lock, flags);
#endif

#ifdef DEBUG_CMD
	pr_info(DEVICE_NAME ": %s(CAN%u): %08x:%08x\n",
		__func__, dev->nChannel+1,
		le32_to_cpu(*(u32 *)dev->ucan.cmd_head),
		le32_to_cpu(*(u32 *)(dev->ucan.cmd_head + 4)));
#endif

	writel(*(u32 *)dev->ucan.cmd_head,
			dev->port.pci.pvVirtPort + UCAN_PCI_REG_CMD_PORT_L);
	writel(*(u32 *)(dev->ucan.cmd_head + 4),
			dev->port.pci.pvVirtPort + UCAN_PCI_REG_CMD_PORT_H);

#ifdef UCAN_64BITS_CMD_MUST_BE_ATOMIC
	pcan_lock_put_irqrestore(&ucan->ucan_lock, flags);
#endif
	return 0;
}

static void ucan_pci_dma_set(struct pcandev *dev)
{
	/* reset all RX_CTL bits */
	ucan_x_writel(dev, 0xffffffff, UCAN_PCI_REG_RX_CTL_CLR);

	/* set the RST bit for Rx */
	ucan_x_writel(dev, UCAN_CTL_RST_BIT, UCAN_PCI_REG_RX_CTL_SET);

	/* write the logical address to the DMA engine for this uCAN */
	ucan_x_writel(dev, (u32 )dev->port.pci.rx_dma_laddr,
						UCAN_PCI_REG_RX_DMA_ADDR_L);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	ucan_x_writel(dev, (u32 )(dev->port.pci.rx_dma_laddr >> 32),
						UCAN_PCI_REG_RX_DMA_ADDR_H);
#else
	ucan_x_writel(dev, 0, UCAN_PCI_REG_RX_DMA_ADDR_H);
#endif

#ifdef UCAN_USES_NON_COHERENT_DMA
	/* also indicates FPGA that DMA is not cacheable */
	ucan_x_writel(dev, UCAN_CTL_UNC_BIT, UCAN_PCI_REG_RX_CTL_SET);
#else
	/* the DMA memory is cacheable (useless since all bits are CLRred) */
	//ucan_x_writel(dev, UCAN_CTL_UNC_BIT, UCAN_PCI_REG_RX_CTL_CLR);
#endif

	/* reset all TX_CTL bits */
	ucan_x_writel(dev, 0xffffffff, UCAN_PCI_REG_TX_CTL_CLR);

	/* set the RST bit for Tx */
	ucan_x_writel(dev, UCAN_CTL_RST_BIT, UCAN_PCI_REG_TX_CTL_SET);

	/* write the logical address to the DMA engine for this uCAN */
	ucan_x_writel(dev, (u32 )dev->port.pci.tx_dma_laddr,
						UCAN_PCI_REG_TX_DMA_ADDR_L);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	ucan_x_writel(dev, (u32 )(dev->port.pci.tx_dma_laddr >> 32),
						UCAN_PCI_REG_TX_DMA_ADDR_H);
#else
	ucan_x_writel(dev, 0, UCAN_PCI_REG_TX_DMA_ADDR_H);
#endif

#ifdef UCAN_USES_NON_COHERENT_DMA
	/* also indicates FPGA that DMA is not cacheable */
	ucan_x_writel(dev, UCAN_CTL_UNC_BIT, UCAN_PCI_REG_TX_CTL_SET);
#else
	/* the DMA memory is cacheable (useless since all bits are CLRred) */
	//ucan_x_writel(dev, UCAN_CTL_UNC_BIT, UCAN_PCI_REG_TX_CTL_CLR);
#endif
}

/*
 * static int ucan_pci_dma_alloc(struct pcandev *dev)
 */
static int ucan_pci_dma_alloc(struct pcandev *dev)
{
	int i;

	/* allocate non-cacheable DMA'able 4KB memory area for Rx */
	dev->port.pci.rx_dma_vaddr =
		ucan_dma_alloc(&dev->port.pci.pciDev->dev,
				   UCAN_PCI_RX_DMA_SIZE,
				   &dev->port.pci.rx_dma_laddr,
				   GFP_KERNEL);
	if (!dev->port.pci.rx_dma_vaddr) {
		dev_err(&dev->port.pci.pciDev->dev,
				"%s: Rx dma_alloc_coherent(%u) failure\n",
				DEVICE_NAME, UCAN_PCI_RX_DMA_SIZE);
		goto failed;
	}

#ifdef DEBUG_DMA
	pr_info(DEVICE_NAME ": CAN%u Rx DMA=%p (%llx)\n",
		dev->nChannel+1, dev->port.pci.rx_dma_vaddr,
		dev->port.pci.rx_dma_laddr);
#endif

	/*
	 * TODO: is it useful to alloc DMA Tx descriptors if LISTEN_ONLY mode?
	 */

	/* allocate non-cacheable DMA'able 4KB memory area for Tx */
	dev->port.pci.tx_dma_vaddr =
		ucan_dma_alloc(&dev->port.pci.pciDev->dev,
				   UCAN_PCI_TX_DMA_SIZE,
				   &dev->port.pci.tx_dma_laddr,
				   GFP_KERNEL);
	if (!dev->port.pci.tx_dma_vaddr) {
		dev_err(&dev->port.pci.pciDev->dev,
				"%s: Tx dma_alloc_coherent(%u) failure\n",
				DEVICE_NAME, UCAN_PCI_TX_DMA_SIZE);

		goto free_rx_dma;
	}

	/* alloc Tx pages descriptors list */
	dev->port.pci.tx_pages = pcan_malloc(sizeof(struct ucan_pci_page) *
						UCAN_PCI_LNK_COUNT, GFP_KERNEL);
	if (!dev->port.pci.tx_pages) {
		dev_err(&dev->port.pci.pciDev->dev,
				"%s: failed to alloc %u pages table\n",
				DEVICE_NAME, UCAN_PCI_LNK_COUNT);
		goto free_tx_dma;
	}

#ifdef DEBUG_DMA
	pr_info(DEVICE_NAME ": CAN%u Tx DMA=%p (%llx)\n",
		dev->nChannel+1, dev->port.pci.tx_dma_vaddr,
		dev->port.pci.tx_dma_laddr);
#endif

	dev->port.pci.tx_pages_free = UCAN_PCI_LNK_COUNT - 1;
	dev->port.pci.tx_page_index = 0;

	dev->port.pci.tx_pages[0].vbase = dev->port.pci.tx_dma_vaddr;
	dev->port.pci.tx_pages[0].lbase = dev->port.pci.tx_dma_laddr;
	for (i = 0; i < UCAN_PCI_LNK_COUNT; i++) {
		dev->port.pci.tx_pages[i].offset = 0;
		dev->port.pci.tx_pages[i].size = UCAN_PCI_TX_PAGE_SIZE -
					sizeof(struct ucan_pci_tx_link);;
		if (i) {
			dev->port.pci.tx_pages[i].vbase =
				dev->port.pci.tx_pages[i-1].vbase +
							UCAN_PCI_TX_PAGE_SIZE;
			dev->port.pci.tx_pages[i].lbase =
				dev->port.pci.tx_pages[i-1].lbase +
							UCAN_PCI_TX_PAGE_SIZE;
		}
	}

#ifdef DEBUG
	ucan_pci_dev_dump(dev, __func__, 0, -1);
#endif

	return 0;

#if 0
free_pages_table:
	pcan_free(dev->port.pci.tx_pages);
	dev->port.pci.tx_pages = NULL;
#endif

free_tx_dma:
	ucan_dma_free(&dev->port.pci.pciDev->dev,
				  UCAN_PCI_TX_DMA_SIZE,
				  dev->port.pci.tx_dma_vaddr,
				  dev->port.pci.tx_dma_laddr);
	dev->port.pci.tx_dma_vaddr = NULL;

free_rx_dma:
	ucan_dma_free(&dev->port.pci.pciDev->dev,
				  UCAN_PCI_RX_DMA_SIZE,
				  dev->port.pci.rx_dma_vaddr,
				  dev->port.pci.rx_dma_laddr);
	dev->port.pci.rx_dma_vaddr = NULL;

failed:
	return -ENOMEM;
}

static void ucan_pci_dma_clr(struct pcandev *dev)
{
	/* reset all TX_CTL bits */
	ucan_x_writel(dev, 0xffffffff, UCAN_PCI_REG_TX_CTL_CLR);

	/* set the RST bit for Tx */
	ucan_x_writel(dev, UCAN_CTL_RST_BIT, UCAN_PCI_REG_TX_CTL_SET);

	/* write the logical address to the DMA engine for this uCAN */
	ucan_x_writel(dev, 0, UCAN_PCI_REG_TX_DMA_ADDR_L);
	ucan_x_writel(dev, 0, UCAN_PCI_REG_TX_DMA_ADDR_H);

	/* reset all RX_CTL bits */
	ucan_x_writel(dev, 0xffffffff, UCAN_PCI_REG_RX_CTL_CLR);

	/* set the RST bit for Rx */
	ucan_x_writel(dev, UCAN_CTL_RST_BIT, UCAN_PCI_REG_RX_CTL_SET);

	/* write the logical address to the DMA engine for this uCAN */
	ucan_x_writel(dev, 0, UCAN_PCI_REG_RX_DMA_ADDR_L);
	ucan_x_writel(dev, 0, UCAN_PCI_REG_RX_DMA_ADDR_H);
}

/*
 * static void ucan_pci_dma_free(struct pcandev *dev)
 */
static void ucan_pci_dma_free(struct pcandev *dev)
{
	/* just to be sure, not confident in pcan core... */
	if (dev->port.pci.tx_pages) {
		pcan_free(dev->port.pci.tx_pages);
		dev->port.pci.tx_pages = NULL;
	}

	if (dev->port.pci.tx_dma_vaddr) {

#ifdef DEBUG_FREE_DMA
		pr_info(DEVICE_NAME ": CAN%u Tx DMA=%p (%p) free\n",
			dev->nChannel+1,
			dev->port.pci.tx_dma_vaddr,
			(void *)dev->port.pci.tx_dma_laddr);
#endif
		ucan_dma_free(&dev->port.pci.pciDev->dev,
				  UCAN_PCI_TX_DMA_SIZE,
				  dev->port.pci.tx_dma_vaddr,
				  dev->port.pci.tx_dma_laddr);

		dev->port.pci.tx_dma_vaddr = NULL;
	}

	if (dev->port.pci.rx_dma_vaddr) {

#ifdef DEBUG_FREE_DMA
		pr_info(DEVICE_NAME ": CAN%u Rx DMA=%p (%p) free\n",
			dev->nChannel+1,
			dev->port.pci.rx_dma_vaddr,
			(void *)dev->port.pci.rx_dma_laddr);
#endif
		ucan_dma_free(&dev->port.pci.pciDev->dev,
				  UCAN_PCI_RX_DMA_SIZE,
				  dev->port.pci.rx_dma_vaddr,
				  dev->port.pci.rx_dma_laddr);

		dev->port.pci.rx_dma_vaddr = NULL;
	}

}

/*
 * static int __ucan_pci_device_write(struct pcandev *dev,
 *					struct pcan_udata *ctx)
 */
static int __ucan_pci_device_write(struct pcandev *dev, struct pcan_udata *ctx)
{
	PCI_PORT *pdpci = &dev->port.pci;
	struct ucan_pci_page *page = pdpci->tx_pages +
						dev->port.pci.tx_page_index;
	struct ucan_pci_tx_link *lk = NULL;
	int err;
#ifdef DEBUG_WRITE
	int len = 0, frc = 0;
	int offset = page->offset;
#endif

#ifdef DEBUG
	pr_info(DEVICE_NAME ": %s(CAN%u): head=%p end=%p (left=%u)\n",
		__func__, dev->nChannel+1,
		page->vbase + page->offset, page->vbase + page->size,
		page->size - page->offset);
#endif

	/* write as much messages as contained in the pcandev Tx FIFO */
	while (1) {

		err = ucan_encode_msg(dev, page->vbase + page->offset,
						page->size - page->offset);
		if (err < 0) {

			/* if not enough space in the current page, change it */
			if (err != -ENOSPC)
				break;

			/* no more page free ? */
			if (!pdpci->tx_pages_free) {
#if 1//def DEBUG_WRITE
				pr_info(DEVICE_NAME
					": %s(CAN%u): no more free pages\n",
					__func__, dev->nChannel+1);
#endif
				err = 0;
				break;
			}

			pdpci->tx_pages_free--;

			lk = page->vbase + page->offset;

#ifdef DEBUG_WRITE
			/* dump all that has been copied before inserting
			 * link */
			ucan_pci_tx_dma_dump(dev, __func__, offset, len);
			offset = 0;
			len = 0;
#endif

			/* retry in some other page */
			pdpci->tx_page_index = 1 - pdpci->tx_page_index;
			page = pdpci->tx_pages + pdpci->tx_page_index;

			/* put the link at the end of old page */
			lk->size = cpu_to_le16(sizeof(struct ucan_pci_tx_link));
			lk->type = cpu_to_le16(UCAN_MSG_LNK_TX);
			lk->laddr_lo = cpu_to_le32(page->lbase);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
			lk->laddr_hi = cpu_to_le32(page->lbase >> 32);
#else
			lk->laddr_hi = 0;
#endif

			/* next msgs will be put from the begininng */
			page->offset = 0;

			/* sure we'll get a Tx IRQ for this! */

#ifdef DEBUG_IRQ_LOST
			((struct ucan_adapter *)dev->adapter)->lnk_set[dev->nChannel]++;
#endif
#ifdef DEBUG_IRQ_TX
			pr_info(DEVICE_NAME ": CAN%u TX engine: STARTED\n",
				dev->nChannel+1);
#endif
#ifdef PCAN_USES_OLD_TX_ENGINE_STATE
			atomic_set(&dev->tx_engine_state, TX_ENGINE_STARTED);
#else
			dev->locked_tx_engine_state = TX_ENGINE_STARTED;
#endif
			continue;
		}

		if (err > 0) {
#ifdef DEBUG_WRITE
			frc++;
			len += err;
#endif
#ifdef DEBUG
			pr_info(DEVICE_NAME ": %s(CAN%u): %d bytes written\n",
				__func__, dev->nChannel+1, err);
#endif
			page->offset += err;
			dev->tx_frames_counter++;

			/* tell the device that one message has been written */
			ucan_x_writel(dev, cpu_to_le32(1),
						UCAN_PCI_REG_TX_REQ_ACC);
		}
	}

	/* no LNK inserted => no Tx IRQ => tell user to send by himself */
	if (!lk) {
#ifdef PCAN_USES_OLD_TX_ENGINE_STATE
#ifdef DEBUG_IRQ_TX
		if (atomic_read(&dev->tx_engine_state) != TX_ENGINE_STOPPED)
			pr_info(DEVICE_NAME ": CAN%u TX engine: STOPPED\n",
				dev->nChannel+1);
#endif
		atomic_set(&dev->tx_engine_state, TX_ENGINE_STOPPED);
#else
#ifdef DEBUG_IRQ_TX
		if (dev->locked_tx_engine_state != TX_ENGINE_STOPPED)
			pr_info(DEVICE_NAME ": CAN%u TX engine: STOPPED\n",
				dev->nChannel+1);
#endif
		dev->locked_tx_engine_state = TX_ENGINE_STOPPED;
#endif
	}

#ifdef DEBUG_WRITE
	/* if at least ONE message has been written, say it! */
	if (frc > 0) {
		pr_info(DEVICE_NAME
			": %u messages written (%u total, err %d):\n",
			frc, dev->tx_frames_counter, err);
		ucan_pci_tx_dma_dump(dev, __func__, offset, len);
	} else {
		pr_info(DEVICE_NAME
			": no new message written (err %d)\n",
			err);
	}
#endif

	return err;
}

static irqreturn_t ucan_pci_rx_irq_thread(int irq, void *arg)
{
	struct pcandev *dev = (struct pcandev *)arg;

	/* if some CAN messages were received, wake up any "waiting-for-read"
	 * task */
	if (dev->port.pci.irq_status.rx_cnt) {
#ifdef DEBUG_IRQ_RX
		pr_info(DEVICE_NAME
			": CAN%u rx_cnt=%u signaling reading task...\n",
			dev->nChannel+1, dev->port.pci.irq_status.rx_cnt);
#endif
		pcan_event_signal(&dev->in_event);
		return PCAN_IRQ_HANDLED;
	}

	return PCAN_IRQ_NONE;
}

static irqreturn_t ucan_pci_tx_irq_thread(int irq, void *arg)
{
	struct pcandev *dev = (struct pcandev *)arg;

	/* in case of Tx "page" complete IRQ, restart flushing tx fifo and
	 * wake up any "waiting-for-write" task in case of empty fifo */
	if (dev->port.pci.irq_status.lnk) {
		int err;
#ifndef UCAN_LOCK_ENTIRE_ISR
		pcan_lock_irqsave_ctxt flags;
		pcan_lock_get_irqsave(&dev->isr_lock, flags);
#endif

#ifdef DEBUG_IRQ_LOST
		((struct ucan_adapter *)dev->adapter)->lnk_irq[dev->nChannel]++;
#endif
		dev->port.pci.tx_pages_free++;

		/* restart flushing output queue */
		err = __ucan_pci_device_write(dev, NULL);

#ifndef UCAN_LOCK_ENTIRE_ISR
		pcan_lock_put_irqrestore(&dev->isr_lock, flags);
#endif

#if 0
		if (err == -ENODATA) {
#else
		/* err == 0 => no more free page in Tx DMA area */
		if (err) {
#endif
#ifdef DEBUG_IRQ_TX
			pr_info(DEVICE_NAME
				": CAN%u lnk=%u signaling writing task...\n",
				dev->nChannel+1, dev->port.pci.irq_status.lnk);
#endif
			pcan_event_signal(&dev->out_event);

#ifdef NETDEV_SUPPORT
			if (dev->netdev)
				netif_wake_queue(dev->netdev);
#endif
		}

		return PCAN_IRQ_HANDLED;
	}

	return PCAN_IRQ_NONE;
}

/*
 * static irqreturn_t ucan_pci_irq_thread(int irq, void *arg)
 *
 * the threaded part of an IRQ: handle all that don't deal with hardware.
 */
static irqreturn_t ucan_pci_irq_thread(int irq, void *arg)
{
#ifdef UCAN_PCI_USE_THREADED_IRQ

	/* wakeup process in the threaded half */
	return PCAN_IRQ_WAKE_THREAD;
#else
	/* if some CAN messages were received, wake up any "waiting-for-read"
	 * task */
	ucan_pci_rx_irq_thread(irq, arg);

	/* in case of Tx "page" complete IRQ, wake up any "waiting-for-write"
	 * task */
	ucan_pci_tx_irq_thread(irq, arg);

	return PCAN_IRQ_HANDLED;
#endif
}

/*
 * ACK hardware to re-enable DMA
 */
static void ucan_pci_dma_ack(struct pcandev *dev)
{
#if 0
	/* give a tag which value is not the value of 1st u32 in DMA area */
	dev->port.pci.irq_tag = le32_to_cpu(*(u32 *)dev->port.pci.rx_dma_vaddr);
#endif
	dev->port.pci.irq_tag++;
	dev->port.pci.irq_tag &= 0xf;

	ucan_x_writel(dev, cpu_to_le32(dev->port.pci.irq_tag),
					UCAN_PCI_REG_RX_CTL_ACK);
}

#ifdef DEBUG_IRQ_LOST
static void display_dev_stats(struct ucan_adapter *ua, struct pcandev *dev)
{
	int i = dev->nChannel;

	pr_info(DEVICE_NAME
		": CAN%u tx_eng=%u dev_tag=%u hw_tag=%u rx_cnt=%u lnk=%u "
		"[int=%u tx=%u rx=%u lnk_set=%u lnk_irq=%u]\n",
		dev->nChannel+1, dev->locked_tx_engine_state,
		dev->port.pci.irq_tag, dev->port.pci.irq_status.irq_tag,
		dev->port.pci.irq_status.rx_cnt, dev->port.pci.irq_status.lnk,
		dev->dwInterruptCounter,
		dev->tx_frames_counter, dev->rx_frames_counter,
		ua->lnk_set[i], ua->lnk_irq[i]);
}

static void display_stats(struct ucan_adapter *ua)
{
	struct pcandev *dev = ua->pcan_dev;
	int i;

	for (i = 0; i < ua->adapter.can_count; i++, dev++)
		if (dev->flags & PCAN_DEV_OPENED)
			display_dev_stats(ua, dev);
}
#endif

/*
 * static irqreturn_t ucan_pci_irq_handler(int irq, void *arg)
 * 
 * the hardware part of the IRQ: ack as fast as possibel what blocks hw.
 */
static irqreturn_t ucan_pci_irq_handler(int irq, void *arg)
{
	struct pcandev *dev = (struct pcandev *)arg;
	struct ucan_pci_rx_dma *rx_dma =
			(struct ucan_pci_rx_dma *)dev->port.pci.rx_dma_vaddr;
	int err = PCAN_IRQ_NONE;
#ifdef DEBUG_IRQ_LOST
	struct ucan_adapter *ua = pci_get_drvdata(dev->port.pci.pciDev);
#endif
#ifdef UCAN_LOCK_ENTIRE_ISR
	pcan_lock_irqsave_ctxt flags;

	pcan_lock_get_irqsave(&dev->isr_lock, flags);
#endif

	/* INTA mode only: dummy read to finalize PCIe transactions */
#ifdef UCAN_PCI_ENABLE_MSI
	if (!pci_dev_msi_enabled(dev->port.pci.pciDev))
#endif
		(void )ucan_sys_readl(pci_get_drvdata(dev->port.pci.pciDev),
					UCAN_PCI_REG_VER1);

	/* read the irq status from the 1st 32-bits of the Rx DMA area */
	dev->port.pci.irq_status = *(struct ucan_pci_irq_status *)rx_dma;

#ifdef DEBUG_IRQ_LOST
	ua->irq_pass++;
#endif
	/* check if IRQ is for me: if the IRQ tag read in my Rx DMA area does
	 * match the one I'm waiting for, the IRQ does concern me! */
	if (dev->port.pci.irq_status.irq_tag != dev->port.pci.irq_tag) {
			goto irq_handler_exit;
	}

#if defined(DEBUG_IRQ_RX)
	if (dev->port.pci.irq_status.rx_cnt)
		pr_info(DEVICE_NAME ": %s(CAN%u): irq_tag=%02xh rx_cnt=%u\n",
			__func__, dev->nChannel+1,
			dev->port.pci.irq_status.irq_tag,
			dev->port.pci.irq_status.rx_cnt);
#endif
#if defined(DEBUG_IRQ_TX)
	if (dev->port.pci.irq_status.lnk)
		pr_info(DEVICE_NAME ": %s(CAN%u): irq_tag=%02xh lnk=%u\n",
			__func__, dev->nChannel+1,
			dev->port.pci.irq_status.irq_tag,
			dev->port.pci.irq_status.lnk);
#endif

	if (!dev->port.pci.irq_status.rx_cnt && !dev->port.pci.irq_status.lnk) {
#ifdef DEBUG_IRQ_SPURIOUS
		pr_warn(DEVICE_NAME
			": CAN%u got spurious interrupt (rx_cnt=%u lnk=%u)\n",
			dev->nChannel+1,
			dev->port.pci.irq_status.rx_cnt,
			dev->port.pci.irq_status.lnk);
#endif
		goto irq_handler_exit;
	}

	dev->dwInterruptCounter++;

	/* keep adapter time for further sync */
	pcan_sync_times(dev, le32_to_cpu(rx_dma->sys_time_low),
				le32_to_cpu(rx_dma->sys_time_high), 0);

#if defined(DEBUG_RXDMA) || defined(DEBUG_IRQ_RX)
	/* dump Rx DMA area before handling its content */
	ucan_pci_rx_dma_dump(dev, __func__, 0, -1);
#endif

	/* handle all the incoming messages (if any) */
	err = ucan_handle_msgs_list(&dev->ucan, rx_dma->msg,
					dev->port.pci.irq_status.rx_cnt);
	switch (err) {

	case 0:
		/* this is normal */
		if (!dev->port.pci.irq_status.rx_cnt)
			break;

	case -ENOSPC:
		/* lost dev->port.pci.irq_status.rx_cnt frame(s) */
		break;

	default:
		/* err frames have been saved */
		if (err > 0) {
			/* lost dev->port.pci.irq_status.rx_cnt-err frame(s) */
			break;
		}

		pr_err(DEVICE_NAME
			": %s(CAN%u): failed to handle rx msgs list err %d\n",
			__func__, dev->nChannel+1, err);
		break;
	}

#if 0
	/* re-enable DMA transfer for this uCAN asap */
	ucan_pci_dma_ack(dev);
#endif

	/* wakeup tasks now and/or continue writing */
	err = ucan_pci_irq_thread(irq, arg);

#if 1
	/* re-enable DMA transfer for this uCAN */
	ucan_pci_dma_ack(dev);
#endif

irq_handler_exit:
#ifdef DEBUG_IRQ_LOST
	if (err == PCAN_IRQ_NONE)
		ua->irq_none++;

	/* use '==' to avoid reentrance */
	if (ua->irq_pass == ua->adapter.opened_count) {

		/* if IRQ has not be handled at all, display it */
		if (ua->irq_none >= ua->adapter.opened_count) {
			pr_info(DEVICE_NAME
				": unhandled IRQ (%u = %uxCANs)\n",
				ua->irq_none, ua->adapter.opened_count);

			display_stats(ua);
		}

		ua->irq_pass = 0;
		ua->irq_none = 0;
	}
#endif

#ifdef UCAN_LOCK_ENTIRE_ISR
	pcan_lock_put_irqrestore(&dev->isr_lock, flags);
#endif

	return err;
}

#ifndef NO_RT
/* RT version of the IRQ handler */
static int ucan_pci_irq_handler_rt(rtdm_irq_t *irq_context)
{
	struct pcandev *dev = rtdm_irq_get_arg(irq_context, struct pcandev);

	return ucan_pci_irq_handler(dev->wIrq, dev);
}
#endif

#ifdef DEBUG_TIMER_PERIOD_MS
static void ucan_pci_polling_timer(unsigned long arg)
{
	struct pcandev *dev = (struct pcandev *)arg;

	ucan_pci_dev_dump(dev, __func__, 0, -1);

	mod_timer(&dev->polling_timer,
			jiffies + msecs_to_jiffies(DEBUG_TIMER_PERIOD_MS));
}
#endif

/*
 * static int ucan_pci_driver_open(struct pcandev *dev)
 *
 * 1st callback called  by pcan_open_path(),
 * before req_irq() (ucan_pci_driver_req_irq()).
 */
static int ucan_pci_driver_open(struct pcandev *dev)
{
	int err = 0;

#if defined(DEBUG)
	pr_info(DEVICE_NAME ": %s(CAN%u)\n", __func__, dev->nChannel+1);
#endif
	/* Note: the following commands MUST be done in RESET mode */
	ucan_set_bus_off(dev);

	/* to force to init sync objects */
	dev->time_sync.ts_us = 0;

	/* reset timestamps */
	ucan_x_writel(dev, 0, UCAN_PCI_REG_MISC);

#ifdef UCAN_PCI_OPEN_ALLOC_DMA
	/* allocate non-cacheable DMA'able 4KB memory areas */
	err = ucan_pci_dma_alloc(dev);
	if (err)
		return err;

	ucan_pci_dma_set(dev);
#endif

	return err;
}

/*
 * static int ucan_pci_driver_req_irq(struct pcandev *dev)
 *
 * 2nd calbacks called by pcan_open_path(),
 * after open() (ucan_pci_driver_open())
 */
static int ucan_pci_driver_req_irq(struct pcandev *dev, struct pcan_udata *ctx)
{
	int irq_flags = IRQF_SHARED;
	int err;

#ifdef UCAN_PCI_ENABLE_MSI
	/* if driver got the requested count of MSI, IRQ is not shared */
	if (pci_dev_msi_enabled(dev->port.pci.pciDev))
		if (!(dev->flags & PCAN_DEV_MSI_SHARED))
			irq_flags &= ~IRQF_SHARED;
#endif /* UCAN_PCI_ENABLE_MSI */

#if defined(DEBUG) || defined(DEBUG_IRQ_RX) || defined(DEBUG_IRQ_TX)
	pr_info(DEVICE_NAME ": %s(CAN%u) irq_flags=%0xh ctx=%p\n",
		__func__, dev->nChannel+1, irq_flags, ctx);
#endif

#ifndef NO_RT
	/* RT irq requesting */
	err = rtdm_irq_request(&dev->irq_handle,
			dev->wIrq,
			ucan_pci_irq_handler_rt,
			RTDM_IRQTYPE_SHARED | RTDM_IRQTYPE_EDGE,
			ctx->context->device->proc_name,
			dev);

#elif defined(UCAN_PCI_USE_THREADED_IRQ)
	/* using kernel threaded interrupt */
	err = request_threaded_irq(dev->wIrq, ucan_pci_irq_handler,
			ucan_pci_irq_thread, irq_flags, DEVICE_NAME, dev);
#else
	/* using legacy interrupt mechanism */
	err = request_irq(dev->wIrq, ucan_pci_irq_handler,
			irq_flags, DEVICE_NAME, dev);
#endif
	if (err)
		goto fail_req_irq;

#if 0
	pr_info("%s: %s CAN%u requested irq=%u\n",
		DEVICE_NAME, dev->adapter->name, dev->nChannel+1, dev->wIrq);
#endif

#ifdef DEBUG_TIMER_PERIOD_MS
	setup_timer(&dev->polling_timer, ucan_pci_polling_timer,
			                    (unsigned long )dev);
	mod_timer(&dev->polling_timer,
			jiffies + msecs_to_jiffies(DEBUG_TIMER_PERIOD_MS));
#endif
#ifdef DEBUG_IRQ_LOST
	{
		struct ucan_adapter *ua = (struct ucan_adapter *)dev->adapter;

		ua->lnk_irq[dev->nChannel] = 0;
		ua->lnk_set[dev->nChannel] = 0;
	}
#endif

	/* write max count of msgs per IRQ */
	ucan_x_writel(dev, (UCAN_CTL_IRQ_TL_DEF) << 8 | UCAN_CTL_IRQ_CL_DEF,
			UCAN_PCI_REG_RX_CTL_WRT);

	/* enable IRQ for this uCAN */
	ucan_x_writel(dev, UCAN_CTL_IEN_BIT, UCAN_PCI_REG_RX_CTL_SET);

	/* clear DMA RST for Rx */
	ucan_x_writel(dev, UCAN_CTL_RST_BIT, UCAN_PCI_REG_RX_CTL_CLR);

	/* clear DMA RST for Tx */
	ucan_x_writel(dev, UCAN_CTL_RST_BIT, UCAN_PCI_REG_TX_CTL_CLR);

	/* do an initial ACK */
	dev->port.pci.irq_tag = (u32 )jiffies;
	ucan_pci_dma_ack(dev);

#ifdef DEBUG
	/* RX_DMA_ADDR_L should have changed (+0xc) */
	ucan_pci_dev_dump(dev, __func__, 0, -1);
#endif

	return 0;

fail_req_irq:
	pr_err(DEVICE_NAME
		": failed to request irq %d flags=%0xh ctx=%p (err %d)\n",
		dev->wIrq, irq_flags, ctx, err);

	return err;
}

/*
 * static void ucan_pci_driver_free_irq(struct pcandev *dev)
 *
 * Third (and last) callback called by close(),
 * after release() (ucan_pci_driver_release()) and
 * after device_release() (ucan_pci_device_release())
 */
static void ucan_pci_driver_free_irq(struct pcandev *dev,
					struct pcan_udata *ctx)
{
	int i;

#if defined(DEBUG) || defined(DEBUG_IRQ_RX) || defined(DEBUG_IRQ_TX)
	pr_info(DEVICE_NAME ": %s(CAN%u)\n", __func__, dev->nChannel+1);
#endif
	/* disable IRQ for this uCAN */
	ucan_x_writel(dev, UCAN_CTL_IEN_BIT, UCAN_PCI_REG_RX_CTL_CLR);

	/* make several dummy reads */
	for (i = 0; i < 3; i++)
		ucan_sys_readl(pci_get_drvdata(dev->port.pci.pciDev),
					UCAN_PCI_REG_VER1);

#ifdef DEBUG_TIMER_PERIOD_MS
	del_timer_sync(&dev->polling_timer);
#endif

#if 0
	pr_info("%s: %s CAN%u free irq=%u (open=%d)\n",
		DEVICE_NAME, dev->adapter->name, dev->nChannel+1,
		dev->wIrq, dev->nOpenPaths);
#endif

#ifdef NO_RT
	free_irq(dev->wIrq, dev);
#else
	rtdm_irq_free(&dev->irq_handle);
#endif

#ifdef DEBUG_IRQ_LOST
	{
		struct ucan_adapter *ua = (struct ucan_adapter *)dev->adapter;

		ua->lnk_irq[dev->nChannel] = 0;
		ua->lnk_set[dev->nChannel] = 0;
	}
#endif

#ifdef UCAN_PCI_OPEN_ALLOC_DMA
	ucan_pci_dma_clr(dev);

	/* free DMA after free_irq() */
	ucan_pci_dma_free(dev);
#endif
}

/*
 * static int ucan_pci_driver_release(struct pcandev *dev)
 *
 * Second callback called by close(),
 * after release() (ucan_pci_driver_release()) and
 * before free_irq() (ucan_pci_driver_free_irq())
 */
static int ucan_pci_driver_release(struct pcandev *dev)
{
#if defined(DEBUG) || defined(DEBUG_IRQ_RX) || defined(DEBUG_IRQ_TX)
	pr_info(DEVICE_NAME ": %s(CAN%u)\n", __func__, dev->nChannel+1);
#endif

	/* RST DMA engines (Tx first) */
	ucan_tx_abort(dev, UCAN_TX_ABORT_FLUSH);
	ucan_x_writel(dev, UCAN_CTL_RST_BIT, UCAN_PCI_REG_TX_CTL_SET);

	ucan_x_writel(dev, UCAN_CTL_RST_BIT, UCAN_PCI_REG_RX_CTL_SET);

	return 0;
}

/*
 * static int ucan_pci_driver_cleanup(struct pcandev *dev)
 *
 * called when device is removed from the devices list.
 */
static int ucan_pci_driver_cleanup(struct pcandev *dev)
{
#if defined(DEBUG) || defined(DEBUG_IRQ_RX) || defined(DEBUG_IRQ_TX)
	pr_info(DEVICE_NAME ": %s(CAN%u)\n", __func__, dev->nChannel+1);
#endif

#ifndef UCAN_PCI_OPEN_ALLOC_DMA
	ucan_pci_dma_clr(dev);
	ucan_pci_dma_free(dev);
#endif

	pcan_dev_remove_from_list(dev);

	dev->filter = pcan_delete_filter_chain(dev->filter);

	if (_pci_devices > 0)
		_pci_devices--;

	if (ucan_pci_devices > 0) {
		ucan_pci_devices--;

		/* in case we have been called from the module cleanup()
		 * function, then if the last dev is being destroyed, then the
		 * driver must be removed too */
		if (!ucan_pci_devices)
			pcan_pci_ucan_hw_remove(dev->port.pci.pciDev);
	}

	return 0;
}

/*
 * static int ucan_pci_device_open_fd(struct pcandev *dev,
 *					struct pcanfd_init *pfdi)
 *
 * ioctl(init).
 */
static int ucan_pci_device_open_fd(struct pcandev *dev,
					struct pcanfd_init *pfdi)
{
	int err;

#if defined(DEBUG) || defined(DEBUG_IRQ_RX) || defined(DEBUG_IRQ_TX)
	pr_info(DEVICE_NAME ": %s(CAN%u, clk=%u Hz)\n",
		__func__, dev->nChannel+1, pfdi->clock_Hz);
#endif

	/* not allowed to write until receiving 1st STATUS message */
#ifdef DEBUG_IRQ_TX
	pr_info(DEVICE_NAME ": CAN%u TX engine: IDLE\n", dev->nChannel+1);
#endif
#ifdef PCAN_USES_OLD_TX_ENGINE_STATE
	atomic_set(&dev->tx_engine_state, TX_ENGINE_IDLE);
#else
	dev->locked_tx_engine_state = TX_ENGINE_IDLE;
#endif

#if 0
	/* suppose already in RESET mode */
#else
	/* Note: the following commands MUST be done in RESET mode
	 * Note2: consider that several INIT can be done without close()*/
	ucan_set_bus_off(dev);
#endif

#if 0
	/* enable IRQ for this uCAN */
	ucan_x_writel(dev, UCAN_CTL_IEN_BIT, UCAN_PCI_REG_RX_CTL_SET);

	/* clear DMA RST for Rx */
	ucan_x_writel(dev, UCAN_CTL_RST_BIT, UCAN_PCI_REG_RX_CTL_CLR);

	/* clear DMA RST for Tx */
	ucan_x_writel(dev, UCAN_CTL_RST_BIT, UCAN_PCI_REG_TX_CTL_CLR);

	/* do an initial ACK */
	ucan_pci_dma_ack(dev);
#endif

	err = ucan_device_open_fd(dev, pfdi);
	if (err) {
		pr_err(DEVICE_NAME
			": failed to open PCI device CAN%u (err %d)\n",
			dev->nChannel+1, err);
		goto fail;
	}

	/* ask for bus_load info and rx/tx error counters */
	err = ucan_set_options(dev, UCAN_OPTIONS_SET);
	if (err) {
		pr_warn(DEVICE_NAME
			": failed to enable option %04xh (err %d)\n",
			UCAN_OPTIONS_SET, err);
	}

	err = ucan_set_bus_on(dev);
	if (err) {
		pr_err(DEVICE_NAME
			": failed to set PCI CAN%u in operational state "
			"(err %d)\n", dev->nChannel+1, err);
		goto fail;
	}

	return 0;

fail:
	ucan_pci_driver_free_irq(dev, NULL);
	return err;
}

/*
 * static int ucan_pci_device_write(struct pcandev *dev, struct pcan_udata *ctx)
 */
static int ucan_pci_device_write(struct pcandev *dev, struct pcan_udata *ctx)
{
	int err;
	//pcan_lock_irqsave_ctxt flags;

	//pcan_lock_get_irqsave(&dev->isr_lock, flags);

	err = __ucan_pci_device_write(dev, ctx);

	//pcan_lock_put_irqrestore(&dev->isr_lock, flags);

	return err;
}

#ifdef UCAN_PCI_ASYNC_CLOSE
static int ucan_pci_continue_reset(struct pcandev *dev)
{
}
#endif

/*
 * static void ucan_pci_device_release(struct pcandev *dev)
 *
 * First callback called by close(),
 * before release() (ucan_pci_driver_release()) and
 * before free_irq() (ucan_pci_driver_free_irq())
 */
static void ucan_pci_device_release(struct pcandev *dev)
{
#if defined(DEBUG) || defined(DEBUG_IRQ_RX) || defined(DEBUG_IRQ_TX)
	pr_info(DEVICE_NAME ": %s(CAN%u)\n", __func__, dev->nChannel+1);
#endif

#ifdef UCAN_PCI_ASYNC_CLOSE
	/* reset path */
	ucan_reset_path(dev);
#else
	/* remove notifications */
	ucan_clr_options(dev, UCAN_OPTIONS_SET);

	/* bus off */
	ucan_set_bus_off(dev);
#endif
}

/* decode uCAN message timestamp */
static void ucan_pci_decode_timestamp(struct pcandev *dev,
				      u32 ts_low, u32 ts_high,
				      struct timeval *tv)
{
	pcan_sync_decode(dev, ts_low, ts_high, tv);
}

/* handle a UCAN_MSG_CAN_RX message read from Rx DMA */
static int ucan_pci_handle_canrx(struct ucan_engine *ucan,
					struct ucan_msg *rx_msg, void *arg)
{
	struct ucan_rx_msg *rm = (struct ucan_rx_msg *)rx_msg;
	struct pcandev *dev = (struct pcandev *)arg;
	struct timeval tv;

	ucan_pci_decode_timestamp(dev, le32_to_cpu(rm->ts_low),
					le32_to_cpu(rm->ts_high), &tv);
	return ucan_post_canrx_msg(dev, rm, &tv);
}

/* handle a UCAN_MSG_ERROR message recieved from Rx DMA area */
static int ucan_pci_handle_error(struct ucan_engine *ucan,
					struct ucan_msg *rx_msg, void *arg)
{
	struct ucan_error_msg *er = (struct ucan_error_msg *)rx_msg;
	struct pcandev *dev = (struct pcandev *)arg;
	struct timeval tv;

#if 0//def DEBUG
	pr_info(DEVICE_NAME ": %s(%s CAN%u): rx=%u tx=%u\n", __func__,
			dev->adapter->name, dev->nChannel+1,
			er->rx_err_cnt, er->tx_err_cnt);
#endif

	ucan_pci_decode_timestamp(dev, le32_to_cpu(er->ts_low),
					le32_to_cpu(er->ts_high), &tv);
	return ucan_post_error_msg(dev, er, &tv);
}

/* handle a UCAN_MSG_BUSLOAD message recieved from Rx DMA area */
static int ucan_pci_handle_bus_load(struct ucan_engine *ucan,
					struct ucan_msg *rx_msg, void *arg)
{
	struct ucan_bus_load_msg *bl = (struct ucan_bus_load_msg *)rx_msg;
	struct pcandev *dev = (struct pcandev *)arg;
	struct timeval tv;

#if 0//def DEBUG
	pr_info(DEVICE_NAME ": %s(%s CAN%u): rx=%u tx=%u\n", __func__,
			dev->adapter->name, dev->nChannel+1,
			er->rx_err_cnt, er->tx_err_cnt);
#endif

	ucan_pci_decode_timestamp(dev, le32_to_cpu(bl->ts_low),
					le32_to_cpu(bl->ts_high), &tv);
	return ucan_post_bus_load_msg(dev, bl, &tv);
}

/* handle a UCAN_MSG_STATUS message recieved from Rx DMA area */
static int ucan_pci_handle_status(struct ucan_engine *ucan,
					struct ucan_msg *rx_msg, void *arg)
{
	struct ucan_status_msg *st = (struct ucan_status_msg *)rx_msg;
	struct pcandev *dev = (struct pcandev *)arg;
	struct timeval tv;
	int err;
#ifndef UCAN_LOCK_ENTIRE_ISR
	pcan_lock_irqsave_ctxt flags;
#endif

#if defined(DEBUG_WRITE) || defined(DEBUG_IRQ_RX) || defined(DEBUG_IRQ_TX)
	pr_info(DEVICE_NAME ": %s(CAN%u): EP=%u EW=%u BO=%u\n",
		__func__, dev->nChannel+1, !!UCAN_STMSG_PASSIVE(st),
		!!UCAN_STMSG_WARNING(st), !!UCAN_STMSG_BUSOFF(st));
#endif

#ifdef UCAN_PCI_ASYNC_CLOSE
	/* this STATUS is the CNF of the CMD_RX_BARRIER */
	if (UCAN_STMSG_RB(st))
		return ucan_pci_continue_reset(dev);
#endif
		
	ucan_pci_decode_timestamp(dev, le32_to_cpu(st->ts_low),
					le32_to_cpu(st->ts_high), &tv);
	err = ucan_post_status_msg(dev, st, &tv);

	/* this case tells that bus is ok (no error case): (re)start writing */
	//if (!(dev->wCANStatus & CAN_ERR_BUSOFF) &&
	if (dev->bus_state == PCANFD_ERROR_BUSOFF)
		return err;

#ifdef PCAN_USES_OLD_TX_ENGINE_STATE
	if (atomic_read(&dev->tx_engine_state) != TX_ENGINE_IDLE)
		return err;
#else
#ifndef UCAN_LOCK_ENTIRE_ISR
	pcan_lock_get_irqsave(&dev->isr_lock, flags);
#endif
	if (dev->locked_tx_engine_state != TX_ENGINE_IDLE) {
#ifndef UCAN_LOCK_ENTIRE_ISR
		pcan_lock_put_irqrestore(&dev->isr_lock, flags);
#endif
		return err;
	}
#endif

	err = __ucan_pci_device_write(dev, NULL);

#ifndef UCAN_LOCK_ENTIRE_ISR
	pcan_lock_put_irqrestore(&dev->isr_lock, flags);
#endif
	/* err == 0 => no more free page in Tx DMA area */
	if (err) {

#if defined(DEBUG_WRITE) || defined(DEBUG_IRQ_TX)
		pr_info(DEVICE_NAME ": %s(CAN%u): signaling writing task\n",
			__func__, dev->nChannel+1);
#endif

		pcan_event_signal(&dev->out_event);

#ifdef NETDEV_SUPPORT
		/* restart netdev in case pcan fifo was full */
		if (dev->netdev)
			netif_wake_queue(dev->netdev);
#endif
		err = 0;
	}

	return err;
}

/* select the clock domain by wrting the corresponding register */
static int ucan_pci_set_clk_domain(struct pcandev *dev,
					struct pcanfd_init *pfdi)
{
	/* select the 80MHz clock for the CAN with UCAN_PCI_REG_CLK_SEL */
	switch (pfdi->clock_Hz) {
	case 20000000:
		ucan_x_writel(dev, UCAN_CLK_SEL_20MHZ, UCAN_PCI_REG_CLK_SEL);
		break;
	case 24000000:
		ucan_x_writel(dev, UCAN_CLK_SEL_24MHZ, UCAN_PCI_REG_CLK_SEL);
		break;
	case 30000000:
		ucan_x_writel(dev, UCAN_CLK_SEL_30MHZ, UCAN_PCI_REG_CLK_SEL);
		break;
	case 40000000:
		ucan_x_writel(dev, UCAN_CLK_SEL_40MHZ, UCAN_PCI_REG_CLK_SEL);
		break;
	case 60000000:
		ucan_x_writel(dev, UCAN_CLK_SEL_60MHZ, UCAN_PCI_REG_CLK_SEL);
		break;
	case 80000000:
		ucan_x_writel(dev, UCAN_CLK_SEL_80MHZ, UCAN_PCI_REG_CLK_SEL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/* interface functions used to send commands / handle msgs to PCI/uCAN */
static int (*ucan_pci_handlers[])(struct ucan_engine *,
				  struct ucan_msg *,
				  void *) = {
	[UCAN_MSG_CAN_RX] = ucan_pci_handle_canrx,
#if 0
	/* this handler doesn't push any STATUS msg when rx/tx errors counters
	 * change */
	[UCAN_MSG_ERROR] = ucan_handle_error,
	[UCAN_MSG_BUSLOAD] = ucan_handle_bus_load,
#else
	/* this handler pushes STATUS msgs when rx/tx errors counters change */
	[UCAN_MSG_ERROR] = ucan_pci_handle_error,
	[UCAN_MSG_BUSLOAD] = ucan_pci_handle_bus_load,
#endif
	[UCAN_MSG_STATUS] = ucan_pci_handle_status,
};

static struct ucan_ops ucan_pci_driver_ops = {
	.set_clk_domain = ucan_pci_set_clk_domain,
	.send_cmd = ucan_pci_send_cmd,
	.handle_msg_table = ucan_pci_handlers,
	.handle_msg_size = ARRAY_SIZE(ucan_pci_handlers),
};

/* probe one uCAN channel */
static int ucan_pci_dev_probe(struct ucan_adapter *ucan_adapter, int c)
{
	struct pcandev *dev = ucan_adapter->pcan_dev + c;

	dev->nChannel = c;

#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif

	ucan_soft_init(dev, "pcifd", HW_PCI_FD);

	dev->ucPhysicallyInstalled = 1;

	dev->open = ucan_pci_driver_open;
	dev->req_irq = ucan_pci_driver_req_irq;
	dev->free_irq = ucan_pci_driver_free_irq;
	dev->release = ucan_pci_driver_release;
	dev->filter = pcan_create_filter_chain();
	dev->cleanup = ucan_pci_driver_cleanup;

	/* pcan chardev interface */
	dev->device_open_fd = ucan_pci_device_open_fd;
	dev->device_write = ucan_pci_device_write;
	dev->device_release = ucan_pci_device_release;

	dev->port.pci.pciDev = ucan_adapter->pci_dev;
	dev->port.pci.pvVirtConfigPort = ucan_adapter->bar0_addr;
	dev->port.pci.pvVirtPort = ucan_adapter->bar0_addr +
							UCAN_PCI_CANx_ADDR(c);
	dev->dwPort = (u32 )(long )dev->port.pci.pvVirtPort;
#ifdef DEBUG
	ucan_pci_dev_dump(dev, __func__, 0, -1);
#endif
	/* init uCAN engine: here, the uCAN handles one pcandev */
	dev->ucan.ops = &ucan_pci_driver_ops;
	dev->ucan.devs = ucan_adapter->pcan_dev;
	dev->ucan.cmd_head = &dev->port.pci.ucan_cmd;
	dev->ucan.cmd_size = sizeof(u64);
	dev->ucan.cmd_len = 0;

#ifndef UCAN_PCI_OPEN_ALLOC_DMA
	if (ucan_pci_dma_alloc(dev))
		return -ENOMEM;
	ucan_pci_dma_set(dev);
#endif

	/* adjust MSI/INTA irq from adapter device IRQ value */
	dev->flags &= ~PCAN_DEV_MSI_SHARED;
	dev->wIrq = ucan_adapter->pci_dev->irq;

#ifdef UCAN_PCI_ENABLE_MSI
	if (pci_dev_msi_enabled(ucan_adapter->pci_dev)) {
		dev->wIrq += c / ucan_adapter->msi_step;

#ifdef UCAN_PCI_SHARE_MSI
		if (ucan_adapter->msi_step > 1)
			dev->flags |= PCAN_DEV_MSI_SHARED;
#endif
	}
#endif

#ifdef DEBUG
	pr_info("%s: CAN%u: assigned to irq %u (msi_shared=%u)\n",
		DEVICE_NAME, dev->nChannel+1, dev->wIrq,
		!!(dev->flags & PCAN_DEV_MSI_SHARED));
#endif
	dev->nMajor = pcan_drv.nMajor;
	dev->nMinor = PCAN_PCI_MINOR_BASE + _pci_devices++;

	dev->adapter = &ucan_adapter->adapter;

	pcan_lock_init(&dev->isr_lock);

	pcan_add_device_in_list_ex(dev, PCAN_DEV_STATIC);

	ucan_pci_devices++;

	/* Win driver does write a NOP command first... (???) */
	ucan_add_cmd_nop(dev);
	ucan_pci_send_cmd(dev);

	pr_info("%s: pci uCAN device minor %d found\n",
						DEVICE_NAME, dev->nMinor);
	return 0;
}

/* remove a uCAN channel */
static int ucan_pci_dev_remove(struct ucan_adapter *ucan_adapter, int c)
{
	struct pcandev *dev = ucan_adapter->pcan_dev + c;

#ifdef DEBUG
	pr_info("%s: %s(CAN%u)\n", DEVICE_NAME, __func__, dev->nChannel+1);
#endif
	return ucan_pci_driver_cleanup(dev);
}

/* probe a uCAN device */
int pcan_pci_ucan_probe(struct pci_dev *dev, u16 sub_system_id,
			const char *adapter_name, int can_count)
{
	const u64 drm = dma_get_required_mask(&dev->dev);
	struct ucan_adapter *ucan_adapter;
	int err, l, i;
	u32 v1, v2;

	pr_info(DEVICE_NAME
		": uCAN PCI device sub-system id %0xh (%u channels)\n",
		sub_system_id, can_count);

	err = pci_request_regions(dev, DRV_NAME);
	if (err)
		goto fail;

	/* can the DMA controller support the DMA addressing limitation ? */
	if (!dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(64))) {
#ifdef DEBUG_DMA
		pr_info(DEVICE_NAME ": 64-bits DMA controler (%llxh)\n",
			drm);
#endif
	} else {
		err = dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(32));
		if (err) {
			dev_warn(&dev->dev,
				"%s: No suitable DMA available: err %d "
				"(required mask=%llxh)\n",
				DEVICE_NAME, err, drm);
			goto fail_release_regions;
		}
#ifdef DEBUG_DMA
		pr_info(DEVICE_NAME ": 32-bits DMA controler (%llxh)\n",
			drm);
#endif
	}

	/* allocate the adapter object */
	l = sizeof(struct ucan_adapter) + can_count * sizeof(struct pcandev);
	ucan_adapter = kzalloc(l, GFP_KERNEL);
	if (!ucan_adapter)
		goto fail_release_regions;

	/* map SYStem block */
	ucan_adapter->bar0_addr = pci_iomap(dev, 0, UCAN_PCI_BAR0_SIZE);
        if (!ucan_adapter->bar0_addr) {
		dev_err(&dev->dev, "failed to map PCI resource #0\n");
		err = -ENOMEM;
		goto fail_free;
	}

#ifdef DEBUG
	pr_info(DEVICE_NAME ": BAR0=%p\n", ucan_adapter->bar0_addr);
#endif

#ifdef UCAN_PCI_ENABLE_MSI
#ifdef DEBUG_MSI
	err = pci_find_capability(dev, PCI_CAP_ID_MSI);
	if (!err) {
		pr_warn(DEVICE_NAME ": pci_find_capability() failure\n");
	} else {
		u16 msgctl;
		int maxvec;

		pci_read_config_word(dev, err + PCI_MSI_FLAGS, &msgctl);
		pr_info(DEVICE_NAME ": MSI flags=%08xh:\n", msgctl);
		maxvec = 1 << ((msgctl & PCI_MSI_FLAGS_QMASK) >> 1);
		pr_info(DEVICE_NAME ": MSI device cap maxvec=%d\n", maxvec);
	}
#endif

#ifdef UCAN_PCI_ENABLE_MSIX
	for (i = 0; i < 4; i++)
		ucan_adapter->msix_entries[i].entry = i;

	err = pci_enable_msix_range(dev,
				ucan_adapter->msix_entries,
				can_count,
				can_count);
	if (err > 0) {
		pr_info(DEVICE_NAME ": enabling %u MSI-X status: %d\n",
			can_count, err);

		pci_disable_msix(dev);
	} else {
		pr_info(DEVICE_NAME ": MSI-X failed (err %d)\n", err);
	}
#endif

	/* enable MSI for the PCI device */
	//ucan_adapter->msi_count = pci_enable_msi_range(dev, can_count, can_count);

	/* looks like with x86 APIC, Linux Kernel won't give us anything else
	 * then "1" (see "native_setup_msi_irqs()" in
	 * arch/x86/kernel/apic/io_apic.c */
	ucan_adapter->msi_count = pci_enable_msi_range(dev, 1, can_count);

#ifdef DEBUG_MSI
	pr_info(DEVICE_NAME ": enabling %u MSI status: %d\n",
		can_count, ucan_adapter->msi_count);
#endif
#ifdef UCAN_PCI_SHARE_MSI
	if (ucan_adapter->msi_count <= 0) {
#else
	if (ucan_adapter->msi_count != can_count) {

		/* fallback to INTA mode if we can't allocate one IRQ line per
		 * channel */
		if (ucan_adapter->msi_count > 0)
			pci_disable_msi(dev);

		else if (ucan_adapter->msi_count < 0) {
			pr_err(DEVICE_NAME
				": enabling MSI failed (err %d)\n",
				ucan_adapter->msi_count);

			err = ucan_adapter->msi_count;
			goto fail_unmap;
		}
#endif
		pr_info(DEVICE_NAME
			": fallback into INTA mode IRQ%u (err %d)\n",
			dev->irq, ucan_adapter->msi_count);

	} else {
		ucan_adapter->msi_step = can_count / ucan_adapter->msi_count;
#ifdef DEBUG_MSI
		pr_info(DEVICE_NAME ": %u MSI enabled from INT%u\n",
			ucan_adapter->msi_count, dev->irq);
#endif
	}
#endif /* UCAN_PCI_ENABLE_MSI */

	v1 = ucan_sys_readl(ucan_adapter, UCAN_PCI_REG_VER1);
	v2 = ucan_sys_readl(ucan_adapter, UCAN_PCI_REG_VER2);

	ucan_adapter->adapter.hw_ver_major = (v2 & 0x0000f000) >> 12;
	ucan_adapter->adapter.hw_ver_minor = (v2 & 0x00000f00) >> 8;
	ucan_adapter->adapter.hw_ver_subminor = (v2 & 0x000000f0) >> 4;

	pr_info(DEVICE_NAME ": uCAN PCB v%xh FPGA v%u.%u.%u (mode %u)\n",
		(v1 & 0xf0000000) >> 28,
		ucan_adapter->adapter.hw_ver_major,
		ucan_adapter->adapter.hw_ver_minor,
		ucan_adapter->adapter.hw_ver_subminor,
		(v2 & 0x0000000f));
#ifdef DEBUG
	ucan_pci_sys_dump(ucan_adapter, __func__, 0, -1);
#endif

#ifdef UCAN_64BITS_CMD_MUST_BE_ATOMIC
	pcan_lock_init(&ucan_adapter->ucan_lock);
#endif
	pci_set_drvdata(dev, ucan_adapter);
	ucan_adapter->pci_dev = dev;

	/* stop system clock */
	ucan_sys_writel(ucan_adapter,
			UCAN_SYS_CTL_CLK_EN, UCAN_PCI_REG_SYS_CTL_CLR);

	/* do this after iomap */
	pci_set_master(dev);

	ucan_adapter->adapter.name = adapter_name;
	for (i = 0; i < can_count; i++) {
		err = ucan_pci_dev_probe(ucan_adapter, i);
		if (err)
			goto fail_all;
	}

	ucan_adapter->adapter.can_count = can_count;
	ucan_adapter->adapter.index = ucan_pci_adapters++;

	/* reset system timestamp counter */
	ucan_sys_writel(ucan_adapter,
			UCAN_SYS_CTL_TS_RST, UCAN_PCI_REG_SYS_CTL_SET);

	/* Reset uCAN clock */
	for (i = 0; i < can_count; i++)
		ucan_x_writel(ucan_adapter->pcan_dev + i,
				UCAN_MISC_TS_RST, UCAN_PCI_REG_MISC);

	/* wait a bit (read cycle) */
	(void )ucan_sys_readl(ucan_adapter, UCAN_PCI_REG_VER1);

	/* free all clocks */
	ucan_sys_writel(ucan_adapter,
			UCAN_SYS_CTL_TS_RST, UCAN_PCI_REG_SYS_CTL_CLR);
	for (i = 0; i < can_count; i++)
		ucan_x_writel(ucan_adapter->pcan_dev + i, 0, UCAN_PCI_REG_MISC);

	/* start system clock */
	ucan_sys_writel(ucan_adapter,
			UCAN_SYS_CTL_CLK_EN, UCAN_PCI_REG_SYS_CTL_SET);
	return 0;

fail_all:
	while (i-- > 0)
		ucan_pci_dev_remove(ucan_adapter, i);

#ifdef UCAN_PCI_ENABLE_MSI
	pci_disable_msi(dev);
#ifndef UCAN_PCI_SHARE_MSI
fail_unmap:
#endif
#endif
	pci_iounmap(dev, ucan_adapter->bar0_addr);

fail_free:
	pcan_free(ucan_adapter);

fail_release_regions:
	pci_release_regions(dev);

fail:
	return err;
}

/* remove the driver */
static void pcan_pci_ucan_hw_remove(struct pci_dev *dev)
{
	struct ucan_adapter *ucan_adapter = pci_get_drvdata(dev);

#if defined(DEBUG_WRITE) || defined(DEBUG_IRQ_RX) || defined(DEBUG_IRQ_TX)
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif
	if (!ucan_adapter)
		return;

	/* set driver data to NULL to not go back here... */
	pci_set_drvdata(dev, NULL);

#ifdef UCAN_PCI_ENABLE_MSI
	pci_disable_msi(dev);
#endif
	pci_iounmap(dev, ucan_adapter->bar0_addr);
	pcan_free(ucan_adapter);
	pci_release_regions(dev);
}

void pcan_pci_ucan_remove(struct pci_dev *dev)
{
	struct ucan_adapter *ucan_adapter = pci_get_drvdata(dev);
	int i;

#ifdef DEBUG
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif

	if (!ucan_adapter)
		return;

	/* note: because we have to manage two ways of deregistering (Udev /
	 * no Udev), ucan_pci_dev_cleanup() (thus pcan_pci_ucan_hw_remove())
	 * could be reentrant. pci_set_drvdata(NULL) is used to prevent from
	 * this reentrance (see pcan_pci_ucan_hw_remove()) */
	for (i = 0; i < ucan_adapter->adapter.can_count; i++)
		ucan_pci_dev_remove(ucan_adapter, i);

	pcan_pci_ucan_hw_remove(dev);
}

#ifdef UCAN_STANDALONE_PCI_DRIVER

/* probe the uCAN device */
static int ucan_pci_probe(struct pci_dev *dev, const struct pci_device_id *ent)
{
	int err, can_count;
	u16 sub_system_id;

#ifdef DEBUG
	pr_info(DEVICE_NAME ": %s(devid=%08xh subsysid=%08xh)\n",
		__func__, ent->device, ent->subdevice);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 10)
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

	err = pcan_pci_ucan_probe(dev, sub_system_id, can_count);
	if (!err)
		return 0;

fail_disable_pci:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 10)
	pci_disable_device(dev);
fail:
#endif
	return err;
}

static void ucan_pci_remove(struct pci_dev *dev)
{
#ifdef DEBUG
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif
	pcan_pci_ucan_remove(dev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 10)
	pci_disable_device(dev);
#endif
}

static struct pci_driver ucan_pci_driver = {
	.name = DRV_NAME,
	.id_table = ucan_pci_table,
	.probe = ucan_pci_probe,
	.remove = ucan_pci_remove,
};

int ucan_pci_driver_init(void)
{
#ifdef DEBUG
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif
	return pci_register_driver(&ucan_pci_driver);
}

void ucan_pci_driver_exit(void)
{
#ifdef DEBUG
	pr_info("%s: %s()\n", DEVICE_NAME, __func__);
#endif
	pci_unregister_driver(&ucan_pci_driver);
}
#endif /* UCAN_STANDALONE_PCI_DRIVER */
