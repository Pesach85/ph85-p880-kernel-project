#ifndef _TEGRA_VIBRATOR_H
#define _TEGRA_VIBRATOR_H

#define VIBRATOR_NAME "tegra_vibrator"

struct tegra_pwm_data {
	const char *name;
	struct pwm_device *pwm_dev;
	int bank;
};

struct vibrator_platform_data {
	struct tegra_pwm_data pwm_data;
	int pwm_gpio;
	int ena_gpio;
};

#endif  //_TEGRA_VIBRATOR_H
