#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <cstdbool>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <vector>
#include <algorithm>

#include "Error.hpp"
#include "Log.hpp"
#include "Statistics.hpp"
#include "Tarantool.hpp"
#include "Timer.hpp"
#include "Payload.hpp"

template <class Tarantool>
Error
benchmark(Tarantool &tt,
	  const char *request_name,
	  Payload &payload,
	  size_t request_count,
	  size_t request_count_per_transfer,
	  std::vector<uint64_t> &latencies_ns)
{
	/* Do the benchmarking. */
	const size_t transfer_count = request_count /
				      request_count_per_transfer;
	latencies_ns.resize(transfer_count);

	typename Tarantool::TransferGenerator tg(tt, payload, request_name,
						 request_count_per_transfer);

	for (size_t i = 0; i < transfer_count; i++) {
		auto transfer = tg.next();
		if (!transfer)
			return Error_BatchBuild(transfer.error(), i);

		Timer timer;

		if (Error error = tt.execute(*transfer); error)
			return Error_BatchTransfer(error, i);

		latencies_ns[i] = timer.ns();

		if (Error error = tt.check(*transfer); error)
			return Error_ResponseCheck(error, i);
	}

	return {};
}

Error
start(int argc, char **argv)
{
	const char *data = NULL;
	const char *cdf = NULL;
	const char *hist = NULL;
	const char *rcdf = NULL;
	const char *config_file = NULL;
	int port = 3301;
	size_t request_count_per_transfer = 1000;
	size_t request_count = 1000000;
	const char *request_name = NULL;

	while (request_name == NULL) {
		switch (getopt(argc, argv, "b:g:h:r:p:c:i:o:")) {
		case 'b':
			request_count_per_transfer = atol(optarg);
			continue;
		case 'o':
			data = optarg;
			continue;
		case 'g':
			cdf = optarg;
			continue;
		case 'r':
			rcdf = optarg;
			continue;
		case 'h':
			hist = optarg;
			continue;
		case 'p':
			port = atoi(optarg);
			continue;
		case 'c':
			request_count = atol(optarg);
			continue;
		case 'i':
			config_file = optarg;
			continue;
		case '?':
			return Error_Argparse();
		case -1:
			if (optind == argc)
				return Error_Usage(argv[0]);
			request_name = argv[optind];
			break;
		};
	}
	if (request_count % request_count_per_transfer != 0)
		return Error_BatchSize(request_count,
				       request_count_per_transfer);

	/*
	 * Create a test payload. +1 for the first request to compute the
	 * response sizes for next requests.
	 */
	Payload payload(request_count + 1);

	if (Error error = payload.parse_config(config_file); error)
		return Error_ConfigParseFailed(error, config_file);

	/* Connect to Tarantool. */
	Tarantool tt("localhost", port);

	/* Benchmark it. */
	std::vector<uint64_t> latencies_ns;
	if (Error error = benchmark(tt, request_name, payload, request_count,
				    request_count_per_transfer, latencies_ns); error)
		return Error_BenchmarkFailed(error);

	/* Sort the collected data. */
	std::sort(latencies_ns.begin(), latencies_ns.end());

	/* Calculate the overall time. */
	const double overall_ns = std::accumulate(latencies_ns.begin(),
						  latencies_ns.end(), 0ULL);

	/* Calculate statistics. */
	using namespace Statistics;
	const double rps = (double)request_count / (overall_ns / 1000000000.0);
	const double avg_us = average(latencies_ns) / 1000.0;
	const double med_us = median(latencies_ns) / 1000.0;
	const double min_us = (double)latencies_ns[0] / 1000.0;
	const double max_us = (double)latencies_ns[latencies_ns.size() - 1] / 1000.0;
	const double p90_us = percentile(latencies_ns, 0.9) / 1000.0;
	const double p99_us = percentile(latencies_ns, 0.99) / 1000.0;
	const double p999_us = percentile(latencies_ns, 0.999) / 1000.0;

	/* Print it out. */
	printf("Request: %s\n", request_name);
	printf("Batch size: %lu\n", request_count_per_transfer);
	printf("RPS: %.0f\n", rps);
	printf("Avg (μs): %.3f\n", avg_us / request_count_per_transfer);
	printf("Med (μs): %.3f\n", med_us / request_count_per_transfer);
	printf("Min (μs): %.3f\n", min_us / request_count_per_transfer);
	printf("Max (μs): %.3f\n", max_us / request_count_per_transfer);
	printf("90%% (μs): %.3f\n", p90_us / request_count_per_transfer);
	printf("99%% (μs): %.3f\n", p99_us / request_count_per_transfer);
	printf("99.9%% (μs): %.3f\n", p999_us / request_count_per_transfer);

	/* Output the raw data. */
	if (data) {
		FILE *out = fopen(data, "ab");
		fwrite(latencies_ns.data(), sizeof(latencies_ns[0]),
		       latencies_ns.size(), out);
		fclose(out);
	}

	/* Output the cumulative distribution function. */
	if (cdf) {
		FILE *out = fopen(cdf, "w");
		for (size_t i = 0; i < request_count; i++) {
			uint64_t x = latencies_ns[i];
			double y = (double)(i + 1) / (double)request_count;
			fprintf(out, "%zu\t%f\n", x, y);
		}
		fclose(out);
	}

	/* Output the reversed cumulative distribution function. */
	if (rcdf) {
		FILE *out = fopen(rcdf, "w");
		for (size_t i = 0; i < request_count; i++) {
			double x = (double)(i + 1) / request_count;
			uint64_t y = latencies_ns[i];
			fprintf(out, "%f\t%zu\n", x, y);
		}
		fclose(out);
	}

	/* Output the latency histogram. */
	if (hist) {
		FILE *out = fopen(hist, "w");
		uint64_t granularity = 10;
		uint64_t prev_value = latencies_ns[0] - (latencies_ns[0] % granularity);
		size_t prev_count = 1;
		for (size_t i = 0; i < request_count; i++) {
			uint64_t x = latencies_ns[i] -
				     (latencies_ns[i] % granularity);
			if (x != prev_value) {
				fprintf(out, "%zu\t%zu\n", prev_value, prev_count);
				prev_value = x;
				prev_count = 1;
			} else {
				prev_count++;
			}
		}
		fprintf(out, "%zu\t%zu\n", prev_value, prev_count);
		fclose(out);
	}

	return {};
}

int
main(int argc, char **argv)
{
	if (Error error = start(argc, argv); error) {
		error.report();
		return -1;
	}
	return 0;
}
