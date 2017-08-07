/*
 * Copyright (C) 2017 Linaro Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_INTERCONNECT_QCOM_RPM_H
#define __LINUX_INTERCONNECT_QCOM_RPM_H

#include <linux/soc/qcom/smd-rpm.h>

struct qcom_interconnect_rpm {
	struct qcom_smd_rpm *rpm;
};

extern struct qcom_interconnect_rpm interconnect_rpm;

int qcom_interconnect_rpm_send(int ctx, int rsc_type, int id, u32 val);

#endif
