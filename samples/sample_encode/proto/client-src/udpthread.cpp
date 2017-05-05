// vim: set expandtab ts=4 sw=4: 
/*
 *  udpthread.cpp
 *  UDP Video Transfer, Unix Client with low latency and no ACK
 *
 *  Created by Zhongzhi Yu on July 1 2016.
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
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <utility>

#ifdef __APPLE__

#include <libkern/OSByteOrder.h>

#define htobe16(x) OSSwapHostToBigInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)

#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)

#define htobe64(x) OSSwapHostToBigInt64(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)

#endif

enum {
    STATE_INITIALIZED   = 1 << 0,
    STATE_OPENED        = 1 << 1,
    STATE_STARTED       = 1 << 2,
};


#include <list>
#include <deque>
#include <memory>
#include <iostream>
#include <vector>
#include <string>
#include <tuple>
#include <map>

using namespace std;

void network_err()
{
    fprintf(stderr, "READ network failure\n");
    exit(-1);
}

// The max number of packets to keep, when they were out of order or incomplete.
// This number does NOT include the most recent, current packet to be received.
const size_t MAX_OUTOFORDER_LEN = 3;

#include <assert.h>
#include "../inc/cpp_debug.h"
#include "../inc/client.h"

// This class is currently not safe for Multi-threading
// Also: it only manages Video Packets
class PacketSort
{
private:
    class ElemInfo;
    class BufferElem
    {
    public:
        shared_ptr<uint8_t> buf;
        size_t size;
        bool isEnd;
        bool isLost;
        bool isIDR;
        uint64_t id;
        list<ElemInfo>::iterator toInfo;

        BufferElem(shared_ptr<uint8_t> buf, size_t size, bool isEnd, bool isLost, uint64_t id, list<ElemInfo>::iterator toInfo, bool isIDR)
            : buf(buf), size(size), isEnd(isEnd), isLost(isLost), id(id), toInfo(toInfo), isIDR(isIDR) {}
    };

    typedef list<BufferElem> Buffer;

    class ElemInfo
    {
    public:
        Buffer::iterator headOfElem;
        uint64_t fullFrameSize;
        size_t numLostPackets;
        bool hasReceivedEnd;

        ElemInfo(Buffer::iterator head, size_t numLost, uint64_t fullFrameSize)
            : headOfElem(head), numLostPackets(numLost), hasReceivedEnd(false), fullFrameSize(fullFrameSize) {}
    };

    Buffer buffer;
    list<ElemInfo> extraInfo;

    // TODO: in the future, actually store the outputs in here.
    // These two pointers are Created and Managed (Deleted) by third party. We did not use shared_ptr, because that party is a C program, not C++.
    deque<shared_ptr<seekable> >* outputs;
    pthread_spinlock_t* output_lock;

    uint64_t headId()
    {
        if(buffer.size() == 0)
            return -1;
        else return buffer.front().id;
    }

    // The id of the next packet to be pushed in Buffer.
    uint64_t nextId = -1;

    uint64_t lastClearedId = 0;

    // Call this only on the same thread as UDP thread.
    // However you still needs to acquire the lock, in some parts.
    void checkAndSyncOutput()
    {
        while(extraInfo.size() > 0 && extraInfo.front().hasReceivedEnd && extraInfo.begin()->numLostPackets == 0)
        {
            //TODO: update "outputs"

            // calculate the [begin, end) range where BufferElems will be outputed and cleared from "buffer".
            auto begin = buffer.begin();
            auto end = extraInfo.size() == 1 ? buffer.end() : std::next(extraInfo.begin())->headOfElem;

            // Glue the pieces together and verify the size.
            static AveragePrinter<int> avgPrint(0, "Sorter seekable size check (expect - real)");

            const auto expected_size = extraInfo.front().fullFrameSize;

            shared_ptr<seekable> ans(new seekable(buffer.begin()->isIDR));
            for(auto i=begin; i!=end; i++)
            {
                ans->copy_push_back(i->buf.get(), i->size);

                auto inext = std::next(i);
                if(inext != end)
                {
                    cout << i->id << " -- "  << inext->id << endl;
                    assert(i->id +1 == inext->id);
                }
            }
            
            avgPrint.print((int64_t)expected_size - ans->len());

            if(outputs == nullptr)
            {
                // Fake THREADING Delay:
                // NOTE: chaning this number does not help with NETWORK packet loss.
                //       It should represent the delay of actual calculations on this thread.
                usleep(1 * 1000);
            }
            else
            {
                // LIMIT. BOOKMARK.
                // if(outputs->size() < MAX_NUM_FRAMES_IN_BUFFER)
                {
                    static AveragePrinter<int> avgPrint(0, "Sorter seekable size check (expect - real)");

                    const auto expected_size = extraInfo.front().fullFrameSize;

                    shared_ptr<seekable> ans(new seekable(buffer.begin()->isIDR));
                    for(auto i=begin; i!=end; i++)
                    {
                        ans->copy_push_back(i->buf.get(), i->size);

                        auto inext = std::next(i);
                        if(inext != end)
                        {
                            // cout << i->id << " -- "  << inext->id << endl;
                            assert(i->id +1 == inext->id);
                        }
                    }
                    
                    avgPrint.print((int64_t)expected_size - ans->len());

                    pthread_spin_lock(output_lock);
                    outputs->push_back(ans);
                    pthread_spin_unlock(output_lock);
                }
            }

            // record the lastClearedId
            lastClearedId = std::prev(end)->id;

            // clear this buffer.
            extraInfo.pop_front();
            buffer.erase(begin, end);
        }
    }
public:

    // use nullptr to disable real outputs.
    PacketSort(deque<shared_ptr<seekable> >* outputs, pthread_spinlock_t* output_lock)
        : outputs(outputs), output_lock(output_lock)
    {
    }

    void printMemInfo() const
    {
        static AveragePrinter<size_t> printer(0, "Sorter Buffer PACKT count");
        static AveragePrinter<size_t> printer2(0, "Sorter Buffer FRAME count");
        printer.print(buffer.size());
        printer2.print(extraInfo.size());
    }

    void printBuf() const
    {
        for(auto i = buffer.begin(); i!=buffer.end(); i++)
            cout << i->id << endl;
    }

    void addPiece(shared_ptr<uint8_t> buf, size_t size, uint64_t id, bool isEnd, bool isIDR, uint64_t fullFrameSize)
    {
        if(id < lastClearedId)
            return;

        if(nextId == -1)
        {
            buffer.push_back(BufferElem(buf, size, isEnd, false, id, extraInfo.end(), isIDR));
            extraInfo.push_back(ElemInfo(std::prev(buffer.end()), 0, fullFrameSize));

            buffer.back().toInfo = std::prev(extraInfo.end());
            extraInfo.back().hasReceivedEnd = isEnd;

            nextId = id + 1;
        }
        else
        {
            if(id < nextId)
            {
                if(headId() == -1 || id < headId())
                    return;             // This packet is of no use.
                auto itr = id == headId() ? buffer.begin() : std::next(buffer.begin(), (id - headId()));
                auto actualId = itr->id;
                
                if (actualId != id)
                    printBuf();
                assert(actualId == id);

                if(!itr->isLost)
                    return;             // This packet is of no use.

                // cerr << "WARNING: special case on id: " << id << endl;

                itr->isLost = false;
                itr->isIDR = isIDR;
                if(isEnd && (!itr->isEnd))               // Special condition.
                {
                    // TODO: proof check this code.
                    auto end = buffer.end();
                    auto nextInfo = std::next(itr->toInfo);
                    if(nextInfo != extraInfo.end())
                        end = nextInfo->headOfElem;

                    // Walk through the packets after End.
                    uint64_t numLost = 0;
                    auto newInfo = extraInfo.insert(std::next(itr->toInfo), ElemInfo(std::next(itr), 0, -1));
                    ElemInfo& info = *newInfo;
                    for(auto j = std::next(itr); j != end; j++)
                    {
                        if(j->isLost)
                            numLost ++;
                        j->toInfo = newInfo;
                    }

                    info.numLostPackets = numLost;
                    info.hasReceivedEnd = itr->toInfo->hasReceivedEnd;

                    // (See the two commented-out "assert"s below)
                    newInfo->fullFrameSize = itr->toInfo->fullFrameSize;

                    itr->toInfo->hasReceivedEnd = true;
                }
                itr->isEnd = isEnd;
                itr->buf = buf;
                itr->size = size;
                itr->toInfo->numLostPackets--;
                if(itr->toInfo->fullFrameSize == -1)
                    itr->toInfo->fullFrameSize = fullFrameSize;
                else // assert(itr->toInfo->fullFrameSize == fullFrameSize);
                {
                    // here we will need to reset fullFrameSize of older frames
                    // (see similar problem below)
                    itr->toInfo->fullFrameSize = fullFrameSize;
                }
            }
            else // id >= nextId
            {
                uint64_t numLost = id - nextId;
                bool isHeadPacket = buffer.empty() || buffer.back().isEnd;

                if(isHeadPacket)
                    extraInfo.push_back(ElemInfo(std::prev(buffer.end()), numLost, fullFrameSize));
                else 
                {
                    extraInfo.back().numLostPackets += numLost;
                    if(extraInfo.back().fullFrameSize == -1)
                        extraInfo.back().fullFrameSize = fullFrameSize;
                    else // assert(extraInfo.back().fullFrameSize == fullFrameSize);
                    {
                        // here we will need to reset fullFrameSize of "newer" frames
                        // (see similar problem below)
                        extraInfo.back().fullFrameSize = fullFrameSize;
                    }
                }

                for(uint64_t i = 0; i < numLost; i++)
                    buffer.push_back(BufferElem(nullptr, 0, false, true, (nextId+ i), std::prev(extraInfo.end()), isIDR));

                buffer.push_back(BufferElem(buf, size, isEnd, false, id, std::prev(extraInfo.end()), isIDR));
                extraInfo.back().hasReceivedEnd = isEnd;

                if(isHeadPacket)
                {
                    // set current value of headOfElem for extraInfo
                    extraInfo.back().headOfElem ++;
                    assert(extraInfo.back().headOfElem != buffer.end());

                    if(extraInfo.size() > MAX_OUTOFORDER_LEN + 1)
                    {
                        // HERE it's possible to exceed MAX_OUTOFORDER_LEN
                        // it's also a possible way for packet loss to happen
                        cerr << "FRAME LOSS !!!!!!!!!!!!!!!";
                        extraInfo.pop_front();
#if 1
                        for(auto j=buffer.begin(); j!=extraInfo.front().headOfElem; j++)
                        {
                            if(j->isLost)
                                cerr <<" X";
                            else cerr << " " << j->id ;
                        }
#endif
                        cerr << endl;
                        buffer.erase(buffer.begin(), extraInfo.front().headOfElem);
                    }
                }
            
                nextId = id + 1;
            }
        }
        checkAndSyncOutput();
    }

};

namespace udp_looper_private
{
    class A { public: static const char* name;}; const char* A::name = "FINISH";
    class B { public: static const char* name;}; const char* B::name = "RECV UDP";
    class C { public: static const char* name;}; const char* C::name = "BASELINE";
    class D { public: static const char* name;}; const char* D::name = "Post-process";
    class E { public: static const char* name;}; const char* E::name = "BEFORE_IF_ADD_FRAME";
    class F { public: static const char* name;}; const char* F::name = "AFTER_ADD_FRAME";
    class G { public: static const char* name;}; const char* G::name = "CALL INTO lambda";
};

// it's promised that Video Frame 0 is always the first seekable in outputs.
void udp_looper(const char* ip, uint16_t port, bool enableDelayMeasurements, deque<shared_ptr<seekable> >* outputs, pthread_spinlock_t* outputs_lock)
{
    using namespace udp_looper_private;

    // create UDP socket
    auto ret = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ret == -1)
        network_err();
    int socket_fd;
    struct sockaddr_in si_server;

    socket_fd = ret;
    memset(&si_server, 0, sizeof(si_server));
    si_server.sin_family = AF_INET;
    si_server.sin_port   = htons(port);
    if (inet_aton(ip, &si_server.sin_addr)==0)
        network_err();

    // TODO: replace this magic number (40960) with a variable in a protocol header file
    // NOTE: changing this number does not help with network packet loss.
    size_t rcv_buf_size = 40960;
    ret = setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, (char*)&rcv_buf_size, sizeof(rcv_buf_size));
    if(ret < 0) {
        network_err();
    }

    // trigger streaming
    // here we use "&ret" or any legal pointer as data being sent
    if(sendto(socket_fd, &ret, 1, 0, (struct sockaddr*) &si_server, sizeof(si_server)) == -1)
        network_err();

    // The buffer to receive a RAW packet from UDP side.
    uint8_t *raw_udp_buf = new uint8_t[NET_BUF_LEN];

    TimerPrinter timeTrack(enableDelayMeasurements);

    bool gotFirstFrame = false;

    // Prepare frameZeroSorter
    deque<shared_ptr<seekable> > frame0;
    pthread_spinlock_t frame0_lock;             // Actually there is no need to use this lock, we are using frame0 in only one thread.
    pthread_spin_init(&frame0_lock, PTHREAD_PROCESS_PRIVATE);
    
    // There are multiple sorters.
    // Initially only frameZeroSorter is used, so that we can ensure frame 0 is received.
    // After that, for video frames we use videoFrameSorter.
    // For audio frames we use audioFrameSorter.

    PacketSort videoFrameSorter(outputs, outputs_lock);
    PacketSort frameZeroSorter(&frame0, &frame0_lock);

    while(true) {
        
        timeTrack.template print<D>();
        ret = recv(socket_fd, raw_udp_buf, NET_BUF_LEN, 0);
        timeTrack.template print<B>();
        timeTrack.template print<C>();

        if(ret < 0) {
            fprintf(stderr, "ERROR: %d while recv...\n", ret);
            exit(-1);
        }
        if(ret < tailer_size) {
            fprintf(stderr, "ERROR: unexpected small packet\n");
            exit(-1);
        }

        // The size of actual data.
        const size_t curr_packet_size = ret - tailer_size;

        using namespace VideoProto;

        // parse "tailer" info.
        uint64_t id = be64toh(*((uint64_t*) (           raw_udp_buf + curr_packet_size)));
        bool hasMultiPacks = 0 != *(                    raw_udp_buf + curr_packet_size + sizeof(uint64_t));
        bool isEnd = 0 != *(                            raw_udp_buf + curr_packet_size + sizeof(uint64_t) + sizeof(int8_t));
        PacketType type = (PacketType) *(               raw_udp_buf + curr_packet_size + sizeof(uint64_t) + 2 * sizeof(int8_t)); 
        uint64_t fullFrameSize = be64toh(*((uint64_t*)( raw_udp_buf + curr_packet_size + sizeof(uint64_t) + 3 * sizeof(int8_t))));

        // cout << "[all packet id] " << id << endl;

        if (type == SERVER_DISCARD_CLIENT) {
            fprintf(stderr, "Server timed out. Please reconnect.\n");
            exit(-1);
        }
        
        // Check whether we need to wait for FIRST frame
        if (type == VIDEO_FRAME_ZERO)
        {
            if (gotFirstFrame)
            {
                const uint64_t ack = frame0.front()->len();
                if(sendto(socket_fd, (const char*)&ack, sizeof(uint64_t), 0, (struct sockaddr*) &si_server, sizeof(si_server)) == -1) {
                    fprintf(stderr, "ERROR while sending ACK...\n");
                    exit(-1);
                }
            }
            else
            {
                shared_ptr<uint8_t> buf(new uint8_t[curr_packet_size]);
                memcpy(buf.get(), raw_udp_buf, curr_packet_size);
                frameZeroSorter.addPiece(buf, curr_packet_size, id, isEnd || (!hasMultiPacks), true, fullFrameSize);
                if(frame0.size() > 0)
                {
                    const uint32_t ack = htobe32(frame0.front()->len());
                    if(sendto(socket_fd, (const char*)&ack, sizeof(ack), 0, (struct sockaddr*) &si_server, sizeof(si_server)) == -1) {
                        fprintf(stderr, "ERROR while sending ACK...\n");
                        exit(-1);
                    }

                    if(outputs)
                    {
                        pthread_spin_lock(outputs_lock);
                        // WARNING: TODO: FIXME: maybe we should prevent user of "outputs" from modifying frame0?
                        outputs->push_back(frame0.front());
                        pthread_spin_unlock(outputs_lock);
                    }

                    gotFirstFrame = true;
                    cerr << "Streaming started!" << endl;
                }
            }
            continue;
        }

        if (!gotFirstFrame)
            continue;

        // Handle packets.
        if (type == ASK_ACK)
        {
            // Warning: magic: sizeof(uint32_t), need to build protocol header?
            if(sendto(socket_fd, raw_udp_buf, sizeof(uint32_t), 0, (struct sockaddr*) &si_server, sizeof(si_server)) == -1) {
                fprintf(stderr, "ERROR while sending ACK...\n");
                exit(-1);
            }
            cerr << "\t\t\tACK ACK ACK from server." << endl;
            // break so that prev_id is still keeping track
            continue;
        } else if (type == AUDIO) {
            continue;
        } else if (type == FRAME_NOT_IDR || type == FRAME_IDR) {
            // Till this point, only Video packets are left.
            bool isIDR = type == FRAME_IDR;

            shared_ptr<uint8_t> buf(new uint8_t[curr_packet_size]);
            memcpy(buf.get(), raw_udp_buf, curr_packet_size);

            // cout << "[curr id] " << id << endl;
            videoFrameSorter.addPiece(buf, curr_packet_size, id, isEnd || (!hasMultiPacks), isIDR, fullFrameSize);
            videoFrameSorter.printMemInfo();
        }

    }

    // impossible to reach
    delete[] raw_udp_buf;
    raw_udp_buf = NULL;
    pthread_exit(NULL);
}
