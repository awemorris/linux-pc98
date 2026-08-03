// SPDX-License-Identifier: GPL-2.0-only
/*
 * Melco/Buffalo LGY-98 C-Bus Ethernet driver
 *
 * The LGY-98 is a 16-bit NE2000-compatible adapter.  Its DP8390 register
 * block is conventional, but the NE2000 ASIC data port and reset latch are
 * mapped into separate PC-98 C-Bus I/O windows:
 *
 *   base + 0x000 .. 0x00f  DP8390 registers
 *   base + 0x200            remote-DMA data port
 *   base + 0x300            reset latch
 *
 * Keep the generic ISA NE2000 driver free of PC-98 conditionals and use the
 * common 8390 PIO core for the protocol-independent part of the device.
 *
 * Copyright (C) 2026 Awe Morris
 */

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include <asm/io.h>

#include "8390.h"

#define DRV_NAME		"ne2k-lgy98"

#define LGY98_DEFAULT_IO	0x00d0
#define LGY98_DEFAULT_IRQ	6

#define LGY98_REG_EXTENT	0x10
#define LGY98_DATA_OFFSET	0x200
#define LGY98_RESET_OFFSET	0x300

#define LGY98_TX_START_PAGE	0x40
#define LGY98_STOP_PAGE		0x80
#define LGY98_DCR		0x49

static int io = LGY98_DEFAULT_IO;
static int irq = LGY98_DEFAULT_IRQ;
static u32 msg_enable;
static struct net_device *lgy98_dev;

module_param_hw(io, int, ioport, 0444);
MODULE_PARM_DESC(io, "LGY-98 DP8390 I/O base (default 0x00d0)");
module_param_hw(irq, int, irq, 0444);
MODULE_PARM_DESC(irq, "LGY-98 IRQ (default 6)");
module_param_named(msg_enable, msg_enable, uint, 0444);
MODULE_PARM_DESC(msg_enable, "Debug message level");

static inline unsigned long lgy98_data_port(const struct net_device *dev)
{
	return dev->base_addr + LGY98_DATA_OFFSET;
}

static inline unsigned long lgy98_reset_port(const struct net_device *dev)
{
	return dev->base_addr + LGY98_RESET_OFFSET;
}

static bool lgy98_request_regions(unsigned long ioaddr)
{
	if (!request_region(ioaddr, LGY98_REG_EXTENT, DRV_NAME))
		return false;

	if (!request_region(ioaddr + LGY98_DATA_OFFSET, 1, DRV_NAME))
		goto release_regs;

	if (!request_region(ioaddr + LGY98_RESET_OFFSET, 1, DRV_NAME))
		goto release_data;

	return true;

release_data:
	release_region(ioaddr + LGY98_DATA_OFFSET, 1);
release_regs:
	release_region(ioaddr, LGY98_REG_EXTENT);
	return false;
}

static void lgy98_release_regions(unsigned long ioaddr)
{
	release_region(ioaddr + LGY98_RESET_OFFSET, 1);
	release_region(ioaddr + LGY98_DATA_OFFSET, 1);
	release_region(ioaddr, LGY98_REG_EXTENT);
}

static int lgy98_reset_card(struct net_device *dev)
{
	unsigned long timeout = jiffies + 2 * HZ / 100;
	unsigned long reset_port = lgy98_reset_port(dev);

	/* NE2000 reset: read the latch and write the value back. */
	outb(inb(reset_port), reset_port);

	while (!(inb_p(dev->base_addr + EN0_ISR) & ENISR_RESET)) {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		cpu_relax();
	}

	outb_p(ENISR_RESET, dev->base_addr + EN0_ISR);
	return 0;
}

static void lgy98_reset_8390(struct net_device *dev)
{
	struct ei_device *ei_local = netdev_priv(dev);

	netif_dbg(ei_local, hw, dev, "resetting the 8390\n");

	ei_local->txing = 0;
	ei_local->dmaing = 0;

	if (lgy98_reset_card(dev))
		netdev_err(dev, "card reset did not complete\n");
}

static void lgy98_get_8390_hdr(struct net_device *dev,
			       struct e8390_pkt_hdr *hdr, int ring_page)
{
	int nic_base = dev->base_addr;

	if (ei_status.dmaing) {
		netdev_err(dev, "DMA conflict while reading packet header\n");
		return;
	}

	ei_status.dmaing = 1;
	outb_p(E8390_NODMA | E8390_PAGE0 | E8390_START,
	       nic_base + E8390_CMD);
	outb_p(sizeof(*hdr), nic_base + EN0_RCNTLO);
	outb_p(0, nic_base + EN0_RCNTHI);
	outb_p(0, nic_base + EN0_RSARLO);
	outb_p(ring_page, nic_base + EN0_RSARHI);
	outb_p(E8390_RREAD | E8390_START, nic_base + E8390_CMD);

	insw(lgy98_data_port(dev), hdr, sizeof(*hdr) >> 1);

	outb_p(ENISR_RDC, nic_base + EN0_ISR);
	ei_status.dmaing = 0;
	le16_to_cpus(&hdr->count);
}

static void lgy98_block_input(struct net_device *dev, int count,
			      struct sk_buff *skb, int ring_offset)
{
	int nic_base = dev->base_addr;
	u8 *buf = skb->data;

	if (ei_status.dmaing) {
		netdev_err(dev, "DMA conflict while receiving packet\n");
		return;
	}

	ei_status.dmaing = 1;
	outb_p(E8390_NODMA | E8390_PAGE0 | E8390_START,
	       nic_base + E8390_CMD);
	outb_p(count & 0xff, nic_base + EN0_RCNTLO);
	outb_p(count >> 8, nic_base + EN0_RCNTHI);
	outb_p(ring_offset & 0xff, nic_base + EN0_RSARLO);
	outb_p(ring_offset >> 8, nic_base + EN0_RSARHI);
	outb_p(E8390_RREAD | E8390_START, nic_base + E8390_CMD);

	insw(lgy98_data_port(dev), buf, count >> 1);
	if (count & 1)
		buf[count - 1] = inb(lgy98_data_port(dev));

	outb_p(ENISR_RDC, nic_base + EN0_ISR);
	ei_status.dmaing = 0;
}

static void lgy98_block_output(struct net_device *dev, int count,
			       const unsigned char *buf, int start_page)
{
	int nic_base = dev->base_addr;
	unsigned long timeout;

	if (count & 1)
		count++;

	if (ei_status.dmaing) {
		netdev_err(dev, "DMA conflict while transmitting packet\n");
		return;
	}

	ei_status.dmaing = 1;
	outb_p(E8390_NODMA | E8390_PAGE0 | E8390_START,
	       nic_base + E8390_CMD);
	outb_p(ENISR_RDC, nic_base + EN0_ISR);
	outb_p(count & 0xff, nic_base + EN0_RCNTLO);
	outb_p(count >> 8, nic_base + EN0_RCNTHI);
	outb_p(0, nic_base + EN0_RSARLO);
	outb_p(start_page, nic_base + EN0_RSARHI);
	outb_p(E8390_RWRITE | E8390_START, nic_base + E8390_CMD);

	outsw(lgy98_data_port(dev), buf, count >> 1);

	timeout = jiffies + 2 * HZ / 100;
	while (!(inb_p(nic_base + EN0_ISR) & ENISR_RDC)) {
		if (time_after(jiffies, timeout)) {
			netdev_warn(dev, "timeout waiting for remote DMA\n");
			lgy98_reset_8390(dev);
			NS8390p_init(dev, 1);
			break;
		}
		cpu_relax();
	}

	outb_p(ENISR_RDC, nic_base + EN0_ISR);
	ei_status.dmaing = 0;
}

static int __init lgy98_read_prom(struct net_device *dev, u8 *prom)
{
	static const struct {
		u8 value;
		u8 reg;
	} init[] = {
		{ E8390_NODMA | E8390_PAGE0 | E8390_STOP, E8390_CMD },
		{ 0x48, EN0_DCFG },
		{ 0x00, EN0_RCNTLO },
		{ 0x00, EN0_RCNTHI },
		{ 0x00, EN0_IMR },
		{ 0xff, EN0_ISR },
		{ E8390_RXOFF, EN0_RXCR },
		{ E8390_TXOFF, EN0_TXCR },
		{ 32, EN0_RCNTLO },
		{ 0x00, EN0_RCNTHI },
		{ 0x00, EN0_RSARLO },
		{ 0x00, EN0_RSARHI },
		{ E8390_RREAD | E8390_START, E8390_CMD },
	};
	u8 raw[32];
	int i;

	for (i = 0; i < ARRAY_SIZE(init); i++)
		outb_p(init[i].value, dev->base_addr + init[i].reg);

	for (i = 0; i < sizeof(raw); i++)
		raw[i] = inb(lgy98_data_port(dev));

	/* A 16-bit NE2000 returns every PROM byte twice in byte-wide mode. */
	for (i = 0; i < 16; i++) {
		if (raw[2 * i] != raw[2 * i + 1])
			return -ENODEV;
		prom[i] = raw[2 * i];
	}

	if (prom[14] != 0x57 || prom[15] != 0x57)
		return -ENODEV;

	outb_p(LGY98_DCR, dev->base_addr + EN0_DCFG);
	return 0;
}

static int __init lgy98_probe(struct net_device *dev)
{
	struct ei_device *ei_local = netdev_priv(dev);
	unsigned long ioaddr = dev->base_addr;
	u8 prom[16];
	u8 saved_cmd, saved_reg;
	int ret;

	if (!lgy98_request_regions(ioaddr))
		return -EBUSY;

	saved_cmd = inb(ioaddr + E8390_CMD);
	if (saved_cmd == 0xff) {
		ret = -ENODEV;
		goto release;
	}

	/* Non-destructive preliminary DP8390 register check. */
	outb_p(E8390_NODMA | E8390_PAGE1 | E8390_STOP,
	       ioaddr + E8390_CMD);
	saved_reg = inb_p(ioaddr + 0x0d);
	outb_p(0xff, ioaddr + 0x0d);
	outb_p(E8390_NODMA | E8390_PAGE0, ioaddr + E8390_CMD);
	inb_p(ioaddr + EN0_COUNTER0);
	if (inb_p(ioaddr + EN0_COUNTER0) != 0) {
		outb_p(saved_cmd, ioaddr + E8390_CMD);
		outb_p(saved_reg, ioaddr + 0x0d);
		ret = -ENODEV;
		goto release;
	}

	ret = lgy98_reset_card(dev);
	if (ret)
		goto release;

	ret = lgy98_read_prom(dev, prom);
	if (ret)
		goto release;

	ret = request_irq(dev->irq, eip_interrupt, 0, DRV_NAME, dev);
	if (ret)
		goto release;

	eth_hw_addr_set(dev, prom);

	ei_local->name = "LGY-98";
	ei_local->tx_start_page = LGY98_TX_START_PAGE;
	ei_local->rx_start_page = LGY98_TX_START_PAGE + TX_PAGES;
	ei_local->stop_page = LGY98_STOP_PAGE;
	ei_local->word16 = 1;
	ei_local->reset_8390 = lgy98_reset_8390;
	ei_local->get_8390_hdr = lgy98_get_8390_hdr;
	ei_local->block_input = lgy98_block_input;
	ei_local->block_output = lgy98_block_output;
	ei_local->msg_enable = msg_enable;

	dev->netdev_ops = &eip_netdev_ops;
	NS8390p_init(dev, 0);

	ret = register_netdev(dev);
	if (ret)
		goto free_irq;

	netdev_info(dev, "LGY-98 at %#lx, IRQ %d, MAC %pM\n",
		    ioaddr, dev->irq, dev->dev_addr);
	return 0;

free_irq:
	free_irq(dev->irq, dev);
release:
	lgy98_release_regions(ioaddr);
	return ret;
}

static int __init lgy98_init(void)
{
	struct net_device *dev;
	int ret;

	if (io <= 0 || irq <= 0)
		return -EINVAL;

	dev = alloc_eip_netdev();
	if (!dev)
		return -ENOMEM;

	dev->base_addr = io;
	dev->irq = irq;

	ret = lgy98_probe(dev);
	if (ret) {
		free_netdev(dev);
		return ret;
	}

	lgy98_dev = dev;
	return 0;
}
module_init(lgy98_init);

static void __exit lgy98_exit(void)
{
	struct net_device *dev = lgy98_dev;

	unregister_netdev(dev);
	free_irq(dev->irq, dev);
	lgy98_release_regions(dev->base_addr);
	free_netdev(dev);
}
module_exit(lgy98_exit);

MODULE_DESCRIPTION("Melco/Buffalo LGY-98 C-Bus NE2000 driver");
MODULE_AUTHOR("PC-9800 Lovers");
MODULE_LICENSE("GPL");
