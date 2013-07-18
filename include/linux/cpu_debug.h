#ifndef __CPU_DEBUG_H__
#define __CPU_DEBUG_H__

#define CPU_DEBUG_TAG		"[CPUDEBUG]"

#define CPU_DEBUG_GOVERNOR	0x01
#define CPU_DEBUG_FREQ		0x02
#define CPU_DEBUG_HOTPLUG	0x04
#define CPU_DEBUG_RQ		0x08
#define CPU_DEBUG_BTHP      0x10
#define CPU_DEBUG_BTHP_LB   0x20
#define CPU_DEBUG_BTHP_LBX  0x40

<<<<<<< HEAD
/* unsigned int get_cpu_debug(void); */

/*
 * EternityProject, 27/07/2012:
 * Hack the hackish hack.
 */
#define CPU_DEBUG_NONE		0x00
unsigned int get_cpu_debug(void)
{
	return CPU_DEBUG_NONE;
}
=======
unsigned int get_cpu_debug(void);
>>>>>>> ef06eaf... Optimize the hell out of it

#define CPU_DEBUG_PRINTK(flag, fmt, ...)				\
	if (get_cpu_debug() & flag) {					\
		pr_info(CPU_DEBUG_TAG pr_fmt(fmt), ##__VA_ARGS__);	\
	}								\

#endif
