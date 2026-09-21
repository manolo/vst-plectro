// Pure decision for whether the rhythmic tremolo detector should run. JUCE-free so it can be
// unit tested in isolation from the processor.
#pragma once

namespace plectro {

// After a keyswitch-aware host queries IKeyswitchController we suppress the rhythmic detector,
// because such a host notates tremolo explicitly and emits ornament expansions as rapid repeated
// notes that would misfire the detector. But a query alone must not disable detection forever: a
// host may read our map to build an expression map yet have the user place no keyswitch at all.
// Once that many musical notes have played after a query with still no keyswitch, we treat the host
// as query-only and let auto detection resume.
inline constexpr int kQueryGraceNotes = 8;

// Whether the rhythmic tremolo detector should run this block.
//   - Off if the master tremolo toggle is off.
//   - Off once an actual keyswitch has been seen: the host drives articulations explicitly.
//   - Off right after a host queries our keyswitches, but only until kQueryGraceNotes musical notes
//     have played with no keyswitch; after that a query-only host gets its auto detection back, so a
//     bare query never leaves the plugin without detection indefinitely.
inline bool shouldAutoDetect(bool tremoloEnabled,
                             bool keyswitchSeen,
                             bool hostQueriedKeyswitches,
                             int musicalNotesSinceStart,
                             int graceNotes = kQueryGraceNotes)
{
    if (!tremoloEnabled)
        return false;
    if (keyswitchSeen)
        return false;
    if (hostQueriedKeyswitches && musicalNotesSinceStart < graceNotes)
        return false;
    return true;
}

} // namespace plectro
