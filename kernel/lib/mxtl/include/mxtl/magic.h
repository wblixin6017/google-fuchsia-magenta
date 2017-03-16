#pragma once

#include <assert.h>

template <uint32_t M> class Magic {
    protected:
    void AssertMagic() { DEBUG_ASSERT(magic_ == M); }
    ~Magic() {
        AssertMagic();
    }
    private:
     uint32_t magic_ = M;
};
