// SPDX-License-Identifier: GPL-2.0
/*
 * MUSB OTG driver DMA controller abstraction
 *
 * Copyright 2005 Mentor Graphics Corporation
 * Copyright (C) 2005-2006 by Texas Instruments
 * Copyright (C) 2006-2007 Nokia Corporation
 */

#ifndef __MUSB_DMA_H__
#define __MUSB_DMA_H__

struct musb_hw_ep;

/*
 * DMA Controller Abstraction
 *
 * DMA Controllers are abstracted to allow use of a variety of different
 * implementations of DMA, as allowed by the Inventra USB cores.  On the
 * host side, usbcore sets up the DMA mappings and flushes caches; on the
 * peripheral side, the gadget controller driver does.  Responsibilities
 * of a DMA controller driver include:
 *
 *  - Handling the details of moving multiple USB packets
 *    in cooperation with the Inventra USB core, including especially
 *    the correct RX side treatment of short packets and buffer-full
 *    states (both of which terminate transfers).
 *
 *  - Knowing the correlation between dma channels and the
 *    Inventra core's local endpoint resources and data direction.
 *
 *  - Maintaining a list of allocated/available channels.
 *
 *  - Updating channel status on interrupts,
 *    whether shared with the Inventra core or separate.
 */

#define	DMA_ADDR_INVALID	(~(dma_addr_t)0)

#ifdef CONFIG_MUSB_PIO_ONLY
#define	is_dma_capable()	(0)
#else
#define	is_dma_capable()	(1)
#endif

#ifdef CONFIG_USB_UX500_DMA
#define musb_dma_ux500(musb)		(musb->ops->quirks & MUSB_DMA_UX500)
#else
#define musb_dma_ux500(musb)		0
#endif

#ifdef CONFIG_USB_TI_CPPI41_DMA
#define musb_dma_cppi41(musb)		(musb->ops->quirks & MUSB_DMA_CPPI41)
#else
#define musb_dma_cppi41(musb)		0
#endif

#ifdef CONFIG_USB_TI_CPPI_DMA
#define musb_dma_cppi(musb)		(musb->ops->quirks & MUSB_DMA_CPPI)
#else
#define musb_dma_cppi(musb)		0
#endif

#ifdef CONFIG_USB_TUSB_OMAP_DMA
#define tusb_dma_omap(musb)		(musb->ops->quirks & MUSB_DMA_TUSB_OMAP)
#else
#define tusb_dma_omap(musb)		0
#endif

#ifdef CONFIG_USB_INVENTRA_DMA
#define musb_dma_inventra(musb)		(musb->ops->quirks & MUSB_DMA_INVENTRA)
#else
#define musb_dma_inventra(musb)		0
#endif

#if defined(CONFIG_USB_TI_CPPI_DMA) || defined(CONFIG_USB_TI_CPPI41_DMA)
#define	is_cppi_enabled(musb)		\
	(musb_dma_cppi(musb) || musb_dma_cppi41(musb))
#else
#define	is_cppi_enabled(musb)	0
#endif

/*
 * DMA channel status ... updated by the dma controller driver whenever that
 * status changes, and protected by the overall controller spinlock.
 */
enum dma_channel_status {
	/* unallocated */
	MUSB_DMA_STATUS_UNKNOWN,
	/* allocated ... but not busy, no errors */
	MUSB_DMA_STATUS_FREE,
	/* busy ... transactions are active */
	MUSB_DMA_STATUS_BUSY,
	/* transaction(s) aborted due to ... dma or memory bus error */
	MUSB_DMA_STATUS_BUS_ABORT,
	/* transaction(s) aborted due to ... core error or USB fault */
	MUSB_DMA_STATUS_CORE_ABORT
};

struct dma_controller;

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
 */
struct dma_channel {
	void			*private_data;
	/* FIXME not void* private_data, but a dma_controller * */
	size_t			max_len;
	size_t			actual_len;
	enum dma_channel_status	status;
	bool			desired_mode;
	bool			rx_packet_done;
};

/*
 * dma_channel_status - return status of dma channel
 * @c: the channel
 *
 * Returns the software's view of the channel status.  If that status is BUSY
 * then it's possible that the hardware has completed (or aborted) a transfer,
 * so the driver needs to update that status.
 */
static inline enum dma_channel_status
dma_channel_status(struct dma_channel *c)
{
	return (is_dma_capable() && c) ? c->status : MUSB_DMA_STATUS_UNKNOWN;
}

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
 */
struct dma_controller {
	struct musb *musb; //defined in musb_core.h
/**
/*
 * struct musb - Driver instance data.

struct musb {
	/* device lock 
	spinlock_t		lock;
	spinlock_t		list_lock;	/* resume work list lock 

	struct musb_io		io;
	const struct musb_platform_ops *ops;
	struct musb_context_registers context;

	irqreturn_t		(*isr)(int, void *);
	struct delayed_work	irq_work;
	struct delayed_work	deassert_reset_work;
	struct delayed_work	finish_resume_work;
	struct delayed_work	gadget_work;
	u16			hwvers;

	u16			intrrxe;
	u16			intrtxe;
/* this hub status bit is reserved by USB 2.0 and not seen by usbcore 
#define MUSB_PORT_STAT_RESUME	(1 << 31)

	u32			port1_status;

	unsigned long		rh_timer;

	enum musb_h_ep0_state	ep0_stage;

	/* bulk traffic normally dedicates endpoint hardware, and each
	 * direction has its own ring of host side endpoints.
	 * we try to progress the transfer at the head of each endpoint's
	 * queue until it completes or NAKs too much; then we try the next
	 * endpoint.
	 
	struct musb_hw_ep	*bulk_ep;

	struct list_head	control;	/* of musb_qh 
	struct list_head	in_bulk;	/* of musb_qh 
	struct list_head	out_bulk;	/* of musb_qh 
	struct list_head	pending_list;	/* pending work list

	struct timer_list	otg_timer;
	struct timer_list	dev_timer;
	struct notifier_block	nb;

	struct dma_controller	*dma_controller;

	struct device		*controller; // defined in include/linux/device.h
//---------

/**
 * struct device - The basic device structure
 * @parent:	The device's "parent" device, the device to which it is attached.
 * 		In most cases, a parent device is some sort of bus or host
 * 		controller. If parent is NULL, the device, is a top-level device,
 * 		which is not usually what you want.
 * @p:		Holds the private data of the driver core portions of the device.
 * 		See the comment of the struct device_private for detail.
 * @kobj:	A top-level, abstract class from which other classes are derived.
 * @init_name:	Initial name of the device.
 * @type:	The type of device.
 * 		This identifies the device type and carries type-specific
 * 		information.
 * @mutex:	Mutex to synchronize calls to its driver.
 * @bus:	Type of bus device is on.
 * @driver:	Which driver has allocated this
 * @platform_data: Platform data specific to the device.
 * 		Example: For devices on custom boards, as typical of embedded
 * 		and SOC based hardware, Linux often uses platform_data to point
 * 		to board-specific structures describing devices and how they
 * 		are wired.  That can include what ports are available, chip
 * 		variants, which GPIO pins act in what additional roles, and so
 * 		on.  This shrinks the "Board Support Packages" (BSPs) and
 * 		minimizes board-specific #ifdefs in drivers.
 * @driver_data: Private pointer for driver specific info.
 * @links:	Links to suppliers and consumers of this device.
 * @power:	For device power management.
 *		See Documentation/driver-api/pm/devices.rst for details.
 * @pm_domain:	Provide callbacks that are executed during system suspend,
 * 		hibernation, system resume and during runtime PM transitions
 * 		along with subsystem-level and driver-level callbacks.
 * @pins:	For device pin management.
 *		See Documentation/driver-api/pinctl.rst for details.
 * @msi_list:	Hosts MSI descriptors
 * @msi_domain: The generic MSI domain this device is using.
 * @numa_node:	NUMA node this device is close to.
 * @dma_ops:    DMA mapping operations for this device.
 * @dma_mask:	Dma mask (if dma'ble device).
 * @coherent_dma_mask: Like dma_mask, but for alloc_coherent mapping as not all
 * 		hardware supports 64-bit addresses for consistent allocations
 * 		such descriptors.
 * @bus_dma_mask: Mask of an upstream bridge or bus which imposes a smaller DMA
 *		limit than the device itself supports.
 * @dma_pfn_offset: offset of DMA memory range relatively of RAM
 * @dma_parms:	A low level driver may set these to teach IOMMU code about
 * 		segment limitations.
 * @dma_pools:	Dma pools (if dma'ble device).
 * @dma_mem:	Internal for coherent mem override.
 * @cma_area:	Contiguous memory area for dma allocations
 * @archdata:	For arch-specific additions.
 * @of_node:	Associated device tree node.
 * @fwnode:	Associated device node supplied by platform firmware.
 * @devt:	For creating the sysfs "dev".
 * @id:		device instance
 * @devres_lock: Spinlock to protect the resource of the device.
 * @devres_head: The resources list of the device.
 * @knode_class: The node used to add the device to the class list.
 * @class:	The class of the device.
 * @groups:	Optional attribute groups.
 * @release:	Callback to free the device after all references have
 * 		gone away. This should be set by the allocator of the
 * 		device (i.e. the bus driver that discovered the device).
 * @iommu_group: IOMMU group the device belongs to.
 * @iommu_fwspec: IOMMU-specific properties supplied by firmware.
 *
 * @offline_disabled: If set, the device is permanently online.
 * @offline:	Set after successful invocation of bus type's .offline().
 * @of_node_reused: Set if the device-tree node is shared with an ancestor
 *              device.
 * @dma_coherent: this particular device is dma coherent, even if the
 *		architecture supports non-coherent devices.
 *
 * At the lowest level, every device in a Linux system is represented by an
 * instance of struct device. The device structure contains the information
 * that the device model core needs to model the system. Most subsystems,
 * however, track additional information about the devices they host. As a
 * result, it is rare for devices to be represented by bare device structures;
 * instead, that structure, like kobject structures, is usually embedded within
 * a higher-level representation of the device.
 
----------//
	void __iomem		*ctrl_base;
	void __iomem		*mregs;

#if IS_ENABLED(CONFIG_USB_MUSB_TUSB6010)
	dma_addr_t		async;
	dma_addr_t		sync;
	void __iomem		*sync_va;
	u8			tusb_revision;
#endif

	/* passed down from chip/board specific irq handlers 
	u8			int_usb;
	u16			int_rx;
	u16			int_tx;

	struct usb_phy		*xceiv;
	struct phy		*phy;

	int nIrq;
	unsigned		irq_wake:1;

	struct musb_hw_ep	 endpoints[MUSB_C_NUM_EPS];
#define control_ep		endpoints

#define VBUSERR_RETRY_COUNT	3
	u16			vbuserr_retry;
	u16 epmask;
	u8 nr_endpoints;

	int			(*board_set_power)(int state);

	u8			min_power;	/* vbus for periph, in mA/2 

	enum musb_mode		port_mode;
	bool			session;
	unsigned long		quirk_retries;
	bool			is_host;

	int			a_wait_bcon;	/* VBUS timeout in msecs 
	unsigned long		idle_timeout;	/* Next timeout in jiffies

	unsigned		is_initialized:1;
	unsigned		is_runtime_suspended:1;

	/* active means connected and not suspended 
	unsigned		is_active:1;

	unsigned is_multipoint:1;

	unsigned		hb_iso_rx:1;	/* high bandwidth iso rx? 
	unsigned		hb_iso_tx:1;	/* high bandwidth iso tx? 
	unsigned		dyn_fifo:1;	/* dynamic FIFO supported? 

	unsigned		bulk_split:1;
#define	can_bulk_split(musb,type) \
	(((type) == USB_ENDPOINT_XFER_BULK) && (musb)->bulk_split)

	unsigned		bulk_combine:1;
#define	can_bulk_combine(musb,type) \
	(((type) == USB_ENDPOINT_XFER_BULK) && (musb)->bulk_combine)

	/* is_suspended means USB B_PERIPHERAL suspend 
	unsigned		is_suspended:1;

	/* may_wakeup means remote wakeup is enabled 
	unsigned		may_wakeup:1;

	/* is_self_powered is reported in device status and the
	 * config descriptor.  is_bus_powered means B_PERIPHERAL
	 * draws some VBUS current; both can be true.
	 
	unsigned		is_self_powered:1;
	unsigned		is_bus_powered:1;

	unsigned		set_address:1;
	unsigned		test_mode:1;
	unsigned		softconnect:1;

	unsigned		flush_irq_work:1;

	u8			address;
	u8			test_mode_nr;
	u16			ackpend;		/* ep0 
	enum musb_g_ep0_state	ep0_state;
	struct usb_gadget	g;			/* the gadget 
	struct usb_gadget_driver *gadget_driver;	/* its driver 
	struct usb_hcd		*hcd;			/* the usb hcd 

	const struct musb_hdrc_config *config;

	int			xceiv_old_state;
#ifdef CONFIG_DEBUG_FS
	struct dentry		*debugfs_root;
#endif
};

*/
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

/* called after channel_program(), may indicate a fault */
extern void musb_dma_completion(struct musb *musb, u8 epnum, u8 transmit);

#ifdef CONFIG_MUSB_PIO_ONLY
static inline struct dma_controller *
musb_dma_controller_create(struct musb *m, void __iomem *io)
{
	return NULL;
}

static inline void musb_dma_controller_destroy(struct dma_controller *d) { }

#else

extern struct dma_controller *
(*musb_dma_controller_create)(struct musb *, void __iomem *);

extern void (*musb_dma_controller_destroy)(struct dma_controller *);
#endif

/* Platform specific DMA functions */
extern struct dma_controller *
musbhs_dma_controller_create(struct musb *musb, void __iomem *base);
extern void musbhs_dma_controller_destroy(struct dma_controller *c);

extern struct dma_controller *
tusb_dma_controller_create(struct musb *musb, void __iomem *base);
extern void tusb_dma_controller_destroy(struct dma_controller *c);

extern struct dma_controller *
cppi_dma_controller_create(struct musb *musb, void __iomem *base);
extern void cppi_dma_controller_destroy(struct dma_controller *c);

extern struct dma_controller *
cppi41_dma_controller_create(struct musb *musb, void __iomem *base);
extern void cppi41_dma_controller_destroy(struct dma_controller *c);

extern struct dma_controller *
ux500_dma_controller_create(struct musb *musb, void __iomem *base);
extern void ux500_dma_controller_destroy(struct dma_controller *c);

#endif	/* __MUSB_DMA_H__ */
