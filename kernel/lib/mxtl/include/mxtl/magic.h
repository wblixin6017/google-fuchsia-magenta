#pragma once

#include <assert.h>

template <uint32_t M> class Magic {
    protected:
    void AssertMagic() const { DEBUG_ASSERT_MSG(magic_ == M, "Invalid magic (expt: %08x, got: %08x\n", M, magic_); }
    ~Magic() {
        AssertMagic();
        *(volatile uint32_t*)&magic_ = 0;
    }
    private:
     uint32_t magic_ = M;
};
