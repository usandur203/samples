#ifndef _VIDEO_PROTO_CLIENT_H
#define _VIDEO_PROTO_CLIENT_H

#include "common.h"
#include "spinlock.h"
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <deque>
#include <memory>
#include <string.h>
#include <iostream>

// This file is shared between only clients, not hosts.

#define NET_BUF_LEN 1024000
#define tailer_size (sizeof(uint64_t) + 3*sizeof(int8_t) + sizeof(uint64_t))

// LIMIT. CONFIG. BOOKMARK.
const size_t MAX_NUM_FRAMES_IN_BUFFER = 4;

// data fetched so far.
class seekable
{
    seekable(const seekable& other) = delete;
    seekable(seekable&& other) = delete;
public:
    std::vector<uint8_t> content;
    const uint8_t* data() const
    {
        return content.data();
    }
    size_t len() const
    {
        return content.size();
    }

    size_t pos;
    const bool isIDR;
    seekable(bool isIDR)
        : isIDR(isIDR)
        , pos(0)
    {
    }

    void copy_push_back(uint8_t* buf, size_t size)
    {
        content.insert(content.end(), buf, buf + size);
    }

};

typedef std::deque<std::shared_ptr<seekable> > ProtoOutput;

#include "cpp_debug.h"

inline static std::shared_ptr<seekable> extract_one_seekable(pthread_spinlock_t* lock, ProtoOutput* frames, bool printDebugInfo)
{
    std::shared_ptr<seekable> ans = nullptr;

    pthread_spin_lock(lock);

    // wait for frames.
    while(frames->size() == 0) {
        pthread_spin_unlock(lock);
        usleep(1 * 1000);
        pthread_spin_lock(lock);
    }

    // get the pointer so we can unlock as soon as possible
    ans = frames->front();
    // we can actually remove this pointer from deque now.
    frames->pop_front();

    pthread_spin_unlock(lock);
    return ans;
}

inline static int ffvadec_read_seekable(pthread_spinlock_t* lock, ProtoOutput* frames, uint8_t* buf, int buf_size, bool printDebugInfo)
{
    // The frame that is currently TO BE FINISHED by ffmpeg thread
    static std::shared_ptr<seekable> curr_frame = nullptr;

    if(curr_frame == nullptr) {
        curr_frame = extract_one_seekable(lock, frames, printDebugInfo);
    }

    // num bytes to copy
    size_t ret = std::min((size_t)buf_size, curr_frame->len() - curr_frame->pos);

    // TODO: probably should check return value of memcpy
    memcpy(buf, curr_frame->data() + curr_frame->pos, ret);

    curr_frame->pos += ret;

    // remove the frame if necessary
    if(curr_frame->pos >= curr_frame->len())
        curr_frame = nullptr;

    if(printDebugInfo)
    {
        pthread_spin_lock(lock);
        static AveragePrinter<size_t> printer(0, "Seekable Buffer count, NEW");
        printer.print(frames->size());
        pthread_spin_unlock(lock);
    }

    return ret;
}

// Use this template to enable or hide time/delay printer
extern void udp_looper(const char* ip, uint16_t port, bool enableDelayMeasurements, ProtoOutput* outputs, pthread_spinlock_t* outputs_lock);

#endif//_VIDEO_PROTO_CLIENT_H
