#include "MikageSDLHostPlatform.h"

#import <AVFAudio/AVFAudio.h>

bool MikageSDLHostPlatformSetAudioSessionActive(bool active, std::string &error)
{
    NSError *failure = nil;
    if (active) {
        [[AVAudioSession sharedInstance] setActive:YES error:&failure];
    } else {
        [[AVAudioSession sharedInstance]
            setActive:NO
            withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
            error:&failure];
    }
    if (!failure)
        return true;
    const char *description = failure.localizedDescription.UTF8String;
    error = description ? description : "";
    return false;
}
