#pragma once

// only included in case there's a C++11 compiler out there that doesn't support `#pragma once`
#ifndef DKM_KMEANS_H
#define DKM_KMEANS_H

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <vector>

#if defined(_DEBUG) && (defined(WIN32) || defined(_WINDOWS))
#undef _DEBUG
#include <random>
#define _DEBUG
#else
#include <random>
#endif

/*
DKM - A k-means implementation that is generic across variable data dimensions.
*/
namespace dkm {

/*
These functions are all private implementation details and shouldn't be referenced outside of this
file.
*/
namespace details {

/*
Calculate the square of the distance between two points.
*/
template <typename T, size_t N>
T distance_squared(const std::array<T, N>& point_a, const std::array<T, N>& point_b) {
	T d_squared = T();
	for (typename std::array<T, N>::size_type i = 0; i < N; ++i) {
		auto delta = point_a[i] - point_b[i];
		d_squared += delta * delta;
	}
	return d_squared;
}

template <typename T, size_t N>
T distance(const std::array<T, N>& point_a, const std::array<T, N>& point_b) {
    return std::sqrt(distance_squared(point_a, point_b));
}
    
/*
 Calculate the mean square float distance between a collection of points.
 */
template <typename T, size_t N>
float point_collection_epsilon(const std::vector< std::array<T, N> >& point_a, const std::vector< std::array<T, N> >& point_b) {
    assert( point_a.size() == point_b.size() );
    float d_squared = 0.0f;
    std::array<T, N> means_a;
    std::array<T, N> means_b;
    for (int dim=0; dim<N; dim++){
        for (int pointIndex=0; pointIndex<point_a.size();pointIndex++){
            means_a[dim]+= point_a[pointIndex][dim];
        }
        means_a[dim]/=point_a.size();
    }
    for (int dim=0; dim<N; dim++){
        for (int pointIndex=0; pointIndex<point_b.size();pointIndex++){
            means_b[dim]+= point_b[pointIndex][dim];
        }
        means_b[dim]/=point_b.size();
    }
    d_squared =distance(means_a, means_b);
    
    return d_squared;
}

/*
Calculate the smallest distance between each of the data points and any of the input means.
*/
template <typename T, size_t N>
std::vector<T> closest_distance(
	const std::vector<std::array<T, N>>& means, const std::vector<std::array<T, N>>& data, uint32_t k) {
	std::vector<T> distances;
	distances.reserve(k);
	for (auto& d : data) {
		T closest = distance_squared(d, means[0]);
		for (auto& m : means) {
			T distance = distance_squared(d, m);
			if (distance < closest)
				closest = distance;
		}
		distances.push_back(closest);
	}
	return distances;
}

/*
This is an alternate initialization method based on the [kmeans++](https://en.wikipedia.org/wiki/K-means%2B%2B)
initialization algorithm.

A default seed value can help to make things reproducible. This argument was added to fix Rhythmiq's save-load system.
More info [here](https://github.com/accusonus/rhythmiq/issues/844)
*/
template <typename T, size_t N>
    std::vector<std::array<T, N>> random_plusplus(const std::vector<std::array<T, N>>& data, uint32_t k, int defaultSeed = -1) {
	assert(k > 0);
	using input_size_t = typename std::array<T, N>::size_type;
	std::vector<std::array<T, N>> means;
	// Using a very simple PRBS generator, parameters selected according to
	// https://en.wikipedia.org/wiki/Linear_congruential_generator#Parameters_in_common_use
    auto seed = defaultSeed;
    if (defaultSeed == -1)
    {
        std::random_device rand_device;
        seed = rand_device();
    }
    std::linear_congruential_engine<uint64_t, 6364136223846793005, 1442695040888963407, UINT64_MAX> rand_engine (seed);

	// Select first mean at random from the set
	{
		std::uniform_int_distribution<input_size_t> uniform_generator(0, data.size() - 1);
		means.push_back(data[uniform_generator(rand_engine)]);
	}

	for (uint32_t count = 1; count < k; ++count) {
		// Calculate the distance to the closest mean for each data point
		auto distances = details::closest_distance(means, data, k);
		// Pick a random point weighted by the distance from existing means
		// TODO: This might convert floating point weights to ints, distorting the distribution for small weights
#if !defined(_MSC_VER) || _MSC_VER >= 1900
		std::discrete_distribution<input_size_t> generator(distances.begin(), distances.end());
#else  // MSVC++ older than 14.0
		input_size_t i = 0;
		std::discrete_distribution<input_size_t> generator(distances.size(), 0.0, 0.0, [&distances, &i](double) { return distances[i++]; });
#endif
        auto index = generator(rand_engine);
        if (index == generator.probabilities().size())
        {
            index = 0;
        }
        means.push_back(data[index]);
	}
	return means;
}

/*
Calculate the index of the mean a particular data point is closest to (euclidean distance)
*/
template <typename T, size_t N>
uint32_t closest_mean(const std::array<T, N>& point, const std::vector<std::array<T, N>>& means) {
	assert(!means.empty());
	T smallest_distance = distance_squared(point, means[0]);
	typename std::array<T, N>::size_type index = 0;
	T distance;
	for (size_t i = 1; i < means.size(); ++i) {
		distance = distance_squared(point, means[i]);
		if (distance < smallest_distance) {
			smallest_distance = distance;
			index = i;
		}
	}
	return index;
}

/*
Calculate the index of the mean each data point is closest to (euclidean distance).
*/
template <typename T, size_t N>
std::vector<uint32_t> calculate_clusters(
	const std::vector<std::array<T, N>>& data, const std::vector<std::array<T, N>>& means) {
	std::vector<uint32_t> clusters;
	for (auto& point : data) {
		clusters.push_back(closest_mean(point, means));
	}
	return clusters;
}

/*
Calculate means based on data points and their cluster assignments.
*/
template <typename T, size_t N>
std::vector<std::array<T, N>> calculate_means(const std::vector<std::array<T, N>>& data,
	const std::vector<uint32_t>& clusters,
	const std::vector<std::array<T, N>>& old_means,
	uint32_t k) {
	std::vector<std::array<T, N>> means(k);
	std::vector<T> count(k, T());
	for (size_t i = 0; i < std::min(clusters.size(), data.size()); ++i) {
		auto& mean = means[clusters[i]];
		count[clusters[i]] += 1;
		for (size_t j = 0; j < std::min(data[i].size(), mean.size()); ++j) {
			mean[j] += data[i][j];
		}
	}
	for (size_t i = 0; i < k; ++i) {
		if (count[i] == 0) {
			means[i] = old_means[i];
		} else {
			for (size_t j = 0; j < means[i].size(); ++j) {
				means[i][j] /= count[i];
			}
		}
	}
	return means;
}

} // namespace details


/*
Implementation of k-means generic across the data type and the dimension of each data item. Expects
the data to be a vector of fixed-size arrays. Generic parameters are the type of the base data (T)
and the dimensionality of each data point (N). All points must have the same dimensionality.

e.g. points of the form (X, Y, Z) would be N = 3.

Returns a std::tuple containing:
  0: A vector holding the means for each cluster from 0 to k-1.
  1: A vector containing the cluster number (0 to k-1) for each corresponding element of the input
	 data vector.

Implementation details:
This implementation of k-means uses [Lloyd's Algorithm](https://en.wikipedia.org/wiki/Lloyd%27s_algorithm)
with the [kmeans++](https://en.wikipedia.org/wiki/K-means%2B%2B)
used for initializing the means.

@param seed     the default engine seed number for the initialization of kmeans++.
                By default (seed = -1) the kmeans++ algorithm chooses the cluster center at random.
*/
template <typename T, size_t N>
std::tuple<std::vector<std::array<T, N>>, std::vector<uint32_t>> kmeans_lloyd(
	const std::vector<std::array<T, N>>& data, uint32_t k, int maxIter, int seed=-1, float epsilon=0.0f) {
	static_assert(std::is_arithmetic<T>::value && std::is_signed<T>::value,
		"kmeans_lloyd requires the template parameter T to be a signed arithmetic type (e.g. float, double, int)");
	assert(k > 0); // k must be greater than zero
    assert(maxIter > 0); //Maximum kmeans iterations must be greater than zero
	assert(data.size() >= k); // there must be at least k data points
	std::vector<std::array<T, N>> means = details::random_plusplus(data, k, seed);

	std::vector<std::array<T, N>> old_means;
	std::vector<uint32_t> clusters;
	// Calculate new means until convergence is reached
	int count = 0;
	do {
		clusters = details::calculate_clusters(data, means);
		old_means = means;
		means = details::calculate_means(data, clusters, old_means, k);
		++count;
	} while (details::point_collection_epsilon(means, old_means) > epsilon && count < maxIter);

	return std::tuple<std::vector<std::array<T, N>>, std::vector<uint32_t>>(means, clusters);
}
    

} // namespace dkm

#endif /* DKM_KMEANS_H */
