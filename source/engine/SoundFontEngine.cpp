#include "SoundFontEngine.h"

#include <algorithm>
#include <cmath>

#if defined(HVST_HAVE_FLUIDSYNTH)
  #include <fluidsynth.h>
#endif

namespace plectro {

namespace {
[[maybe_unused]] int gainToExpression(float gain)
{
    const float g = std::clamp(gain, 0.0f, 1.0f);
    return std::clamp(static_cast<int>(std::lround(g * 127.0f)), 0, 127);
}

// Cents -> 14-bit pitch bend value, assuming the default +/- 2 semitone (200 cent) range.
[[maybe_unused]] int centsToBend(float cents)
{
    const int v = 8192 + static_cast<int>(std::lround(cents / 200.0f * 8192.0f));
    return std::clamp(v, 0, 16383);
}

// Pan (-1 left .. 0 centre .. +1 right) -> MIDI CC10 (0 hard left, 64 centre, 127 hard right).
[[maybe_unused]] int panToCC(float pan)
{
    const float p = std::clamp(pan, -1.0f, 1.0f);
    return std::clamp(static_cast<int>(std::lround((p + 1.0f) * 63.5f)), 0, 127);
}
} // namespace

SoundFontEngine::SoundFontEngine()
{
    channelVoice_.fill(-1);
}

SoundFontEngine::~SoundFontEngine()
{
#if defined(HVST_HAVE_FLUIDSYNTH)
    if (synth_ != nullptr)
        delete_fluid_synth(synth_);
    if (settings_ != nullptr)
        delete_fluid_settings(settings_);
#endif
}

void SoundFontEngine::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
#if defined(HVST_HAVE_FLUIDSYNTH)
    if (synth_ != nullptr)
        delete_fluid_synth(synth_);
    if (settings_ != nullptr)
        delete_fluid_settings(settings_);

    settings_ = new_fluid_settings();
    // FluidSynth only accepts 8k..96k; clamp so odd host probe rates do not error.
    const double sr = std::clamp(sampleRate, 8000.0, 96000.0);
    fluid_settings_setnum(settings_, "synth.sample-rate", sr);
    fluid_settings_setint(settings_, "synth.reverb.active", 0);
    fluid_settings_setint(settings_, "synth.chorus.active", 0);
    fluid_settings_setint(settings_, "synth.midi-channels", kNumChannels);
    synth_ = new_fluid_synth(settings_);
    // FluidSynth defaults its master gain to 0.2 (roughly -14 dB), which is why the plugin
    // sounded very quiet. Open it up; per-note dynamics ride on CC11 expression.
    fluid_synth_set_gain(synth_, 1.0f);
    sfId_ = -1;
    loaded_ = false;
#endif
    channelVoice_.fill(-1);
    nextChannel_ = 0;
}

bool SoundFontEngine::loadSoundFont(const std::string& path)
{
#if defined(HVST_HAVE_FLUIDSYNTH)
    if (synth_ == nullptr)
        return false;
    sfId_ = fluid_synth_sfload(synth_, path.c_str(), 1);
    loaded_ = (sfId_ >= 0);
    if (loaded_)
        for (int ch = 0; ch < kNumChannels; ++ch)
            fluid_synth_program_select(synth_, ch, sfId_, 0, 0);
    return loaded_;
#else
    (void) path;
    return false;
#endif
}

std::vector<SoundFontEngine::PresetInfo> SoundFontEngine::listPresets() const
{
    std::vector<PresetInfo> out;
#if defined(HVST_HAVE_FLUIDSYNTH)
    if (synth_ == nullptr || !loaded_)
        return out;
    fluid_sfont_t* sfont = fluid_synth_get_sfont_by_id(synth_, sfId_);
    if (sfont == nullptr)
        return out;
    fluid_sfont_iteration_start(sfont);
    while (fluid_preset_t* preset = fluid_sfont_iteration_next(sfont))
    {
        PresetInfo info;
        info.bank = fluid_preset_get_banknum(preset);
        info.preset = fluid_preset_get_num(preset);
        const char* n = fluid_preset_get_name(preset);
        info.name = n != nullptr ? n : "";
        out.push_back(std::move(info));
    }
#endif
    return out;
}

int SoundFontEngine::channelForVoice(int voiceId)
{
    for (int ch = 0; ch < kNumChannels; ++ch)
        if (channelVoice_[static_cast<std::size_t>(ch)] == voiceId)
            return ch;

    // Prefer a free channel, else steal round-robin.
    for (int i = 0; i < kNumChannels; ++i)
    {
        const int ch = (nextChannel_ + i) % kNumChannels;
        if (channelVoice_[static_cast<std::size_t>(ch)] == -1)
        {
            channelVoice_[static_cast<std::size_t>(ch)] = voiceId;
            nextChannel_ = (ch + 1) % kNumChannels;
            return ch;
        }
    }
    const int ch = nextChannel_;
    channelVoice_[static_cast<std::size_t>(ch)] = voiceId;
    nextChannel_ = (ch + 1) % kNumChannels;
    return ch;
}

void SoundFontEngine::freeVoiceChannel(int voiceId)
{
    for (auto& v : channelVoice_)
        if (v == voiceId)
            v = -1;
}

void SoundFontEngine::noteOn(int voiceId, int key, int velocity, float gain, int bank, int preset, float detuneCents, float pan)
{
    const int ch = channelForVoice(voiceId);
#if defined(HVST_HAVE_FLUIDSYNTH)
    if (synth_ != nullptr)
    {
        if (loaded_)
            fluid_synth_program_select(synth_, ch, sfId_, static_cast<unsigned int>(bank), preset);
        fluid_synth_pitch_bend(synth_, ch, centsToBend(detuneCents));
        fluid_synth_cc(synth_, ch, 11 /* expression */, gainToExpression(gain));
        fluid_synth_cc(synth_, ch, 10 /* pan */, panToCC(pan));
        fluid_synth_noteon(synth_, ch, key, velocity);
    }
#else
    (void) ch; (void) key; (void) velocity; (void) gain; (void) bank; (void) preset; (void) detuneCents; (void) pan;
#endif
}

void SoundFontEngine::setPitch(int voiceId, float detuneCents)
{
    const int ch = channelForVoice(voiceId);
#if defined(HVST_HAVE_FLUIDSYNTH)
    if (synth_ != nullptr)
        fluid_synth_pitch_bend(synth_, ch, centsToBend(detuneCents));
#else
    (void) ch; (void) detuneCents;
#endif
}

void SoundFontEngine::setGain(int voiceId, float gain)
{
    const int ch = channelForVoice(voiceId);
#if defined(HVST_HAVE_FLUIDSYNTH)
    if (synth_ != nullptr)
        fluid_synth_cc(synth_, ch, 11 /* expression */, gainToExpression(gain));
#else
    (void) ch; (void) gain;
#endif
}

void SoundFontEngine::noteOff(int voiceId, int key)
{
    const int ch = channelForVoice(voiceId);
#if defined(HVST_HAVE_FLUIDSYNTH)
    if (synth_ != nullptr)
        fluid_synth_noteoff(synth_, ch, key);
#else
    (void) ch; (void) key;
#endif
    freeVoiceChannel(voiceId);
}

void SoundFontEngine::allNotesOff()
{
#if defined(HVST_HAVE_FLUIDSYNTH)
    if (synth_ != nullptr)
        for (int ch = 0; ch < kNumChannels; ++ch)
            fluid_synth_all_notes_off(synth_, ch);
#endif
    channelVoice_.fill(-1);
}

void SoundFontEngine::render(float* left, float* right, int numSamples)
{
    std::fill(left, left + numSamples, 0.0f);
    std::fill(right, right + numSamples, 0.0f);
#if defined(HVST_HAVE_FLUIDSYNTH)
    if (synth_ != nullptr)
        fluid_synth_write_float(synth_, numSamples, left, 0, 1, right, 0, 1);
#endif
    if (masterGain_ != 1.0f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] *= masterGain_;
            right[i] *= masterGain_;
        }
    }
}

} // namespace plectro
