#pragma once

#include <climits>

#include <string_view>
#include <numeric>
#include <expected>

#include "Data.hpp"
#include "Net.hpp"
#include "Payload.hpp"

#include "MsgPack.hpp"

class Tarantool {
public:
	struct Transfer {
		/* Amount of requests in the transfer. */
		size_t request_count;
		/* Packed sequence of requests. */
		std::vector<uint8_t> request_batch;
		/* Size of each respective packed request. */
		std::vector<size_t> response_sizes;
		/* The buffer to write response into. */
		std::vector<uint8_t> &response_buffer;

		Transfer(std::vector<uint8_t> &&request_batch_arg,
			 std::vector<size_t> &&response_sizes_arg,
			 std::vector<uint8_t> &response_buffer)
		: request_count(response_sizes_arg.size())
		, request_batch(std::move(request_batch_arg))
		, response_sizes(std::move(response_sizes_arg))
		, response_buffer(response_buffer)
		{}

		Transfer() {}
	};

	class TupleGenerator {
	public:
		TupleGenerator(Payload &payload)
		: m_payload(payload)
		{}

		size_t
		next(std::vector<uint8_t> &output)
		{
			const size_t output_original_size = output.size();
			m_values.clear();
			m_payload.next(m_values);
			const size_t part_count = m_values.size();
			const size_t extent = MsgPack::sizeof_array(part_count);
			output.resize(output.size() + extent);
			MsgPack::encode_array(&output[output.size() - extent], part_count);
			for (auto value: m_values) {
				if (value.type == Payload::Part::Type::UINT64) {
					const size_t extent = MsgPack::sizeof_uint(value.value.uint64);
					output.resize(output.size() + extent);
					MsgPack::encode_uint(&output[output.size() - extent], value.value.uint64);
				} else {
					/* TODO: Error. */
				}
			}
			return output.size() - output_original_size;
		}

	private:
		Payload &m_payload;

		/* A local variable made object field. */
		std::vector<Payload::Part::Value> m_values;
	};

	class TransferGenerator {
	public:
		TransferGenerator(Tarantool &tt, Payload &payload,
				  const char *request_name,
				  size_t request_count_per_transfer)
		: m_tt(tt)
		, m_tuple_generator(payload)
		, m_request_name(request_name)
		, m_request_count_per_transfer(request_count_per_transfer)
		, m_append_tuple(false)
		, m_invalid_request_name(false)
		{
			std::string_view request_name_sv(request_name);
			if (request_name_sv == "ping") {
				write_ping_request(m_first_bytes);
			} else if (request_name_sv == "insert") {
				/* TODO: custom space ID. */
				write_insert_request(m_first_bytes, 512);
				m_append_tuple = true;
			} else if (request_name_sv == "replace") {
				/* TODO: custom space ID. */
				write_replace_request(m_first_bytes, 512);
				m_append_tuple = true;
			} else if (request_name_sv == "delete") {
				/* TODO: custom space ID. */
				/* TODO: custom index ID. */
				/* TODO: partial keys. */
				write_delete_request(m_first_bytes, 512, 0);
				m_append_tuple = true;
			} else if (request_name_sv == "select") {
				/* TODO: custom space ID. */
				/* TODO: custom index ID. */
				/* TODO: custom limit. */
				/* TODO: custom offset. */
				/* TODO: custom iterator. */
				/* TODO: partial keys. */
				write_select_request(m_first_bytes, 512, 0,
						     0xFFFFFFFF, 0, 0);
				m_append_tuple = true;
			} else {
				m_invalid_request_name = true;
				return;
			}

			/*
			 * Create and send the first request in order to get the
			 * raw request size. The size is calculated without the
			 * generated tuple size, so we can later compute sizes
			 * of each next request individually.
			 */
			std::vector<uint8_t> first_request(m_first_bytes.begin(),
							   m_first_bytes.end());

			/* Generate and append a tuple if required. */
			const size_t tuple_size = m_append_tuple ?
						  m_tuple_generator.next(first_request) : 0;

			/*
			 * Fix-up the request header and body size field
			 * according to the generated tuple size.
			 *
			 * FIXME: MP_UINT32 expected.
			 */
			const size_t old_header_and_body_size = Data::get_uint32_be(&first_request[1]);
			Data::set_uint32_be(&first_request[1], old_header_and_body_size + tuple_size);

			/* Execute the first request to get response size. */
			const size_t first_response_size = m_tt.discover_response_size(first_request.size(),
										       first_request.data());
			/* Compute the raw response size for this request. */
			m_raw_response_size = first_response_size - tuple_size;
		}

		std::expected<Transfer, Error>
		next()
		{
			if (m_invalid_request_name)
				return std::unexpected(unknown_request());

			std::vector<uint8_t> request_batch;
			std::vector<size_t> response_sizes;

			for (size_t i = 0; i < m_request_count_per_transfer; i++) {
				request_batch.insert(request_batch.end(),
						     m_first_bytes.begin(),
						     m_first_bytes.end());

				/* Generate and append a tuple if required. */
				const size_t tuple_size = m_append_tuple ?
							  m_tuple_generator.next(request_batch) : 0;

				/* Pointer to the request we have just inserted. */
				uint8_t *const current_request = &request_batch[request_batch.size()] -
								 m_first_bytes.size() - tuple_size;

				/*
				 * Fix-up the request header and body size field
				 * according to the generated tuple size.
				 *
				 * FIXME: MP_UINT32 expected.
				 */
				const size_t old_header_and_body_size = Data::get_uint32_be(current_request + 1);
				Data::set_uint32_be(current_request + 1, old_header_and_body_size + tuple_size);

				const size_t response_size = m_raw_response_size + tuple_size;
				response_sizes.push_back(response_size);
			}

			m_common_response_buffer.resize(std:accumulate(response_sizes.begin(),
								       response_sizes.end(), 0ULL));

			return Transfer(std::move(request_batch),
					std::move(response_sizes),
					m_common_response_buffer);
		}

	private:
		void
		write_ping_request(std::vector<uint8_t> &data)
		{
			size_t estimated_size = 11;
			MsgPack::Builder builder(estimated_size);

			builder.append_uint32(6, "header and body size");
			builder.append_raw(0x82, "header");
			{
				builder.append_raw(0x00, "IPROTO_REQUEST_TYPE");
				builder.append_raw(0x40, "IPROTO_PING");
				builder.append_raw(0x01, "IPROTO_SYNC");
				builder.append_raw(0x00, "unchecked sync value");
			}
			builder.append_raw(0x80, "body");

			/* Check & write. */
			/* TODO: Return error. */
			builder.check();
			builder.build_into(data);
		}

		void
		write_insert_request(std::vector<uint8_t> &data, uint32_t space_id)
		{
			size_t sizeof_space_id = MsgPack::sizeof_uint(space_id);
			size_t estimated_size = 13 + sizeof_space_id;
			if (estimated_size > UINT32_MAX) {
				Log::fatal_error("The request size is expected to be at"
						 " most %lu, but the estimated size is "
						 "%lu\n", UINT32_MAX, estimated_size);
			}

			MsgPack::Builder builder(estimated_size);

			builder.append_uint32(estimated_size - 5, "header and body size");

			builder.append_raw(0x82, "header");
			{
				builder.append_raw(0x00, "IPROTO_REQUEST_TYPE");
				builder.append_raw(0x02, "IPROTO_INSERT");
				builder.append_raw(0x01, "IPROTO_SYNC");
				builder.append_raw(0x00, "unchecked sync value");
			}

			builder.append_raw(0x82, "body");
			{
				builder.append_raw(0x10, "IPROTO_SPACE_ID");
				builder.append_uint(space_id, "space ID");
				builder.append_raw(0x21, "IPROTO_TUPLE");
			}

			/* Check & write. */
			builder.check();
			builder.build_into(data);
		}

		void
		write_replace_request(std::vector<uint8_t> &data, uint32_t space_id)
		{
			size_t sizeof_space_id = MsgPack::sizeof_uint(space_id);
			size_t estimated_size = 13 + sizeof_space_id;
			if (estimated_size > UINT32_MAX) {
				Log::fatal_error("The request size is expected to be at"
						 " most %lu, but the estimated size is "
						 "%lu\n", UINT32_MAX, estimated_size);
			}

			MsgPack::Builder builder(estimated_size);

			builder.append_uint32(estimated_size - 5, "header and body size");

			builder.append_raw(0x82, "header");
			{
				builder.append_raw(0x00, "IPROTO_REQUEST_TYPE");
				builder.append_raw(0x03, "IPROTO_REPLACE");
				builder.append_raw(0x01, "IPROTO_SYNC");
				builder.append_raw(0x00, "unchecked sync value");
			}

			builder.append_raw(0x82, "body");
			{
				builder.append_raw(0x10, "IPROTO_SPACE_ID");
				builder.append_uint(space_id, "space ID");
				builder.append_raw(0x21, "IPROTO_TUPLE");
			}

			/* Check & write. */
			builder.check();
			builder.build_into(data);
		}

		void
		write_delete_request(std::vector<uint8_t> &data,
				     uint32_t space_id, uint32_t index_id)
		{
			size_t sizeof_space_id = MsgPack::sizeof_uint(space_id);
			size_t sizeof_index_id = MsgPack::sizeof_uint(index_id);
			size_t estimated_size = 14 + sizeof_space_id + sizeof_index_id;
			if (estimated_size > UINT32_MAX) {
				Log::fatal_error("The request size is expected to be at"
						 " most %lu, but the estimated size is "
						 "%lu\n", UINT32_MAX, estimated_size);
			}

			MsgPack::Builder builder(estimated_size);

			builder.append_uint32(estimated_size - 5, "header and body size");

			builder.append_raw(0x82, "header");
			{
				builder.append_raw(0x00, "IPROTO_REQUEST_TYPE");
				builder.append_raw(0x05, "IPROTO_REPLACE");
				builder.append_raw(0x01, "IPROTO_SYNC");
				builder.append_raw(0x00, "unchecked sync value");
			}

			builder.append_raw(0x83, "body");
			{
				builder.append_raw(0x10, "IPROTO_SPACE_ID");
				builder.append_uint(space_id, "space ID");
				builder.append_raw(0x11, "IPROTO_INDEX_ID");
				builder.append_uint(index_id, "space ID");
				builder.append_raw(0x20, "IPROTO_KEY");
			}

			/* Check & write. */
			builder.check();
			builder.build_into(data);
		}

		void
		write_select_request(std::vector<uint8_t> &data,
				     uint32_t space_id, uint32_t index_id,
				     uint32_t limit, uint32_t offset,
				     unsigned iterator)
		{
			size_t sizeof_space_id = MsgPack::sizeof_uint(space_id);
			size_t sizeof_index_id = MsgPack::sizeof_uint(index_id);
			size_t sizeof_limit = MsgPack::sizeof_uint(limit);
			size_t sizeof_offset = MsgPack::sizeof_uint(offset);
			size_t sizeof_iterator = MsgPack::sizeof_uint(iterator);
			size_t estimated_size = 5 /* Header and body size field. */ +
						5 /* Header. */ +
						1 /* Body map. */ +
						1 + sizeof_space_id +
						1 + sizeof_index_id +
						1 + sizeof_limit +
						1 + sizeof_offset +
						1 + sizeof_iterator +
						1 /* Unfinished IPROTO_KEY. */;
			if (estimated_size > UINT32_MAX) {
				Log::fatal_error("The request size is expected to be at"
						 " most %lu, but the estimated size is "
						 "%lu\n", UINT32_MAX, estimated_size);
			}

			MsgPack::Builder builder(estimated_size);

			builder.append_uint32(estimated_size - 5, "header and body size");

			builder.append_raw(0x82, "header");
			{
				builder.append_raw(0x00, "IPROTO_REQUEST_TYPE");
				builder.append_raw(0x05, "IPROTO_REPLACE");
				builder.append_raw(0x01, "IPROTO_SYNC");
				builder.append_raw(0x00, "unchecked sync value");
			}

			builder.append_raw(0x86, "body");
			{
				builder.append_raw(0x10, "IPROTO_SPACE_ID");
				builder.append_uint(space_id, "space ID");
				builder.append_raw(0x11, "IPROTO_INDEX_ID");
				builder.append_uint(index_id, "space ID");
				builder.append_raw(0x12, "IPROTO_LIMIT");
				builder.append_uint(limit, "limit");
				builder.append_raw(0x13, "IPROTO_OFFSET");
				builder.append_uint(offset, "offset");
				builder.append_raw(0x14, "IPROTO_ITERATOR");
				builder.append_uint(iterator, "iterator type");
				builder.append_raw(0x20, "IPROTO_KEY");
			}

			/* Check & write. */
			builder.check();
			builder.build_into(data);
		}

		Error
		unknown_request()
		{
			return Error_UnknownRequest(m_request_name);
		}

	private:
		Tarantool &m_tt;
		TupleGenerator m_tuple_generator;
		const char *m_request_name;
		size_t m_request_count_per_transfer;

		/* First bytes of a request are always almost the same. */
		std::vector<uint8_t> m_first_bytes;

		/* The response buffer reused by all transfers. */
		std::vector<uint8_t> m_common_response_buffer;

		/* Does the request include a generated tuple? */
		bool m_append_tuple;

		/*
		 * The expected size of all responses not including the sizes
		 * of the tuples sent.
		 */
		size_t m_raw_response_size;

		/* Error in the constructor. */
		bool m_invalid_request_name;
	};

public:
	Tarantool(const char *hostname, int port)
	{
		/* Connect to the server. */
		m_fd = Net::connect(hostname, port);

		/* Read the greeting. */
		char greeting[128] = {};
		read(m_fd, greeting, sizeof(greeting));
	}

	Error
	execute(struct Transfer &t)
	{
		/* Send the batch of requests. */
		assert(t.request_batch.size() <= SSIZE_MAX);
		const size_t bytes_sent = write(m_fd, t.request_batch.data(),
						t.request_batch.size());
		if (bytes_sent != t.request_batch.size())
			return Error_System("Can't send the request batch.");

		/* Read the responses. */
		assert(t.response_buffer.size() <= SSIZE_MAX);
		const size_t bytes_read = recv(m_fd, &t.response_buffer[0],
					       t.response_buffer.size(),
					       MSG_WAITALL);
		if (bytes_read != t.response_buffer.size())
			return Error_System("Can't recv the response.");

		return {};
	}

	Error
	check(const struct Transfer &t)
	{
		const uint8_t *data = t.response_buffer.data();
		const uint8_t *const end = data + t.response_buffer.size();
		for (size_t i = 0; i < t.request_count; i++) {
			/*
			 * Get the size of header and body. This value is
			 * expected to be encoded in a 5-byte MessagePack
			 * unsigned integer.
			 */
			assert(data + 5 < end);

			if (data[0] != 0xCE)
				return Error_ResponseSize();

			/* FIXME: MP_UINT32 expected. */
			const size_t header_and_body_size = Data::get_uint32_be(&data[1]);

			/* 5 is the size of the size specifier itself. */
			if (header_and_body_size != t.response_sizes[i] - 5) {
				fprintf(stderr, "Error: Response size mismatch,"
					" expected %lu, got %lu. Response:\n\n",
					header_and_body_size,
					t.response_sizes[i] - 5);
				Log::data(t.response_sizes[i], data);
				return {}; // TODO
			}

			/* TODO: Check if no error. */

			/* Step over the response. */
			data += t.response_sizes[i];
		}
		return {};
	}

private:
	size_t
	discover_response_size(size_t req_size, const uint8_t *req)
	{
		assert(req_size <= SSIZE_MAX);

		/* Send the request. */
		if (size_t(write(m_fd, req, req_size)) != req_size)
			Log::fatal_error("Couldn't get request response size");

		/* Read the size of response header and body. */
		uint8_t res_size_buf[5];
		read(m_fd, res_size_buf, 5);

		/* Parse the size of response header and body. */
		if (res_size_buf[0] != 0xCE) {
			Log::fatal_error("Unexpected size of IPROTO request"
					 " size specifier in response. Expected"
					 " 0xCE, got %0x\n", res_size_buf[0]);
		}

		/* FIXME: MP_UINT32 expected. */
		const size_t res_header_and_body_size =
			Data::get_uint32_be(&res_size_buf[1]);

		/* Read the response header and body. */
		uint8_t *res_header_and_body_buf =
			new uint8_t [res_header_and_body_size];
		if (res_header_and_body_buf == NULL) {
			Log::fatal_error("Couldn't allocate %lu "
					 "bytes for response body",
					 res_header_and_body_size);
		}
		read(m_fd, res_header_and_body_buf, res_header_and_body_size);
		delete[] res_header_and_body_buf;

		return sizeof(res_size_buf) + res_header_and_body_size;
	}

private:
	int m_fd = -1;
};
