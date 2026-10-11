"""Compile production Emote adapter/method bodies with real TJS and ncbind."""
import argparse
from pathlib import Path

def block(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

parser = argparse.ArgumentParser()
parser.add_argument('--core', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = args.core / 'plugins/emoteplayer'
runner = (source / 'emoterunner.cpp').read_text(encoding='utf-8')
file_source = (source / 'emotefile.cpp').read_text(encoding='utf-8')
file_header = (source / 'emotefile.h').read_text(encoding='utf-8')
header = (source / 'emoterunner.h').read_text(encoding='utf-8')
player = (source / 'emoteplayerclass.cpp').read_text(encoding='utf-8')
player_header = (source / 'emoteplayerclass.h').read_text(encoding='utf-8')
engine_names = ['emoteengine::emoteengine()', 'void emoteengine::resetAnimationState(',
    'void emoteengine::ensureAnimationState(', 'void emoteengine::advanceAnimation(',
    'void emoteengine::updateAnimationSelectors(', 'void emoteengine::setAnimationVariable(',
    'bool emoteengine::getMotionParameter(', 'void emoteengine::copyAnimationStateFrom(',
    'std::string emoteengine::serializeAnimationState()', 'bool emoteengine::restoreAnimationState(',
    'bool emoteengine::getTickByName(', 'void emoteengine::updateEyeControl(',
    'void emoteengine::startTimeline(', 'void emoteengine::stopTimeline(',
    'bool emoteengine::checkTimline(', 'void emoteengine::updateTimelineControl(',
    'void emoteengine::setVariable(', 'tjs_real emoteengine::getVariable(',
    'void emoteengine::recordAnimationProgress(', 'void emoteengine::recordAnimationDraw(']
player_names = ['void EmotePlayer::setVariable(', 'tjs_error EmotePlayer::cb_setVariable(',
    'tjs_real EmotePlayer::getVariable(', 'void EmotePlayer::progress(',
    'void EmotePlayer::playTimeline(', 'void EmotePlayer::stopTimeline(',
    'bool EmotePlayer::getTimelinePlaying(', 'bool EmotePlayer::getLoopTimeline(',
    'tjs_real EmotePlayer::getTimelineTotalFrameCount(',
    'void EmotePlayer::setTimelineBlendRatio(', 'void EmotePlayer::fadeInTimeline(',
    'void EmotePlayer::fadeOutTimeline(', 'tTJSVariant EmotePlayer::getPlayingTimelineInfoList(',
    'tTJSVariant EmotePlayer::serialize()', 'void EmotePlayer::unserialize(',
    'void EmotePlayer::copyAnimationStateFrom(']
args.output.mkdir(parents=True, exist_ok=True)
(args.output / 'ProductionEngineDeclaration.inc').write_text(block(header, 'class emoteengine\n') + ';\n', encoding='utf-8')
(args.output / 'ProductionIntegration.inc').write_text('\n\n'.join(block(runner, s) for s in engine_names)
    + '\n' + block(runner, 'float emotemotionref::getTickByIdx(')
    + '\n' + block(file_source, 'bool emotefile::getTickByName(')
    + '\n' + '\n\n'.join(block(player, s) for s in player_names), encoding='utf-8')
(args.output / 'ProductionParameter.inc').write_text(block(file_header, 'struct emoteVar') + ';\n', encoding='utf-8')
registration = (source / 'emoteplayer.cpp').read_text(encoding='utf-8')
line = next(line.strip() for line in registration.splitlines() if 'NCB_METHOD_RAW_CALLBACK(setVariable, &EmotePlayer::' in line)
(args.output / 'ProductionRegistration.inc').write_text('NCB_REGISTER_CLASS(EmotePlayer) { NCB_CONSTRUCTOR(());\n' + line + '\nNCB_PROPERTY(queuing,get_queuing,set_queuing);\n}\n', encoding='utf-8')
(args.output / 'ProductionClockMembers.inc').write_text('\n'.join(block(player_header, signature) for signature in
    ['bool get_playing()', 'void set_playing(', 'tjs_real get_tickCount()', 'void set_tickCount(', 'bool get_queuing()', 'void set_queuing(', 'void inheritAnimationModeFrom(']) + '\n', encoding='utf-8')
native = (args.core / 'plugins/DrawDeviceD3D/D3DEmotePlayer.cpp').read_text(encoding='utf-8')
(args.output / 'ProductionD3D.inc').write_text('\n'.join(block(native, signature) for signature in
    ['void D3DEmotePlayer::setVariable(', 'void D3DEmotePlayer::progress(', 'tTJSVariant D3DEmotePlayer::clone(']) + '\n', encoding='utf-8')
native_registration = (args.core / 'plugins/DrawDeviceD3D/DrawDeviceD3D_reg.cpp').read_text(encoding='utf-8')
methods = [next(line.strip() for line in native_registration.splitlines() if line.strip() == 'NCB_METHOD(' + name + ');') for name in ['setVariable','progress','clone']]
(args.output / 'ProductionD3DRegistration.inc').write_text('NCB_REGISTER_CLASS(D3DEmotePlayer) { NCB_CONSTRUCTOR(());\n' + '\n'.join(methods) + '\n}\n', encoding='utf-8')
