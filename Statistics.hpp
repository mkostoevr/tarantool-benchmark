#pragma once

namespace Statistics {

template<class Data>
double
average(const Data &data)
{
	double sum = 0.0;
	for (size_t i = 0; i < data.size(); i++)
		sum += data[i];
	return sum / data.size();
}

template<class Data>
double
median(const Data &data)
{
	if (data.size() == 0)
		return 0;
	if (data.size() % 2 == 1)
		return data[data.size() / 2];
	size_t i_right = data.size() / 2;
	size_t i_left = i_right - 1;
	return ((double)data[i_left] + (double)data[i_right]) / 2.0;
}

template<class Data>
double
percentile(const Data &data, double p)
{
	return data[(size_t)((data.size() - 1) * p)];
}

} // namespace Statistics
