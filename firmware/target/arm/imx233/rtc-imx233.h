/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright © 2011 by Amaury Pouly
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#ifndef RTC_IMX233_H
#define RTC_IMX233_H

#include "config.h"
#include "system.h"
#include "cpu.h"

#include "regs/regs-rtc.h"

#define HW_RTC_PERSISTENTn(n)   *(&HW_RTC_PERSISTENT0 + 4 * (n))

struct imx233_rtc_info_t
{
    uint32_t seconds;
    uint32_t persistent[6];
};

static inline void imx233_rtc_init(void)
{
    BF_CLR(RTC_CTRL, CLKGATE);
}

static inline uint32_t imx233_rtc_read_seconds(void)
{
    return HW_RTC_SECONDS;
}

static inline uint32_t imx233_rtc_read_persistent(int idx)
{
    return HW_RTC_PERSISTENTn(idx);
}

static inline void imx233_rtc_clear_msec_irq(void)
{
    BF_CLR(RTC_CTRL, ONEMSEC_IRQ);
}

static inline void imx233_rtc_enable_msec_irq(bool enable)
{
    imx233_rtc_clear_msec_irq();
    if(enable)
        BF_SET(RTC_CTRL, ONEMSEC_IRQ_EN);
    else
        BF_CLR(RTC_CTRL, ONEMSEC_IRQ_EN);
}

void imx233_rtc_write_seconds(uint32_t seconds);
void imx233_rtc_write_persistent(int idx, uint32_t val);

struct imx233_rtc_info_t imx233_rtc_get_info(void);

#endif /* RTC_IMX233_H */
