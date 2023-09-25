#pragma once

namespace Rng {

uint32_t
u32()
{
	static uint32_t state = 1;
	return state = (uint64_t)state * 48271 % 0x7fffffff;
}

} // namespace Rng
