#pragma once

#include "msgpuck/msgpuck.h"

namespace MsgPack  {

size_t
sizeof_uint(uint64_t value)
{
	return mp_sizeof_uint(value);
}

size_t
sizeof_array(uint32_t size)
{
	return mp_sizeof_array(size);
}

void
encode_uint(uint8_t *buffer, uint64_t value)
{
	mp_encode_uint((char *)buffer, value);
}

void
encode_array(uint8_t *buffer, uint32_t size)
{
	mp_encode_array((char *)buffer, size);
}

class Builder {
	std::vector<uint8_t> m_buffer;
	uint8_t *m_ptr;
	const uint8_t *const m_end;
	bool m_overflown;
	std::string m_message;

public:
	Builder(size_t estimated_size)
	: m_buffer(estimated_size)
	, m_ptr(&m_buffer[0])
	, m_end(m_ptr + m_buffer.size())
	, m_overflown(false)
	{
		assert(m_buffer.size() != 0);
	}

	void
	append_uint(uint64_t value, const char *name)
	{
		assert(m_buffer.size() != 0);
		size_t sz = MsgPack::sizeof_uint(value);
		if (m_ptr + sz > m_end)
			return overflow(name);
		m_ptr = (uint8_t *)mp_encode_uint((char *)m_ptr, value);
	}

	void
	append_uint32(uint32_t value, const char *name)
	{
		assert(m_buffer.size() != 0);
		if (m_ptr + 5 > m_end)
			return overflow(name);
		*m_ptr++ = 0xCE;
		Data::set_uint32_be(m_ptr, value);
		m_ptr += 4;
	}

	void
	append_raw(uint8_t value, const char *name)
	{
		assert(m_buffer.size() != 0);
		if (m_ptr >= m_end)
			return overflow(name);
		*m_ptr++ = value;
	}

	void
	append_raw(std::vector<uint8_t> data, const char *name)
	{
		assert(m_buffer.size() != 0);
		if (m_ptr + data.size() > m_end)
			return overflow(name);
		memcpy(m_ptr, data.data(), data.size());
		m_ptr += data.size();
	}

	void
	check()
	{
		if (m_overflown)
			Log::fatal_error(m_message.c_str());
	}

	void
	build_into(std::vector<uint8_t> &data)
	{
		assert(m_buffer.size() != 0);
		assert(m_ptr == m_end);
		data.insert(data.end(), m_buffer.begin(), m_buffer.end());
	}

private:
	void
	overflow(const char *name)
	{
		if (!m_overflown) {
			m_message = std::string("Overflow while buliding ") +
				    name;
		}
		m_overflown = true;
	}
};

}
