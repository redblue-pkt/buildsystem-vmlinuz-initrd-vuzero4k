/*
 * ARM-specific support for Broadcom STB S2/S3/S5 power management
 *
 * S2: clock gate CPUs and as many peripherals as possible
 * S3: power off all of the chip except the Always ON (AON) island; keep DDR is
 *     self-refresh
 * S5: (a.k.a. S3 cold boot) much like S3, except DDR is powered down, so we
 *     treat this mode like a soft power-off, with wakeup allowed from AON
 *
 * Copyright © 2014-2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "brcmstb-pm: " fmt

#ifdef CONFIG_BRCMSTB_PM_DEBUG
#include <linux/debugfs.h>
#endif

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/suspend.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/compiler.h>
#include <linux/pm.h>
#include <linux/bitops.h>
#include <linux/dma-mapping.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/kconfig.h>
#include <linux/memblock.h>
#include <linux/sort.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <asm/fncpy.h>
#include <asm/suspend.h>
#include <asm/setup.h>

#include <linux/brcmstb/brcmstb.h>
#include <linux/brcmstb/memory_api.h>
#include <soc/brcmstb/common.h>

#include "pm.h"
#include "aon_defs.h"
#include "xpt_dma.h"

#define SHIMPHY_DDR_PAD_CNTRL		0x8c

/* Method #0 */
#define SHIMPHY_PAD_PLL_SEQUENCE	BIT(8)
#define SHIMPHY_PAD_GATE_PLL_S3		BIT(9)

/* Method #1 */
#define PWRDWN_SEQ_NO_SEQUENCING	0
#define PWRDWN_SEQ_HOLD_CHANNEL		1
#define	PWRDWN_SEQ_RESET_PLL		2
#define PWRDWN_SEQ_POWERDOWN_PLL	3

#define SHIMPHY_PAD_S3_PWRDWN_SEQ_MASK	0x00f00000
#define SHIMPHY_PAD_S3_PWRDWN_SEQ_SHIFT	20

#define	DDR_FORCE_CKE_RST_N		BIT(3)
#define	DDR_PHY_RST_N			BIT(2)
#define	DDR_PHY_CKE			BIT(1)

#define	DDR_PHY_NO_CHANNEL		0xffffffff

#define MAX_NUM_MEMC			3

/* Capped for performance reasons */
#define MAX_HASH_SIZE			SZ_256M
/* Max per bank, to keep some fairness */
#define MAX_HASH_SIZE_BANK		SZ_64M

struct brcmstb_memc {
	void __iomem *ddr_phy_base;
	void __iomem *ddr_shimphy_base;
	void __iomem *ddr_ctrl;
};

struct brcmstb_pm_control {
	void __iomem *aon_ctrl_base;
	void __iomem *aon_sram;
	struct brcmstb_memc memcs[MAX_NUM_MEMC];

	void __iomem *boot_sram;
	size_t boot_sram_len;

	bool support_warm_boot;
	size_t pll_status_offset;
	int num_memc;

	struct brcmstb_s3_params *s3_params;
	dma_addr_t s3_params_pa;
	int s3entry_method;
	u32 warm_boot_offset;
	u32 phy_a_standby_ctrl_offs;
	u32 phy_b_standby_ctrl_offs;
	bool needs_ddr_pad;
	struct platform_device *pdev;
};

enum bsp_initiate_command {
	BSP_CLOCK_STOP		= 0x00,
	BSP_GEN_RANDOM_KEY	= 0x4A,
	BSP_RESTORE_RANDOM_KEY	= 0x55,
	BSP_GEN_FIXED_KEY	= 0x63,
};

#define PM_INITIATE		0x01
#define PM_INITIATE_SUCCESS	0x00
#define PM_INITIATE_FAIL	0xfe

static struct brcmstb_pm_control ctrl;

#define MAX_EXCLUDE				16
#define MAX_REGION				16
#define MAX_EXTRA				8
static int num_exclusions;
static int num_regions;
static struct dma_region exclusions[MAX_EXCLUDE];
static struct dma_region regions[MAX_REGION];
static struct brcmstb_memory bm;

extern const unsigned long brcmstb_pm_do_s2_sz;
extern asmlinkage int brcmstb_pm_do_s2(void __iomem *aon_ctrl_base,
		void __iomem *ddr_phy_pll_status);

static int (*brcmstb_pm_do_s2_sram)(void __iomem *aon_ctrl_base,
		void __iomem *ddr_phy_pll_status);

#define BRCMSTB_PM_DEBUG_NAME	"brcmstb-pm"

struct procfs_data {
	struct dma_region *region;
	unsigned len;
};

static int brcm_pm_proc_show(struct seq_file *s, void *data)
{
	int i;
	struct procfs_data *procfs_data = s->private;

	if (!procfs_data) {
		seq_puts(s, "--- No region pointer ---\n");
		return 0;
	}
	if (procfs_data->len == 0) {
		seq_puts(s, "--- Nothing to display ---\n");
		return 0;
	}
	if (!procfs_data->region) {
		seq_printf(s, "--- Pointer is NULL, but length is %u ---\n",
			procfs_data->len);
		return 0;
	}

	for (i = 0; i < procfs_data->len; i++) {
		unsigned long addr = procfs_data->region[i].addr;
		unsigned long len = procfs_data->region[i].len;
		unsigned long end = (addr > 0 || len > 0) ? addr + len - 1 : 0;

		seq_printf(s, "%3d\t0x%08lx\t%12lu\t0x%08lx\n", i, addr, len,
			end);
	}
	return 0;
}

static ssize_t brcm_pm_seq_write(struct file *file, const char __user *buf,
	size_t size, loff_t *ppos)
{
	unsigned long start_addr, len;
	int ret;
	char str[128];
	char *len_ptr;
	struct seq_file *s = file->private_data;
	struct procfs_data *procfs_data = s->private;
	bool is_exclusion;

	if (!procfs_data)
		return -ENOMEM;

	if (size >= sizeof(str))
		return -E2BIG;

	is_exclusion = (procfs_data->region == exclusions);

	memset(str, 0, sizeof(str));
	ret = copy_from_user(str, buf, size);
	if (ret)
		return ret;

	/* Strip trailing newline */
	len_ptr = str + strlen(str) - 1;
	while (*len_ptr == '\r' || *len_ptr == '\n')
		*len_ptr-- = '\0';

	/* Special command "clear" empties the exclusions or regions list. */
	if (strcmp(str, "clear") == 0) {
		int region_len = procfs_data->len * sizeof(*procfs_data->region);

		if (is_exclusion)
			num_exclusions = 0;
		else
			num_regions = 0;
		memset(procfs_data->region, 0, region_len);
		return size;
	}

	/*
	 * We expect userland input to be in the format
	 *     <start-address> <length>
	 * where start-address and length are separated by one or more spaces.
	 * Both must be valid numbers. We do accept decimal, hexadecimal and
	 * octal numbers.
	 */
	len_ptr = strchr(str, ' ');
	if (!len_ptr)
		return -EINVAL;
	*len_ptr = '\0';
	do {
		len_ptr++;
	} while (*len_ptr == ' ');

	if (kstrtoul(str, 0, &start_addr) != 0)
		return -EINVAL;
	if (kstrtoul(len_ptr, 0, &len) != 0)
		return -EINVAL;

	if (is_exclusion)
		ret = brcmstb_pm_mem_exclude(start_addr, len);
	else
		ret = brcmstb_pm_mem_region(start_addr, len);

	return ret < 0 ? ret : size;
}

static int brcm_pm_proc_open(struct inode *inode, struct file *file)
{
	void *data;

	/*
	 * Using debugfs, inode->i_private contains our private data. For
	 * procfs, our private data resides in PDE_DATA(inode) instead.
	 */
	if (inode->i_private)
		data = inode->i_private;
	else
		data = PDE_DATA(inode);

	return single_open(file, brcm_pm_proc_show, data);
}

static const struct file_operations brcm_pm_proc_ops = {
	.open		= brcm_pm_proc_open,
	.read		= seq_read,
	.write		= brcm_pm_seq_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#ifdef CONFIG_BRCMSTB_PM_DEBUG

static int brcm_pm_debug_init(void)
{
	struct dentry *dir;
	struct procfs_data *exclusion_data, *region_data;

	dir = debugfs_create_dir(BRCMSTB_PM_DEBUG_NAME, NULL);
	if (IS_ERR_OR_NULL(dir))
		return IS_ERR(dir) ? PTR_ERR(dir) : -ENOENT;

	/*
	 * This driver has no "exit" function, so we don't worry about freeing
	 * these memory areas if setup succeeds.
	 */
	exclusion_data = kmalloc(sizeof(*exclusion_data), GFP_KERNEL);
	if (!exclusion_data)
		return -ENOMEM;
	region_data = kmalloc(sizeof(*region_data), GFP_KERNEL);
	if (!region_data) {
		kfree(exclusion_data);
		return -ENOMEM;
	}

	exclusion_data->region = exclusions;
	exclusion_data->len = ARRAY_SIZE(exclusions);
	region_data->region = regions;
	region_data->len = ARRAY_SIZE(regions);

	debugfs_create_file("exclusions", S_IFREG | S_IRUGO | S_IWUSR, dir,
		exclusion_data, &brcm_pm_proc_ops);
	debugfs_create_file("regions", S_IFREG | S_IRUGO | S_IWUSR, dir,
		region_data, &brcm_pm_proc_ops);

	return 0;
}

fs_initcall(brcm_pm_debug_init);

#endif /* CONFIG_BRCMSTB_PM_DEBUG */

static int brcmstb_init_sram(struct device_node *dn)
{
	void __iomem *sram;
	struct resource res;
	int ret;

	ret = of_address_to_resource(dn, 0, &res);
	if (ret)
		return ret;

	/* Cached, executable remapping of SRAM */
#ifdef CONFIG_ARM
	sram = __arm_ioremap_exec(res.start, resource_size(&res), true);
#else
	sram = __ioremap(res.start, resource_size(&res),
			 __pgprot(PAGE_KERNEL_EXEC));
#endif
	if (!sram)
		return -ENOMEM;

	ctrl.boot_sram = sram;
	ctrl.boot_sram_len = resource_size(&res);

	return 0;
}

/*
 * Latch into the BRCM SRAM compatible property here to be more specific than
 * the standard "mmio-sram". Could be supported with genalloc too, but that
 * would be overkill for its current single use-case.
 */
static const struct of_device_id sram_dt_ids[] = {
	{ .compatible = "brcm,boot-sram" },
	{}
};

static int do_bsp_initiate_command(enum bsp_initiate_command cmd)
{
	void __iomem *base = ctrl.aon_ctrl_base;
	int ret;
	int timeo = 1000 * 1000; /* 1 second */

	__raw_writel(0, base + AON_CTRL_PM_INITIATE);
	(void)__raw_readl(base + AON_CTRL_PM_INITIATE);

	/* Go! */
	__raw_writel((cmd << 1) | PM_INITIATE, base + AON_CTRL_PM_INITIATE);

	/*
	 * If firmware doesn't support the 'ack', then just assume it's done
	 * after 10ms. Note that this only works for command 0, BSP_CLOCK_STOP
	 */
	if (of_machine_is_compatible("brcm,bcm74371a0")) {
		(void)__raw_readl(base + AON_CTRL_PM_INITIATE);
		mdelay(10);
		return 0;
	}

	for (;;) {
		ret = __raw_readl(base + AON_CTRL_PM_INITIATE);
		if (!(ret & PM_INITIATE))
			break;
		if (timeo <= 0) {
			pr_err("error: timeout waiting for BSP (%x)\n", ret);
			break;
		}
		timeo -= 50;
		udelay(50);
	}

	return (ret & 0xff) != PM_INITIATE_SUCCESS;
}

static int brcmstb_pm_handshake(void)
{
	void __iomem *base = ctrl.aon_ctrl_base;
	u32 tmp;
	int ret;

	/* BSP power handshake, v1 */
	tmp = __raw_readl(base + AON_CTRL_HOST_MISC_CMDS);
	tmp &= ~1UL;
	__raw_writel(tmp, base + AON_CTRL_HOST_MISC_CMDS);
	(void)__raw_readl(base + AON_CTRL_HOST_MISC_CMDS);

	ret = do_bsp_initiate_command(BSP_CLOCK_STOP);
	if (ret)
		pr_err("BSP handshake failed\n");

	/*
	 * HACK: BSP may have internal race on the CLOCK_STOP command.
	 * Avoid touching the BSP for a few milliseconds.
	 */
	mdelay(3);

	return ret;
}


static inline void shimphy_set(u32 value, u32 mask)
{
	int i;

	if (!ctrl.needs_ddr_pad)
		return;

	for (i = 0; i < ctrl.num_memc; i++) {
		u32 tmp;

		tmp = __raw_readl(ctrl.memcs[i].ddr_shimphy_base +
			SHIMPHY_DDR_PAD_CNTRL);
		tmp = value | (tmp & mask);
		__raw_writel(tmp, ctrl.memcs[i].ddr_shimphy_base +
			SHIMPHY_DDR_PAD_CNTRL);
	}
	wmb(); /* Complete sequence in order. */
}

static inline void ddr_ctrl_set(bool warmboot)
{
	int i;

	for (i = 0; i < ctrl.num_memc; i++) {
		u32 tmp;

		tmp = __raw_readl(ctrl.memcs[i].ddr_ctrl +
				ctrl.warm_boot_offset);
		if (warmboot)
			tmp |= 1;
		else
			tmp &= ~1; /* Cold boot */
		__raw_writel(tmp, ctrl.memcs[i].ddr_ctrl +
				ctrl.warm_boot_offset);
	}
	wmb(); /* Complete sequence in order. */
}

static inline void s3entry_method0(void)
{
	shimphy_set(SHIMPHY_PAD_GATE_PLL_S3 | SHIMPHY_PAD_PLL_SEQUENCE,
		    0xffffffff);
}

static inline void s3entry_method1(void)
{
	/*
	 * S3 Entry Sequence
	 * -----------------
	 * Step 1: SHIMPHY_ADDR_CNTL_0_DDR_PAD_CNTRL [ S3_PWRDWN_SEQ ] = 3
	 * Step 2: MEMC_DDR_0_WARM_BOOT [ WARM_BOOT ] = 1
	 */
	shimphy_set((PWRDWN_SEQ_POWERDOWN_PLL <<
		    SHIMPHY_PAD_S3_PWRDWN_SEQ_SHIFT),
		    ~SHIMPHY_PAD_S3_PWRDWN_SEQ_MASK);

	ddr_ctrl_set(true);
}

static inline void s5entry_method1(void)
{
	int i;

	/*
	 * S5 Entry Sequence
	 * -----------------
	 * Step 1: SHIMPHY_ADDR_CNTL_0_DDR_PAD_CNTRL [ S3_PWRDWN_SEQ ] = 3
	 * Step 2: MEMC_DDR_0_WARM_BOOT [ WARM_BOOT ] = 0
	 * Step 3: DDR_PHY_CONTROL_REGS_[AB]_0_STANDBY_CONTROL[ CKE ] = 0
	 *	   DDR_PHY_CONTROL_REGS_[AB]_0_STANDBY_CONTROL[ RST_N ] = 0
	 */
	shimphy_set((PWRDWN_SEQ_POWERDOWN_PLL <<
		    SHIMPHY_PAD_S3_PWRDWN_SEQ_SHIFT),
		    ~SHIMPHY_PAD_S3_PWRDWN_SEQ_MASK);

	ddr_ctrl_set(false);

	for (i = 0; i < ctrl.num_memc; i++) {
		u32 tmp;

		/* Step 3: Channel A (RST_N = CKE = 0) */
		tmp = __raw_readl(ctrl.memcs[i].ddr_phy_base +
				  ctrl.phy_a_standby_ctrl_offs);
		tmp &= ~(DDR_PHY_RST_N | DDR_PHY_RST_N);
		__raw_writel(tmp, ctrl.memcs[i].ddr_phy_base +
			     ctrl.phy_a_standby_ctrl_offs);

		/* Step 3: Channel B? */
		if (ctrl.phy_b_standby_ctrl_offs != DDR_PHY_NO_CHANNEL) {
			tmp = __raw_readl(ctrl.memcs[i].ddr_phy_base +
					  ctrl.phy_b_standby_ctrl_offs);
			tmp &= ~(DDR_PHY_RST_N | DDR_PHY_RST_N);
			__raw_writel(tmp, ctrl.memcs[i].ddr_phy_base +
				     ctrl.phy_b_standby_ctrl_offs);
		}
	}
	wmb(); /* Must complete */
}

/*
 * Run a Power Management State Machine (PMSM) shutdown command and put the CPU
 * into a low-power mode
 */
static void brcmstb_do_pmsm_power_down(unsigned long base_cmd, bool onewrite)
{
	void __iomem *base = ctrl.aon_ctrl_base;

	if ((ctrl.s3entry_method == 1) && (base_cmd == PM_COLD_CONFIG))
		s5entry_method1();

	/* pm_start_pwrdn transition 0->1 */
	__raw_writel(base_cmd, base + AON_CTRL_PM_CTRL);

	if (!onewrite) {
		(void)__raw_readl(base + AON_CTRL_PM_CTRL);

		__raw_writel(base_cmd | PM_PWR_DOWN, base + AON_CTRL_PM_CTRL);
		(void)__raw_readl(base + AON_CTRL_PM_CTRL);
	}
	wfi();
}

/* Support S5 cold boot out of "poweroff" */
static void brcmstb_pm_poweroff(void)
{
	brcmstb_pm_handshake();

	/* Clear magic S3 warm-boot value */
	__raw_writel(0, ctrl.aon_sram + AON_REG_MAGIC_FLAGS);
	(void)__raw_readl(ctrl.aon_sram + AON_REG_MAGIC_FLAGS);

	/* Skip wait-for-interrupt signal; just use a countdown */
	__raw_writel(0x10, ctrl.aon_ctrl_base + AON_CTRL_PM_CPU_WAIT_COUNT);
	(void)__raw_readl(ctrl.aon_ctrl_base + AON_CTRL_PM_CPU_WAIT_COUNT);

	if (ctrl.s3entry_method == 1) {
		shimphy_set((PWRDWN_SEQ_POWERDOWN_PLL <<
			     SHIMPHY_PAD_S3_PWRDWN_SEQ_SHIFT),
			     ~SHIMPHY_PAD_S3_PWRDWN_SEQ_MASK);
		ddr_ctrl_set(false);
		brcmstb_do_pmsm_power_down(M1_PM_COLD_CONFIG, true);
		return; /* We should never actually get here */
	}

	brcmstb_do_pmsm_power_down(PM_COLD_CONFIG, false);
}

static void *brcmstb_pm_copy_to_sram(void *fn, size_t len)
{
	unsigned int size = ALIGN(len, FNCPY_ALIGN);

	if (ctrl.boot_sram_len < size) {
		pr_err("standby code will not fit in SRAM\n");
		return NULL;
	}

	return fncpy(ctrl.boot_sram, fn, size);
}

/*
 * S2 suspend/resume picks up where we left off, so we must execute carefully
 * from SRAM, in order to allow DDR to come back up safely before we continue.
 */
static int brcmstb_pm_s2(void)
{
	/* A previous S3 can set a value hazardous to S2, so make sure. */
	if (ctrl.s3entry_method == 1) {
		shimphy_set((PWRDWN_SEQ_NO_SEQUENCING <<
			    SHIMPHY_PAD_S3_PWRDWN_SEQ_SHIFT),
			    ~SHIMPHY_PAD_S3_PWRDWN_SEQ_MASK);
		ddr_ctrl_set(false);
	}

	brcmstb_pm_do_s2_sram = brcmstb_pm_copy_to_sram(&brcmstb_pm_do_s2,
			brcmstb_pm_do_s2_sz);
	if (!brcmstb_pm_do_s2_sram)
		return -EINVAL;

	return brcmstb_pm_do_s2_sram(ctrl.aon_ctrl_base,
			ctrl.memcs[0].ddr_phy_base +
			ctrl.pll_status_offset);
}

static int brcmstb_pm_s3_control_hash(struct brcmstb_s3_params *params,
		phys_addr_t params_pa)
{
	size_t hash_len = sizeof(*params) - BOOTLOADER_SCRATCH_SIZE;
	struct dma_region region[] = {
		{
			.addr	= params_pa + BOOTLOADER_SCRATCH_SIZE,
			.len	= hash_len,
		},
	};
	uint32_t hash[BRCMSTB_HASH_LEN / 4];
	int i, ret;

	/* Co-opt bootloader scratch area temporarily */
	ret = memdma_prepare_descs((struct mcpb_dma_desc *)params->scratch,
				   params_pa, region, ARRAY_SIZE(region), true);
	if (ret)
		return ret;

	dma_sync_single_for_device(&ctrl.pdev->dev, params_pa, sizeof(*params),
				   DMA_TO_DEVICE);

	ret = memdma_run(params_pa, 0, false);
	if (ret)
		return ret;

	get_hash(hash, false);

	/* Store hash in AON */
	for (i = 0; i < BRCMSTB_HASH_LEN / 4; i++)
		__raw_writel(hash[i], ctrl.aon_sram + AON_REG_S3_HASH + i * 4);
	__raw_writel(hash_len, ctrl.aon_sram + AON_REG_CONTROL_HASH_LEN);

	return 0;
}

static inline int region_collision(struct dma_region *reg1,
				   struct dma_region *reg2)
{
	return (reg1->addr + reg1->len > reg2->addr) &&
	       (reg2->addr + reg2->len > reg1->addr);
}

/**
 * Check if @regions[0] collides with regions in @exceptions, and modify
 * regions[0..(max-1)] to ensure that they they exclude any area in @exceptions
 *
 * Note that the regions in @exceptions must be sorted into ascending order
 * prior to calling this function
 *
 * Returns the number of @regions used
 *
 * e.g., if @regions[0] and @exceptions do not overlap, return 1 and do nothing
 *       if @exceptions contains two ranges and both are entirely contained
 *          within @regions[0], split @regions[0] into @regions[0],
 *          @regions[1], and @regions[2], and return 3
 */
static int region_handle_collisions(struct dma_region *regions, int max,
				struct dma_region *exceptions, int num_except)
{
	int i;
	struct dma_region *reg = &regions[0];
	int reg_count = 1;

	/*
	 * Since the list of regions is ordered in ascending order we need only
	 * to compare the last entry in regions against each exception region
	 */
	for (i = 0; i < num_except; i++) {
		struct dma_region *except = &exceptions[i];
		dma_addr_t start = reg->addr;
		dma_addr_t end = reg->addr + reg->len;

		if (!region_collision(reg, except))
			/* No collision */
			continue;

		if (start < except->addr && end > except->addr + except->len) {
			reg->len = except->addr - start;
			if (reg_count < max) {
				/* Split in 2 */
				reg++;
				reg_count++;
				reg->addr = except->addr + except->len;
				reg->len = end - reg->addr;
			} else {
				pr_warn("Not enough space to split region\n");
				break;
			}
		} else if (start < except->addr) {
			/* Overlap at right edge; truncate end of 'reg' */
			reg->len = except->addr - start;
		} else if (end > except->addr + except->len) {
			/* Overlap at left edge; truncate beginning of 'reg' */
			reg->addr = except->addr + except->len;
			reg->len = end - reg->addr;
		} else {
			/*
			 * 'reg' is entirely contained within 'except'?  This
			 * should not happen, but trim to zero-length just in
			 * case
			 */
			reg->len = 0;
			reg_count--;
			break;
		}
	}

	return reg_count;
}

static int dma_region_compare(const void *a, const void *b)
{
	struct dma_region *reg_a = (struct dma_region *)a;
	struct dma_region *reg_b = (struct dma_region *)b;

	if (reg_a->addr < reg_b->addr)
		return -1;
	if (reg_a->addr > reg_b->addr)
		return 1;
	return 0;
}

/* Initialize the DMA region list and return the number of regions */
static int configure_main_hash(struct dma_region *regions, int max,
			       struct dma_region *exclude, int num_exclude)
{
	struct brcmstb_range *range;
	int idx = 0, memc;
	size_t total = 0;

	/*
	 * First sort the excluded regions in ascending order. This makes things
	 * easier when we come to adding the regions since we avoid having to
	 * add entries in the middle of the region list
	 */
	sort(exclude, num_exclude, sizeof(exclude[0]), &dma_region_compare,
			NULL);

	/*
	 * Hash up to MAX_HASH_SIZE_BANK from each memory bank, with a
	 * total limit of MAX_HASH_SIZE. Account for collisions with the
	 * 'exclude' regions.
	 */
	for_each_range_of_memc(bm, memc, range) {
		phys_addr_t block_start = range->addr;
		phys_addr_t size_limit = range->size;

		struct dma_region *reg = &regions[idx];
		int i, count;
		size_t bank_total = 0;

		reg->addr = block_start;
		reg->len = size_limit;

		/*
		 * Check for collisions with the excluded regions.  'reg' may be
		 * split into 0 to (num_exclude + 1) segments, so account
		 * accordingly
		 */
		count = region_handle_collisions(reg, max - idx, exclude,
						 num_exclude);

		/*
		 * Add region length(s) to total. Cap at MAX_HASH_SIZE_BANK
		 * per bank and MAX_HASH_SIZE total.
		 */
		for (i = 0; i < count; i++) {
			/* Don't create 0-sized regions */
			if (total >= MAX_HASH_SIZE)
				break;
			if (bank_total >= MAX_HASH_SIZE_BANK)
				break;
			if (total + reg[i].len > MAX_HASH_SIZE)
				reg[i].len = MAX_HASH_SIZE - total;
			if (bank_total + reg[i].len > MAX_HASH_SIZE_BANK)
				reg[i].len = MAX_HASH_SIZE_BANK - bank_total;
			total += reg[i].len;
			bank_total += reg[i].len;
		}

		idx += i;

		if (idx >= max)
			break;

		/* Apply total cap */
		if (total >= MAX_HASH_SIZE)
			break;
	}

	return idx;
}

/*
 * Run a DMA hash on the given regions, splitting evenly into two channels if
 * possible
 *
 * If 2 channels were run, return the byte offset of the second (from descs_pa)
 * If 1 channel was run, return 0
 * Otherwise (on errors) return negative
 */
static int run_dual_hash(struct dma_region *regions, int numregions,
		struct mcpb_dma_desc *descs, phys_addr_t descs_pa,
		uint32_t *hash)
{
	phys_addr_t pa1, pa2;
	struct mcpb_dma_desc *desc1, *desc2;
	int regions1, regions2;
	int ret;

	/* Split regions into 2 partitions */
	regions2 = numregions / 2;
	regions1 = numregions - regions2;
	desc1 = descs;
	desc2 = desc1 + regions1;
	pa1 = descs_pa;
	pa2 = pa1 + regions1 * sizeof(*desc1);

	/* Prepare both sets of descriptors */
	ret = memdma_prepare_descs(desc1, pa1, &regions[0], regions1, true);
	if (ret)
		return ret;
	ret = memdma_prepare_descs(desc2, pa2, &regions[regions1], regions2,
		false);
	if (ret)
		return ret;

	dma_sync_single_for_device(&ctrl.pdev->dev, pa1, sizeof(*desc1) * numregions,
				   DMA_TO_DEVICE);

	/* Go! */
	ret = memdma_run(pa1, pa2, !!regions2);
	if (ret)
		return ret;

	get_hash(hash, !!regions2);

	if (regions2)
		return regions1 * sizeof(*desc1);
	else
		return 0;
}

static int brcmstb_pm_s3_main_memory_hash(struct brcmstb_s3_params *params,
		phys_addr_t params_pa, struct dma_region *except,
		int num_except)
{
	struct dma_region combined_regions[MAX_EXCLUDE + MAX_REGION + MAX_EXTRA];
	phys_addr_t descs_pa;
	struct mcpb_dma_desc *descs;
	int nregs, ret, i;
	const int max = ARRAY_SIZE(combined_regions);

	memset(&combined_regions, 0, sizeof(combined_regions));
	nregs = configure_main_hash(combined_regions, max, except, num_except);
	if (nregs < 0)
		return nregs;

	for (i = 0; i < num_regions && nregs + i < max; i++)
		combined_regions[nregs + i] = regions[i];
	nregs += i;

	/* Flush out before hashing main memory */
	flush_cache_all();

	/* Get base pointers */
	descs_pa = params_pa + offsetof(struct brcmstb_s3_params, descriptors);
	descs = (struct mcpb_dma_desc *)params->descriptors;

	/* Split, run hash */
	ret = run_dual_hash(combined_regions, nregs, descs, descs_pa,
		params->hash);
	if (ret < 0)
		return ret;
	params->desc_offset_2 = ret;

	return 0;
}

int brcmstb_pm_mem_exclude(phys_addr_t addr, size_t len)
{
	if (num_exclusions >= MAX_EXCLUDE) {
		pr_err("exclusion list is full\n");
		return -ENOSPC;
	}

	exclusions[num_exclusions].addr = addr;
	exclusions[num_exclusions].len = len;
	num_exclusions++;

	return 0;
}
EXPORT_SYMBOL(brcmstb_pm_mem_exclude);

int brcmstb_pm_mem_region(phys_addr_t addr, size_t len)
{
	if (num_regions >= MAX_REGION) {
		pr_err("regions list is full\n");
		return -ENOSPC;
	}

	regions[num_regions].addr = addr;
	regions[num_regions].len = len;
	num_regions++;

	return 0;
}
EXPORT_SYMBOL(brcmstb_pm_mem_region);

/*
 * This function is called on a new stack, so don't allow inlining (which will
 * generate stack references on the old stack)
 */
static noinline int brcmstb_pm_s3_finish(void)
{
	struct brcmstb_s3_params *params = ctrl.s3_params;
	phys_addr_t params_pa = ctrl.s3_params_pa;
	phys_addr_t reentry = virt_to_phys(&cpu_resume);
	enum bsp_initiate_command cmd;
	int ret, num_exclude;
	u32 flags;

	ret = brcmstb_pm_mem_exclude(params_pa, sizeof(*params));
	if (ret) {
		pr_err("failed to add parameter exclusion region\n");
		return ret;
	}

	/*
	 * Clear parameter structure, but not DTU area, which has already been
	 * filled in. We know DTU is a the end, so we can just subtract its
	 * size.
	 */
	memset(params, 0, sizeof(*params) - sizeof(params->dtu));

	flags = __raw_readl(ctrl.aon_sram + AON_REG_MAGIC_FLAGS);

	flags &= S3_BOOTLOADER_RESERVED;
#ifndef CONFIG_BRCMSTB_XPT_HASH
	flags |= S3_FLAG_NO_MEM_VERIFY;
#endif
	flags |= S3_FLAG_LOAD_RANDKEY; /* TODO: make this selectable */

	/* Load random / fixed key */
	if (flags & S3_FLAG_LOAD_RANDKEY)
		cmd = BSP_GEN_RANDOM_KEY;
	else
		cmd = BSP_GEN_FIXED_KEY;
	if (do_bsp_initiate_command(cmd)) {
		pr_info("key loading failed\n");
		return -EIO;
	}

	/* Reset exclusion regions */
	num_exclude = num_exclusions;
	num_exclusions = 0;

	/* Hash main memory */
	ret = brcmstb_pm_s3_main_memory_hash(params, params_pa, exclusions,
					     num_exclude);
	if (ret)
		return ret;

	params->magic = BRCMSTB_S3_MAGIC;
	params->reentry = reentry;

	/* No more writes to DRAM */
	flush_cache_all();

	/* Hash S3 saved parameters */
	ret = brcmstb_pm_s3_control_hash(params, params_pa);
	if (ret)
		return ret;

	flags |= BRCMSTB_S3_MAGIC_SHORT;

	__raw_writel(flags, ctrl.aon_sram + AON_REG_MAGIC_FLAGS);
	__raw_writel(lower_32_bits(params_pa),
		     ctrl.aon_sram + AON_REG_CONTROL_LOW);
	__raw_writel(upper_32_bits(params_pa),
		     ctrl.aon_sram + AON_REG_CONTROL_HIGH);

	switch (ctrl.s3entry_method) {
	case 0:
		s3entry_method0();
		brcmstb_do_pmsm_power_down(PM_WARM_CONFIG, false);
		break;
	case 1:
		s3entry_method1();
		brcmstb_do_pmsm_power_down(M1_PM_WARM_CONFIG, true);
		break;
	default:
		return -EINVAL;
	}

	/* Must have been interrupted from wfi()? */
	return -EINTR;
}

#define SWAP_STACK(new_sp, saved_sp) \
	__asm__ __volatile__ ( \
		 "mov	%[save], sp\n" \
		 "mov	sp, %[new]\n" \
		 : [save] "=&r" (saved_sp) \
		 : [new] "r" (new_sp) \
		)

/*
 * S3 mode resume to the bootloader before jumping back to Linux, so we can be
 * a little less careful about running from DRAM.
 */
static int brcmstb_pm_do_s3(unsigned long sp)
{
	unsigned long save_sp;
	int ret;

	/* Move to a new stack */
	SWAP_STACK(sp, save_sp);

	/* should not return */
	ret = brcmstb_pm_s3_finish();

	SWAP_STACK(save_sp, sp);

	pr_err("Could not enter S3\n");

	return ret;
}

static int brcmstb_pm_s3(void)
{
	void __iomem *sp = ctrl.boot_sram + ctrl.boot_sram_len;

	return cpu_suspend((unsigned long)sp, brcmstb_pm_do_s3);
}

static int brcmstb_pm_standby(bool deep_standby)
{
	int ret;

	if (brcmstb_pm_handshake())
		return -EIO;

	if (deep_standby) {
		/* Save DTU registers for S3 only. SAGE won't let us for S2. */
		dtu_save();
		ret = brcmstb_pm_s3();
	} else {
		ret = brcmstb_pm_s2();
	}
	if (ret)
		pr_err("%s: standby failed\n", __func__);

	return ret;
}

static int brcmstb_pm_enter(suspend_state_t state)
{
	int ret = -EINVAL;

	switch (state) {
	case PM_SUSPEND_STANDBY:
		ret = brcmstb_pm_standby(false);
		break;
	case PM_SUSPEND_MEM:
		ret = brcmstb_pm_standby(true);
		break;
	}

	return ret;
}

static int brcmstb_pm_valid(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_STANDBY:
		return true;
	case PM_SUSPEND_MEM:
		return ctrl.support_warm_boot;
	default:
		return false;
	}
}

static int __init proc_pm_init(void)
{
	struct procfs_data *exclusion_data, *region_data;
	struct proc_dir_entry *brcmstb_root;
	const int perm = S_IRUSR | S_IWUSR;

	brcmstb_root = proc_mkdir("brcmstb", NULL);
	if (!brcmstb_root)
		return 0;

	/*
	 * This driver has no "exit" function, so we don't worry about freeing
	 * these memory areas if setup succeeds.
	 */
	exclusion_data = kmalloc(sizeof(*exclusion_data), GFP_KERNEL);
	if (!exclusion_data)
		return -ENOMEM;
	region_data = kmalloc(sizeof(*region_data), GFP_KERNEL);
	if (!region_data) {
		kfree(exclusion_data);
		return -ENOMEM;
	}

	exclusion_data->region = exclusions;
	exclusion_data->len = ARRAY_SIZE(exclusions);
	region_data->region = regions;
	region_data->len = ARRAY_SIZE(regions);

	if (!proc_create_data("regions", perm, brcmstb_root, &brcm_pm_proc_ops,
			      region_data))
		goto err_out;
	if (!proc_create_data("exclusions", perm, brcmstb_root,
			      &brcm_pm_proc_ops, exclusion_data))
		goto err_out;

	return 0;

err_out:
	proc_remove(brcmstb_root); /* cleans up recursively */
	kfree(exclusion_data);
	kfree(region_data);

	return -ENOENT;
}
module_init(proc_pm_init);

static const struct platform_suspend_ops brcmstb_pm_ops = {
	.enter		= brcmstb_pm_enter,
	.valid		= brcmstb_pm_valid,
};

static const struct of_device_id aon_ctrl_dt_ids[] = {
	{ .compatible = "brcm,brcmstb-aon-ctrl" },
	{}
};

struct ddr_phy_ofdata {
	bool supports_warm_boot;
	size_t pll_status_offset;
	int s3entry_method;
	u32 warm_boot_offset;
	u32 phy_a_standby_ctrl_offs;
	u32 phy_b_standby_ctrl_offs;
};

static struct ddr_phy_ofdata ddr_phy_71_1 = {
	.supports_warm_boot = true,
	.pll_status_offset = 0x0c,
	.s3entry_method = 1,
	.warm_boot_offset = 0x2c,
	.phy_a_standby_ctrl_offs = 0x198,
	.phy_b_standby_ctrl_offs = DDR_PHY_NO_CHANNEL
};

static struct ddr_phy_ofdata ddr_phy_72_0 = {
	.supports_warm_boot = true,
	.pll_status_offset = 0x10,
	.s3entry_method = 1,
	.warm_boot_offset = 0x40,
	.phy_a_standby_ctrl_offs = 0x2a4,
	.phy_b_standby_ctrl_offs = 0x8a4
};

static struct ddr_phy_ofdata ddr_phy_225_1 = {
	.supports_warm_boot = false,
	.pll_status_offset = 0x4,
	.s3entry_method = 0
};

static struct ddr_phy_ofdata ddr_phy_240_1 = {
	.supports_warm_boot = true,
	.pll_status_offset = 0x4,
	.s3entry_method = 0
};

static const struct of_device_id ddr_phy_dt_ids[] = {
	{
		.compatible = "brcm,brcmstb-ddr-phy-v71.1",
		.data = &ddr_phy_71_1,
	},
	{
		.compatible = "brcm,brcmstb-ddr-phy-v72.0",
		.data = &ddr_phy_72_0,
	},
	{
		.compatible = "brcm,brcmstb-ddr-phy-v225.1",
		.data = &ddr_phy_225_1,
	},
	{
		.compatible = "brcm,brcmstb-ddr-phy-v240.1",
		.data = &ddr_phy_240_1,
	},
	{
		/* Same as v240.1, for the registers we care about */
		.compatible = "brcm,brcmstb-ddr-phy-v240.2",
		.data = &ddr_phy_240_1,
	},
	{}
};

struct ddr_seq_ofdata {
	bool needs_ddr_pad;
	u32 warm_boot_offset;
};

static const struct ddr_seq_ofdata ddr_seq_b22 = {
	.needs_ddr_pad = false,
	.warm_boot_offset = 0x2c,
};

static const struct ddr_seq_ofdata ddr_seq = {
	.needs_ddr_pad = true,
};

static const struct of_device_id ddr_shimphy_dt_ids[] = {
	{ .compatible = "brcm,brcmstb-ddr-shimphy-v1.0" },
	{}
};

static const struct of_device_id brcmstb_memc_of_match[] = {
	{
		.compatible = "brcm,brcmstb-memc-ddr-rev-b.2.2",
		.data = &ddr_seq_b22,
	},
	{
		.compatible = "brcm,brcmstb-memc-ddr",
		.data = &ddr_seq,
	},
	{},
};

static inline void __iomem *brcmstb_ioremap_node(struct device_node *dn,
						 int index)
{
	return of_io_request_and_map(dn, index, dn->full_name);
}

static void __iomem *brcmstb_ioremap_match(const struct of_device_id *matches,
					   int index, const void **ofdata)
{
	struct device_node *dn;
	const struct of_device_id *match;

	dn = of_find_matching_node_and_match(NULL, matches, &match);
	if (!dn)
		return ERR_PTR(-EINVAL);

	if (ofdata)
		*ofdata = match->data;

	return brcmstb_ioremap_node(dn, index);
}

static int brcmstb_pm_panic_notify(struct notifier_block *nb,
		unsigned long action, void *data)
{
	__raw_writel(BRCMSTB_PANIC_MAGIC,
		ctrl.aon_sram + AON_REG_PANIC);

	return NOTIFY_DONE;
}

static struct notifier_block brcmstb_pm_panic_nb = {
	.notifier_call = brcmstb_pm_panic_notify,
};

static int brcmstb_pm_probe(struct platform_device *pdev)
{
	const struct ddr_phy_ofdata *ddr_phy_data;
	const struct ddr_seq_ofdata *ddr_seq_data;
	const struct of_device_id *of_id = NULL;
	struct device_node *dn;
	void __iomem *base;
	int ret, i;

	/* AON ctrl registers */
	base = brcmstb_ioremap_match(aon_ctrl_dt_ids, 0, NULL);
	if (IS_ERR(base)) {
		pr_err("error mapping AON_CTRL\n");
		return PTR_ERR(base);
	}
	ctrl.aon_ctrl_base = base;

	/* AON SRAM registers */
	base = brcmstb_ioremap_match(aon_ctrl_dt_ids, 1, NULL);
	if (IS_ERR(base)) {
		/* Assume standard offset */
		ctrl.aon_sram = ctrl.aon_ctrl_base +
				     AON_CTRL_SYSTEM_DATA_RAM_OFS;
	} else {
		ctrl.aon_sram = base;
	}

	__raw_writel(0, ctrl.aon_sram + AON_REG_PANIC);

	/* DDR PHY registers */
	base = brcmstb_ioremap_match(ddr_phy_dt_ids, 0,
				     (const void **)&ddr_phy_data);
	if (IS_ERR(base)) {
		pr_err("error mapping DDR PHY\n");
		return PTR_ERR(base);
	}
	ctrl.support_warm_boot = ddr_phy_data->supports_warm_boot;
	ctrl.pll_status_offset = ddr_phy_data->pll_status_offset;
	/* Only need DDR PHY 0 for now? */
	ctrl.memcs[0].ddr_phy_base = base;
	ctrl.s3entry_method = ddr_phy_data->s3entry_method;
	ctrl.phy_a_standby_ctrl_offs = ddr_phy_data->phy_a_standby_ctrl_offs;
	ctrl.phy_b_standby_ctrl_offs = ddr_phy_data->phy_b_standby_ctrl_offs;
	/*
	 * Slightly grosss to use the phy ver to get a memc,
	 * offset but that is the only versioned things so far
	 * we can test for.
	 */
	ctrl.warm_boot_offset = ddr_phy_data->warm_boot_offset;

	/* DDR SHIM-PHY registers */
	for_each_matching_node(dn, ddr_shimphy_dt_ids) {
		i = ctrl.num_memc;
		if (i >= MAX_NUM_MEMC) {
			pr_warn("too many MEMCs (max %d)\n", MAX_NUM_MEMC);
			break;
		}
		base = brcmstb_ioremap_node(dn, 0);
		if (IS_ERR(base)) {
			if (!ctrl.support_warm_boot)
				break;

			pr_err("error mapping DDR SHIMPHY %d\n", i);
			return PTR_ERR(base);
		}
		ctrl.memcs[i].ddr_shimphy_base = base;
		ctrl.num_memc++;
	}

	/* Sequencer DRAM Param and Control Registers */
	i = 0;
	for_each_matching_node(dn, brcmstb_memc_of_match) {
		base = of_iomap(dn, 0);
		if (!base) {
			pr_err("error mapping DDR Sequencer %d\n", i);
			return -ENOMEM;
		}

		of_id = of_match_node(brcmstb_memc_of_match, dn);
		if (!of_id) {
			iounmap(base);
			return -EINVAL;
		}

		ddr_seq_data = of_id->data;
		ctrl.needs_ddr_pad = ddr_seq_data->needs_ddr_pad;
		/* Adjust warm boot offset based on the DDR sequencer */
		if (ddr_seq_data->warm_boot_offset)
			ctrl.warm_boot_offset = ddr_seq_data->warm_boot_offset;

		ctrl.memcs[i].ddr_ctrl = base;
		i++;
	}

	pr_debug("PM: supports warm boot:%d, method:%d, wboffs:%x\n",
		ctrl.support_warm_boot, ctrl.s3entry_method,
		ctrl.warm_boot_offset);

	dn = of_find_matching_node(NULL, sram_dt_ids);
	if (!dn) {
		pr_err("SRAM not found\n");
		return -EINVAL;
	}

	ret = brcmstb_init_sram(dn);
	if (ret) {
		pr_err("error setting up SRAM for PM\n");
		return ret;
	}

	ctrl.pdev = pdev;

	ctrl.s3_params = kmalloc(sizeof(*ctrl.s3_params), GFP_KERNEL);
	if (!ctrl.s3_params)
		return -ENOMEM;
	ctrl.s3_params_pa = dma_map_single(&pdev->dev, ctrl.s3_params,
					   sizeof(*ctrl.s3_params),
					   DMA_TO_DEVICE);
	if (dma_mapping_error(&pdev->dev, ctrl.s3_params_pa)) {
		pr_err("error mapping DMA memory\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = brcmstb_memory_get(&bm);
	if (ret) {
		pr_err("error getting brcmstb memory\n");
		goto out2;
	}

	ret = brcmstb_regsave_init();
	if (ret)
		goto out2;

	/*
	 * This code assumes only that one DTU config area needs to be saved.
	 * Should this ever change, we'll have to do something more elaborate
	 * here.
	 */
	ret = brcmstb_dtusave_init(ctrl.s3_params->dtu[0].dtu_state_map,
				   ctrl.s3_params->dtu[0].dtu_config);
	if (ret)
		goto out2;

	atomic_notifier_chain_register(&panic_notifier_list,
				       &brcmstb_pm_panic_nb);

	pm_power_off = brcmstb_pm_poweroff;
	suspend_set_ops(&brcmstb_pm_ops);
	return 0;

out2:
	dma_unmap_single(&pdev->dev, ctrl.s3_params_pa, sizeof(*ctrl.s3_params),
			 DMA_TO_DEVICE);
out:
	kfree(ctrl.s3_params);

	pr_warn("PM: initialization failed with code %d\n", ret);

	return ret;
}

static struct platform_driver brcmstb_pm_driver = {
	.driver = {
		.name	= "brcmstb-pm",
		.of_match_table = aon_ctrl_dt_ids,
	},
};

static int __init brcmstb_pm_init(void)
{
	return platform_driver_probe(&brcmstb_pm_driver,
				     brcmstb_pm_probe);
}
module_init(brcmstb_pm_init);
