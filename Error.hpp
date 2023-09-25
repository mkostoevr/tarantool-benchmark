#include <cstdarg>

#include <memory>

#define Error_0(...) Error(make_error_message(__VA_ARGS__))
#define Error_1(e, ...) Error(e, make_error_message(__VA_ARGS__))

#define Error_Argparse() Error_0("Error parsing the command line")

#define Error_Usage(argv0)						\
	Error_0("Usage: `%s <request> [-b <request_count_per_transfer>"	\
		"] [-p <port>][-c <request_count>]'", argv0)

#define Error_BatchSize(request_count, request_count_per_transfer)	\
	Error_0("Request count must be divisible by the batch size. "	\
		"Given request count: %lu, given batch size: %lu.",	\
		request_count, request_count_per_transfer)

#define Error_ConfigParseFailed(config_parse_error, name)		\
	Error_1(config_parse_error,					\
		"Failed to parse the '%s' config file", name)

#define Error_BenchmarkFailed(benchmark_error)				\
	Error_1(benchmark_error, "Failed to benchmark Tarantool")

#define Error_BatchBuild(build_error, i)				\
	Error_1(build_error, "Couldn't prepare transfer #%lu", i)

#define Error_BatchTransfer(batch_error, i)				\
	Error_1(batch_error, "Couldn't perform transfer #%lu", i)

#define Error_ResponseCheck(check_error, i)				\
	Error_1(check_error, "Unexpected response to transfer #%lu", i)

#define Error_ResponseSize()						\
	Error_0("Expected 4-byte MsgPack as the response size")

#define Error_System(message)				\
	Error_0("%s: ", message, strerror(errno))

#define Error_UnknownRequest(name)	\
	Error_0("Unknown request name: '%s'", name)

namespace {

std::unique_ptr<char[]>
make_error_message(const char *fmt, ...) {
	va_list args0, args1;
	va_start(args0, fmt);
	va_copy(args1, args0);
	const size_t bufsz = vsnprintf(NULL, 0, fmt, args0) + 1;
	va_end(args0);
	std::unique_ptr<char[]> message(new char[bufsz]());
	if (message == NULL) {
		fprintf(stderr, "Fatal: couldn't allocate memory for "
				"error message :|");
		exit(-1);
	}
	const size_t result = vsnprintf(&message[0], bufsz, fmt, args1);
	va_end(args1);
	if (result > bufsz - 1) {
		fprintf(stderr, "Internal: error message overflow.");
		exit(-1);
	}
	return message;
}

}

class Error {
	std::unique_ptr<Error> m_next;
	std::unique_ptr<char[]> m_message;

public:
	Error(Error &other) = delete;

	Error(Error &&other) = default;

	Error()
	: m_next(nullptr)
	, m_message(nullptr)
	{}

	Error(std::unique_ptr<char[]> message)
	: m_next(nullptr)
	, m_message(std::move(message))
	{}

	Error(Error &next, std::unique_ptr<char[]> message)
	: m_next(std::make_unique<Error>(std::move(next)))
	, m_message(std::move(message))
	{}

	/*
	Error &operator=(Error &&other)
	{
		
		return *this;
	}
	*/

	void
	report(int level = 0)
	{
		for (int i = 0; i < level; i++)
			fputs("  ", stderr);
		fputs(&m_message[0], stderr);
		if (m_next) {
			fputs(":\n", stderr);
			m_next->report(level + 1);
		} else {
			fputs(".\n", stderr);
		}
	}

	operator bool()
	{
		return m_message != nullptr;
	}
};
