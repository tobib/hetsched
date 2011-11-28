#include "debug.h"
#include <sstream>
#include <stdlib.h>
#include <sys/time.h>
#include <iostream>
#include <iomanip>

#include <unistd.h>
#include <sys/syscall.h> 

int Debug::verbos_level = DEBUG_LEVEL;

static Debug* debugInstance = 0;

static DebugStream* debug_crit = 0;
static DebugStream* debug_error = 0;
static DebugStream* debug_warn = 0;
static DebugStream* debug_info = 0;
static DebugStream* debug_fine = 0;
static DebugStream* debug_finer = 0;
static DebugStream* debug_finest = 0;


DebugStream::DebugStream(std::ostream &os, int level)
    : output_stream(os)
{
	debug_level = level;	
	//verbosity = DEBUG_LEVEL;
	doDisplay = (level <= verbosity);
}

DebugStream& DebugStream::operator<<(const Tendl &)
{
	pthread_mutex_lock(&debug_mutex);
	if (this->doDisplay){ output_stream << std::endl; output_stream.flush(); }
	pthread_mutex_unlock(&debug_mutex);
	return *this;
}

void DebugStream::setVerbosity (int verbos)
{
	this->doDisplay = (this->debug_level <= verbos);
}

/*
bool DebugStream::display(int level)
{
	//verbosity = Crawler::verbosity;
    return this->doDisplay;
    //return(true);
}
*/
Debug::Debug(void)
{
	pthread_mutex_init(&debug_mutex, NULL);
	verbosity = DEBUG_LEVEL;
}

Debug::~Debug(void)
{
	if (debug_crit != 0) delete(debug_crit);
	if (debug_error != 0) delete(debug_error);
	if (debug_warn != 0) delete(debug_warn);
	if (debug_info != 0) delete(debug_info);
	if (debug_fine != 0) delete(debug_fine);
	if (debug_finer != 0) delete(debug_finer);
	if (debug_finest != 0) delete(debug_finest);

	debug_crit = 0;
	debug_error = 0;
	debug_warn = 0;
	debug_info = 0;
	debug_fine = 0;
	debug_finer = 0;
	debug_finest = 0;
	pthread_mutex_destroy(&debug_mutex);
}

void Debug::dropDebug()
{
   if (debugInstance)
   	delete debugInstance;
	debugInstance = 0;
}
         
Debug* Debug::getDebug()
{
	if (! debugInstance){
		 debugInstance = new Debug();
	}
    return debugInstance;
}

DebugStream Debug::crit()
{
	if (! debug_crit) debug_crit = new DebugStream(std::cerr, 1);
    return *debug_crit;
}
DebugStream Debug::error()
{
	if (! debug_error) debug_error = new DebugStream(std::cerr, 2);
    return *debug_error;
}
DebugStream Debug::warn()
{
	if (! debug_warn) debug_warn = new DebugStream(std::cerr, 3);
    return *debug_warn;
}
DebugStream Debug::info()
{
	if (! debug_info) debug_info = new DebugStream(std::cout, 4);
    return *debug_info;
}
DebugStream Debug::fine()
{
	if (! debug_fine) debug_fine = new DebugStream(std::cout, 5);
    return *debug_fine;
}
DebugStream Debug::finer()
{
	if (! debug_finer) debug_finer = new DebugStream(std::cout, 6);
    return *debug_finer;
}
DebugStream Debug::finest()
{
	if (! debug_finest) debug_finest = new DebugStream(std::cout, 7);
    return *debug_finest;
}

void Debug::verbosityWrapper (int verbos)
{
	verbosity = verbos;
	verbos_level = verbos;
	this->crit().setVerbosity(verbos);
	this->error().setVerbosity(verbos);
	this->warn().setVerbosity(verbos);
	this->info().setVerbosity(verbos);
	this->fine().setVerbosity(verbos);
	this->finer().setVerbosity(verbos);
	this->finest().setVerbosity(verbos);
}

/*
static Debugging::DebugStream debug_crit(std::cerr, 1);
static Debugging::DebugStream debug_error(std::cerr, 2);
static Debugging::DebugStream debug_warn(std::cerr, 3);
static Debugging::DebugStream debug_info(std::cout, 4);
static Debugging::DebugStream debug_fine(std::cout, 5);
static Debugging::DebugStream debug_finer(std::cout, 6);
static Debugging::DebugStream debug_finest(std::cout, 7);
*/

void workerAnnounce(int id, DebugStream stream, std::string msg)
{
	std::stringstream tmp;
	/* locatime as string */
	struct tm * tm;
	struct timeval tv;
	gettimeofday(&tv, 0);
	tm=localtime(&tv.tv_sec);
	//std::string timestamp = sprintf("%.04u-%.02u-%.02u %.02u:%.02u:%.02u.%.07u", (1900 + tm->tm_year), (1 + tm->tm_mon), tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, tv.tv_usec);
	tmp << std::setfill('0') << std::setw(4) << (1900 + tm->tm_year) << "-" << std::setw(2) << (1 + tm->tm_mon) << "-" << std::setw(2) << (tm->tm_mday) << " " << std::setw(2) << (tm->tm_hour) << ":" << std::setw(2) << (tm->tm_min) << ":" << std::setw(2) << (tm->tm_sec) << "." << std::setw(6) << tv.tv_usec;

	tmp << " [" << (id < 10 ? "0" : "") << id << ";" << syscall(SYS_gettid) << "] " << msg << std::endl;
	stream << tmp.str();
}
