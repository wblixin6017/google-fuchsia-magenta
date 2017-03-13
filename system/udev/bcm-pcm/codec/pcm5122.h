// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/device/audio2.h>

#pragma once

#define PCM5122_REG_PLL_ENABLE              (uint8_t)(4)

#define PCM5122_REG_GPIO_ENABLE             (uint8_t)(8)

#define PCM5122_REG_PLL_CLK_SOURCE          (uint8_t)(13)
#define PCM5122_REG_DAC_CLK_SOURCE          (uint8_t)(14)

#define PCM5122_REG_PLL_P                   (uint8_t)(20)
#define PCM5122_REG_PLL_J                   (uint8_t)(21)
#define PCM5122_REG_PLL_D_HI                (uint8_t)(22)
#define PCM5122_REG_PLL_D_LO                (uint8_t)(23)
#define PCM5122_REG_PLL_R                   (uint8_t)(24)

#define PCM5122_REG_DSP_CLK_DIVIDER         (uint8_t)(27)
#define PCM5122_REG_DAC_CLK_DIVIDER         (uint8_t)(28)
#define PCM5122_REG_NCP_CLK_DIVIDER         (uint8_t)(29)
#define PCM5122_REG_OSR_CLK_DIVIDER         (uint8_t)(30)
#define PCM5122_REG_FS_SPEED_MODE           (uint8_t)(34)

#define PCM5122_REG_ERROR_MASK              (uint8_t)(37)
#define PCM5122_REG_I2S_CONTROL             (uint8_t)(40)



#define PCM5122_REG_GPIO4_OUTPUT_SELECTION  (uint8_t)(83)
#define PCM5122_REG_GPIO_CONTROL            (uint8_t)(86)


#define PCM5122_STATE_INITIALIZED           ( 1 << 0)


static inline void pcm5122_write_reg(int fd, uint8_t address, uint8_t value) {
    uint8_t argbuff[2] = {address, value};
    write(fd,argbuff,2);
}


