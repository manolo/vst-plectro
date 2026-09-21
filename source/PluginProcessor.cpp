#include "PluginProcessor.h"
#include "EditorSupport.h"
#include "Edition.h"
#include "core/PresetMapping.h"

#include <algorithm>
#include <cmath>

#if defined(HVST_DEBUG_LOG)
  #include <cstdio>
namespace {
std::FILE* plectroLog()
{
    static std::FILE* f = std::fopen("/tmp/pp_debug.log", "w");
    return f;
}
void plectroLogf(const char* fmt, long long a, int b, int c, int d)
{
    if (auto* f = plectroLog()) { std::fprintf(f, fmt, a, b, c, d); std::fflush(f); }
}
}
  #define PLECTRO_LOG(...) plectroLogf(__VA_ARGS__)
#else
  #define PLECTRO_LOG(...) ((void) 0)
#endif

namespace plectro {

namespace {
// Rebase a layer's voice commands so they never collide with another layer's voices, fold in the
// layer's relative gain, and stamp its stereo placement. voiceId offset keeps the engine treating
// each layer independently.
void applyLayerOffsetGain(std::vector<VoiceCommand>& cmds, int layerIndex, float gainMul, float pan, int stride)
{
    const int off = layerIndex * stride;
    for (auto& c : cmds)
    {
        c.voiceId += off;
        c.gain *= gainMul;
        c.pan = pan;
    }
}
} // namespace

PlectroProcessor::PlectroProcessor()
    : juce::AudioProcessor(BusesProperties()
                           .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "PARAMS", pid::createLayout())
{
    // Default to the SF2 bundled with this edition. Fall back to the dev soundfont if the
    // bundled resource is missing (for example a bare build without the asset copied in).
    auto bundled = bundledSoundFontPath();
    sf2Path_ = juce::File(bundled).existsAsFile()
                   ? bundled
                   : juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                         .getChildFile("Github/music/pulso-pua-sound-fonts/PulsoPua.sf2")
                         .getFullPathName();

    // Give each fresh instance a different humanization seed so duplicated staves decorrelate
    // automatically. A loaded project overwrites this via setStateInformation.
    if (auto* p = apvts_.getParameter(pid::instanceSeed))
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(juce::Random::getSystemRandom().nextInt(100000))));
}

PlaybackParams PlectroProcessor::readParams() const
{
    PlaybackParams p;
    p.sampleRate = sampleRate_;
    p.tremoloOn = apvts_.getRawParameterValue(pid::tremoloOn)->load() > 0.5f;
    // Auto: the rhythmic detector runs only when the host does NOT drive articulations by keyswitch.
    // We stop auto-detecting once a keyswitch has been seen this session, and also right after a
    // keyswitch-aware host queries our IKeyswitchController (MuseScore does at load): such a host
    // notates tremolo explicitly, so the detector is redundant and would otherwise misfire on
    // ornament expansions (trills, mordents, turns) that arrive as rapid repeated notes. The query
    // suppression is BOUNDED (shouldAutoDetect): a host that queries but never sends a keyswitch
    // gets auto detection back after a few notes, so a bare query does not disable it forever.
    p.enableDetection = shouldAutoDetect(p.tremoloOn,
                                         keyswitchSeen_.load(std::memory_order_relaxed),
                                         hostQueriedKeyswitches(),
                                         musicalNotesSinceStart_.load(std::memory_order_relaxed));
    p.detectWindowMs = apvts_.getRawParameterValue(pid::detectWindowMs)->load();
    p.minRepeats = static_cast<int>(apvts_.getRawParameterValue(pid::minRepeats)->load());
    p.jitterMs = apvts_.getRawParameterValue(pid::jitterMs)->load();
    p.breathingDepthMs = apvts_.getRawParameterValue(pid::breathingDepthMs)->load();
    p.breathingRateHz = apvts_.getRawParameterValue(pid::breathingRateHz)->load();
    p.velocityCurve = apvts_.getRawParameterValue(pid::velocityCurve)->load();
    p.variationDepth = apvts_.getRawParameterValue(pid::variationDepth)->load();
    const bool outputOn = apvts_.getRawParameterValue(pid::outputOn)->load() > 0.5f;
    p.compression = outputOn ? apvts_.getRawParameterValue(pid::compression)->load() : 0.0; // Output off = no compression
    p.lengthVariation = apvts_.getRawParameterValue(pid::lengthVariation)->load();
    p.legatoOverlapMs = apvts_.getRawParameterValue(pid::noteOverlapMs)->load();
    p.detuneCents = apvts_.getRawParameterValue(pid::detuneCents)->load();
    p.bank = static_cast<int>(apvts_.getRawParameterValue(pid::instrumentBank)->load());
    // Articulation -> preset comes from the loaded SF2's preset names (rebuildPresetMap), so it
    // follows the SoundFont layout instead of fixed indices that can point at the wrong sample.
    for (int i = 0; i < kNumArticulations; ++i)
        p.presetByArticulation[i] = presetMap_[i].load(std::memory_order_relaxed);
    p.tremoloPickedPreset = tremoloPickedPreset_.load(std::memory_order_relaxed);
    p.globalSeed = static_cast<std::uint64_t>(apvts_.getRawParameterValue(pid::globalSeed)->load());
    p.instanceSeed = static_cast<std::uint64_t>(apvts_.getRawParameterValue(pid::instanceSeed)->load());
    p.lookaheadSamples = lookaheadSamples_;

    // The Humanize switch bypasses the per note variation while keeping articulation routing and
    // tremolo detection intact. In the free edition the humanization math is neutral regardless
    // (the linked VariationSource returns no deviations), so no edition check is needed here.
    if (apvts_.getRawParameterValue(pid::humanize)->load() < 0.5f)
        neutralizePerNoteVariation(p);

    return p;
}

void PlectroProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    const double lookaheadMs = apvts_.getRawParameterValue(pid::lookaheadMs)->load();
    lookaheadSamples_ = static_cast<std::int64_t>(lookaheadMs * 0.001 * sampleRate);
    setLatencySamples(static_cast<int>(lookaheadSamples_));

    engine_.prepare(sampleRate);
    juce::ignoreUnused(samplesPerBlock);
    reloadSoundFont();

    if (static_cast<int>(layers_.size()) != kMaxLayers)
        layers_.resize(kMaxLayers);
    for (auto& L : layers_)
        L.stream.reset();
    activeLayers_ = 1;
    lastSelection_ = INT_MIN; // force the first block to configure the layers from the selection
    articulationLatch_.fill(Articulation::Auto);
    legatoActive_.fill(false);
    trillActive_.fill(false);
    trillMainKey_.fill(-1);
    keyswitchSeen_.store(false, std::memory_order_relaxed);
    musicalNotesSinceStart_.store(0, std::memory_order_relaxed);
    wasPlaying_ = false;
    hostSample_ = 0;
    schedPos_ = 0;
    scheduled_.clear();
}

void PlectroProcessor::releaseResources()
{
    engine_.allNotesOff();
    for (auto& L : layers_)
        L.stream.reset();
    articulationLatch_.fill(Articulation::Auto);
    legatoActive_.fill(false);
    trillActive_.fill(false);
    trillMainKey_.fill(-1);
    keyswitchSeen_.store(false, std::memory_order_relaxed);
    musicalNotesSinceStart_.store(0, std::memory_order_relaxed);
    wasPlaying_ = false;
    scheduled_.clear();
    schedPos_ = 0;
}

bool PlectroProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void PlectroProcessor::ingestMidi(const juce::MidiBuffer& midi)
{
    // Two-pass processing: keyswitches first, then regular notes.
    // This ensures articulation changes are applied before note events,
    // regardless of their arrival order within the same buffer.
    const bool captureTrills = apvts_.getRawParameterValue(pid::captureTrills)->load() > 0.5f;

    // PASS 1: Process all keyswitches
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (!msg.isNoteOn()) continue;
        
        const int note = msg.getNoteNumber();
        if (note < pid::kKeyswitchBase || note > pid::kKeyswitchZoneTop)
            continue;
        
        const std::int64_t at = hostSample_ + meta.samplePosition;
        const int chan = juce::jlimit(1, 16, msg.getChannel());

        // Legato is a ranged modifier the host presses at every note it covers (so starting playback
        // partway through a slur still engages it) and releases at the range end (a note-off, handled
        // in PASS 2). It does not change the articulation latch. Only a fresh span (the latch was off)
        // clears legatoNoteSeen_, so the first note under the slur is not yet a continuation while a
        // re-press within the same span leaves the continuation state intact.
        if (keyswitchNoteIsLegato(note - pid::kKeyswitchBase))
        {
            keyswitchSeen_.store(true, std::memory_order_relaxed);
            if (!legatoActive_[chan])
                legatoNoteSeen_[chan] = false;
            legatoActive_[chan] = true;
            continue;
        }

        keyswitchSeen_.store(true, std::memory_order_relaxed);
        const Articulation oldArt = articulationLatch_[chan];
        const bool isTrill = keyswitchNoteIsTrill(note - pid::kKeyswitchBase);
        // A trill keyswitch selects Tremolo (see the layout). With trill capture off, treat it as
        // normal so the trill plays as its written notes instead of one sustained tremolo.
        Articulation newArt = pid::articulationForKeyswitch(note);
        if (isTrill && !captureTrills)
            newArt = Articulation::Picked;

        // A keyswitch arriving while a span articulation (tremolo) is active means the host is
        // marking a boundary: a new span of the same articulation (re-sent keyswitch) or a
        // switch away from it. Either way, close the active voices so the next span/note starts
        // fresh instead of extending the current sustain. Point articulations never do this, so
        // consecutive picked/pizzicato notes are left untouched.
        if (isSpanArticulation(oldArt))
            closeLayersOnBoundary(at);
        
        articulationLatch_[chan] = newArt;

        // Trill capture: keep only the main pitch (one sustained tremolo) and drop the alternating
        // upper note in PASS 2. The first trill note captures the main pitch.
        const bool trill = isTrill && captureTrills;
        if (trill && !trillActive_[chan])
            trillMainKey_[chan] = -1;
        trillActive_[chan] = trill;
    }

    // Track held notes so the editor can show the sounding note (top of a chord) and flag chords.
    // Balanced across note-on/off; only the audio thread touches the counts.
    auto trackNote = [this](int note, bool on) {
        if (note < 0 || note > 127) return;
        if (on) { if (noteOnCount_[note]++ == 0) ++activeNoteTotal_; }
        else if (noteOnCount_[note] > 0 && --noteOnCount_[note] == 0) --activeNoteTotal_;
        int top = -1;
        for (int n = 127; n >= 0; --n) if (noteOnCount_[n] > 0) { top = n; break; }
        currentTopNote_.store(top, std::memory_order_relaxed);
        currentChord_.store(activeNoteTotal_ > 1, std::memory_order_relaxed);
    };

    // PASS 2: Process all regular NoteOns and NoteOffs
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const std::int64_t at = hostSample_ + meta.samplePosition;
        const int chan = juce::jlimit(1, 16, msg.getChannel());

        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            if (note >= pid::kKeyswitchBase && note <= pid::kKeyswitchZoneTop)
                continue;

            trackNote(note, true);

            // Count musical notes so a host that queried our keyswitches but never sends one gets
            // auto detection back after a few notes (see shouldAutoDetect). Saturates; only the
            // first kQueryGraceNotes matter, and it is meaningless once a keyswitch has been seen.
            if (musicalNotesSinceStart_.load(std::memory_order_relaxed) < kQueryGraceNotes)
                musicalNotesSinceStart_.fetch_add(1, std::memory_order_relaxed);

            if (trillActive_[chan])
            {
                if (trillMainKey_[chan] < 0)
                    trillMainKey_[chan] = note;   // the main pitch: play it as a sustained tremolo
                else if (note != trillMainKey_[chan])
                    continue;                     // drop the alternating upper note
            }
            lastInputVelocity_.store(msg.getVelocity(), std::memory_order_relaxed);
            // A note is a legato continuation only once a note has already sounded in the active
            // span, so the first note under the slur still attacks and later ones connect. Gated to
            // editions with legato tremolo (free always plays P+T, ignoring the slur).
            const bool legatoContinuation =
                kEdition.legatoTremolo && legatoActive_[chan] && legatoNoteSeen_[chan];
            pushToLayers({at, note, msg.getVelocity(), true, chan, articulationLatch_[chan], legatoContinuation});
            if (legatoActive_[chan])
                legatoNoteSeen_[chan] = true;
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            // The legato span ends with the keyswitch note-off; clear the modifier for this channel.
            if (keyswitchNoteIsLegato(note - pid::kKeyswitchBase))
            {
                legatoActive_[chan] = false;
                legatoNoteSeen_[chan] = false;
                continue;
            }
            if (note >= pid::kKeyswitchBase && note <= pid::kKeyswitchZoneTop)
                continue;

            trackNote(note, false);

            if (trillActive_[chan] && trillMainKey_[chan] >= 0 && note != trillMainKey_[chan])
                continue; // drop the note-off of the dropped upper note

            pushToLayers({at, note, 0, false, chan, articulationLatch_[chan], false});
        }
    }
}

void PlectroProcessor::renderScheduled(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : left;

    int pos = 0;
    while (pos < numSamples)
    {
        // Execute every command due at or before the current position.
        while (schedPos_ < scheduled_.size()
               && (scheduled_[schedPos_].targetSample - hostSample_) <= pos)
        {
            const auto& c = scheduled_[schedPos_];
            switch (c.type)
            {
                case VoiceCommandType::NoteOn:  engine_.noteOn(c.voiceId, c.key, c.velocity, c.gain, c.bank, c.preset, c.detuneCents, c.pan); break;
                case VoiceCommandType::SetGain: engine_.setGain(c.voiceId, c.gain); break;
                case VoiceCommandType::SetPitch: engine_.setPitch(c.voiceId, c.detuneCents); break;
                case VoiceCommandType::NoteOff: engine_.noteOff(c.voiceId, c.key); break;
            }
            ++schedPos_;
        }

        // Render until the next command boundary (or the end of the block).
        int next = numSamples;
        if (schedPos_ < scheduled_.size())
        {
            const std::int64_t off = scheduled_[schedPos_].targetSample - hostSample_;
            next = static_cast<int>(std::clamp<std::int64_t>(off, pos, numSamples));
            if (next <= pos)
                next = pos + 1;
        }

        engine_.render(left + pos, right + pos, next - pos);
        pos = next;
    }

    // Compact the scheduled buffer once most of it has been consumed.
    if (schedPos_ > 4096)
    {
        scheduled_.erase(scheduled_.begin(), scheduled_.begin() + static_cast<std::ptrdiff_t>(schedPos_));
        schedPos_ = 0;
    }
}

void PlectroProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // Reset the keyswitch session on transport Stop, so the next play re-detects the host.
    bool isPlaying = false;
    double bpm = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            isPlaying = pos->getIsPlaying();
            if (auto b = pos->getBpm())
                bpm = *b;
        }
    if (wasPlaying_ && !isPlaying)
    {
        keyswitchSeen_.store(false, std::memory_order_relaxed);
        musicalNotesSinceStart_.store(0, std::memory_order_relaxed);
        articulationLatch_.fill(Articulation::Auto);
        legatoActive_.fill(false);
        legatoNoteSeen_.fill(false);
        lastInputVelocity_.store(64, std::memory_order_relaxed); // recentre the compression indicator
        std::fill(std::begin(noteOnCount_), std::end(noteOnCount_), 0);
        activeNoteTotal_ = 0;
        currentTopNote_.store(-1, std::memory_order_relaxed);
        currentChord_.store(false, std::memory_order_relaxed);
    }
    wasPlaying_ = isPlaying;

    auto p = readParams();
    // Beat length from the host tempo, so an explicit tremolo can wait one beat for its next stroke
    // at slow tempo (see StreamingScheduler). 0 leaves the humanizer's default.
    p.beatSamples = bpm > 0.0 ? static_cast<std::int64_t>(60.0 / bpm * sampleRate_) : 0;

    // Reconfigure the ensemble layers when the instrument selection changes (and on the first
    // block). A single instrument is one layer; an "All <family>" selection is several.
    if (p.bank != lastSelection_)
    {
        std::vector<VoiceCommand> offs;
        reconfigureLayers(p.bank, hostSample_, offs);
        lastSelection_ = p.bank;
        if (!offs.empty())
        {
            scheduled_.insert(scheduled_.end(), offs.begin(), offs.end());
            std::stable_sort(scheduled_.begin() + static_cast<std::ptrdiff_t>(schedPos_), scheduled_.end(),
                             [](const VoiceCommand& a, const VoiceCommand& b) { return a.targetSample < b.targetSample; });
        }
    }

    // Each active layer gets the shared params with its own bank, a decorrelating seed offset,
    // and the central anchor forced dehumanized (in tune, on tempo).
    for (int i = 0; i < activeLayers_; ++i)
    {
        PlaybackParams lp = p;
        lp.bank = layers_[static_cast<std::size_t>(i)].bank;
        lp.instanceSeed = p.instanceSeed + static_cast<std::uint64_t>(layers_[static_cast<std::size_t>(i)].seedOffset);
        if (layers_[static_cast<std::size_t>(i)].neutralize)
            neutralizePerNoteVariation(lp);
        layers_[static_cast<std::size_t>(i)].stream.setParams(lp);
    }

    // Output off bypasses the output stage (unity gain). The ensemble is attenuated by 1/sqrt(N)
    // so N decorrelated layers stay close to a single instrument's loudness (N=1 leaves it unity).
    // The trimmed anchor plus the decorrelated (and now panned) copies still land softer than a
    // single instrument, so an ensemble gets a fixed +6 dB make-up to match the solo loudness.
    const bool outputOn = apvts_.getRawParameterValue(pid::outputOn)->load() > 0.5f;
    const float base = outputOn
                       ? juce::Decibels::decibelsToGain(apvts_.getRawParameterValue(pid::masterGain)->load())
                       : 1.0f;
    const float atten = 1.0f / std::sqrt(static_cast<float>(std::max(1, activeLayers_)));
    const float makeup = activeLayers_ > 1 ? juce::Decibels::decibelsToGain(6.0f) : 1.0f;
    engine_.setMasterGain(base * atten * makeup);

    ingestMidi(midi);
    midi.clear(); // instrument consumes MIDI, emits none

    const std::int64_t latestInput = hostSample_ + numSamples;
    std::vector<VoiceCommand> fresh;
    for (int i = 0; i < activeLayers_; ++i)
    {
        std::vector<VoiceCommand> lc;
        layers_[static_cast<std::size_t>(i)].stream.advance(latestInput, lc);
        applyLayerOffsetGain(lc, i, layers_[static_cast<std::size_t>(i)].gainMul, layers_[static_cast<std::size_t>(i)].pan, kVoiceStride);
        fresh.insert(fresh.end(), lc.begin(), lc.end());
    }

    // Publish tremolo activity for the editor's indicator LEDs (OR across the layers).
    bool ksActive = false, detActive = false, ksLegato = false;
    for (int i = 0; i < activeLayers_; ++i)
    {
        bool k = false, d = false, kl = false;
        layers_[static_cast<std::size_t>(i)].stream.tremoloActivity(k, d, kl);
        ksActive = ksActive || k;
        detActive = detActive || d;
        ksLegato = ksLegato || kl;
    }
    keyswitchTremoloActive_.store(ksActive, std::memory_order_relaxed);
    detectorTremoloActive_.store(detActive, std::memory_order_relaxed);
    keyswitchTremoloLegato_.store(ksLegato, std::memory_order_relaxed);
#if defined(HVST_DEBUG_LOG)
    for (const auto& c : fresh)
    {
        // type: 0=NoteOn 1=NoteOff 2=SetGain
        PLECTRO_LOG("OUT sample=%lld preset=%d key=%d type=%d\n",
                 static_cast<long long>(c.targetSample), c.preset, c.key, static_cast<int>(c.type));
    }
#endif
    if (!fresh.empty())
    {
        scheduled_.insert(scheduled_.end(), fresh.begin(), fresh.end());
        std::stable_sort(scheduled_.begin() + static_cast<std::ptrdiff_t>(schedPos_), scheduled_.end(),
                         [](const VoiceCommand& a, const VoiceCommand& b) {
                             return a.targetSample < b.targetSample;
                         });
    }

    renderScheduled(buffer);

    // Output peak for the VU meter.
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = std::max(peak, buffer.getMagnitude(ch, 0, numSamples));
    outputLevel_.store(std::clamp(peak, 0.0f, 1.0f), std::memory_order_relaxed);

    hostSample_ += numSamples;
}

juce::AudioProcessorEditor* PlectroProcessor::createEditor()
{
    return makeEditor(*this);
}

// Map a host track/instrument name to the SF2 bank of the matching plucked-string instrument.
// Returns -1 for an unrecognised name, so the current instrument is kept.
static int bankForInstrumentName(const juce::String& rawName)
{
    const juce::String n = rawName.toLowerCase();
    if (n.contains("bandurr"))
        return 0;
    if (n.contains("mandolin"))
        return 20;
    if (n.contains("laud") || n.contains(juce::String::fromUTF8("la\xc3\xba" "d"))) // laud / laúd
        return 10;
    return -1;
}

void PlectroProcessor::updateTrackProperties(const TrackProperties& properties)
{
    hostTrackName_ = properties.name; // keep it for the editor, even if it does not map to a bank

    // A restored project owns the instrument choice: the host re-sends the track name on reload, so
    // auto-mapping here would clobber the selection that setStateInformation just restored.
    if (instrumentPinned_)
        return;

    const int bank = bankForInstrumentName(properties.name);
    if (bank < 0)
        return; // unknown track name: leave the instrument as the user set it

    if (auto* p = apvts_.getParameter(pid::instrumentBank))
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(bank)));
}

juce::String PlectroProcessor::bundledSoundFontPath() const
{
    // The plugin binary lives at <bundle>/Contents/<arch>/<binary>; resources are at
    // <bundle>/Contents/Resources. Resolve the edition SF2 relative to the running module.
    auto module = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    return module.getParentDirectory()          // Contents/<arch>
                 .getParentDirectory()          // Contents
                 .getChildFile("Resources")
                 .getChildFile(kEdition.bundledSf2)
                 .getFullPathName();
}

void PlectroProcessor::setSoundFontPath(const juce::String& path)
{
    sf2Path_ = path;
    engine_.prepare(sampleRate_);
    reloadSoundFont();
    lastSelection_ = INT_MIN; // the layers reference banks from the new SF2: reconfigure next block
}

void PlectroProcessor::useBundledSoundFont()
{
    setSoundFontPath(bundledSoundFontPath());
}

void PlectroProcessor::reloadSoundFont()
{
    // A stale saved path (a renamed or moved asset, an SF2 from another machine) must not leave the
    // instrument silent: fall back to the bundled font. sf2Path_ ends up as whatever actually loaded.
    if (! juce::File(sf2Path_).existsAsFile())
        sf2Path_ = bundledSoundFontPath();
    engine_.loadSoundFont(sf2Path_.toStdString());
    rebuildPresetMap();
    rebuildFamilyBanks();
}

void PlectroProcessor::rebuildFamilyBanks()
{
    familyBankCount_ = { 0, 0, 0 };
    const auto all = engine_.listPresets();
    // For each family, list the SF2 banks present in its range (a bank exists if it has preset 0),
    // ascending. This is the membership of the "All <family>" ensembles.
    for (int fam = 0; fam < 3; ++fam)
    {
        const auto range = rangeOfFamily(static_cast<Family>(fam + 1)); // Bandurria=1, Laud=2, Mandolina=3
        for (int bank = range.lo; bank <= range.hi; ++bank)
        {
            bool present = false;
            for (const auto& pr : all)
                if (pr.bank == bank && pr.preset == 0) { present = true; break; }
            auto& count = familyBankCount_[static_cast<std::size_t>(fam)];
            if (present && count < 10)
                familyBanks_[static_cast<std::size_t>(fam)][static_cast<std::size_t>(count++)] = bank;
        }
    }
}

void PlectroProcessor::pushToLayers(const NoteEvent& e)
{
    for (int i = 0; i < activeLayers_; ++i)
        layers_[static_cast<std::size_t>(i)].stream.push(e);
}

void PlectroProcessor::closeLayersOnBoundary(std::int64_t atSample)
{
    // A span (tremolo) boundary: close every layer's open voices so the next span starts fresh.
    std::vector<VoiceCommand> offs;
    for (int i = 0; i < activeLayers_; ++i)
    {
        std::vector<VoiceCommand> tmp;
        layers_[static_cast<std::size_t>(i)].stream.forceCloseAll(atSample, tmp);
        applyLayerOffsetGain(tmp, i, layers_[static_cast<std::size_t>(i)].gainMul, layers_[static_cast<std::size_t>(i)].pan, kVoiceStride);
        offs.insert(offs.end(), tmp.begin(), tmp.end());
    }
    if (!offs.empty())
    {
        scheduled_.insert(scheduled_.end(), offs.begin(), offs.end());
        std::stable_sort(scheduled_.begin() + static_cast<std::ptrdiff_t>(schedPos_), scheduled_.end(),
                         [](const VoiceCommand& a, const VoiceCommand& b) { return a.targetSample < b.targetSample; });
    }
}

void PlectroProcessor::reconfigureLayers(int selection, std::int64_t atSample, std::vector<VoiceCommand>& offs)
{
    // Close every currently active layer's voices at the change point so nothing hangs.
    for (int i = 0; i < activeLayers_; ++i)
    {
        std::vector<VoiceCommand> tmp;
        layers_[static_cast<std::size_t>(i)].stream.forceCloseAll(atSample, tmp);
        applyLayerOffsetGain(tmp, i, layers_[static_cast<std::size_t>(i)].gainMul, layers_[static_cast<std::size_t>(i)].pan, kVoiceStride);
        offs.insert(offs.end(), tmp.begin(), tmp.end());
    }

    // Resolve the family banks for the selection, then plan the layers.
    const bool isAll = isAllSelection(selection);
    std::vector<int> banks;
    if (isAll)
    {
        const Family f = familyForSelection(selection);
        if (f != Family::None)
        {
            const int fam = static_cast<int>(f) - 1;
            for (int k = 0; k < familyBankCount_[static_cast<std::size_t>(fam)]; ++k)
                banks.push_back(familyBanks_[static_cast<std::size_t>(fam)][static_cast<std::size_t>(k)]);
        }
        if (banks.empty())
            banks.push_back(0); // family absent from this SF2: fall back to a single bank
    }
    else
    {
        banks.push_back(selection);
    }

    const auto specs = buildLayerSet(banks, isAll);
    activeLayers_ = std::max(1, std::min(static_cast<int>(specs.size()), kMaxLayers));
    for (int i = 0; i < kMaxLayers; ++i)
        layers_[static_cast<std::size_t>(i)].stream.reset();
    for (int i = 0; i < activeLayers_; ++i)
    {
        const auto& s = specs[static_cast<std::size_t>(i)];
        layers_[static_cast<std::size_t>(i)].bank = s.bank;
        layers_[static_cast<std::size_t>(i)].neutralize = s.neutralize;
        layers_[static_cast<std::size_t>(i)].seedOffset = s.seedOffset;
        layers_[static_cast<std::size_t>(i)].gainMul = s.gainMul;
        layers_[static_cast<std::size_t>(i)].pan = s.pan;
    }
}

void PlectroProcessor::rebuildPresetMap()
{
    const auto all = engine_.listPresets();

    // Preset numbering is uniform across this SoundFont's instrument banks, so derive the map from
    // the lowest bank present (always available) and it applies to whichever bank is selected.
    int refBank = -1;
    for (const auto& pr : all)
        if (refBank < 0 || pr.bank < refBank)
            refBank = pr.bank;

    std::vector<SoundFontPreset> bank;
    for (const auto& pr : all)
        if (pr.bank == refBank)
            bank.push_back({ pr.preset, pr.name });

    const auto map = articulationPresetMap(bank);
    for (int i = 0; i < kNumArticulations; ++i)
        presetMap_[i].store(map[i], std::memory_order_relaxed);
    tremoloPickedPreset_.store(pickedTremoloPreset(bank), std::memory_order_relaxed);
}

void PlectroProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto stateTree = apvts_.copyState();
    stateTree.setProperty("sf2Path", sf2Path_, nullptr);
    if (auto xml = stateTree.createXml())
        copyXmlToBinary(*xml, destData);
}

void PlectroProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        auto tree = juce::ValueTree::fromXml(*xml);
        if (tree.isValid())
        {
            if (tree.hasProperty("sf2Path"))
            {
                sf2Path_ = tree.getProperty("sf2Path").toString();
                // A project saved with an older build may reference a since renamed or moved font;
                // fall back to the bundled one so restoring a project never leaves it silent.
                if (! juce::File(sf2Path_).existsAsFile())
                    sf2Path_ = bundledSoundFontPath();
            }
            apvts_.replaceState(tree);
            // The restored state carries the project's instrument choice; pin it so the host's
            // track-name auto-mapping cannot overwrite it when the track properties are re-sent.
            instrumentPinned_ = true;
        }
    }
}

} // namespace plectro

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new plectro::PlectroProcessor();
}
