/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2005-2006 by Texas Instruments */

#ifndef _SUNXI_DMA_H_
#define _SUNXI_DMA_H_

#include <linux/slab.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/dmapool.h>
#include <linux/dmaengine.h>

#include "musb_core.h"
#include "musb_dma.h"

/* CPPI RX/TX state RAM */

struct cppi_tx_stateram {
	u32 tx_head;			/* "DMA packet" head descriptor */
	u32 tx_buf;
	u32 tx_current;			/* current descriptor */
	u32 tx_buf_current;
	u32 tx_info;			/* flags, remaining buflen */
	u32 tx_rem_len;
	u32 tx_dummy;			/* unused */
	u32 tx_complete;
};

struct cppi_rx_stateram {
	u32 rx_skipbytes;
	u32 rx_head;
	u32 rx_sop;			/* "DMA packet" head descriptor */
	u32 rx_current;			/* current descriptor */
	u32 rx_buf_current;
	u32 rx_len_len;
	u32 rx_cnt_cnt;
	u32 rx_complete;
};

/* hw_options bits in CPPI buffer descriptors */
#define CPPI_SOP_SET	((u32)(1 << 31))
#define CPPI_EOP_SET	((u32)(1 << 30))
#define CPPI_OWN_SET	((u32)(1 << 29))	/* owned by cppi */
#define CPPI_EOQ_MASK	((u32)(1 << 28))
#define CPPI_ZERO_SET	((u32)(1 << 23))	/* rx saw zlp; tx issues one */
#define CPPI_RXABT_MASK	((u32)(1 << 19))	/* need more rx buffers */

#define CPPI_RECV_PKTLEN_MASK 0xFFFF
#define CPPI_BUFFER_LEN_MASK 0xFFFF

#define CPPI_TEAR_READY ((u32)(1 << 31))

/* CPPI data structure definitions */

#define	CPPI_DESCRIPTOR_ALIGN	16	/* bytes; 5-dec docs say 4-byte align */

struct cppi_descriptor {
	/* hardware overlay */
	u32		hw_next;	/* next buffer descriptor Pointer */
	u32		hw_bufp;	/* i/o buffer pointer */
	u32		hw_off_len;	/* buffer_offset16, buffer_length16 */
	u32		hw_options;	/* flags:  SOP, EOP etc*/

	struct cppi_descriptor *next;
	dma_addr_t	dma;		/* address of this descriptor */
	u32		buflen;		/* for RX: original buffer length */
} __attribute__ ((aligned(CPPI_DESCRIPTOR_ALIGN)));


struct cppi;

/* CPPI  Channel Control structure */
struct cppi_channel {
	struct dma_channel	channel;

	/* back pointer to the DMA controller structure */
	struct cppi		*controller;

	/* which direction of which endpoint? */
	struct musb_hw_ep	*hw_ep;
	bool			transmit;
	u8			index;

	/* DMA modes:  RNDIS or "transparent" */
	u8			is_rndis;

	/* book keeping for current transfer request */
	dma_addr_t		buf_dma;
	u32			buf_len;
	u32			maxpacket;
	u32			offset;		/* dma requested */

	void __iomem		*state_ram;	/* CPPI state */

	struct cppi_descriptor	*freelist;

	/* BD management fields */
	struct cppi_descriptor	*head;
	struct cppi_descriptor	*tail;
	struct cppi_descriptor	*last_processed;

	/* use tx_complete in host role to track endpoints waiting for
	 * FIFONOTEMPTY to clear.
	 */
	struct list_head	tx_complete;
};

/* CPPI DMA controller object */
struct cppi {
	struct dma_controller		controller;
	void __iomem			*mregs;		/* Mentor regs */
	void __iomem			*tibase;	/* TI/CPPI regs */

	int				irq;

	struct cppi_channel		tx[4];
	struct cppi_channel		rx[4];

	struct dma_pool			*pool;

	struct list_head		tx_complete;
};

/* CPPI IRQ handler */
extern irqreturn_t cppi_interrupt(int, void *);

struct cppi41_dma_channel {
	struct dma_channel channel; //defined in musb_dma.h
/*
/**
 * struct dma_channel - A DMA channel.
 * @private_data: channel-private data
 * @max_len: the maximum number of bytes the channel can move in one
 *	transaction (typically representing many USB maximum-sized packets)
 * @actual_len: how many bytes have been transferred
 * @status: current channel status (updated e.g. on interrupt)
 * @desired_mode: true if mode 1 is desired; false if mode 0 is desired
 *
 * channels are associated with an endpoint for the duration of at least
 * one usb transfer.
 
struct dma_channel {
	void			*private_data;
	size_t			max_len;
	size_t			actual_len;
	enum dma_channel_status	status;
	bool			desired_mode;
	bool			rx_packet_done;
};
*/

	struct cppi41_dma_controller *controller; // defined in musb_cppi41.c

/*
struct cppi41_dma_controller {
	struct dma_controller controller;
	struct cppi41_dma_channel *rx_channel;
	struct cppi41_dma_channel *tx_channel;
	struct hrtimer early_tx;
	struct list_head early_tx_list;
	u32 rx_mode;
	u32 tx_mode;
	u32 auto_req;

	u32 tdown_reg;
	u32 autoreq_reg;

	void (*set_dma_mode)(struct cppi41_dma_channel *cppi41_channel,
			     unsigned int mode);
	u8 num_channels;
};
*/
	struct musb_hw_ep *hw_ep; //defined in musb_core.h
/*
/*
 * struct musb_hw_ep - endpoint hardware (bidirectional)
 *
 * Ordered slightly for better cacheline locality.
 
struct musb_hw_ep {
	struct musb		*musb;
	void __iomem		*fifo;
	void __iomem		*regs;

#if IS_ENABLED(CONFIG_USB_MUSB_TUSB6010)
	void __iomem		*conf;
#endif

	/* index in musb->endpoints[]  
	u8			epnum;

	/* hardware configuration, possibly dynamic 
	bool			is_shared_fifo;
	bool			tx_double_buffered;
	bool			rx_double_buffered;
	u16			max_packet_sz_tx;
	u16			max_packet_sz_rx;

	struct dma_channel	*tx_channel;
	struct dma_channel	*rx_channel;

#if IS_ENABLED(CONFIG_USB_MUSB_TUSB6010)
	dma_addr_t		fifo_async;
	dma_addr_t		fifo_sync;
	void __iomem		*fifo_sync_va;
#endif

	/* currently scheduled peripheral endpoint 
	struct musb_qh		*in_qh;
	struct musb_qh		*out_qh;

	u8			rx_reinit;
	u8			tx_reinit;

	/* peripheral side 
	struct musb_ep		ep_in;			/* TX 
	struct musb_ep		ep_out;			/* RX 
};
*/
	struct dma_chan *dc; //defined in dmaengine.h

/*
/**
 * struct dma_chan - devices supply DMA channels, clients use them
 * @device: ptr to the dma device who supplies this channel, always !%NULL
 * @cookie: last cookie value returned to client
 * @completed_cookie: last completed cookie for this channel
 * @chan_id: channel ID for sysfs
 * @dev: class device for sysfs
 * @device_node: used to add this to the device chan list
 * @local: per-cpu pointer to a struct dma_chan_percpu
 * @client_count: how many clients are using this channel
 * @table_count: number of appearances in the mem-to-mem allocation table
 * @router: pointer to the DMA router structure
 * @route_data: channel specific data for the router
 * @private: private data for certain client-channel associations
 
struct dma_chan {
	struct dma_device *device;
	dma_cookie_t cookie;
	dma_cookie_t completed_cookie;

	/* sysfs 
	int chan_id;
	struct dma_chan_dev *dev;

	struct list_head device_node;
	struct dma_chan_percpu __percpu *local;
	int client_count;
	int table_count;

	/* DMA router 
	struct dma_router *router;
	void *route_data;

	void *private;
};

*/
	dma_cookie_t cookie;

/*
/**
 * typedef dma_cookie_t - an opaque DMA cookie
 *
 * if dma_cookie_t is >0 it's a DMA request cookie, <0 it's an error code
 
typedef s32 dma_cookie_t;
*/
	u8 port_num;
	u8 is_tx;
	u8 is_allocated;
	u8 usb_toggle;

	dma_addr_t buf_addr;
//typedef unsigned long dma_addr_t;
	u32 total_len;
	u32 prog_len;
	u32 transferred;
	u32 packet_sz;
	struct list_head tx_check;
/*
struct list_head {
	struct list_head *next, *prev;
};*/
	int tx_zlp;
};

#endif				/* end of ifndef _CPPI_DMA_H_ */
