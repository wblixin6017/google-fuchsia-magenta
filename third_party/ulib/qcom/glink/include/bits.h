// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

/* Trick to get a 1 of the right size */
#define _ONE(x) (1 + ((x) - (x)))

#define BIT(bit) (1 << (bit))
#define BIT_SHIFT(x, bit) (((x) >> (bit)) & 1)
#define BITS_SHIFT(x, high, low) (((x) >> (low)) & ((_ONE(x)<<((high)-(low)+1))-1))
