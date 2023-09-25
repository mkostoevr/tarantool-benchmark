#pragma once

class Timer {
	struct timespec m_t0;

public:
	Timer()
	{
		clock_gettime(CLOCK_MONOTONIC, &m_t0);
	}

	uint64_t
	ns()
	{
		struct timespec t1;
		clock_gettime(CLOCK_MONOTONIC, &t1);
		/* TODO: Fix this comparison. */
		return (t1.tv_sec * 1000000000llu + t1.tv_nsec) -
		       (m_t0.tv_sec * 1000000000llu + m_t0.tv_nsec);
	}
};
