/*
 *  cpp_debug.h
 *  Latency debug tools for: UDP Video Transfer Protocol with low latency and no ACK
 *
 *  Created by Zhongzhi Yu on July 18 2016.
 *
 * Copyright (c) 2016 Zhongzhi Yu.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, this list of
 *   conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice, this list of
 *   conditions and the following disclaimer in the documentation and/or other materials
 *   provided with the distribution.
 * - Neither the name of the Author nor the names of its contributors may be used to
 *   endorse or promote products derived from this software without specific prior written
 *   permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef ZYU_CPP_DEBUG_H
#define ZYU_CPP_DEBUG_H
#include <tuple>
#include <iostream>

#ifdef WIN32
#include <Windows.h>
#include <stdint.h> // portable: uint64_t   MSVC: __int64 
namespace cpp_debug_priv
{
	// unit: us (miCRO seconds)
	inline uint64_t getTime()
	{
		return GetTickCount64() * 1000;
	}
};

#else 

#include <sys/time.h>
namespace cpp_debug_priv
{

	inline uint64_t getTime()
	{
		struct timeval now;
		gettimeofday(&now, NULL);

		return now.tv_sec * 1000000 + now.tv_usec;
	}
};

#endif

#include <string.h>

template<typename T>
class AveragePrinter
{
public:
    AveragePrinter(const T& init, const char* TAG)
        : init(init), total(init), size(0), TAG(TAG)
    {}

    T total;
    const T init;

    uint64_t size;
    const char* TAG;

    void print(const T& val)
    {
        using namespace std;
        // Tuple of: <#lowDelay, #highDelay, #HUGEDelay, SUM(delay)>. (!!! mutual exclusive) So that, Total count = sum of (low, high, and HUGE counts)
        // each TAG will have its own avgCache according to c++ template standard.
        
        total += val;
        size ++;
        
        if(size == 100)
        {
            cout << "AVERAGE: [" << TAG <<"] " << (double) total/size << endl;
            size = 0;
            total = init;
        }
    }
};

class TimerPrinter
{
private:
    uint64_t tv;

    bool initted;
    void init()
    {
        initted = true;
		tv = cpp_debug_priv::getTime();
    }
public:

    const bool active;

    TimerPrinter(bool activated)
        : active(activated)
    {
        initted = false;
    }

    // Any activity of duration > minHighDelay(ms) is considered high.
    // Any activity of duration > minHugeDelay(ms) is considered HUGE.
    // And print() only outputs the frequency of high&HUGE activities.
    const static uint64_t minHighDelay = 3 * 1000;
    const static uint64_t minHugeDelay = 8 * 1000;

    template<class TAG>
    void print()
    {
        if(!active)
            return;

        using namespace std;
        // The following values are: <#lowDelay, #highDelay, #HUGEDelay, SUM(delay)>
        // each TAG will have its own values according to c++ template standard.
        static uint32_t low, high, huge;
        static uint64_t delay;

        if(!initted)
        {
            init();
            low = high = huge = 0;
            delay = 0;
            return;
        }

        uint64_t now = cpp_debug_priv::getTime();
		uint64_t diff = now - tv;

        // Update tv (must start new Round to calc freq.)
        memcpy(&tv, &now, sizeof(now));
        uint32_t dx, dy, dz;
        if(diff < minHighDelay)
        {
            dy = dz = 0;
            dx = 1;
        }
        else if(diff < minHugeDelay)
        {
            dx = dz = 0;
            dy = 1;
        }
        else
        {
            dx = dy = 0;
            dz = 1;
        }
        auto tot = low + high + huge + 1;
        if(tot >= 100)                  // Output frequency: once per 100 prints
        {
            cout<<"[" << TAG::name << "] P(low ) = "<< (double)(low +dx) / tot << endl;
            cout<<"[" << TAG::name << "] P(high) = "<< (double)(high +dy) / tot << endl;
            cout<<"[" << TAG::name << "] P(huge) = "<< (double)(huge +dz) / tot << endl;
            cout<<"[" << TAG::name << "] Average = "<< (double)(delay +diff) / tot << endl;
            low = high = huge = 0; delay = 0;
        }
        // dx dy dz have already been added to low high and huge.
        else
        {
            low += dx;
            high += dy;
            huge += dz;
            delay += diff;
        }
    }
};

class NullPrinter
{
public:
    NullPrinter() {};
    template <class T>
    void print() {};
};

class LoopDurationPrinter
{
private:
    uint64_t tv;

    bool initted;
    void init()
    {
        initted = true;
		tv = cpp_debug_priv::getTime();
    }
public:

    LoopDurationPrinter(const char* name)
        :name (name)
    {
    }
    
    const char* name;
    
    uint64_t count = 0;
    uint64_t sumDiff = 0;

    void print()
    {
        using namespace std;
        if(!initted)
        {
            init();
            return;
        }

        uint64_t now = cpp_debug_priv::getTime();
		uint64_t diff = now - tv;

        // Update tv (must start new Round to calc freq.)
        memcpy(&tv, &now, sizeof(now));
        
        count ++;
        sumDiff += diff;
        if(count == 100) {
            cout << "[" << name << "] duration (us) = " << (double)sumDiff/count << endl;
            count = 0;
            sumDiff = 0;
        }
    }
};

#endif
