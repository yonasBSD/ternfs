#pragma once

#include <pthread.h>
#include <memory>
#include <signal.h>
#include <list>

#include "SBRMUnix.hpp"

#if 0
             ...
           ;::::;
          ;::::; :;
         ;:::::/   :;
        ;:::::;     ;.
       ,:::::/       ;           OOO\
       ::::::;       ;          OOOOO\
       ;:::::;       ;         OOOOOOOO
      ,;::::::;     ;/         / OOOOOOO
    ;:::::::::`. ,,,;.        /  / DOOOOOO
  ./;:::::::::::::::::;,     /  /     DOOOO
 ,::::::;::::::;;;;::::;,   /  /        DOOO
;`::::::`/::::::;;;::::: ,#/  /          DOOO
:`:::::::`;::::::;;::: ;::#  /            DOOO
::`:::::::`;:::::::: ;::::# /              DOO
`:`:::::::`;:::::: ;::::::#/               DOO
 :::`:::::::`;; ;:::::::::##                OO
 ::::`:::::::`;::::::::;:::#                OO
 `:::::`::::::::::::;/`:;::#                O
 `:::::`::::::::;/ /  / `:#
#endif

/**
 * The undertaker provides signal management, reaps offending AppThreads and tidies up the mess.
 *
 * Essentially two ways of using this:
 * 1. Outside of the App framework: 
 *    just call Undertaker::configureErrorHandlers() at the beginning of main() and be done.
 * 2. Within the App framework: 
 *    instanciate an Undertaker, register Reapable AppThreads and spin them out; then call reap().
 *
 * If a critical/terminate signal is delivered to the undertaker it will:
 * 1. call cleanup() on the offending Reapable
 * 2. destroy all other registered Reapables
 * 3. exit/abort
 */

class Undertaker {
public:
    /**
     * Something (like an AppThread) that can be harvested
     * NOTE: the contract on these methods is that they _MUST_ be thread safe
     */
    class Reapable {
    public:
        virtual ~Reapable();

        // ask for a clean termination
        virtual void terminate() = 0;
        // last chance to run abort handlers before going down hard
        virtual void onAbort() = 0;
    };

    // Two methods of setting up the undertaker - can only call one once per application
    static Undertaker * acquireUndertaker();
    static void configureErrorHandlers();

    // checkin with the undertaker: register a Reapable instance running on this thread. NOTE: cleanup order is in reverse to checkin order
    void checkin(std::unique_ptr<Reapable> r, pthread_t threadId, const std::string& threadName);
    // block until either a thread exits or a signal is received: does not return if a critical signal is received
    void reap(size_t reapAfter = 1);

    // callbacks for signals handlers

    // run abort handlers for all threads
    void onAbort();
    // try to terminate normally at own convenience
    void onExit();

    static bool isTerminating();

    static void handleCriticalSignal(int signum, siginfo_t *info, void *ucontext);

private:
    Undertaker();
    ~Undertaker();

    struct ReapableThread {
        ReapableThread(pthread_t tid, const std::string& name, std::unique_ptr<Reapable> r);
        ReapableThread();

        pthread_t _tid;
        bool _joined = false;
        std::string _name;
        std::unique_ptr<Reapable> _reapable;
    };
    typedef std::list<ReapableThread> Condemned;    

    FDHolder    _evtfd;
    std::mutex  _lock;
    Condemned   _condemned;
    pthread_t   _reaperTid;
};

