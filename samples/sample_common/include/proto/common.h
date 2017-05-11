#ifndef _VIDEO_PROTO_COMMON_H
#define _VIDEO_PROTO_COMMON_H

// This is the file shared between guest and host.

#define VIDEO_PROTO_PACKET_SIZE 1000

// This file can only be used in C++ source files
namespace VideoProto
{
	enum PacketType
	{
		FRAME_NOT_IDR = 0,
		FRAME_IDR = 1,
		/* An ASK_ACK packet will still contain "videoFrameData".
		 * Those data must be sent back in raw form (without any additional data, in a single UDP packet)
		 *
		 * DO NOT even check the endian, just sent the whole thing in videoFrameData back.
		 */
		ASK_ACK = 2,

		 /* This represents an ACK packet sent from server to client
		  * No data should be included as "videoFrameData"
		  */
		 SERVER_ACK_CLIENT = 3,

		 /* No data should be included as "videoFrameData"
		  */
		 SERVER_DISCARD_CLIENT = 4,

		AUDIO = 5,

		/*
		 * This packet indicates that audio service has been reset.
		 * No reply is needed.
		 */
		AUDIO_RESTART = 6,

		// The first frame of video ES. This frame is important for decoders
		// Client must reply ACK by sending a uint32_t with value equal to the full size of Frame 0
		 VIDEO_FRAME_ZERO = 7
	};
};

#endif//_VIDEO_PROTO_COMMON_H