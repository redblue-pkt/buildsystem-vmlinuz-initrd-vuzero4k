/*
 * Code borrowed from powerpc/kernel/pci-common.c
 *
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 * Copyright (C) 2014 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 */

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

#include <asm/pci-bridge.h>

/*
 * Called after each bus is probed, but before its children are examined
 */
void pcibios_fixup_bus(struct pci_bus *bus)
{
	/* nothing to do, expected to be removed in the future */
}

/*
 * We don't have to worry about legacy ISA devices, so nothing to do here
 */
resource_size_t pcibios_align_resource(void *data, const struct resource *res,
				resource_size_t size, resource_size_t align)
{
	return res->start;
}

/*
 * Try to assign the IRQ number from DT when adding a new device
 */
int pcibios_add_device(struct pci_dev *dev)
{
	dev->irq = of_irq_parse_and_map_pci(dev, 0, 0);

	return 0;
}

/*
 * raw_pci_read/write - Platform-specific PCI config space access.
 */
int raw_pci_read(unsigned int domain, unsigned int bus,
		  unsigned int devfn, int reg, int len, u32 *val)
{
	return -ENXIO;
}

int raw_pci_write(unsigned int domain, unsigned int bus,
		unsigned int devfn, int reg, int len, u32 val)
{
	return -ENXIO;
}

/*
 * swizzle the device pin each time we cross a bridge.  if a platform does
 * not provide a swizzle function, we perform the standard pci swizzling.
 *
 * the default swizzling walks up the bus tree one level at a time, applying
 * the standard swizzle function at each step, stopping when it finds the pci
 * root bus.  this will return the slot number of the bridge device on the
 * root bus and the interrupt pin on that device which should correspond
 * with the downstream device interrupt.
 *
 * platforms may override this, in which case the slot and pin returned
 * depend entirely on the platform code.  however, please note that the
 * pci standard swizzle is implemented on plug-in cards and cardbus based
 * pci extenders, so it can not be ignored.
 */
static u8 pcibios_swizzle(struct pci_dev *dev, u8 *pin)
{
	struct pci_sys_data *sys = dev->sysdata;
	int slot, oldpin = *pin;

	if (sys->swizzle)
		slot = sys->swizzle(dev, pin);
	else
		slot = pci_common_swizzle(dev, pin);

	pr_debug("pci: %s swizzling pin %d => pin %d slot %d\n",
		 pci_name(dev), oldpin, *pin, slot);

	return slot;
}

/*
 * map a slot/pin to an irq.
 */
static int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct pci_sys_data *sys = dev->sysdata;
	int irq = -1;

	if (sys->map_irq)
		irq = sys->map_irq(dev, slot, pin);

	pr_debug("pci: %s mapping slot %d pin %d => irq %d\n",
		 pci_name(dev), slot, pin, irq);

	return irq;
}

static int pcibios_init_resources(int busnr, struct pci_sys_data *sys)
{
	int ret;
	struct resource_entry *window;

	if (list_empty(&sys->resources)) {
		pci_add_resource_offset(&sys->resources,
			 &iomem_resource, sys->mem_offset);
	}

	resource_list_for_each_entry(window, &sys->resources)
		if (resource_type(window->res) == IORESOURCE_IO)
			return 0;

	sys->io_res.start = (busnr * SZ_64K) ?  : PCIBIOS_MIN_IO;
	sys->io_res.end = (busnr + 1) * SZ_64K - 1;
	sys->io_res.flags = IORESOURCE_IO;
	sys->io_res.name = sys->io_res_name;
	sprintf(sys->io_res_name, "PCI%d I/O", busnr);

	ret = request_resource(&ioport_resource, &sys->io_res);
	if (ret) {
		pr_err("PCI: unable to allocate I/O port region (%d)\n", ret);
		return ret;
	}
	pci_add_resource_offset(&sys->resources, &sys->io_res,
				sys->io_offset);

	return 0;
}


static void pcibios_init_hw(struct device *parent, struct hw_pci *hw,
			    struct list_head *head)
{
	struct pci_sys_data *sys = NULL;
	int ret;
	int nr, busnr;

	for (nr = busnr = 0; nr < hw->nr_controllers; nr++) {
		sys = kzalloc(sizeof(struct pci_sys_data), GFP_KERNEL);
		if (!sys)
			panic("PCI: unable to allocate sys data!");

#ifdef CONFIG_PCI_MSI
		sys->msi_ctrl = hw->msi_ctrl;
#endif
		sys->busnr   = busnr;
		sys->swizzle = hw->swizzle;
		sys->map_irq = hw->map_irq;
		sys->align_resource = hw->align_resource;
		INIT_LIST_HEAD(&sys->resources);

		if (hw->private_data)
			sys->private_data = hw->private_data[nr];

		ret = hw->setup(nr, sys);

		if (ret > 0) {
			ret = pcibios_init_resources(nr, sys);
			if (ret)  {
				kfree(sys);
				break;
			}

			if (hw->scan)
				sys->bus = hw->scan(nr, sys);
			else
				sys->bus = pci_scan_root_bus(parent, sys->busnr,
						hw->ops, sys, &sys->resources);

			if (!sys->bus)
				panic("PCI: unable to scan bus!");

			busnr = sys->bus->busn_res.end + 1;

			list_add(&sys->node, head);
		} else {
			kfree(sys);
			if (ret < 0)
				break;
		}
	}
}


void pci_common_init_dev(struct device *parent, struct hw_pci *hw)
{
	struct pci_sys_data *sys;
	LIST_HEAD(head);

	pci_add_flags(PCI_REASSIGN_ALL_RSRC);
	if (hw->preinit)
		hw->preinit();
	pcibios_init_hw(parent, hw, &head);
	if (hw->postinit)
		hw->postinit();

	pci_fixup_irqs(pcibios_swizzle, pcibios_map_irq);

	list_for_each_entry(sys, &head, node) {
		struct pci_bus *bus = sys->bus;

		if (!pci_has_flag(PCI_PROBE_ONLY)) {
			/*
			 * Size the bridge windows.
			 */
			pci_bus_size_bridges(bus);

			/*
			 * Assign resources.
			 */
			pci_bus_assign_resources(bus);
		}

		/*
		 * Tell drivers about devices found.
		 */
		pci_bus_add_devices(bus);
	}

	list_for_each_entry(sys, &head, node) {
		struct pci_bus *bus = sys->bus;

		/* Configure PCI Express settings */
		if (bus && !pci_has_flag(PCI_PROBE_ONLY)) {
			struct pci_bus *child;

			list_for_each_entry(child, &bus->children, node)
				pcie_bus_configure_settings(child);
		}
	}
}

#ifdef CONFIG_ACPI
/* Root bridge scanning */
struct pci_bus *pci_acpi_scan_root(struct acpi_pci_root *root)
{
	/* TODO: Should be revisited when implementing PCI on ACPI */
	return NULL;
}
#endif
