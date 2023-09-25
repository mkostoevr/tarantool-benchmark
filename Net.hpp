#pragma once

#include <errno.h>
#include <netdb.h>
#include <unistd.h>

namespace Net {

int
connect(const char *hostname, uint16_t port)
{
	/* Create the connection socket. */
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		Log::fatal_error("Couldn't create a socket");
	/* Set socket options. */
	{
		const struct timeval tmout_send = {};
		const struct timeval tmout_recv = {};
		if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
			       &tmout_send, sizeof(tmout_send)) == -1)
			Log::fatal_error("Couldn't set socket send timeout");
		if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
			       &tmout_recv, sizeof(tmout_recv)) == -1)
			Log::fatal_error("Couldn't set socket recv timeout");
	}
	/* Get Tarantool address. */
	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	{
		struct addrinfo *addr_info = NULL;
		if (getaddrinfo(hostname, NULL, NULL, &addr_info) != 0)
			Log::fatal_error("Couldn't resolve the IP address");
		const struct in_addr *const sin_addr =
			&((struct sockaddr_in *)addr_info->ai_addr)->sin_addr;
		memcpy(&addr.sin_addr, (void *)sin_addr, sizeof(addr.sin_addr));
		freeaddrinfo(addr_info);
	}
	/* Connect to the Tarantool. */
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
		Log::fatal_error("Couldn't connect to Tarantool");
	return fd;
}

} // namespace Net
