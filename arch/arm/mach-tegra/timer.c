/*
 * arch/arch/mach-tegra/timer.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2011 NVIDIA Corporation.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *
 * Copyright (C) 2010-2011 NVIDIA Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/err.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/syscore_ops.h>
#include <linux/rtc.h>

#include <asm/mach/time.h>
#include <asm/localtimer.h>
#include <asm/smp_twd.h>
#include <asm/sched_clock.h>

#include <mach/iomap.h>
#include <mach/irqs.h>

#include "board.h"
#include "clock.h"
#include "timer.h"

static void __iomem *timer_reg_base = IO_ADDRESS(TEGRA_TMR1_BASE);
static void __iomem *rtc_base = IO_ADDRESS(TEGRA_RTC_BASE);

static struct timespec persistent_ts;
static u64 persistent_ms, last_persistent_ms;
static u32 usec_config;
static u32 usec_offset;
static bool usec_suspended;

static u32 system_timer;

#define timer_writel(value, reg) \
	__raw_writel(value, (u32)timer_reg_base + (reg))
#define timer_readl(reg) \
	__raw_readl((u32)timer_reg_base + (reg))

static int tegra_timer_set_next_event(unsigned long cycles,
					 struct clock_event_device *evt)
{
	u32 reg;

	reg = 0x80000000 | ((cycles > 1) ? (cycles-1) : 0);
	timer_writel(reg, system_timer + TIMER_PTV);

	return 0;
}

static void tegra_timer_set_mode(enum clock_event_mode mode,
				    struct clock_event_device *evt)
{
	u32 reg;

	timer_writel(0, system_timer + TIMER_PTV);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		reg = 0xC0000000 | ((1000000/HZ)-1);
		timer_writel(reg, system_timer + TIMER_PTV);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static struct clock_event_device tegra_clockevent = {
	.name		= "timer0",
	.rating		= 300,
	.features	= CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
	.set_next_event	= tegra_timer_set_next_event,
	.set_mode	= tegra_timer_set_mode,
};

static u32 notrace tegra_read_sched_clock(void)
{
	return timer_readl(TIMERUS_CNTR_1US);
}

/*
 * tegra_rtc_read - Reads the Tegra RTC registers
 * Care must be taken that this funciton is not called while the
 * tegra_rtc driver could be executing to avoid race conditions
 * on the RTC shadow register
 */
static u64 tegra_rtc_read_ms(void)
{
	u32 ms = readl(rtc_base + RTC_MILLISECONDS);
	u32 s = readl(rtc_base + RTC_SHADOW_SECONDS);
	return (u64)s * MSEC_PER_SEC + ms;
}

/*
 * read_persistent_clock -  Return time from a persistent clock.
 *
 * Reads the time from a source which isn't disabled during PM, the
 * 32k sync timer.  Convert the cycles elapsed since last read into
 * nsecs and adds to a monotonically increasing timespec.
 * Care must be taken that this funciton is not called while the
 * tegra_rtc driver could be executing to avoid race conditions
 * on the RTC shadow register
 */
void read_persistent_clock(struct timespec *ts)
{
	u64 delta;
	struct timespec *tsp = &persistent_ts;

	last_persistent_ms = persistent_ms;
	persistent_ms = tegra_rtc_read_ms();
	delta = persistent_ms - last_persistent_ms;

	timespec_add_ns(tsp, delta * NSEC_PER_MSEC);
	*ts = *tsp;
}

static irqreturn_t tegra_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = (struct clock_event_device *)dev_id;
	timer_writel(1<<30, system_timer + TIMER_PCR);
	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static struct irqaction tegra_timer_irq = {
	.name		= "timer0",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_TRIGGER_HIGH,
	.handler	= tegra_timer_interrupt,
	.dev_id		= &tegra_clockevent,
};

static int tegra_timer_suspend(void)
{
	usec_config = timer_readl(TIMERUS_USEC_CFG);

	usec_offset += timer_readl(TIMERUS_CNTR_1US);
	usec_suspended = true;

	return 0;
}

static void tegra_timer_resume(void)
{
	timer_writel(usec_config, TIMERUS_USEC_CFG);

	usec_offset -= timer_readl(TIMERUS_CNTR_1US);
	usec_suspended = false;
}

static struct syscore_ops tegra_timer_syscore_ops = {
	.suspend = tegra_timer_suspend,
	.resume = tegra_timer_resume,
};

#ifdef CONFIG_HAVE_ARM_TWD
int tegra_twd_get_state(struct tegra_twd_context *context)
{
	context->twd_ctrl = readl(twd_base + TWD_TIMER_CONTROL);
	context->twd_load = readl(twd_base + TWD_TIMER_LOAD);
	context->twd_cnt = readl(twd_base + TWD_TIMER_COUNTER);

	return 0;
}

void tegra_twd_suspend(struct tegra_twd_context *context)
{
	context->twd_ctrl = readl(twd_base + TWD_TIMER_CONTROL);
	context->twd_load = readl(twd_base + TWD_TIMER_LOAD);
	if ((context->twd_load == 0) &&
	    (context->twd_ctrl & TWD_TIMER_CONTROL_PERIODIC) &&
	    (context->twd_ctrl & (TWD_TIMER_CONTROL_ENABLE |
				  TWD_TIMER_CONTROL_IT_ENABLE))) {
		WARN("%s: TWD enabled but counter was 0\n", __func__);
		context->twd_load = 1;
	}
	__raw_writel(0, twd_base + TWD_TIMER_CONTROL);
}

void tegra_twd_resume(struct tegra_twd_context *context)
{
	BUG_ON((context->twd_load == 0) &&
	       (context->twd_ctrl & TWD_TIMER_CONTROL_PERIODIC) &&
	       (context->twd_ctrl & (TWD_TIMER_CONTROL_ENABLE |
				     TWD_TIMER_CONTROL_IT_ENABLE)));
	writel(context->twd_load, twd_base + TWD_TIMER_LOAD);
	writel(context->twd_ctrl, twd_base + TWD_TIMER_CONTROL);
}
#endif

#ifdef CONFIG_RTC_CLASS
/**
 * has_readtime - check rtc device has readtime ability
 * @dev: current device
 * @name_ptr: name to be returned
 *
 * This helper function checks to see if the rtc device can be
 * used for reading time
 */
static int has_readtime(struct device *dev, void *name_ptr)
{
	struct rtc_device *candidate = to_rtc_device(dev);

	if (!candidate->ops->read_time)
		return 0;

	return 1;
}
#endif

static void __init tegra_init_timer(void)
{
	struct clk *clk;
	int ret;

	clk = clk_get_sys("timer", NULL);
	BUG_ON(IS_ERR(clk));
	clk_enable(clk);

	/*
	 * rtc registers are used by read_persistent_clock, keep the rtc clock
	 * enabled
	 */
	clk = clk_get_sys("rtc-tegra", NULL);
	BUG_ON(IS_ERR(clk));
	clk_enable(clk);

#ifdef CONFIG_HAVE_ARM_TWD
	twd_base = IO_ADDRESS(TEGRA_ARM_PERIF_BASE + 0x600);
#endif

#ifdef CONFIG_ARCH_TEGRA_2x_SOC
	tegra2_init_timer(&system_timer, &tegra_timer_irq.irq);
#else
	tegra3_init_timer(&system_timer, &tegra_timer_irq.irq);
#endif

	setup_sched_clock(tegra_read_sched_clock, 32, 1000000);

	if (clocksource_mmio_init(timer_reg_base + TIMERUS_CNTR_1US,
		"timer_us", 1000000, 300, 32, clocksource_mmio_readl_up)) {
		printk(KERN_ERR "Failed to register clocksource\n");
		BUG();
	}

	ret = setup_irq(tegra_timer_irq.irq, &tegra_timer_irq);
	if (ret) {
		printk(KERN_ERR "Failed to register timer IRQ: %d\n", ret);
		BUG();
	}

	clockevents_calc_mult_shift(&tegra_clockevent, 1000000, 5);
	tegra_clockevent.max_delta_ns =
		clockevent_delta2ns(0x1fffffff, &tegra_clockevent);
	tegra_clockevent.min_delta_ns =
		clockevent_delta2ns(0x1, &tegra_clockevent);
	tegra_clockevent.cpumask = cpu_all_mask;
	tegra_clockevent.irq = tegra_timer_irq.irq;
	clockevents_register_device(&tegra_clockevent);

	register_syscore_ops(&tegra_timer_syscore_ops);
}

struct sys_timer tegra_timer = {
	.init = tegra_init_timer,
};
