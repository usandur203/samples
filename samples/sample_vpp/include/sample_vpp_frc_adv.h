/******************************************************************************\
Copyright (c) 2005-2016, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

This sample was distributed or derived from the Intel's Media Samples package.
The original version of this sample may be obtained from https://software.intel.com/en-us/intel-media-server-studio
or https://software.intel.com/en-us/media-client-solutions-support.
\**********************************************************************************/

#ifndef __SAMPLE_VPP_FRC_ADV_H
#define __SAMPLE_VPP_FRC_ADV_H

#include <memory>
#include <list>
#include <stdio.h>

#include "mfxvideo.h"
#include "sample_vpp_frc.h"

class FRCAdvancedChecker : public BaseFRCChecker
{
public:
    FRCAdvancedChecker();
    virtual ~FRCAdvancedChecker(){};

    mfxStatus Init(mfxVideoParam *par, mfxU32 asyncDeep);

    // notify FRCChecker about one more input frame
    bool  PutInputFrameAndCheck(mfxFrameSurface1* pSurface);

    // notify FRCChecker about one more output frame and check result
    bool  PutOutputFrameAndCheck(mfxFrameSurface1* pSurface);

private:

    mfxU64    GetExpectedPTS( mfxU32 frameNumber, mfxU64 timeOffset, mfxU64 timeJump );

    bool IsTimeStampsNear( mfxU64 timeStampRef, mfxU64 timeStampTst,  mfxU64 eps);

    mfxU64 m_minDeltaTime;

    bool   m_bIsSetTimeOffset;

    mfxU64 m_timeOffset;
    mfxU64 m_expectedTimeStamp;
    mfxU64 m_timeStampJump;
    mfxU32 m_numOutputFrames;

    bool   m_bReadyOutput;
    mfxU64 m_defferedInputTimeStamp;

    mfxVideoParam m_videoParam;

    std::list<mfxU64> m_ptsList;
};

#endif /* __SAMPLE_VPP_PTS_ADV_H*/