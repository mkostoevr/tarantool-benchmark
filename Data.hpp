#pragma once

namespace Data {

uint64_t
get_unsigned_be(const uint8_t *buf, int bytes)
{
	uint64_t result = 0;
	for (int i = 0; i < bytes; i++)
		result |= buf[i] >> ((bytes - 1 - i) * 8);
	return result;
}

uint64_t
get_uint64_be(const uint8_t *buf)
{
	return get_unsigned_be(buf, 8);
}

uint32_t
get_uint32_be(const uint8_t *buf)
{
	return get_unsigned_be(buf, 4);
}

void
set_unsigned_be(uint8_t *buf, uint64_t value, int bytes)
{
	for (int i = 0; i < bytes; i++)
		buf[i] = (value >> ((bytes - 1 - i) * 8)) & 0xFF;
}

void
set_unsigned_le(uint8_t *buf, uint64_t value, int bytes)
{
	for (int i = 0; i < bytes; i++)
		buf[i] = (value >> (i * 8)) & 0xFF;
}

void
set_uint32_be(uint8_t *buf, uint32_t value)
{
	set_unsigned_be(buf, value, sizeof(value));
}

void
set_uint32_le(uint8_t *buf, uint32_t value)
{
	set_unsigned_le(buf, value, sizeof(value));
}

} // namespace Data
