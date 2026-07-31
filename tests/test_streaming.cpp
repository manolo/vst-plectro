#include <catch2/catch_test_macros.hpp>

#include "humanizer/StreamingHumanizer.h"

using namespace plectro;

static HumanizerParams makeParams()
{
    HumanizerParams p;
    p.sampleRate = 48000.0;
    p.detectWindowMs = 90.0; // 4320 samples
    p.jitterMs = 0.0;
    p.variationDepth = 0.0;
    p.breathingDepthMs = 0.0;
    p.pickedVelocity = 48;
    p.tremoloVelocity = 96;
    p.lookaheadSamples = 4320;
    return p;
}

static int countType(const std::vector<VoiceCommand>& c, VoiceCommandType t)
{
    int n = 0;
    for (const auto& x : c)
        if (x.type == t)
            ++n;
    return n;
}

TEST_CASE("Picked note plays immediately and keeps its full duration", "[streaming]")
{
    StreamingHumanizer s;
    auto p = makeParams();
    p.lookaheadSamples = 480; // small, immediate-attack model
    s.setParams(p);
    s.push({10000, 60, 80, true});
    s.push({34000, 60, 0, false}); // 24000-sample note
    std::vector<VoiceCommand> out;
    s.advance(100000, out);

    REQUIRE(countType(out, VoiceCommandType::NoteOn) == 1);
    REQUIRE(countType(out, VoiceCommandType::NoteOff) == 1);

    std::int64_t on = -1, off = -1;
    int preset = -1;
    for (const auto& c : out)
    {
        if (c.type == VoiceCommandType::NoteOn) { on = c.targetSample; preset = c.preset; }
        if (c.type == VoiceCommandType::NoteOff) off = c.targetSample;
    }
    REQUIRE(preset == 0);          // picked preset
    REQUIRE(on == 10000 + 480);    // onset near real time, only the small look-ahead
    REQUIRE(off - on == 24000);    // duration preserved
}

TEST_CASE("Tremolo starts with an immediate pick then a sustained tremolo voice", "[streaming]")
{
    StreamingHumanizer s;
    auto p = makeParams();
    p.lookaheadSamples = 480;
    s.setParams(p);
    for (int i = 0; i < 8; ++i)
    {
        const std::int64_t t = i * 1500;
        s.push({t, 67, 90, true});
        s.push({t + 1490, 67, 0, false});
    }
    std::vector<VoiceCommand> out;
    s.advance(200000, out);

    // One immediate picked attack (preset 0) plus one sustained tremolo (preset 1).
    int pick = 0, trem = 0;
    for (const auto& c : out)
        if (c.type == VoiceCommandType::NoteOn)
        {
            if (c.preset == 0) ++pick;
            if (c.preset == 1) ++trem;
        }
    REQUIRE(pick == 1);
    REQUIRE(trem == 1);
    REQUIRE(countType(out, VoiceCommandType::NoteOff) == 2); // pick released, tremolo released
    REQUIRE(countType(out, VoiceCommandType::SetGain) >= 4); // dynamic follows the strokes
}

TEST_CASE("Long tremolo across multiple blocks stays one voice", "[streaming]")
{
    StreamingHumanizer s;
    s.setParams(makeParams());
    std::vector<VoiceCommand> all;

    for (int i = 0; i < 5; ++i)
    {
        const std::int64_t t = i * 1500;
        s.push({t, 67, 90, true});
        s.push({t + 1490, 67, 0, false});
    }
    s.advance(5 * 1500, all); // mid-burst

    std::vector<VoiceCommand> more;
    for (int i = 5; i < 10; ++i)
    {
        const std::int64_t t = i * 1500;
        s.push({t, 67, 90, true});
        s.push({t + 1490, 67, 0, false});
    }
    s.advance(200000, more);
    all.insert(all.end(), more.begin(), more.end());

    // Exactly one sustained tremolo voice (preset 1) across the whole burst.
    int trem = 0;
    for (const auto& c : all)
        if (c.type == VoiceCommandType::NoteOn && c.preset == 1)
            ++trem;
    REQUIRE(trem == 1);
}

TEST_CASE("Explicit articulation selects its preset and overrides the tremolo detector", "[streaming][articulation]")
{
    StreamingHumanizer s;
    auto p = makeParams();
    p.lookaheadSamples = 480;
    // presetByArticulation defaults: Auto=0 Picked=0 Tremolo=1 Pizzicato=2 Harmonic=3 Mute=4.
    s.setParams(p);
    // Two fast onsets that WOULD promote to tremolo if Auto, but stamped Pizzicato.
    s.push({0, 60, 80, true, 1, Articulation::Pizzicato});
    s.push({1490, 60, 0, false, 1, Articulation::Pizzicato});
    s.push({1500, 60, 80, true, 1, Articulation::Pizzicato});
    s.push({2990, 60, 0, false, 1, Articulation::Pizzicato});
    std::vector<VoiceCommand> out;
    s.advance(200000, out);

    int pizz = 0, trem = 0;
    for (const auto& c : out)
        if (c.type == VoiceCommandType::NoteOn)
        {
            if (c.preset == 2) ++pizz;
            if (c.preset == 1) ++trem;
        }
    REQUIRE(pizz == 2);                                      // two independent pizzicato notes
    REQUIRE(trem == 0);                                      // never promoted to tremolo
    REQUIRE(countType(out, VoiceCommandType::SetGain) == 0); // detector did not run
}

TEST_CASE("Explicit tremolo keyswitch sustains one tremolo voice for repeated notes", "[streaming][articulation]")
{
    StreamingHumanizer s;
    auto p = makeParams();
    p.lookaheadSamples = 480;
    s.setParams(p);
    // A notated stem tremolo reaches the plugin as rapid repeated notes stamped Tremolo.
    for (int i = 0; i < 8; ++i)
    {
        const std::int64_t t = i * 1500;
        s.push({t, 67, 90, true, 1, Articulation::Tremolo});
        s.push({t + 1490, 67, 0, false, 1, Articulation::Tremolo});
    }
    std::vector<VoiceCommand> out;
    s.advance(200000, out);

    // A single sustained tremolo voice (tremolo preset), not eight retriggers and not a second
    // layered tremolo voice (which would phase against the first).
    REQUIRE(countType(out, VoiceCommandType::NoteOn) == 1);
    REQUIRE(countType(out, VoiceCommandType::NoteOff) == 1);
    for (const auto& c : out)
        if (c.type == VoiceCommandType::NoteOn)
            REQUIRE(c.preset == 1);                           // tremolo preset, sustained
    REQUIRE(countType(out, VoiceCommandType::SetGain) >= 4);  // sustained, follows the strokes
}

TEST_CASE("Explicit tremolo sustains one voice even when strokes are wider than the detector window",
          "[streaming][articulation]")
{
    // At a slow tempo MuseScore spaces the expanded tremolo strokes far apart, wider than the
    // detector window. An explicit tremolo keyswitch must still sustain one voice (the host has
    // told us it is a tremolo); the window only gates the Auto detector.
    StreamingHumanizer s;
    auto p = makeParams();               // detectWindowMs 90 -> 4320 samples at 48 kHz
    p.lookaheadSamples = 480;
    p.beatSamples = 48000;               // 1 beat = 1 s (slow tempo); the strokes below fit within it
    s.setParams(p);
    for (int i = 0; i < 6; ++i)
    {
        const std::int64_t t = i * 9000;  // 9000 > 4320 window, but < one beat
        s.push({t, 67, 90, true, 1, Articulation::Tremolo});
        s.push({t + 8900, 67, 0, false, 1, Articulation::Tremolo});
    }
    std::vector<VoiceCommand> out;
    s.advance(300000, out);

    // One sustained tremolo voice, not six re-triggers of the sample attack.
    REQUIRE(countType(out, VoiceCommandType::NoteOn) == 1);
    REQUIRE(countType(out, VoiceCommandType::NoteOff) == 1);
    for (const auto& c : out)
        if (c.type == VoiceCommandType::NoteOn)
            REQUIRE(c.preset == 1);                            // tremolo preset, sustained
    REQUIRE(countType(out, VoiceCommandType::SetGain) >= 4);   // strokes follow the dynamic
}

TEST_CASE("Auto articulation still promotes repeated notes to a tremolo voice", "[streaming][articulation]")
{
    StreamingHumanizer s;
    auto p = makeParams();
    p.lookaheadSamples = 480;
    s.setParams(p);
    // Default articulation is Auto: the rhythmic detector must still work.
    for (int i = 0; i < 8; ++i)
    {
        const std::int64_t t = i * 1500;
        s.push({t, 67, 90, true}); // channel/articulation default to 1/Auto
        s.push({t + 1490, 67, 0, false});
    }
    std::vector<VoiceCommand> out;
    s.advance(200000, out);

    int trem = 0;
    for (const auto& c : out)
        if (c.type == VoiceCommandType::NoteOn && c.preset == 1)
            ++trem;
    REQUIRE(trem == 1); // one sustained tremolo voice from the detector
}

TEST_CASE("Tremolo is a span articulation; point articulations are not", "[streaming][articulation][span]")
{
    // A span articulation is one the plugin renders as a single sustained gesture built from
    // many rapid sub-notes. Re-selecting it (a new span) or leaving it must restart the gesture;
    // point articulations (picked, pizzicato...) must NOT, or every repeated note would falsely
    // reset the instrument.
    REQUIRE(isSpanArticulation(Articulation::Tremolo));
    REQUIRE_FALSE(isSpanArticulation(Articulation::Auto));
    REQUIRE_FALSE(isSpanArticulation(Articulation::Picked));
    REQUIRE_FALSE(isSpanArticulation(Articulation::Pizzicato));
    REQUIRE_FALSE(isSpanArticulation(Articulation::Harmonic));
    REQUIRE_FALSE(isSpanArticulation(Articulation::Mute));
}

TEST_CASE("A keyswitch retrigger between two tremolo spans yields two separate tremolo voices",
          "[streaming][articulation][span]")
{
    StreamingHumanizer s;
    auto p = makeParams();
    p.lookaheadSamples = 480;
    s.setParams(p);

    auto pushSpan = [&](std::int64_t base) {
        for (int i = 0; i < 6; ++i)
        {
            const std::int64_t t = base + i * 1500;
            s.push({t, 67, 90, true, 1, Articulation::Tremolo});
            s.push({t + 1490, 67, 0, false, 1, Articulation::Tremolo});
        }
    };

    std::vector<VoiceCommand> out;

    // Span 1: a notated tremolo arrives as rapid repeated notes; the tremolo voice stays open.
    pushSpan(0);
    const std::int64_t span1End = 5 * 1500;
    s.advance(span1End, out);

    // The host re-sends the tremolo keyswitch to mark a new span. Because tremolo is a span
    // articulation, the plugin restarts the gesture instead of extending the current one.
    const Articulation active = Articulation::Tremolo;
    if (isSpanArticulation(active))
        s.forceCloseAll(span1End, out);

    // Span 2 begins right after span 1, close enough that WITHOUT the restart it would merge
    // into the first tremolo voice.
    pushSpan(6 * 1500);
    s.advance(200000, out);

    int tremOns = 0;
    for (const auto& c : out)
        if (c.type == VoiceCommandType::NoteOn && c.preset == 1)
            ++tremOns;
    REQUIRE(tremOns == 2); // two independent tremolo spans, not one merged sustain
}

TEST_CASE("With detection off, Auto rapid repeats stay discrete (no tremolo promotion)", "[streaming][articulation]")
{
    // When a keyswitch-aware host drives articulations, the plugin turns the rhythmic detector off
    // (enableDetection == false). Rapid repeated notes, as an ornament expansion produces, must then
    // stay discrete picked notes instead of merging into a false sustained tremolo.
    StreamingHumanizer s;
    auto p = makeParams();
    p.lookaheadSamples = 480;
    p.enableDetection = false;
    s.setParams(p);
    for (int i = 0; i < 8; ++i)
    {
        const std::int64_t t = i * 1500;
        s.push({t, 67, 90, true}); // Auto
        s.push({t + 1490, 67, 0, false});
    }
    std::vector<VoiceCommand> out;
    s.advance(200000, out);

    int trem = 0;
    for (const auto& c : out)
        if (c.type == VoiceCommandType::NoteOn && c.preset == 1)
            ++trem;
    REQUIRE(trem == 0);                                      // never promoted to a tremolo voice
    REQUIRE(countType(out, VoiceCommandType::NoteOn) == 8);  // eight discrete picked notes
}

TEST_CASE("Detune is deterministic, decorrelates per instance, and zero when off", "[streaming]")
{
    auto firstOnDetune = [](std::uint64_t instanceSeed, double cents) {
        StreamingHumanizer s;
        auto p = makeParams();
        p.detuneCents = cents;
        p.instanceSeed = instanceSeed;
        s.setParams(p);
        s.push({10000, 60, 80, true});
        s.push({34000, 60, 0, false});
        std::vector<VoiceCommand> out;
        s.advance(100000, out);
        for (const auto& c : out)
            if (c.type == VoiceCommandType::NoteOn)
                return c.detuneCents;
        return 0.0f;
    };
    REQUIRE(firstOnDetune(0, 8.0) == firstOnDetune(0, 8.0));   // deterministic
    REQUIRE(firstOnDetune(0, 8.0) != firstOnDetune(7, 8.0));   // per-instance offset differs
    REQUIRE(firstOnDetune(0, 0.0) == 0.0f);                     // no detune when amount is 0
}

TEST_CASE("Streaming output is deterministic", "[streaming]")
{
    auto run = []() {
        StreamingHumanizer s;
        auto p = makeParams();
        p.jitterMs = 8.0;
        p.variationDepth = 0.1;
        s.setParams(p);
        for (int i = 0; i < 4; ++i)
        {
            const std::int64_t t = 20000 + i * 30000;
            s.push({t, 62, 70, true});
            s.push({t + 15000, 62, 0, false});
        }
        std::vector<VoiceCommand> o;
        s.advance(300000, o);
        return o;
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        REQUIRE(a[i].targetSample == b[i].targetSample);
        REQUIRE(a[i].gain == b[i].gain);
    }
}
