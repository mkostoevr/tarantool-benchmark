#pragma once

#include <cstdarg>

namespace Log {

void
data(size_t size, const void *data)
{
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		printf("%02X ", ((uint8_t *)data)[i]);
		if (((uint8_t *)data)[i] >= ' ' &&
		    ((uint8_t *)data)[i] <= '~') {
			ascii[i % 16] = ((uint8_t *)data)[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i + 1) % 8 == 0 || i + 1 == size) {
			printf(" ");
			if ((i + 1) % 16 == 0) {
				printf("|  %s \n", ascii);
			} else if (i + 1 == size) {
				ascii[(i + 1) % 16] = '\0';
				if ((i + 1) % 16 <= 8) {
					printf(" ");
				}
				for (j = (i + 1) % 16; j < 16; ++j) {
					printf("   ");
				}
				printf("|  %s \n", ascii);
			}
		}
	}
}

void
fatal_error(const char *fmt, ...)
{
	/* First print the program error message. */
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	/* Print the system error if any. */
	if (errno)
		perror("\n\nSystem error message");

	/* Exit since this is a fatal error. */
	exit(-1);
}

} // namespace Log
