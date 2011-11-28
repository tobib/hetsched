#ifndef DAEMON_H_INCLUDED
#define DAEMON_H_INCLUDED

//stl
#include <map>
#include <set>

//timeings
#include <stdint.h>

//math
#include <math.h>

#include "../common/uss_config.h"

using namespace std;


/*
 * less than helper for timeval => enables STL usage
 */
struct timevalLessThan : public std::binary_function<struct timeval, struct val_collection, bool>
{
	bool operator() (const struct timeval& t1, const struct timeval& t2)
	{
		return (t1.tv_sec < t2.tv_sec || (t1.tv_sec == t2.tv_sec && t1.tv_usec < t2.tv_usec));
	}
};

/*
 *this is a class encapsularing a time value in ns
 */
class uss_nanotime
{
	public:
	uint64_t time; //nanoseconds
	
	uss_nanotime()
	{
		this->time = (uint64_t)0;
	}
	
	uss_nanotime(uint64_t ct)
	{
		this->time = ct;
	}
	
	uss_nanotime(const uss_nanotime& copyfrom)
	{
		this->time = copyfrom.time;
	}
	
	bool operator< (const struct uss_nanotime& n) const
	{
		return (this->time < n.time);
	}
	
	bool operator== (const struct uss_nanotime& n) const
	{
		return (this->time == n.time);
	}
};


/*
 * this system information can be get
 * by asking an object of class uss_elevator
 * to retrieve it
 */
struct uss_system_load
{
	int number_running_p;
	int number_blocked_p;
	double load_averages;
	struct timeval timestamp;
};

/*
 * this is essentially a cyclic list that contains elements 
 * of uss_system_load and allows to track utilization
 * 
 * circle_length:
 * how many uss_system_load elements are present
 */
class uss_system_load_history
{
	private:
	int circle_length;
	struct uss_system_load load;
	struct uss_system_load_history *next;
	
	public:
	//gives a list of all available history
	int getSystemLoad();
	//gives the most current uss_system_load
	int getLastSystemLoad();
};


#endif
