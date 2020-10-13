// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner sun4i MUSB Glue Layer
 *
 * Copyright (C) 2015 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on code from
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 */

#include <linux/of_dma.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/extcon.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy-sun4i-usb.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/soc/sunxi/sunxi_sram.h>
#include <linux/usb/musb.h>
#include <linux/usb/of.h>
#include <linux/usb/usb_phy_generic.h>
#include <linux/workqueue.h>
#include "musb_core.h"
#include "sunxi.h"

/*
 * Register offsets, note sunxi musb has a different layout then most
 * musb implementations, we translate the layout in musb_readb & friends.
 */
#define SUNXI_MUSB_POWER			0x0040
#define SUNXI_MUSB_DEVCTL			0x0041
#define SUNXI_MUSB_INDEX			0x0042
#define SUNXI_MUSB_VEND0			0x0043
#define SUNXI_MUSB_INTRTX			0x0044
#define SUNXI_MUSB_INTRRX			0x0046
#define SUNXI_MUSB_INTRTXE			0x0048
#define SUNXI_MUSB_INTRRXE			0x004a
#define SUNXI_MUSB_INTRUSB			0x004c
#define SUNXI_MUSB_INTRUSBE			0x0050
#define SUNXI_MUSB_FRAME			0x0054
#define SUNXI_MUSB_TXFIFOSZ			0x0090
#define SUNXI_MUSB_TXFIFOADD			0x0092
#define SUNXI_MUSB_RXFIFOSZ			0x0094
#define SUNXI_MUSB_RXFIFOADD			0x0096
#define SUNXI_MUSB_FADDR			0x0098
#define SUNXI_MUSB_TXFUNCADDR			0x0098
#define SUNXI_MUSB_TXHUBADDR			0x009a
#define SUNXI_MUSB_TXHUBPORT			0x009b
#define SUNXI_MUSB_RXFUNCADDR			0x009c
#define SUNXI_MUSB_RXHUBADDR			0x009e
#define SUNXI_MUSB_RXHUBPORT			0x009f
#define SUNXI_MUSB_CONFIGDATA			0x00c0

/* VEND0 bits */
#define SUNXI_MUSB_VEND0_PIO_MODE		0

/* flags */
#define SUNXI_MUSB_FL_ENABLED			0
#define SUNXI_MUSB_FL_HOSTMODE			1
#define SUNXI_MUSB_FL_HOSTMODE_PEND		2
#define SUNXI_MUSB_FL_VBUS_ON			3
#define SUNXI_MUSB_FL_PHY_ON			4
#define SUNXI_MUSB_FL_HAS_SRAM			5
#define SUNXI_MUSB_FL_HAS_RESET			6
#define SUNXI_MUSB_FL_NO_CONFIGDATA		7
#define SUNXI_MUSB_FL_PHY_MODE_PEND		8

/* Our read/write methods need access and do not get passed in a musb ref :| */
static struct musb *sunxi_musb;

struct sunxi_glue {
	struct device		*dev;
	struct musb		*musb;
	struct platform_device	*musb_pdev;
	struct clk		*clk;
	struct reset_control	*rst;
	struct phy		*phy;
	struct platform_device	*usb_phy;
	struct usb_phy		*xceiv;
	enum phy_mode		phy_mode;
	unsigned long		flags;
	struct work_struct	work;
	struct extcon_dev	*extcon;
	struct notifier_block	host_nb;
};

/* phy_power_on / off may sleep, so we use a workqueue  */
static void sunxi_musb_work(struct work_struct *work)
{
	struct sunxi_glue *glue = container_of(work, struct sunxi_glue, work);
	bool vbus_on, phy_on;

	if (!test_bit(SUNXI_MUSB_FL_ENABLED, &glue->flags))
		return;

	if (test_and_clear_bit(SUNXI_MUSB_FL_HOSTMODE_PEND, &glue->flags)) {
		struct musb *musb = glue->musb;
		unsigned long flags;
		u8 devctl;

		spin_lock_irqsave(&musb->lock, flags);

		devctl = readb(musb->mregs + SUNXI_MUSB_DEVCTL);
		if (test_bit(SUNXI_MUSB_FL_HOSTMODE, &glue->flags)) {
			set_bit(SUNXI_MUSB_FL_VBUS_ON, &glue->flags);
			musb->xceiv->otg->state = OTG_STATE_A_WAIT_VRISE;
			MUSB_HST_MODE(musb);
			devctl |= MUSB_DEVCTL_SESSION;
		} else {
			clear_bit(SUNXI_MUSB_FL_VBUS_ON, &glue->flags);
			musb->xceiv->otg->state = OTG_STATE_B_IDLE;
			MUSB_DEV_MODE(musb);
			devctl &= ~MUSB_DEVCTL_SESSION;
		}
		writeb(devctl, musb->mregs + SUNXI_MUSB_DEVCTL);

		spin_unlock_irqrestore(&musb->lock, flags);
	}

	vbus_on = test_bit(SUNXI_MUSB_FL_VBUS_ON, &glue->flags);
	phy_on = test_bit(SUNXI_MUSB_FL_PHY_ON, &glue->flags);

	if (phy_on != vbus_on) {
		if (vbus_on) {
			phy_power_on(glue->phy);
			set_bit(SUNXI_MUSB_FL_PHY_ON, &glue->flags);
		} else {
			phy_power_off(glue->phy);
			clear_bit(SUNXI_MUSB_FL_PHY_ON, &glue->flags);
		}
	}

	if (test_and_clear_bit(SUNXI_MUSB_FL_PHY_MODE_PEND, &glue->flags))
		phy_set_mode(glue->phy, glue->phy_mode);
}

static void sunxi_musb_set_vbus(struct musb *musb, int is_on)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);

	if (is_on) {
		set_bit(SUNXI_MUSB_FL_VBUS_ON, &glue->flags);
		musb->xceiv->otg->state = OTG_STATE_A_WAIT_VRISE;
	} else {
		clear_bit(SUNXI_MUSB_FL_VBUS_ON, &glue->flags);
	}

	schedule_work(&glue->work);
}

static void sunxi_musb_pre_root_reset_end(struct musb *musb)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);

	sun4i_usb_phy_set_squelch_detect(glue->phy, false);
}

static void sunxi_musb_post_root_reset_end(struct musb *musb)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);

	sun4i_usb_phy_set_squelch_detect(glue->phy, true);
}

static irqreturn_t sunxi_musb_interrupt(int irq, void *__hci)
{
	struct musb *musb = __hci;
	unsigned long flags;

	spin_lock_irqsave(&musb->lock, flags);

	musb->int_usb = readb(musb->mregs + SUNXI_MUSB_INTRUSB);
	if (musb->int_usb)
		writeb(musb->int_usb, musb->mregs + SUNXI_MUSB_INTRUSB);

	if ((musb->int_usb & MUSB_INTR_RESET) && !is_host_active(musb)) {
		/* ep0 FADDR must be 0 when (re)entering peripheral mode */
		musb_ep_select(musb->mregs, 0);
		musb_writeb(musb->mregs, MUSB_FADDR, 0);
	}

	musb->int_tx = readw(musb->mregs + SUNXI_MUSB_INTRTX);
	if (musb->int_tx)
		writew(musb->int_tx, musb->mregs + SUNXI_MUSB_INTRTX);

	musb->int_rx = readw(musb->mregs + SUNXI_MUSB_INTRRX);
	if (musb->int_rx)
		writew(musb->int_rx, musb->mregs + SUNXI_MUSB_INTRRX);

	musb_interrupt(musb);

	spin_unlock_irqrestore(&musb->lock, flags);

	return IRQ_HANDLED;
}

static int sunxi_musb_host_notifier(struct notifier_block *nb,
				    unsigned long event, void *ptr)
{
	struct sunxi_glue *glue = container_of(nb, struct sunxi_glue, host_nb);

	if (event)
		set_bit(SUNXI_MUSB_FL_HOSTMODE, &glue->flags);
	else
		clear_bit(SUNXI_MUSB_FL_HOSTMODE, &glue->flags);

	set_bit(SUNXI_MUSB_FL_HOSTMODE_PEND, &glue->flags);
	schedule_work(&glue->work);

	return NOTIFY_DONE;
}

static int sunxi_musb_init(struct musb *musb)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);
	int ret;

	sunxi_musb = musb;
	musb->phy = glue->phy;
	musb->xceiv = glue->xceiv;

	if (test_bit(SUNXI_MUSB_FL_HAS_SRAM, &glue->flags)) {
		ret = sunxi_sram_claim(musb->controller->parent);
		if (ret)
			return ret;
	}

	ret = clk_prepare_enable(glue->clk);
	if (ret)
		goto error_sram_release;

	if (test_bit(SUNXI_MUSB_FL_HAS_RESET, &glue->flags)) {
		ret = reset_control_deassert(glue->rst);
		if (ret)
			goto error_clk_disable;
	}

	writeb(SUNXI_MUSB_VEND0_PIO_MODE, musb->mregs + SUNXI_MUSB_VEND0);

	/* Register notifier before calling phy_init() */
	ret = devm_extcon_register_notifier(glue->dev, glue->extcon,
					EXTCON_USB_HOST, &glue->host_nb);
	if (ret)
		goto error_reset_assert;

	ret = phy_init(glue->phy);
	if (ret)
		goto error_reset_assert;

	musb->isr = sunxi_musb_interrupt;

	/* Stop the musb-core from doing runtime pm (not supported on sunxi) */
	pm_runtime_get(musb->controller);

	return 0;

error_reset_assert:
	if (test_bit(SUNXI_MUSB_FL_HAS_RESET, &glue->flags))
		reset_control_assert(glue->rst);
error_clk_disable:
	clk_disable_unprepare(glue->clk);
error_sram_release:
	if (test_bit(SUNXI_MUSB_FL_HAS_SRAM, &glue->flags))
		sunxi_sram_release(musb->controller->parent);
	return ret;
}

static int sunxi_musb_exit(struct musb *musb)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);

	pm_runtime_put(musb->controller);

	cancel_work_sync(&glue->work);
	if (test_bit(SUNXI_MUSB_FL_PHY_ON, &glue->flags))
		phy_power_off(glue->phy);

	phy_exit(glue->phy);

	if (test_bit(SUNXI_MUSB_FL_HAS_RESET, &glue->flags))
		reset_control_assert(glue->rst);

	clk_disable_unprepare(glue->clk);
	if (test_bit(SUNXI_MUSB_FL_HAS_SRAM, &glue->flags))
		sunxi_sram_release(musb->controller->parent);

	devm_usb_put_phy(glue->dev, glue->xceiv);

	return 0;
}

static void sunxi_musb_enable(struct musb *musb)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);

	glue->musb = musb;

	/* musb_core does not call us in a balanced manner */
	if (test_and_set_bit(SUNXI_MUSB_FL_ENABLED, &glue->flags))
		return;

	schedule_work(&glue->work);
}

static void sunxi_musb_disable(struct musb *musb)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);

	clear_bit(SUNXI_MUSB_FL_ENABLED, &glue->flags);
}

static struct dma_controller *
sunxi_musb_dma_controller_create(struct musb *musb, void __iomem *base)
{
	// need to hold onto the __iomem passed in
	struct dma_controller *controller;
	struct platform_device *pdev = to_platform_device(musb->controller);
	struct device *dev = musb->controller;
	struct musb_hdrc_platform_data *plat = dev_get_platdata(musb->controller);
	struct resource	*iomem;
	int ret;
	dma_cap_mask_t mask;

	controller = kzalloc(sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return -EINVAL;

	if (!musb->controller->parent->of_node) {
		dev_err(musb->controller, "Need DT for the DMA engine.\n");
		printk ("sunxi_musb_dma_controller_create Need DT for the DMA engine EEEEEEEEEEEEEEEEEEEEEEE\n");
		// this doesn't print out so its good!?!?!?
		return NULL;
	}
	printk ("sunxi_musb_dma_controller_create AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");

	// is this needed?
	//controller->phy_base = (dma_addr_t) iomem->start;

	if (!plat) {
		dev_err(musb->controller, "No platform dataaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");
		return -EINVAL;
	}
/*
	printk("plat = ");
	printk(plat);
	printk("\n");
*/
	/*data = plat->board_data;
	printk("data = ");
	printk(data);
	printk("\n");
	param_array = data ? data->dma_rx_param_array : NULL;
	printk("param_array = ");
	printk(param_array);
	printk("\n");
	*/

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	if (pdev->dev.of_node == NULL) {
		printk("of_node is null...\n");
	}

	//printk("of_node name = ");
	//printk(pdev->dev.of_node->name);
	//printk("of_node full_name = ");
	//printk(pdev->dev.of_node->full_name);


	struct dma_chan * rx_ep_1_dma_channel;
	rx_ep_1_dma_channel = dma_request_chan(dev->parent, "rx_ep_1");
//		of_dma_request_slave_channel(dev, "rx_ep_1");
/*
		dma_chan = dma_request_channel(mask,
							    data ?
							    data->dma_filter :
							    NULL,
							    param_array ?
							    param_array[ch_num] :
							    NULL);
*/
	if (!rx_ep_1_dma_channel) {
		printk("failed to get dma channel for rx_ep_1!!!!!!!!!!!\n");
	}
	else {
		printk("got a dma channel rx_ep_1 WHAT!!!!!!!!!!!!!!!!!!!???????????????\n");
	}


	struct dma_chan * tx_ep_1_dma_channel = dma_request_chan(dev->parent, "tx_ep_1");

	if (!tx_ep_1_dma_channel) {
		printk("failed to get dma channel for tx_ep_1!!!!!!!!!!!\n");
	}
	else {
		printk("got a dma channel tx_ep_1 WHAT!!!!!!!!!!!!!!!!!!!???????????????\n");
	}
//
//

// I think we need to setup a dma syste like so:

// from ste-dbx5x0.dtsi ->
/*
		usb_per5@a03e0000 {
			compatible = "stericsson,db8500-musb";
			reg = <0xa03e0000 0x10000>;
			interrupts = <GIC_SPI 23 IRQ_TYPE_LEVEL_HIGH>;
			interrupt-names = "mc";

			dr_mode = "otg";

			dmas = <&dma 38 0 0x2>, /* Logical - DevToMem *
			       <&dma 38 0 0x0>, /* Logical - MemToDev *
			       <&dma 37 0 0x2>, /* Logical - DevToMem *
			       <&dma 37 0 0x0>, /* Logical - MemToDev *
			       <&dma 36 0 0x2>, /* Logical - DevToMem *
			       <&dma 36 0 0x0>, /* Logical - MemToDev *
			       <&dma 19 0 0x2>, /* Logical - DevToMem *
			       <&dma 19 0 0x0>, /* Logical - MemToDev *
			       <&dma 18 0 0x2>, /* Logical - DevToMem *
			       <&dma 18 0 0x0>, /* Logical - MemToDev *
			       <&dma 17 0 0x2>, /* Logical - DevToMem *
			       <&dma 17 0 0x0>, /* Logical - MemToDev *
			       <&dma 16 0 0x2>, /* Logical - DevToMem *
			       <&dma 16 0 0x0>, /* Logical - MemToDev *
			       <&dma 39 0 0x2>, /* Logical - DevToMem *
			       <&dma 39 0 0x0>; /* Logical - MemToDev *

			dma-names = "iep_1_9",  "oep_1_9",
				    "iep_2_10", "oep_2_10",
				    "iep_3_11", "oep_3_11",
				    "iep_4_12", "oep_4_12",
				    "iep_5_13", "oep_5_13",
				    "iep_6_14", "oep_6_14",
				    "iep_7_15", "oep_7_15",
				    "iep_8",    "oep_8";

			clocks = <&prcc_pclk 5 0>;
		};
*/

// we'll need to give it channels and names, then I think dma_request_slave_channel will work
//
//
//		}
//	}


	controller->channel_alloc = sunxi_dma_channel_allocate;
/*
notes on allocate:
musb_start_urb in musb_host.c
calls into musb_ep_program in musb_host.c
calls 			dma_channel = dma_controller->channel_alloc(
					dma_controller, hw_ep, is_out);

then at the end, has a hard check:
		else if (is_cppi_enabled(musb) || tusb_dma_omap(musb))
			musb_h_tx_dma_start(hw_ep);

there's also a non dma version?
			musb_h_tx_start(hw_ep);

qh->hw_ep; --> the qh that comes in must handle the transfer stuff

*/
	controller->channel_release = sunxi_dma_channel_release;
	controller->channel_program = sunxi_dma_channel_program;
	controller->channel_abort = sunxi_dma_channel_abort;
	controller->is_compatible = sunxi_is_compatible;
	controller->musb = musb;

	return controller;
}

static void sunxi_musb_dma_controller_destroy(struct dma_controller *c)
{
}


static struct dma_channel *sunxi_dma_channel_allocate(struct dma_controller *c,
				struct musb_hw_ep *hw_ep, u8 is_tx)
{

// check in cppi_dma.c, :
/*
/*
 * Allocate a CPPI Channel for DMA.  With CPPI, channels are bound to
 * each transfer direction of a non-control endpoint, so allocating
 * (and deallocating) is mostly a way to notice bad housekeeping on
 * the software side.  We assume the irqs are always active.

static struct dma_channel *
cppi_channel_allocate(struct dma_controller *c,
		struct musb_hw_ep *ep, u8 transmit)
{
*/
	//printk("sunxi_dma_channel_allocate called 00000000000000000000\n");

	u8 ch_num = hw_ep->epnum - 1;

	//printk("sunxi_dma_channel_allocate ch_num = %u\n", ch_num);
/*
	if (is_tx)
		printk("sunxi_dma_channel_allocate is a TX channel\n");
	else
		printk("sunxi_dma_channel_allocate is a RX channel\n");
*/

	return NULL;
/*
	struct cppi41_dma_controller *controller = container_of(c,
			struct cppi41_dma_controller, controller);
	struct cppi41_dma_channel *cppi41_channel = NULL;
	u8 ch_num = hw_ep->epnum - 1;

	if (ch_num >= controller->num_channels)
		return NULL;

	if (is_tx)
		cppi41_channel = &controller->tx_channel[ch_num];
	else
		cppi41_channel = &controller->rx_channel[ch_num];

	if (!cppi41_channel->dc)
		return NULL;

	if (cppi41_channel->is_allocated)
		return NULL;

	cppi41_channel->hw_ep = hw_ep;
	cppi41_channel->is_allocated = 1;

	trace_musb_cppi41_alloc(cppi41_channel);
	return &cppi41_channel->channel;
*/
}

static void sunxi_dma_channel_release(struct dma_channel *channel)
{
	printk("sunxi_dma_channel_release called 11111111111111111111111111\n");
/*
	struct cppi41_dma_channel *cppi41_channel = channel->private_data;

	trace_musb_cppi41_free(cppi41_channel);
	if (cppi41_channel->is_allocated) {
		cppi41_channel->is_allocated = 0;
		channel->status = MUSB_DMA_STATUS_FREE;
		channel->actual_len = 0;
	}
*/
}

static int sunxi_dma_channel_program(struct dma_channel *channel,
				u16 packet_sz, u8 mode,
				dma_addr_t dma_addr, u32 len)
{
	printk("sunxi_dma_channel_program called 2222222222222222222222222222\n");
	return 0;
/*
	int ret;
	struct cppi41_dma_channel *cppi41_channel = channel->private_data;
	int hb_mult = 0;

	BUG_ON(channel->status == MUSB_DMA_STATUS_UNKNOWN ||
		channel->status == MUSB_DMA_STATUS_BUSY);

	if (is_host_active(cppi41_channel->controller->controller.musb)) {
		if (cppi41_channel->is_tx)
			hb_mult = cppi41_channel->hw_ep->out_qh->hb_mult;
		else
			hb_mult = cppi41_channel->hw_ep->in_qh->hb_mult;
	}

	channel->status = MUSB_DMA_STATUS_BUSY;
	channel->actual_len = 0;

	if (hb_mult)
		packet_sz = hb_mult * (packet_sz & 0x7FF);

	ret = cppi41_configure_channel(channel, packet_sz, mode, dma_addr, len);
	if (!ret)
		channel->status = MUSB_DMA_STATUS_FREE;

	return ret;
*/
}


static int sunxi_dma_channel_abort(struct dma_channel *channel)
{

	printk("sunxi_dma_channel_abort called 33333333333333333333333333\n");
	return 0;
/*
	struct cppi41_dma_channel *cppi41_channel = channel->private_data;
	struct cppi41_dma_controller *controller = cppi41_channel->controller;
	struct musb *musb = controller->controller.musb;
	void __iomem *epio = cppi41_channel->hw_ep->regs;
	int tdbit;
	int ret;
	unsigned is_tx;
	u16 csr;

	is_tx = cppi41_channel->is_tx;
	trace_musb_cppi41_abort(cppi41_channel);

	if (cppi41_channel->channel.status == MUSB_DMA_STATUS_FREE)
		return 0;

	list_del_init(&cppi41_channel->tx_check);
	if (is_tx) {
		csr = musb_readw(epio, MUSB_TXCSR);
		csr &= ~MUSB_TXCSR_DMAENAB;
		musb_writew(epio, MUSB_TXCSR, csr);
	} else {
		cppi41_set_autoreq_mode(cppi41_channel, EP_MODE_AUTOREQ_NONE);

		// delay to drain to cppi dma pipeline for isoch
		udelay(250);

		csr = musb_readw(epio, MUSB_RXCSR);
		csr &= ~(MUSB_RXCSR_H_REQPKT | MUSB_RXCSR_DMAENAB);
		musb_writew(epio, MUSB_RXCSR, csr);

		// wait to drain cppi dma pipe line
		udelay(50);

		csr = musb_readw(epio, MUSB_RXCSR);
		if (csr & MUSB_RXCSR_RXPKTRDY) {
			csr |= MUSB_RXCSR_FLUSHFIFO;
			musb_writew(epio, MUSB_RXCSR, csr);
			musb_writew(epio, MUSB_RXCSR, csr);
		}
	}

	// DA8xx Advisory 2.3.27: wait 250 ms before to start the teardown
	if (musb->ops->quirks & MUSB_DA8XX)
		mdelay(250);

	tdbit = 1 << cppi41_channel->port_num;
	if (is_tx)
		tdbit <<= 16;

	do {
		if (is_tx)
			musb_writel(musb->ctrl_base, controller->tdown_reg,
				    tdbit);
		ret = dmaengine_terminate_all(cppi41_channel->dc);
	} while (ret == -EAGAIN);

	if (is_tx) {
		musb_writel(musb->ctrl_base, controller->tdown_reg, tdbit);

		csr = musb_readw(epio, MUSB_TXCSR);
		if (csr & MUSB_TXCSR_TXPKTRDY) {
			csr |= MUSB_TXCSR_FLUSHFIFO;
			musb_writew(epio, MUSB_TXCSR, csr);
		}
	}

	cppi41_channel->channel.status = MUSB_DMA_STATUS_FREE;
	return 0;
*/
}

static int sunxi_is_compatible(struct dma_channel *channel, u16 maxpacket,
		void *buf, u32 length)
{
	printk("sunxi_is_compatible callled 5555555555555555 \n");
	return 0;
/*
	struct cppi41_dma_channel *cppi41_channel = channel->private_data;
	struct cppi41_dma_controller *controller = cppi41_channel->controller;
	struct musb *musb = controller->controller.musb;

	if (is_host_active(musb)) {
		WARN_ON(1);
		return 1;
	}
	if (cppi41_channel->hw_ep->ep_in.type != USB_ENDPOINT_XFER_BULK)
		return 0;
	if (cppi41_channel->is_tx)
		return 1;
	/* AM335x Advisory 1.0.13. No workaround for device RX mode 
	return 0;
*/
}





















static int sunxi_musb_set_mode(struct musb *musb, u8 mode)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);
	enum phy_mode new_mode;

	switch (mode) {
	case MUSB_HOST:
		new_mode = PHY_MODE_USB_HOST;
		break;
	case MUSB_PERIPHERAL:
		new_mode = PHY_MODE_USB_DEVICE;
		break;
	case MUSB_OTG:
		new_mode = PHY_MODE_USB_OTG;
		break;
	default:
		dev_err(musb->controller->parent,
			"Error requested mode not supported by this kernel\n");
		return -EINVAL;
	}

	if (glue->phy_mode == new_mode)
		return 0;

	if (musb->port_mode != MUSB_OTG) {
		dev_err(musb->controller->parent,
			"Error changing modes is only supported in dual role mode\n");
		return -EINVAL;
	}

	if (musb->port1_status & USB_PORT_STAT_ENABLE)
		musb_root_disconnect(musb);

	/*
	 * phy_set_mode may sleep, and we're called with a spinlock held,
	 * so let sunxi_musb_work deal with it.
	 */
	glue->phy_mode = new_mode;
	set_bit(SUNXI_MUSB_FL_PHY_MODE_PEND, &glue->flags);
	schedule_work(&glue->work);

	return 0;
}

static int sunxi_musb_recover(struct musb *musb)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);

	/*
	 * Schedule a phy_set_mode with the current glue->phy_mode value,
	 * this will force end the current session.
	 */
	set_bit(SUNXI_MUSB_FL_PHY_MODE_PEND, &glue->flags);
	schedule_work(&glue->work);

	return 0;
}

/*
 * sunxi musb register layout
 * 0x00 - 0x17	fifo regs, 1 long per fifo
 * 0x40 - 0x57	generic control regs (power - frame)
 * 0x80 - 0x8f	ep control regs (addressed through hw_ep->regs, indexed)
 * 0x90 - 0x97	fifo control regs (indexed)
 * 0x98 - 0x9f	multipoint / busctl regs (indexed)
 * 0xc0		configdata reg
 */

static u32 sunxi_musb_fifo_offset(u8 epnum)
{
	return (epnum * 4);
}

static u32 sunxi_musb_ep_offset(u8 epnum, u16 offset)
{
	WARN_ONCE(offset != 0,
		  "sunxi_musb_ep_offset called with non 0 offset\n");

	return 0x80; /* indexed, so ignore epnum */
}

static u32 sunxi_musb_busctl_offset(u8 epnum, u16 offset)
{
	return SUNXI_MUSB_TXFUNCADDR + offset;
}

static u8 sunxi_musb_readb(const void __iomem *addr, unsigned offset)
{
	struct sunxi_glue *glue;

	if (addr == sunxi_musb->mregs) {
		/* generic control or fifo control reg access */
		switch (offset) {
		case MUSB_FADDR:
			return readb(addr + SUNXI_MUSB_FADDR);
		case MUSB_POWER:
			return readb(addr + SUNXI_MUSB_POWER);
		case MUSB_INTRUSB:
			return readb(addr + SUNXI_MUSB_INTRUSB);
		case MUSB_INTRUSBE:
			return readb(addr + SUNXI_MUSB_INTRUSBE);
		case MUSB_INDEX:
			return readb(addr + SUNXI_MUSB_INDEX);
		case MUSB_TESTMODE:
			return 0; /* No testmode on sunxi */
		case MUSB_DEVCTL:
			return readb(addr + SUNXI_MUSB_DEVCTL);
		case MUSB_TXFIFOSZ:
			return readb(addr + SUNXI_MUSB_TXFIFOSZ);
		case MUSB_RXFIFOSZ:
			return readb(addr + SUNXI_MUSB_RXFIFOSZ);
		case MUSB_CONFIGDATA + 0x10: /* See musb_read_configdata() */
			glue = dev_get_drvdata(sunxi_musb->controller->parent);
			/* A33 saves a reg, and we get to hardcode this */
			if (test_bit(SUNXI_MUSB_FL_NO_CONFIGDATA,
				     &glue->flags))
				return 0xde;

			return readb(addr + SUNXI_MUSB_CONFIGDATA);
		/* Offset for these is fixed by sunxi_musb_busctl_offset() */
		case SUNXI_MUSB_TXFUNCADDR:
		case SUNXI_MUSB_TXHUBADDR:
		case SUNXI_MUSB_TXHUBPORT:
		case SUNXI_MUSB_RXFUNCADDR:
		case SUNXI_MUSB_RXHUBADDR:
		case SUNXI_MUSB_RXHUBPORT:
			/* multipoint / busctl reg access */
			return readb(addr + offset);
		default:
			dev_err(sunxi_musb->controller->parent,
				"Error unknown readb offset %u\n", offset);
			return 0;
		}
	} else if (addr == (sunxi_musb->mregs + 0x80)) {
		/* ep control reg access */
		/* sunxi has a 2 byte hole before the txtype register */
		if (offset >= MUSB_TXTYPE)
			offset += 2;
		return readb(addr + offset);
	}

	dev_err(sunxi_musb->controller->parent,
		"Error unknown readb at 0x%x bytes offset\n",
		(int)(addr - sunxi_musb->mregs));
	return 0;
}

static void sunxi_musb_writeb(void __iomem *addr, unsigned offset, u8 data)
{
	if (addr == sunxi_musb->mregs) {
		/* generic control or fifo control reg access */
		switch (offset) {
		case MUSB_FADDR:
			return writeb(data, addr + SUNXI_MUSB_FADDR);
		case MUSB_POWER:
			return writeb(data, addr + SUNXI_MUSB_POWER);
		case MUSB_INTRUSB:
			return writeb(data, addr + SUNXI_MUSB_INTRUSB);
		case MUSB_INTRUSBE:
			return writeb(data, addr + SUNXI_MUSB_INTRUSBE);
		case MUSB_INDEX:
			return writeb(data, addr + SUNXI_MUSB_INDEX);
		case MUSB_TESTMODE:
			if (data)
				dev_warn(sunxi_musb->controller->parent,
					"sunxi-musb does not have testmode\n");
			return;
		case MUSB_DEVCTL:
			return writeb(data, addr + SUNXI_MUSB_DEVCTL);
		case MUSB_TXFIFOSZ:
			return writeb(data, addr + SUNXI_MUSB_TXFIFOSZ);
		case MUSB_RXFIFOSZ:
			return writeb(data, addr + SUNXI_MUSB_RXFIFOSZ);
		/* Offset for these is fixed by sunxi_musb_busctl_offset() */
		case SUNXI_MUSB_TXFUNCADDR:
		case SUNXI_MUSB_TXHUBADDR:
		case SUNXI_MUSB_TXHUBPORT:
		case SUNXI_MUSB_RXFUNCADDR:
		case SUNXI_MUSB_RXHUBADDR:
		case SUNXI_MUSB_RXHUBPORT:
			/* multipoint / busctl reg access */
			return writeb(data, addr + offset);
		default:
			dev_err(sunxi_musb->controller->parent,
				"Error unknown writeb offset %u\n", offset);
			return;
		}
	} else if (addr == (sunxi_musb->mregs + 0x80)) {
		/* ep control reg access */
		if (offset >= MUSB_TXTYPE)
			offset += 2;
		return writeb(data, addr + offset);
	}

	dev_err(sunxi_musb->controller->parent,
		"Error unknown writeb at 0x%x bytes offset\n",
		(int)(addr - sunxi_musb->mregs));
}

static u16 sunxi_musb_readw(const void __iomem *addr, unsigned offset)
{
	if (addr == sunxi_musb->mregs) {
		/* generic control or fifo control reg access */
		switch (offset) {
		case MUSB_INTRTX:
			return readw(addr + SUNXI_MUSB_INTRTX);
		case MUSB_INTRRX:
			return readw(addr + SUNXI_MUSB_INTRRX);
		case MUSB_INTRTXE:
			return readw(addr + SUNXI_MUSB_INTRTXE);
		case MUSB_INTRRXE:
			return readw(addr + SUNXI_MUSB_INTRRXE);
		case MUSB_FRAME:
			return readw(addr + SUNXI_MUSB_FRAME);
		case MUSB_TXFIFOADD:
			return readw(addr + SUNXI_MUSB_TXFIFOADD);
		case MUSB_RXFIFOADD:
			return readw(addr + SUNXI_MUSB_RXFIFOADD);
		case MUSB_HWVERS:
			return 0; /* sunxi musb version is not known */
		default:
			dev_err(sunxi_musb->controller->parent,
				"Error unknown readw offset %u\n", offset);
			return 0;
		}
	} else if (addr == (sunxi_musb->mregs + 0x80)) {
		/* ep control reg access */
		return readw(addr + offset);
	}

	dev_err(sunxi_musb->controller->parent,
		"Error unknown readw at 0x%x bytes offset\n",
		(int)(addr - sunxi_musb->mregs));
	return 0;
}

static void sunxi_musb_writew(void __iomem *addr, unsigned offset, u16 data)
{
	if (addr == sunxi_musb->mregs) {
		/* generic control or fifo control reg access */
		switch (offset) {
		case MUSB_INTRTX:
			return writew(data, addr + SUNXI_MUSB_INTRTX);
		case MUSB_INTRRX:
			return writew(data, addr + SUNXI_MUSB_INTRRX);
		case MUSB_INTRTXE:
			return writew(data, addr + SUNXI_MUSB_INTRTXE);
		case MUSB_INTRRXE:
			return writew(data, addr + SUNXI_MUSB_INTRRXE);
		case MUSB_FRAME:
			return writew(data, addr + SUNXI_MUSB_FRAME);
		case MUSB_TXFIFOADD:
			return writew(data, addr + SUNXI_MUSB_TXFIFOADD);
		case MUSB_RXFIFOADD:
			return writew(data, addr + SUNXI_MUSB_RXFIFOADD);
		default:
			dev_err(sunxi_musb->controller->parent,
				"Error unknown writew offset %u\n", offset);
			return;
		}
	} else if (addr == (sunxi_musb->mregs + 0x80)) {
		/* ep control reg access */
		return writew(data, addr + offset);
	}

	dev_err(sunxi_musb->controller->parent,
		"Error unknown writew at 0x%x bytes offset\n",
		(int)(addr - sunxi_musb->mregs));
}

static const struct musb_platform_ops sunxi_musb_ops = {
	.quirks		= MUSB_INDEXED_EP,
	.init		= sunxi_musb_init,
	.exit		= sunxi_musb_exit,
	.enable		= sunxi_musb_enable,
	.disable	= sunxi_musb_disable,
	.fifo_offset	= sunxi_musb_fifo_offset,
	.ep_offset	= sunxi_musb_ep_offset,
	.busctl_offset	= sunxi_musb_busctl_offset,
	.readb		= sunxi_musb_readb,
	.writeb		= sunxi_musb_writeb,
	.readw		= sunxi_musb_readw,
	.writew		= sunxi_musb_writew,
	.dma_init	= sunxi_musb_dma_controller_create,
	.dma_exit	= sunxi_musb_dma_controller_destroy,
	.set_mode	= sunxi_musb_set_mode,
	.recover	= sunxi_musb_recover,
	.set_vbus	= sunxi_musb_set_vbus,
	.pre_root_reset_end = sunxi_musb_pre_root_reset_end,
	.post_root_reset_end = sunxi_musb_post_root_reset_end,
};

/* Allwinner OTG supports up to 5 endpoints */
#define SUNXI_MUSB_MAX_EP_NUM	6
#define SUNXI_MUSB_RAM_BITS	11

static struct musb_fifo_cfg sunxi_musb_mode_cfg[] = {
	MUSB_EP_FIFO_SINGLE(1, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(1, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(2, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(2, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(3, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(3, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(4, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(4, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(5, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(5, FIFO_RX, 512),
};

/* H3/V3s OTG supports only 4 endpoints */
#define SUNXI_MUSB_MAX_EP_NUM_H3	5

static struct musb_fifo_cfg sunxi_musb_mode_cfg_h3[] = {
	MUSB_EP_FIFO_SINGLE(1, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(1, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(2, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(2, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(3, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(3, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(4, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(4, FIFO_RX, 512),
};

static const struct musb_hdrc_config sunxi_musb_hdrc_config = {
	.fifo_cfg       = sunxi_musb_mode_cfg,
	.fifo_cfg_size  = ARRAY_SIZE(sunxi_musb_mode_cfg),
	.multipoint	= true,
	.dyn_fifo	= true,
	.num_eps	= SUNXI_MUSB_MAX_EP_NUM,
	.ram_bits	= SUNXI_MUSB_RAM_BITS,
};

static struct musb_hdrc_config sunxi_musb_hdrc_config_h3 = {
	.fifo_cfg       = sunxi_musb_mode_cfg_h3,
	.fifo_cfg_size  = ARRAY_SIZE(sunxi_musb_mode_cfg_h3),
	.multipoint	= true,
	.dyn_fifo	= true,
	.num_eps	= SUNXI_MUSB_MAX_EP_NUM_H3,
	.ram_bits	= SUNXI_MUSB_RAM_BITS,
};


static int sunxi_musb_probe(struct platform_device *pdev)
{
	struct musb_hdrc_platform_data	pdata;
	struct platform_device_info	pinfo;
	struct sunxi_glue		*glue;
	struct device_node		*np = pdev->dev.of_node;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "Error no device tree node found\n");
		return -EINVAL;
	}

	glue = devm_kzalloc(&pdev->dev, sizeof(*glue), GFP_KERNEL);
	if (!glue)
		return -ENOMEM;

	memset(&pdata, 0, sizeof(pdata));
	switch (usb_get_dr_mode(&pdev->dev)) {
#if defined CONFIG_USB_MUSB_DUAL_ROLE || defined CONFIG_USB_MUSB_HOST
	case USB_DR_MODE_HOST:
		pdata.mode = MUSB_HOST;
		glue->phy_mode = PHY_MODE_USB_HOST;
		break;
#endif
#if defined CONFIG_USB_MUSB_DUAL_ROLE || defined CONFIG_USB_MUSB_GADGET
	case USB_DR_MODE_PERIPHERAL:
		pdata.mode = MUSB_PERIPHERAL;
		glue->phy_mode = PHY_MODE_USB_DEVICE;
		break;
#endif
#ifdef CONFIG_USB_MUSB_DUAL_ROLE
	case USB_DR_MODE_OTG:
		pdata.mode = MUSB_OTG;
		glue->phy_mode = PHY_MODE_USB_OTG;
		break;
#endif
	default:
		dev_err(&pdev->dev, "Invalid or missing 'dr_mode' property\n");
		return -EINVAL;
	}
	pdata.platform_ops	= &sunxi_musb_ops;
	if (!of_device_is_compatible(np, "allwinner,sun8i-h3-musb"))
		pdata.config = &sunxi_musb_hdrc_config;
	else
		pdata.config = &sunxi_musb_hdrc_config_h3;

	glue->dev = &pdev->dev;
	INIT_WORK(&glue->work, sunxi_musb_work);
	glue->host_nb.notifier_call = sunxi_musb_host_notifier;

	if (of_device_is_compatible(np, "allwinner,sun4i-a10-musb") ||
	    of_device_is_compatible(np, "allwinner,suniv-musb")) {
		set_bit(SUNXI_MUSB_FL_HAS_SRAM, &glue->flags);
	}

	if (of_device_is_compatible(np, "allwinner,sun6i-a31-musb"))
		set_bit(SUNXI_MUSB_FL_HAS_RESET, &glue->flags);

	if (of_device_is_compatible(np, "allwinner,sun8i-a33-musb") ||
	    of_device_is_compatible(np, "allwinner,sun8i-h3-musb") ||
	    of_device_is_compatible(np, "allwinner,suniv-musb")) {
		set_bit(SUNXI_MUSB_FL_HAS_RESET, &glue->flags);
		set_bit(SUNXI_MUSB_FL_NO_CONFIGDATA, &glue->flags);
	}

	glue->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(glue->clk)) {
		dev_err(&pdev->dev, "Error getting clock: %ld\n",
			PTR_ERR(glue->clk));
		return PTR_ERR(glue->clk);
	}

	if (test_bit(SUNXI_MUSB_FL_HAS_RESET, &glue->flags)) {
		glue->rst = devm_reset_control_get(&pdev->dev, NULL);
		if (IS_ERR(glue->rst)) {
			if (PTR_ERR(glue->rst) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			dev_err(&pdev->dev, "Error getting reset %ld\n",
				PTR_ERR(glue->rst));
			return PTR_ERR(glue->rst);
		}
	}

	glue->extcon = extcon_get_edev_by_phandle(&pdev->dev, 0);
	if (IS_ERR(glue->extcon)) {
		if (PTR_ERR(glue->extcon) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_err(&pdev->dev, "Invalid or missing extcon\n");
		return PTR_ERR(glue->extcon);
	}

	glue->phy = devm_phy_get(&pdev->dev, "usb");
	if (IS_ERR(glue->phy)) {
		if (PTR_ERR(glue->phy) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_err(&pdev->dev, "Error getting phy %ld\n",
			PTR_ERR(glue->phy));
		return PTR_ERR(glue->phy);
	}

	glue->usb_phy = usb_phy_generic_register();
	if (IS_ERR(glue->usb_phy)) {
		dev_err(&pdev->dev, "Error registering usb-phy %ld\n",
			PTR_ERR(glue->usb_phy));
		return PTR_ERR(glue->usb_phy);
	}

	glue->xceiv = devm_usb_get_phy(&pdev->dev, USB_PHY_TYPE_USB2);
	if (IS_ERR(glue->xceiv)) {
		ret = PTR_ERR(glue->xceiv);
		dev_err(&pdev->dev, "Error getting usb-phy %d\n", ret);
		goto err_unregister_usb_phy;
	}

	platform_set_drvdata(pdev, glue);

	memset(&pinfo, 0, sizeof(pinfo));
	pinfo.name	 = "musb-hdrc";
	pinfo.id	= PLATFORM_DEVID_AUTO;
	pinfo.parent	= &pdev->dev;
	pinfo.res	= pdev->resource;
	pinfo.num_res	= pdev->num_resources;
	pinfo.data	= &pdata;
	pinfo.size_data = sizeof(pdata);

	glue->musb_pdev = platform_device_register_full(&pinfo);
	if (IS_ERR(glue->musb_pdev)) {
		ret = PTR_ERR(glue->musb_pdev);
		dev_err(&pdev->dev, "Error registering musb dev: %d\n", ret);
		goto err_unregister_usb_phy;
	}

	return 0;

err_unregister_usb_phy:
	usb_phy_generic_unregister(glue->usb_phy);
	return ret;
}

static int sunxi_musb_remove(struct platform_device *pdev)
{
	struct sunxi_glue *glue = platform_get_drvdata(pdev);
	struct platform_device *usb_phy = glue->usb_phy;

	platform_device_unregister(glue->musb_pdev);
	usb_phy_generic_unregister(usb_phy);

	return 0;
}

static const struct of_device_id sunxi_musb_match[] = {
	{ .compatible = "allwinner,suniv-musb", },
	{ .compatible = "allwinner,sun4i-a10-musb", },
	{ .compatible = "allwinner,sun6i-a31-musb", },
	{ .compatible = "allwinner,sun8i-a33-musb", },
	{ .compatible = "allwinner,sun8i-h3-musb", },
	{}
};
MODULE_DEVICE_TABLE(of, sunxi_musb_match);

static struct platform_driver sunxi_musb_driver = {
	.probe = sunxi_musb_probe,
	.remove = sunxi_musb_remove,
	.driver = {
		.name = "musb-sunxi",
		.of_match_table = sunxi_musb_match,
	},
};
module_platform_driver(sunxi_musb_driver);

MODULE_DESCRIPTION("Allwinner sunxi MUSB Glue Layer");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL v2");
