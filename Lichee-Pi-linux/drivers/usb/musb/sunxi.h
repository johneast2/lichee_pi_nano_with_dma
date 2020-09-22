#ifndef __SUNXI_H__
#define __SUNXI_H__

static struct dma_channel *sunxi_dma_channel_allocate(struct dma_controller *c,
				struct musb_hw_ep *hw_ep, u8 is_tx);

static void sunxi_dma_channel_release(struct dma_channel *channel);

static int sunxi_dma_channel_program(struct dma_channel *channel,
				u16 packet_sz, u8 mode,
				dma_addr_t dma_addr, u32 len);

static int sunxi_dma_channel_abort(struct dma_channel *channel);

static int sunxi_is_compatible(struct dma_channel *channel, u16 maxpacket,
		void *buf, u32 length);

#endif /*  __SUNXI_H__ */
