// Generated from MikageKRKRRuntime.mm; do not edit.
extern "C" bool MikageKRKRTakeLayerTriangleProfile(MikageKRKRLayerTriangleProfile *profile)
{
    if (!running || !profile || !diagnosticCallback.load(std::memory_order_acquire))
        return false;
    std::memset(profile, 0, sizeof(*profile));
    try {
        const auto sample = TVPTakeMetalLayerTriangleProfile();
        const auto& s = sample.stats;
        profile->intervalNS = s.intervalNS;
        profile->calls = s.calls;
        profile->triangleCount = s.triangleCount;
        profile->gpuCalls = s.gpuCalls;
        profile->gpuPixels = s.gpuPixels;
        profile->clipPixels = s.clipPixels;
        profile->maxClipPixels = s.maxClipPixels;
        profile->maxTargetPixels = s.maxTargetPixels;
        profile->fullSurfaceCalls = s.fullSurfaceCalls;
        profile->target1920x1080Calls = s.target1920x1080Calls;
        profile->targetReadbackBytes = s.targetReadbackBytes;
        profile->sourceReadbackBytes = s.sourceReadbackBytes;
        profile->referenceReadbackBytes = s.referenceReadbackBytes;
        profile->cpuTimeNS = s.cpuTimeNS;
        profile->maxCpuTimeNS = s.maxCpuTimeNS;
        profile->softwareTimeNS = s.softwareTimeNS;
        profile->maxSoftwareTimeNS = s.maxSoftwareTimeNS;
        profile->count2Calls = s.count2Calls;
        profile->singleInputCalls = s.singleInputCalls;
        profile->referenceCalls = s.referenceCalls;
        profile->sourceTargetAliasCalls = s.sourceTargetAliasCalls;
        std::strncpy(profile->methods, sample.methods.c_str(), sizeof(profile->methods) - 1);
        std::strncpy(profile->targetSizes, sample.targetSizes.c_str(), sizeof(profile->targetSizes) - 1);
        std::strncpy(profile->sources, sample.sources.c_str(), sizeof(profile->sources) - 1);
        std::strncpy(profile->stretchModes, sample.stretchModes.c_str(), sizeof(profile->stretchModes) - 1);
        return true;
    } catch (...) {
        return false;
    }
}
extern "C" bool MikageKRKRTakeLayerWorkProfile(MikageKRKRLayerWorkProfile *profile)
{
    if(!running || !profile || !diagnosticCallback.load(std::memory_order_acquire)) return false;
    std::memset(profile,0,sizeof(*profile));
    try {
        const auto sample=krkrsdl3::layer_work::Take();
        profile->intervalNS=sample.intervalNS;
        // Bounds are part of the wire contract. Never emit a silently cut row.
        if(sample.stages.size()>=sizeof(profile->stages) || sample.transfers.size()>=sizeof(profile->transfers) ||
           sample.transferOrigins.size()>=sizeof(profile->transferOrigins) || sample.originOverflow.size()>=sizeof(profile->originOverflow) ||
           sample.transitionProfiles.size()>=sizeof(profile->transitionProfiles) || sample.transitionOverflow.size()>=sizeof(profile->transitionOverflow) ||
           sample.shrinkProfiles.size()>=sizeof(profile->shrinkProfiles) || sample.shrinkOverflow.size()>=sizeof(profile->shrinkOverflow) ||
           sample.shrinkReadWaitSampleCount>krkrsdl3::layer_work::MaxReadWaitSamples ||
           sample.frameSampleCount>krkrsdl3::layer_work::MaxFrameSamples) return false;
        std::memcpy(profile->stages,sample.stages.c_str(),sample.stages.size()+1);
        std::memcpy(profile->transfers,sample.transfers.c_str(),sample.transfers.size()+1);
        profile->amvDecodedFrames=sample.decodedFrames;
        profile->amvDecodedBytes=sample.decodedBytes;
        profile->workProfileVersion=sample.workProfileVersion;
        std::memcpy(profile->transferOrigins,sample.transferOrigins.c_str(),sample.transferOrigins.size()+1);
        std::memcpy(profile->originOverflow,sample.originOverflow.c_str(),sample.originOverflow.size()+1);
        profile->frameSampleCount=sample.frameSampleCount;
        std::copy_n(sample.frameIntervalNS.data(),sample.frameSampleCount,profile->frameIntervalNS);
        std::copy_n(sample.frameCpuWallNS.data(),sample.frameSampleCount,profile->frameCpuWallNS);
        profile->frameSamplesDropped=sample.frameSamplesDropped;
        profile->transitionProfileVersion=sample.transitionProfileVersion;
        std::memcpy(profile->transitionProfiles,sample.transitionProfiles.c_str(),sample.transitionProfiles.size()+1);
        profile->transitionProfilesDropped=sample.transitionProfilesDropped;
        std::memcpy(profile->transitionOverflow,sample.transitionOverflow.c_str(),sample.transitionOverflow.size()+1);
        profile->shrinkProfileVersion=sample.shrinkProfileVersion;
        std::memcpy(profile->shrinkProfiles,sample.shrinkProfiles.c_str(),sample.shrinkProfiles.size()+1);
        profile->shrinkProfilesDropped=sample.shrinkProfilesDropped;
        std::memcpy(profile->shrinkOverflow,sample.shrinkOverflow.c_str(),sample.shrinkOverflow.size()+1);
        profile->shrinkReadWaitSampleCount=sample.shrinkReadWaitSampleCount;
        std::copy_n(sample.shrinkReadWaitSamplesNS.data(),sample.shrinkReadWaitSampleCount,profile->shrinkReadWaitSamplesNS);
        profile->shrinkReadWaitSamplesDropped=sample.shrinkReadWaitSamplesDropped;
        return true;
    } catch(...) { return false; }
}
extern "C" const char *MikageKRKRLayerWorkProfileOrigins(const MikageKRKRLayerWorkProfile *profile)
{
    return profile ? profile->transferOrigins : nullptr;
}
extern "C" const char *MikageKRKRLayerWorkProfileTransitions(const MikageKRKRLayerWorkProfile *profile)
{
    return profile && profile->transitionProfileVersion==1 ? profile->transitionProfiles : nullptr;
}
extern "C" const char *MikageKRKRLayerWorkProfileTransitionOverflow(const MikageKRKRLayerWorkProfile *profile)
{
    return profile && profile->transitionProfileVersion==1 ? profile->transitionOverflow : nullptr;
}
extern "C" const uint64_t *MikageKRKRLayerWorkProfileFrameIntervals(const MikageKRKRLayerWorkProfile *profile)
{
    return profile && profile->frameSampleCount <= krkrsdl3::layer_work::MaxFrameSamples ? profile->frameIntervalNS : nullptr;
}
extern "C" const char *MikageKRKRLayerWorkProfileShrinks(const MikageKRKRLayerWorkProfile *profile)
{
    return profile && profile->shrinkProfileVersion==1 &&
        std::memchr(profile->shrinkProfiles,0,sizeof(profile->shrinkProfiles)) ? profile->shrinkProfiles : nullptr;
}
extern "C" const char *MikageKRKRLayerWorkProfileShrinkOverflow(const MikageKRKRLayerWorkProfile *profile)
{
    return profile && profile->shrinkProfileVersion==1 &&
        std::memchr(profile->shrinkOverflow,0,sizeof(profile->shrinkOverflow)) ? profile->shrinkOverflow : nullptr;
}
extern "C" const uint64_t *MikageKRKRLayerWorkProfileShrinkReadWait(const MikageKRKRLayerWorkProfile *profile)
{
    return profile && profile->shrinkProfileVersion==1 && profile->shrinkReadWaitSampleCount<=krkrsdl3::layer_work::MaxReadWaitSamples
        ? profile->shrinkReadWaitSamplesNS : nullptr;
}
extern "C" const uint64_t *MikageKRKRLayerWorkProfileFrameCpuWall(const MikageKRKRLayerWorkProfile *profile)
{
    return profile && profile->frameSampleCount <= krkrsdl3::layer_work::MaxFrameSamples ? profile->frameCpuWallNS : nullptr;
}
