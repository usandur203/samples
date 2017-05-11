/*
 *  VideoProto.h
 *  UDP Video Transfer Protocol with low latency and no ACK
 *
 *  Created by Zhongzhi Yu on July 7 2016.
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
#ifndef _OIP_VIDEO_PROTO_H
#define _OIP_VIDEO_PROTO_H

#include <functional>
#include <process.h>
#include <vector>
#include <map>

// Currently we only support Windows
#include <concrt.h>
#include <winsock.h>
#include <iostream>
#include "common.h"

#pragma comment(lib, "Ws2_32.lib")
extern "C" void networkEntry(void* param);

#define assert 
inline void GoodSleep(uint64_t ms)
{
	// IF this line fails, CHANGE your timer (CPU/Motherboard) !
	assert(timeBeginPeriod(1) == TIMERR_NOERROR);
	Sleep(ms);
	assert(timeEndPeriod(1) == TIMERR_NOERROR);
}

inline ULONGLONG getTime()
{
	return GetTickCount64();
}

inline uint64_t htonll(uint64_t value)
{
	int num = 42;
	if (*(char *)&num == 42) {
		uint32_t high_part = htonl((uint32_t)(value >> 32));
		uint32_t low_part = htonl((uint32_t)(value & 0xFFFFFFFFLL));
		return (((uint64_t)low_part) << 32) | high_part;
	}
	else {
		return value;
	}
}

// extern std::atomic<size_t> numClients;

// BOOKMARK NetworkThread

class NetBuffer
{
public:
	typedef VideoProto::PacketType Type;
protected:
	std::vector<uint8_t> buffer;
	typedef uint64_t FrameId;
	size_t _size;
	FrameId id;
	size_t _pos = 0;

	template <typename T>
	void write(const T data) {
		size_t len = sizeof(T);
		std::copy((const uint8_t*)&data, ((const uint8_t*)&data) + len, buffer.begin() + _pos);
		_pos += len;
	}

	void write(const void* data, size_t len) {
		std::copy((const uint8_t*)data, ((const uint8_t*)data) + len, buffer.begin() + _pos);
		_pos += len;
	}

	void setpos(size_t x) {
		_pos = x;
	}

	int8_t btoi8(bool x) {
		if (x)
			return 1;
		else return 0;
	}

	static const size_t tailerSize = sizeof(FrameId) + 3 * sizeof(int8_t) + sizeof(uint64_t);

	const Type _type;

public:

	// [Parent child] [parent child child] [parent]
	//  frame			large frame			small frame
	// Full Frame Size is special:  it's not size of the data in this PACKET,
	// it's the size of the whole FRAME.
	NetBuffer(const void* videoFrameData, size_t videoFrameSize, uint64_t FullFrameSize,
		FrameId id, Type packetType, bool isMultiPacket, bool isEndOfMultiPacket)

		: _size(videoFrameSize + tailerSize)
		, _type(packetType)
		, id(id)
	{
		buffer.resize(_size);

		setpos(0);
		if (videoFrameSize > 0)
			write(videoFrameData, videoFrameSize);
		write(htonll(id));
		write(btoi8(isMultiPacket));
		write(btoi8(isEndOfMultiPacket));
		write((int8_t)packetType);

		write(htonll(FullFrameSize));

		// NO NEED to do a checksum for SINGLE UDP Packet.
		// Check "sum" for this single packet
		// write(htonl((TCheckSum)(videoFrameSize + tailerSizeNoChecksum)));
	}
	const char* getAllData() const
	{
		return (const char*) buffer.data();
	}
	FrameId getId() const
	{
		assert(*(FrameId*)(getAllData() + _size - tailerSize) == htonll(id));
		return id;
	}
	size_t size() const
	{
		return _size;
	}
	Type getType() const
	{
		return _type;
	}
};

// Credits to Stack Overflow user: pix0r (http://stackoverflow.com/questions/3022552/is-there-any-standard-htonl-like-function-for-64-bits-integers-in-c)

// BOOKMARK NetBufferApi. This api is responsible for:
//		1) start the network thread
//		2) manage data sharing between network thread and PollingThread

class NetBufferApi
{
public:
	// --------------------- TYPES -----------------------
	
	typedef NetBuffer Buffer;
	typedef std::vector<Buffer> WholeFrame;
	typedef std::map<uint64_t, std::shared_ptr<WholeFrame>> Bufmap;

protected:

	void* videoFirstFrame = nullptr;
	size_t videoFirstFrameSize;

	std::shared_ptr<uint8_t> currAudioFrame = nullptr;
	size_t currAudioFrameSize;

	Bufmap videoFrames;
	concurrency::reader_writer_lock videoFramesLock;

	// audioPort is not set yet.
	UINT videoPort = 50420 , audioPort = 0;
	uintptr_t netThread;
	uint64_t numFramesReceivedFromEncoder = 0;

	// should we store new coming frames?
	// (currently this depends on whether we have valid client connections)
	bool bufferEnabled;

public:

	NetBufferApi()
	{ 
		this->bufferEnabled = false;

		// initialize all other variables before starting thread
		auto hThread = _beginthread(networkEntry, 0, this);
		assert(hThread != -1L);
		this->netThread = hThread;
	}

	// Set whether buffering is enabled. If not, nothing will be stored in Bufmap, and original contents will be cleared.
	void setBufferEnabled(bool enabled)
	{
		lockFrames();
		this->bufferEnabled = enabled;
		if (!enabled)
			videoFrames.clear();
		unlockFrames();
	}
	
	void* getVideoFirstFrame()
	{
		return videoFirstFrame;
	}

	size_t getVideoFirstFrameSize()
	{
		return videoFirstFrameSize;
	}

	void withFrames(std::function<void (Bufmap &)> callback)
	{
		concurrency::reader_writer_lock::scoped_lock_read lock(videoFramesLock);
		callback(videoFrames);
	}

	void unlockFrames() {
		videoFramesLock.unlock();
	}

	void lockFrames() {
		videoFramesLock.lock();
	}

	UINT getVideoPort()
	{
		return videoPort;
	}

	void clearFrames()
	{
		lockFrames();
		videoFrames.clear();
		unlockFrames();
	}

	typedef uint32_t ACKDataType;
	Buffer newACK(ACKDataType dataForVerify)
	{
		return Buffer(&dataForVerify, sizeof(dataForVerify), sizeof(dataForVerify), -1, Buffer::Type::ASK_ACK, false, true);
	}

	Buffer newBufferWithoutData(Buffer::Type type)
	{
		assert(type == Buffer::Type::SERVER_ACK_CLIENT || type == Buffer::Type::SERVER_DISCARD_CLIENT
			|| type == Buffer::Type::AUDIO_RESTART);
		return Buffer(nullptr, 0, 0, -1, type, false, true);
	}

	// Turn a frame of anything into multiple packets and call the netSendFunc on those packets.
	// The packets will be sent in natural order.
	// These packets' IDs will not be kept track of.
	//		The ids in each frame is consistently increasing but this is not true between frames.
	// (Actually, only video frame ids will be kept track of)
	bool packetizeAndSendNoIDTrack(Buffer::Type type, void* buff, size_t bufferSize, const std::function<bool (Buffer&)>& netSendFunc)
	{
		size_t id = 0;
		bool isMultiPacket = bufferSize > segmentSize;
		for (size_t start = 0; start < bufferSize; start += segmentSize) {
			assert(isMultiPacket || (bufferSize - start <= segmentSize));
			if (!netSendFunc(Buffer(((uint8_t*)buff + start), min(bufferSize - start, segmentSize), bufferSize
				, id, type, isMultiPacket, bufferSize - start <= segmentSize)))
				return false;

			id++;
		}

		return true;
	}

	std::shared_ptr<WholeFrame> ensureFrame(uint64_t time)
	{
		if (videoFrames.find(time) == videoFrames.end())
			videoFrames[time].reset(new WholeFrame);
		return videoFrames[time];
	}

	void addBufferWithoutData(Buffer::Type type)
	{
		Buffer && packet = newBufferWithoutData(type);
		ensureFrame(getTime())->push_back(packet);
	}

public:
	uint64_t packetId = 0;
	const size_t segmentSize = VIDEO_PROTO_PACKET_SIZE;

	void addPolledFrame(void* buff, size_t bufferSize, bool isIDR)
	{
		auto orig_count = numFramesReceivedFromEncoder ++;
		if(orig_count == 0)
		{
			// TODO: FIXME: no locks here? (no time to fix this minor sync. issue)
			videoFirstFrameSize = bufferSize;
			videoFirstFrame = new int8_t[videoFirstFrameSize];
			memcpy(videoFirstFrame, buff, videoFirstFrameSize);
		}
		else
		{
			concurrency::reader_writer_lock::scoped_lock lock(videoFramesLock);
			if (!this->bufferEnabled)
				return;

			auto time = getTime();
			bool isMultiPacket = bufferSize > segmentSize;
			for (size_t start = 0; start < bufferSize; start += segmentSize) {
				assert(isMultiPacket || (bufferSize - start <= segmentSize));
				ensureFrame(time)->push_back(Buffer(((uint8_t*)buff + start), min(bufferSize - start, segmentSize), bufferSize
					, packetId, isIDR ? Buffer::Type::FRAME_IDR : Buffer::Type::FRAME_NOT_IDR, isMultiPacket, bufferSize - start <= segmentSize));

				packetId++;
			}
		}
	}

	// Get a packet for most recent audio frame. Returns false if network failed.
	// netSendFunc: return false if network failed.
	bool sendAudioPacket(const std::function<bool (Buffer&)>& netSendFunc)
	{
		if (!currAudioFrame)
			return true;			// although there is no audio frame to send, we should return true (otherwise thread will quit)

		std::shared_ptr<uint8_t> buff;
		size_t bufferSize;
		{
			concurrency::reader_writer_lock::scoped_lock lock(videoFramesLock);
			buff = currAudioFrame;
			bufferSize = currAudioFrameSize;
			currAudioFrame = nullptr;
		}

		return packetizeAndSendNoIDTrack(Buffer::Type::AUDIO, buff.get(), bufferSize, netSendFunc);
	}

	void setCurrAudioPacket(void* buff, size_t bufferSize)
	{
		concurrency::reader_writer_lock::scoped_lock lock(videoFramesLock);
		if (currAudioFrame)
			currAudioFrame = nullptr;
		currAudioFrame.reset(new uint8_t[currAudioFrameSize = bufferSize]);
		memcpy(currAudioFrame.get(), buff, currAudioFrameSize);
	}

	void printMemInfo()
	{
//		static AveragePrinter<size_t> print(0, "NetBuffer size");
		concurrency::reader_writer_lock::scoped_lock lock(videoFramesLock);
//	print.print(videoFrames.size());
	}
};

#endif//_OIP_VIDEO_PROTO_H
