#pragma once

typedef struct hsphy {
    void* regs;
    void* efuse_reg;
} hsphy_t;

mx_status_t hsphy_init(hsphy_t** out);
