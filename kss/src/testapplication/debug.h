#ifndef _DEBUGSTREAM_H
#define _DEBUGSTREAM_H

#include <iostream>
#include <pthread.h>

//#define NDEBUG
#define DEBUG_LEVEL 4
/*	Debug Levels
 *	0 - NONE
 *	1 - CRITICAL
 *	2 - ERROR
 *	3 - WARN
 *	4 - INFO
 *	5 - FINE
 *	6 - FINER
 *	7 - FINEST
 */

#define DBG_CRIT Debug::getDebug()->crit()
#define DBG_ERROR Debug::getDebug()->error()
#define DBG_WARN Debug::getDebug()->warn()
#define DBG_INFO Debug::getDebug()->info()
#define DBG_FINE Debug::getDebug()->fine()
#define DBG_FINER Debug::getDebug()->finer()
#define DBG_FINEST Debug::getDebug()->finest()

static int					verbosity = 4;
static pthread_mutex_t	debug_mutex;

    struct Tendl
    {
    };    

    static const Tendl dendl = Tendl();

class DebugStream
{
	private:
		std::ostream &output_stream;
		int debug_level;
		bool doDisplay;
		//static int verbosity;
        
		//bool display(int level);
        
	public:
		DebugStream(std::ostream &os, int level);
        
		DebugStream& operator<<(const Tendl &);

		template <class T> DebugStream& operator<<(const T &t)
		{
			pthread_mutex_lock(&debug_mutex);
			if (this->doDisplay){ output_stream << t; }
			pthread_mutex_unlock(&debug_mutex);
			return *this;
		}
		
		void setVerbosity (int verbos);
};
    
class Debug
{
      public:
          static int verbos_level;
/*
          static DebugStream crit();
          static DebugStream error();
          static DebugStream warn();
          static DebugStream info();
          static DebugStream fine();
          static DebugStream finer();
          static DebugStream finest();
*/
          DebugStream crit();
          DebugStream error();
          DebugStream warn();
          DebugStream info();
          DebugStream fine();
          DebugStream finer();
          DebugStream finest();
          static void dropDebug();
          static Debug* getDebug();
		  void verbosityWrapper (int verbos);
      private:
          Debug(void);
          ~Debug(void);
};

extern void workerAnnounce(int id, DebugStream stream, std::string msg);

#endif
