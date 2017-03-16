#pragma once

#include <assert.h>

template <uint32_t M> class Magic {
    protected:
    void AssertMagic() const { DEBUG_ASSERT(magic_ == M); }
    ~Magic() {
        AssertMagic();
        *(volatile uint32_t*)&magic_ = 0;
    }
    private:
     uint32_t magic_ = M;
};
