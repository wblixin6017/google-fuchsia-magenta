// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <magenta/syscalls.h>

#include <magenta/device/i2c.h>

#include <unistd.h>


#include "pcm5122.h"
#include "hifi-berry.h"


/*
    HiFiBerry DAC+ - i2s slave, i2c control mode, using BCLK as the reference

    To keep things simple/manageable, always assume a i2s interface with
     64bclk per audio frame

*/

#define HIFIBERRY_I2C_ADDRESS 0x4d
#define DEVNAME "/dev/soc/bcm-i2c/i2c1"

typedef struct {
    int         i2c_fd;
    uint32_t    state;
} hifiberry_t;

static hifiberry_t* hfb = NULL;


mx_status_t hifiberry_release(void) {

    if (!hfb) return NO_ERROR;

    if(hfb->i2c_fd) {
        close(hfb->i2c_fd);
    }

    free(hfb);
    hfb = NULL;

    return NO_ERROR;

}



static mx_status_t hifiberry_LED_ctl(uint8_t state) {

    if (!hfb) return ERR_BAD_STATE;
    if (!hfb->i2c_fd) return ERR_BAD_STATE;
    if (!hfb->state) return ERR_BAD_STATE;

    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_GPIO_CONTROL, (state) ? (1 << 3) : 0 );

    return NO_ERROR;
}

mx_status_t hifiberry_start(void) {
    return hifiberry_LED_ctl(true);
}

mx_status_t hifiberry_stop(void) {
    return hifiberry_LED_ctl(false);
}

mx_status_t hifiberry_init(void) {

    // Check to see if already initialized
    if ((hfb) && (hfb->state)) return ERR_BAD_STATE;

    if (hfb == NULL) {
        hfb = calloc(1,sizeof(hifiberry_t));
        if (!hfb) return ERR_NO_MEMORY;
    }

    hfb->i2c_fd = open(DEVNAME,O_RDWR);
    if (hfb->i2c_fd<0) {
        printf("HIFIBERRY: Control channel not found\n");
        return ERR_NOT_FOUND;
    }

   i2c_ioctl_add_slave_args_t add_slave_args = {
        .chip_address_width = I2C_7BIT_ADDRESS,
        .chip_address = HIFIBERRY_I2C_ADDRESS,
    };

    ssize_t ret = ioctl_i2c_bus_add_slave(hfb->i2c_fd, &add_slave_args);
    if (ret < 0) {
        return ERR_INTERNAL;
    }

    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_GPIO_ENABLE, 0x08);       // configure LED GPIO
    pcm5122_write_reg(hfb->i2c_fd, PCM5122_REG_GPIO4_OUTPUT_SELECTION, 0x02);
    hifiberry_LED_ctl(0);

    // Clock source for pll = 1 (bclk)
    pcm5122_write_reg(hfb->i2c_fd,PCM5122_REG_PLL_CLK_SOURCE, 1 << 4);

    pcm5122_write_reg(hfb->i2c_fd,PCM5122_REG_ERROR_MASK, (1 << 4) |  // Ignore sck detection
                                                          (1 << 3) |  // Ignore sck halt detection
                                                          (1 << 2) ); // Disable clock autoset


    // Most of the below are mode specific, should defer to some mode set routine...
    pcm5122_write_reg(hfb->i2c_fd, 27,    1);         //  DDSP divider 1 (=/2)
    pcm5122_write_reg(hfb->i2c_fd, 28,   15);         // DAC Divider = /16
    pcm5122_write_reg(hfb->i2c_fd, 29,    3);         // CP Divider = /4
    pcm5122_write_reg(hfb->i2c_fd, 30,    7);         // OSR Divider = /8
    pcm5122_write_reg(hfb->i2c_fd, 14, 0x10);         // DAC CLK Mux = PLL


    pcm5122_write_reg(hfb->i2c_fd, 4 , (1 << 0));     // Enable the PLL
    pcm5122_write_reg(hfb->i2c_fd, 20,  0 );              // P = 0
    pcm5122_write_reg(hfb->i2c_fd, 21, 16 );              // J = 16
    pcm5122_write_reg(hfb->i2c_fd, 22,  0 );              // D = 0
    pcm5122_write_reg(hfb->i2c_fd, 23,  0 );              //      (D uses two registers)
    pcm5122_write_reg(hfb->i2c_fd, 24,  1 );              // R = 2

    hfb->state = 1;

    return NO_ERROR;
}


bool pcm5122_is_valid_mode( audio2_stream_cmd_set_format_req_t req  ){

    //Yes this is lame.  Will make it better as we add more modes.
    if  ( (req.frames_per_second == 44100)  &&
          (req.packing == AUDIO2_BIT_PACKING_16BIT_LE) &&
          (req.channels == 2) ) {
        return true;
    }

    return false;
}