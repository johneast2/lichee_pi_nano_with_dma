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
	struct dma_controller controller; //defined in musb_dma.h
	//---
		/**
		 * struct dma_controller - A DMA Controller.
		 * @musb: the usb controller
		 * @start: call this to start a DMA controller;
		 *	return 0 on success, else negative errno
		 * @stop: call this to stop a DMA controller
		 *	return 0 on success, else negative errno
		 * @channel_alloc: call this to allocate a DMA channel
		 * @channel_release: call this to release a DMA channel
		 * @channel_abort: call this to abort a pending DMA transaction,
		 *	returning it to FREE (but allocated) state
		 * @dma_callback: invoked on DMA completion, useful to run platform
		 *	code such IRQ acknowledgment.
		 *
		 * Controllers manage dma channels.
		 
		struct dma_controller {
			struct musb *musb;
			struct dma_channel	*(*channel_alloc)(struct dma_controller *,
							struct musb_hw_ep *, u8 is_tx);
			void			(*channel_release)(struct dma_channel *);
			int			(*channel_program)(struct dma_channel *channel,
									u16 maxpacket, u8 mode,
									dma_addr_t dma_addr,
									u32 length);
			int			(*channel_abort)(struct dma_channel *);
			int			(*is_compatible)(struct dma_channel *channel,
									u16 maxpacket,
									void *buf, u32 length);
			void			(*dma_callback)(struct dma_controller *);
		};

	--//

	struct cppi41_dma_channel *rx_channel;
	struct cppi41_dma_channel *tx_channel;  defined in cppi_dma.h
	// --

		struct cppi41_dma_channel {
			struct dma_channel channel;
			struct cppi41_dma_controller *controller;
			struct musb_hw_ep *hw_ep;
			struct dma_chan *dc;
			dma_cookie_t cookie;
			u8 port_num;
			u8 is_tx;
			u8 is_allocated;
			u8 usb_toggle;

			dma_addr_t buf_addr;
			u32 total_len;
			u32 prog_len;
			u32 transferred;
			u32 packet_sz;
			struct list_head tx_check;
			int tx_zlp;
		};
	--//

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
	// --
		/**
		 * struct dma_device - info on the entity supplying DMA services
		 * @chancnt: how many DMA channels are supported
		 * @privatecnt: how many DMA channels are requested by dma_request_channel
		 * @channels: the list of struct dma_chan
		 * @global_node: list_head for global dma_device_list
		 * @filter: information for device/slave to filter function/param mapping
		 * @cap_mask: one or more dma_capability flags
		 * @max_xor: maximum number of xor sources, 0 if no capability
		 * @max_pq: maximum number of PQ sources and PQ-continue capability
		 * @copy_align: alignment shift for memcpy operations
		 * @xor_align: alignment shift for xor operations
		 * @pq_align: alignment shift for pq operations
		 * @fill_align: alignment shift for memset operations
		 * @dev_id: unique device ID
		 * @dev: struct device reference for dma mapping api
		 * @src_addr_widths: bit mask of src addr widths the device supports
		 *	Width is specified in bytes, e.g. for a device supporting
		 *	a width of 4 the mask should have BIT(4) set.
		 * @dst_addr_widths: bit mask of dst addr widths the device supports
		 * @directions: bit mask of slave directions the device supports.
		 *	Since the enum dma_transfer_direction is not defined as bit flag for
		 *	each type, the dma controller should set BIT(<TYPE>) and same
		 *	should be checked by controller as well
		 * @max_burst: max burst capability per-transfer
		 * @residue_granularity: granularity of the transfer residue reported
		 *	by tx_status
		 * @device_alloc_chan_resources: allocate resources and return the
		 *	number of allocated descriptors
		 * @device_free_chan_resources: release DMA channel's resources
		 * @device_prep_dma_memcpy: prepares a memcpy operation
		 * @device_prep_dma_xor: prepares a xor operation
		 * @device_prep_dma_xor_val: prepares a xor validation operation
		 * @device_prep_dma_pq: prepares a pq operation
		 * @device_prep_dma_pq_val: prepares a pqzero_sum operation
		 * @device_prep_dma_memset: prepares a memset operation
		 * @device_prep_dma_memset_sg: prepares a memset operation over a scatter list
		 * @device_prep_dma_interrupt: prepares an end of chain interrupt operation
		 * @device_prep_slave_sg: prepares a slave dma operation
		 * @device_prep_dma_cyclic: prepare a cyclic dma operation suitable for audio.
		 *	The function takes a buffer of size buf_len. The callback function will
		 *	be called after period_len bytes have been transferred.
		 * @device_prep_interleaved_dma: Transfer expression in a generic way.
		 * @device_prep_dma_imm_data: DMA's 8 byte immediate data to the dst address
		 * @device_config: Pushes a new configuration to a channel, return 0 or an error
		 *	code
		 * @device_pause: Pauses any transfer happening on a channel. Returns
		 *	0 or an error code
		 * @device_resume: Resumes any transfer on a channel previously
		 *	paused. Returns 0 or an error code
		 * @device_terminate_all: Aborts all transfers on a channel. Returns 0
		 *	or an error code
		 * @device_synchronize: Synchronizes the termination of a transfers to the
		 *  current context.
		 * @device_tx_status: poll for transaction completion, the optional
		 *	txstate parameter can be supplied with a pointer to get a
		 *	struct with auxiliary transfer status information, otherwise the call
		 *	will just return a simple status code
		 * @device_issue_pending: push pending transactions to hardware
		 * @descriptor_reuse: a submitted transfer can be resubmitted after completion
		 
		struct dma_device {

			unsigned int chancnt;
			unsigned int privatecnt;
			struct list_head channels;
			struct list_head global_node;
			struct dma_filter filter;
			dma_cap_mask_t  cap_mask;
			unsigned short max_xor;
			unsigned short max_pq;
			enum dmaengine_alignment copy_align;
			enum dmaengine_alignment xor_align;
			enum dmaengine_alignment pq_align;
			enum dmaengine_alignment fill_align;
			#define DMA_HAS_PQ_CONTINUE (1 << 15)

			int dev_id;
			struct device *dev;

			u32 src_addr_widths;
			u32 dst_addr_widths;
			u32 directions;
			u32 max_burst;
			bool descriptor_reuse;
			enum dma_residue_granularity residue_granularity;

			int (*device_alloc_chan_resources)(struct dma_chan *chan);
			void (*device_free_chan_resources)(struct dma_chan *chan);

			struct dma_async_tx_descriptor *(*device_prep_dma_memcpy)(
				struct dma_chan *chan, dma_addr_t dst, dma_addr_t src,
				size_t len, unsigned long flags);
			struct dma_async_tx_descriptor *(*device_prep_dma_xor)(
				struct dma_chan *chan, dma_addr_t dst, dma_addr_t *src,
				unsigned int src_cnt, size_t len, unsigned long flags);
			struct dma_async_tx_descriptor *(*device_prep_dma_xor_val)(
				struct dma_chan *chan, dma_addr_t *src,	unsigned int src_cnt,
				size_t len, enum sum_check_flags *result, unsigned long flags);
			struct dma_async_tx_descriptor *(*device_prep_dma_pq)(
				struct dma_chan *chan, dma_addr_t *dst, dma_addr_t *src,
				unsigned int src_cnt, const unsigned char *scf,
				size_t len, unsigned long flags);
			struct dma_async_tx_descriptor *(*device_prep_dma_pq_val)(
				struct dma_chan *chan, dma_addr_t *pq, dma_addr_t *src,
				unsigned int src_cnt, const unsigned char *scf, size_t len,
				enum sum_check_flags *pqres, unsigned long flags);
			struct dma_async_tx_descriptor *(*device_prep_dma_memset)(
				struct dma_chan *chan, dma_addr_t dest, int value, size_t len,
				unsigned long flags);
			struct dma_async_tx_descriptor *(*device_prep_dma_memset_sg)(
				struct dma_chan *chan, struct scatterlist *sg,
				unsigned int nents, int value, unsigned long flags);
			struct dma_async_tx_descriptor *(*device_prep_dma_interrupt)(
				struct dma_chan *chan, unsigned long flags);

			struct dma_async_tx_descriptor *(*device_prep_slave_sg)(
				struct dma_chan *chan, struct scatterlist *sgl,
				unsigned int sg_len, enum dma_transfer_direction direction,
				unsigned long flags, void *context);
			struct dma_async_tx_descriptor *(*device_prep_dma_cyclic)(
				struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
				size_t period_len, enum dma_transfer_direction direction,
				unsigned long flags);
			struct dma_async_tx_descriptor *(*device_prep_interleaved_dma)(
				struct dma_chan *chan, struct dma_interleaved_template *xt,
				unsigned long flags);
			struct dma_async_tx_descriptor *(*device_prep_dma_imm_data)(
				struct dma_chan *chan, dma_addr_t dst, u64 data,
				unsigned long flags);

			int (*device_config)(struct dma_chan *chan,
					     struct dma_slave_config *config);
			int (*device_pause)(struct dma_chan *chan);
			int (*device_resume)(struct dma_chan *chan);
			int (*device_terminate_all)(struct dma_chan *chan);
			void (*device_synchronize)(struct dma_chan *chan);

			enum dma_status (*device_tx_status)(struct dma_chan *chan,
							    dma_cookie_t cookie,
							    struct dma_tx_state *txstate);
			void (*device_issue_pending)(struct dma_chan *chan);
		};
        -- //

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
