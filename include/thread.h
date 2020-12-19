#pragma once

#include <pthread.h>

#include <atomic>
#include <cstdint>
#include <sys/eventfd.h>
#include <unistd.h>
#include <stdlib.h>

struct thread
{
    thread (thread const &) = delete;
    thread (thread && ) = delete;

    thread (void) = default;
    virtual ~thread (void) { }

    virtual void thread_function (void) = 0;

	bool start (void)
	{
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
		int rez = pthread_create(&_handle, &attr, &thread::threadFunction, this);
		pthread_attr_destroy(&attr);
		return rez == 0;
	}

	bool wait (void)
	{
		return 0 == pthread_join(_handle, 0);
	}
	
    static
	void * threadFunction (void * arg)
	{
		auto self = reinterpret_cast<thread *>(arg);
		self->thread_function();
		return 0;
	}


    pthread_t _handle = 0;
};

struct sema
{
    sema (sema const & ) = delete;
    sema (sema && ) = delete;
    
    int init (uint64_t initval)
    {
        _efd = eventfd(initval, EFD_SEMAPHORE);
        return _efd;
    }

    ~sema () 
    {
        if ( -1 != _efd ) {
            close(_efd);
            _efd = -1;
        }
    }

    void post (uint64_t value)
    {
        ::write(_efd, &value, sizeof(uint64_t));
    }

    uint64_t wait ( ) 
    {
        uint64_t value;
        auto const rc = ::read(_efd, &value, sizeof(uint64_t));
        if ( sizeof(uint64_t) != rc ) {
            value = uint64_t(-1);
        }

        return value;
    }

    int _efd = -1;
};
