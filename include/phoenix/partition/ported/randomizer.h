#pragma once

#include <mutex>
#include <random>
#include <boost/random/linear_congruential.hpp>
#include <boost/random/uniform_real.hpp>
#include <boost/random/variate_generator.hpp>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <memory>
#include <string>

#if defined(__GNUC__)
#   if defined(__WIN32)
#       include "c++/bits/random.h"
#   else
#       include <random>
#   endif // defined
#endif // defined

/*inline int make_seed(int value)
{
	return value;
	unsigned int x = (unsigned int)value;
	x = ((x >> 16) ^ x) * 0x119de1f3;
	x = ((x >> 16) ^ x) * 0x119de1f3;
	x = (x >> 16) ^ x;
	return (int)x;
}*/


inline int make_seed(const std::string& s)
{
	int h = 5381;

	for (int i = 0; i < s.size(); i++) 
	{
		h = ((h << 5) + h) + s.at(i);
	}

	return h;
}

struct randomizer;
typedef std::shared_ptr<randomizer> randomizer_ref;

struct randomizer
{
	virtual int seed() const = 0;

	virtual void seed(int seed_) = 0;

	virtual double random() = 0;

	virtual randomizer_ref clone() = 0;

	int random_int()
	{
		return (int)trunc((1-2*random())*1582601793);
	}

	int random(int min, int max)
	{
		if (min > max)
			std::swap(min, max);
		int result = (int)trunc(min + (max - min + 1)*random());
		return result > max ? max : result;
		//return (int)round(min + (max - min)*random());
	}

	int random(int min, int max, int step)
	{
		if (step <= 0)
			return random(min, max);
		int count = (max - min) / step;
		int result = min + random(0, count)*step;
		return std::min(result, max);
	}

	double random(double min, double max)
	{
		return min + (max - min)*random();
	}

	double random(double min, double max, double step)
	{
		if (step <= 0)
			return random(min, max);
		double count = trunc((max - min) / step);
		double result = min + random(0, (int)count)*step;
		return std::min(result, max);
	}

	template<typename T>
	void shuffle(T& t)
	{
		auto engine = std::default_random_engine((long)(random() * 5489U));
		std::shuffle(t.begin(), t.end(), engine);
	}

};

struct badbadrandomizer : randomizer
{
	badbadrandomizer(int seed) :
		_seed(seed),
		generator(42),
		uni_dist(0, 1),
		uni(generator, uni_dist)
	{
		if (_seed == -1)
			_seed = 456732 * (int)std::time(0);

		generator.seed(static_cast<unsigned int>(_seed));
	}

	virtual randomizer_ref clone()
	{
		return randomizer_ref(new badbadrandomizer(_seed));
	}

	virtual int seed() const
	{
		return _seed;
	}

	virtual void seed(int seed_)
	{
		_seed = seed_;
		generator.seed(static_cast<unsigned int>(_seed));
	}

	virtual double random()
	{
		std::lock_guard<std::mutex> guard(_mutex);
		return uni();
	}

private:
	typedef boost::minstd_rand base_generator_type;
	base_generator_type generator;
	boost::uniform_real<> uni_dist;
	boost::variate_generator<base_generator_type&, boost::uniform_real<> > uni;
	int _seed;
	std::mutex _mutex;
};

struct randomizer_factory
{
	randomizer_factory(int random_seed) :
		_random_seed(random_seed)
	{ }

	randomizer_ref get(int seed) const
	{
		return randomizer_ref(new badbadrandomizer(_random_seed*seed));
	}

	static randomizer_ref get_fixed(int seed)
	{
		return randomizer_ref(new badbadrandomizer(seed));
	}

private:
	int _random_seed;
};

typedef std::shared_ptr<randomizer_factory> randomizer_factory_ref;
