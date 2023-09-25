#pragma once

#include <string>
#include <string_view>
#include <algorithm>
#include <cassert>
#include <yaml.h>

struct Payload {
	struct Part {
		enum Type {
			UINT64,
		};

		/* The object type to return on next value request. */
		struct Value {
			enum Type type;
			struct {
				uint64_t uint64;
			} value;

			Value(uint64_t value)
			: type(Type::UINT64)
			, value({.uint64 = value})
			{}

			Value
			operator=(uint64_t other)
			{
				assert(type == Type::UINT64);
				value.uint64 = other;
				return *this;
			}

			Value
			operator++(int)
			{
				assert(type == Type::UINT64);
				Value result = *this;
				value.uint64++;
				return result;
			}

			Value
			operator--(int)
			{
				assert(type == Type::UINT64);
				Value result = *this;
				value.uint64--;
				return result;
			}

			Value
			operator+(size_t other)
			{
				Value result = *this;
				result.value.uint64 += other;
				return result;
			}

			int
			operator<=>(Value other)
			{
				if (type != other.type)
					return type - other.type;
				if (type == Type::UINT64)
					return value.uint64 - other.value.uint64;
				return -1;
			}

			std::string
			tostring()
			{
				assert(type == Type::UINT64);
				return std::to_string(value.uint64);
			}
		};

		enum Distribution {
			INCREMENTAL, /* From min_value to max_value. */
			DECREMENTAL, /* From max_value to min_value. */
			LINEAR,      /* Random, linear distribution. */
			NORMAL,      /* Random, normal distribution. */
		};

		enum Type type;
		Value min;
		Value max;
		enum Distribution distribution;

	public:
		Part(Value min, Value max, Distribution distribution, size_t request_count)
		: type(min.type)
		, min(min)
		, max(max)
		, distribution(distribution)
		, m_next_value(uint64_t(0))
		{
			assert(min.type == max.type);
			if (distribution == INCREMENTAL) {
				m_next_value = min;
			} else if (distribution == DECREMENTAL) {
				m_next_value = max;
			} else {
				for (Value i = min; i < max; i++)
					m_values.emplace_back(i);
				if (m_values.size() < request_count)
					Log::fatal_error("No enough values between min and max to provide data for at least %lu requests.\n", request_count);
				std::random_shuffle(m_values.begin(),
						    m_values.end());
				m_values_i = 0;
			}
		}

		Value
		next()
		{
			if (distribution == INCREMENTAL)
				return m_next_value++;
			else if (distribution == DECREMENTAL)
				return m_next_value--;
			else
				return m_values[m_values_i++];
		}

	private:
		/* FIXME(multitool): data to be used by tuple generators. */

		/* For random distribution. */
		std::vector<Value> m_values;
		size_t m_values_i; /* Current position in m_values. */

		/* For incremental/decremental distribution. */
		Value m_next_value;
	};

public:
	std::vector<Part> parts;

private:
	size_t m_request_count;

public:
	Payload(size_t request_count)
	: m_request_count(request_count)
	{
		parts.emplace_back(Part::Value(uint64_t(0)),
				   Part::Value(uint64_t(request_count)),
				   Part::Distribution::INCREMENTAL,
				   request_count);
	}

	Error
	parse_config(const char *config_file) {
		if (config_file == NULL)
			return {};

		/* Clear the default config. */
		parts.clear();

		/* Read the config file. */
		FILE *const fp = fopen(config_file, "r");
		if (fp == NULL)
			Log::fatal_error("Can't open the config file: %s", config_file);

		/* Parse the config. */
		yaml_parser_t parser;
		yaml_token_t token;

		if (!yaml_parser_initialize(&parser))
			Log::fatal_error("Can't initialize the YAML parser.");

		yaml_parser_set_input_file(&parser, fp);

		enum {
			PARSE_KEY,
			PARSE_TYPE,
			PARSE_MIN,
			PARSE_MAX,
			PARSE_DISTRIBUTION,
		} state;

		struct {
			std::optional<Part::Type> type;
			std::optional<Part::Value> min;
			std::optional<Part::Value> max;
			std::optional<Part::Distribution> distribution;
		} next_part;

		int level = 0;

		do {
			yaml_parser_scan(&parser, &token);
			switch(token.type)
			{
			case YAML_KEY_TOKEN:
				state = PARSE_KEY;
				break;
			case YAML_SCALAR_TOKEN:
				if (state == PARSE_KEY) {
					std::string_view key((char *)token.data.scalar.value,
							     token.data.scalar.length);
					if (key == "type")
						state = PARSE_TYPE;
					else if (key == "min")
						state = PARSE_MIN;
					else if (key == "max")
						state = PARSE_MAX;
					else if (key == "distribution")
						state = PARSE_DISTRIBUTION;
					else
						Log::fatal_error("Unrecognised part property: %s", key.data());
				} else if (state == PARSE_TYPE) {
					std::string_view key((char *)token.data.scalar.value,
							     token.data.scalar.length);
					if (key == "uint64")
						next_part.type = Part::Type::UINT64;
					else
						Log::fatal_error("Unrecognised part type: %s", key.data());
				} else if (state == PARSE_DISTRIBUTION) {
					std::string_view key((char *)token.data.scalar.value,
							     token.data.scalar.length);
					if (key == "linear")
						next_part.distribution = Part::Distribution::LINEAR;
					else if (key == "incremental")
						next_part.distribution = Part::Distribution::INCREMENTAL;
					else if (key == "decremental")
						next_part.distribution = Part::Distribution::DECREMENTAL;
					else
						Log::fatal_error("Unrecognised distribution: %s", key.data());
					state = PARSE_KEY;
				} else if (state == PARSE_MIN || state == PARSE_MAX) {
					if (!next_part.type)
						Log::fatal_error("Part type must be set prior to the min/max.\n");
					std::string key((char *)token.data.scalar.value,
							token.data.scalar.length);
					if (*next_part.type == Part::Type::UINT64) {
						uint64_t value = std::stoul(key);
						if (state == PARSE_MIN)
							next_part.min = Part::Value(value);
						else
							next_part.max = Part::Value(value);
					} else {
						Log::fatal_error("Min/max for this type is not implemented.\n");
					}
					state = PARSE_KEY;
				}
				break;
			case YAML_BLOCK_ENTRY_TOKEN:
				level++;
				if (parts.size() == 0)
					break;
				[[fallthrough]];
			case YAML_BLOCK_END_TOKEN:
				/* FIXME: get rid of this shit. */
				if (level == 0)
					break;
				level--;
				if (!next_part.type)
					Log::fatal_error("Part type must be specified.\n");
				if (!next_part.min)
					next_part.min = Part::Value(uint64_t(0));
				if (!next_part.max)
					next_part.max = *next_part.min + m_request_count;
				if (!next_part.distribution)
					next_part.distribution = Part::Distribution::LINEAR;
				parts.emplace_back(*next_part.min, *next_part.max,
						   *next_part.distribution, m_request_count);
				next_part.type = {};
				next_part.min = {};
				next_part.max = {};
				next_part.distribution = {};
				break;
			default:
				break;
			}
			if (token.type != YAML_STREAM_END_TOKEN)
				yaml_token_delete(&token);
		} while (token.type != YAML_STREAM_END_TOKEN);

		/* Clean-up. */
		yaml_token_delete(&token);
		yaml_parser_delete(&parser);
		fclose(fp);
		return {};
	}

	void
	next(std::vector<struct Part::Value> &output)
	{
		for (auto &part: parts)
			output.push_back(part.next());
	}
};
