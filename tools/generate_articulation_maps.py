#!/usr/bin/env python3
"""Generate every derived articulation artifact from the single source of truth.

Source of truth:
    articulations/plectro-articulations.json

Generated (never hand-edit these):
    source/core/KeyswitchLayout.h        C++ keyswitch layout used by the plugin
    generated/logic/Plectro.plist        Logic Pro Articulation Set
    generated/cubase/Plectro.expressionmap   Cubase VST Expression Map (XML)
    generated/dorico/Plectro.doricolib   Dorico library with an Expression Map (JSON)
    generated/generic/Plectro-articulations.json   Neutral public map for any tool
    docs/articulations.md                Human documentation and compatibility matrix

Usage:
    python3 tools/generate_articulation_maps.py            # write all files
    python3 tools/generate_articulation_maps.py --check     # verify up to date + valid

--check regenerates every file in memory, compares it byte for byte against what is on
disk, and parses each generated file to confirm it is syntactically valid. It exits
non-zero if any file is stale or invalid, so CI can gate on it. Generation is
deterministic: no timestamps, no randomness, stable ordering.
"""

import argparse
import io
import json
import plistlib
import sys
import xml.dom.minidom as minidom
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SOURCE = REPO_ROOT / "articulations" / "plectro-articulations.json"

# Internal articulation name (JSON) -> C++ Articulation enumerator.
ARTICULATION_ENUM = {
    "Picked": "Articulation::Picked",
    "Tremolo": "Articulation::Tremolo",
    "Pizzicato": "Articulation::Pizzicato",
    "Harmonic": "Articulation::Harmonic",
    "Mute": "Articulation::Mute",
}

BEHAVIOR_ENUM = {
    "Latching": "KeyswitchBehavior::Latching",
    "Momentary": "KeyswitchBehavior::Momentary",
    "Span": "KeyswitchBehavior::Span",
    "Modifier": "KeyswitchBehavior::Modifier",
}

CATEGORY_ENUM = {
    "base": "ArticulationCategory::Base",
    "complementary": "ArticulationCategory::Complementary",
}


def load_source():
    data = json.loads(SOURCE.read_text(encoding="utf-8"))
    arts = data["articulations"]
    _validate_source(data, arts)
    return data, arts


def _validate_source(data, arts):
    """Fail loudly on a malformed source of truth before generating anything."""
    notes, ids = set(), set()
    for a in arts:
        for field in ("note", "id", "name", "shortTitle", "articulation",
                      "behavior", "needsNoteOff", "category"):
            if field not in a:
                raise SystemExit(f"source: articulation {a} is missing '{field}'")
        if not a["name"]:
            raise SystemExit(f"source: articulation on note {a['note']} has an empty name")
        if a["note"] in notes:
            raise SystemExit(f"source: duplicate keyswitch note {a['note']}")
        if a["id"] in ids:
            raise SystemExit(f"source: duplicate id '{a['id']}'")
        if a["articulation"] not in ARTICULATION_ENUM:
            raise SystemExit(f"source: unknown articulation '{a['articulation']}'")
        if a["behavior"] not in BEHAVIOR_ENUM:
            raise SystemExit(f"source: unknown behavior '{a['behavior']}'")
        if a["category"] not in CATEGORY_ENUM:
            raise SystemExit(f"source: unknown category '{a['category']}'")
        notes.add(a["note"])
        ids.add(a["id"])
    zone = data["keyswitchZone"]
    for a in arts:
        if not (zone["base"] <= a["note"] <= zone["top"]):
            raise SystemExit(
                f"source: note {a['note']} falls outside the keyswitch zone "
                f"[{zone['base']}, {zone['top']}]")


# --------------------------------------------------------------------------- C++

def gen_cpp_header(data, arts):
    lines = []
    lines.append("// GENERATED FILE - do not edit by hand.")
    lines.append("// Source of truth: articulations/plectro-articulations.json")
    lines.append("// Regenerate with: python3 tools/generate_articulation_maps.py")
    lines.append("//")
    lines.append("// Single source of truth for Plectro's keyswitch layout: the keyswitch MIDI note, the exact")
    lines.append("// MuseScore articulation name advertised via VST3 IKeyswitchController (so the host matches it")
    lines.append("// without heuristics), and the internal articulation it selects. Both the IKeyswitchController")
    lines.append("// advertiser (KeyswitchSupport.h) and the note decoder (articulationForKeyswitch) derive from this")
    lines.append("// table, so titles and note decoding cannot drift apart. JUCE-free so it can be unit tested.")
    lines.append("//")
    lines.append("// The four tremolo subdivisions are advertised as distinct keyswitches because MuseScore notates")
    lines.append("// them distinctly; Plectro collapses them to the same internal tremolo.")
    lines.append("#pragma once")
    lines.append("")
    lines.append('#include "Types.h" // Articulation')
    lines.append("")
    lines.append("#include <array>")
    lines.append("#include <string_view>")
    lines.append("")
    lines.append("namespace plectro {")
    lines.append("")
    lines.append("// How a keyswitch is delivered and held (see articulations/plectro-articulations.json):")
    lines.append("//   Latching  - a single Note On selects it and it holds until another keyswitch changes it.")
    lines.append("//   Momentary - active only while held (Note On..Note Off). Reserved; none ship today.")
    lines.append("//   Span      - held across a range of notes. Reserved for future range techniques.")
    lines.append("//   Modifier  - layers over the current timbre instead of replacing it (Legato).")
    lines.append("enum class KeyswitchBehavior { Latching, Momentary, Span, Modifier };")
    lines.append("")
    lines.append("// A base timbre versus a complementary modifier layered over one.")
    lines.append("enum class ArticulationCategory { Base, Complementary };")
    lines.append("")
    lines.append("struct KeyswitchDef")
    lines.append("{")
    lines.append("    int note;                      // keyswitch MIDI note (in the reserved low zone)")
    lines.append("    const char* id;                // stable internal identifier")
    lines.append("    const char* name;              // exact MuseScore mpe::ArticulationType name (host matches this)")
    lines.append("    const char* shortTitle;        // abbreviated label for host displays")
    lines.append("    Articulation articulation;     // internal articulation this keyswitch selects")
    lines.append("    KeyswitchBehavior behavior;    // how the keyswitch is held")
    lines.append("    bool needsNoteOff;             // host must send a Note Off (modifiers/spans)")
    lines.append("    ArticulationCategory category; // base timbre vs complementary modifier")
    lines.append("};")
    lines.append("")
    lines.append(f"inline constexpr std::array<KeyswitchDef, {len(arts)}> kKeyswitchLayout = {{ {{")
    for a in arts:
        lines.append(
            "    {{ {note}, {id}, {name}, {short}, {art}, {beh}, {noteoff}, {cat} }},".format(
                note=a["note"],
                id=cpp_str(a["id"]),
                name=cpp_str(a["name"]),
                short=cpp_str(a["shortTitle"]),
                art=ARTICULATION_ENUM[a["articulation"]],
                beh=BEHAVIOR_ENUM[a["behavior"]],
                noteoff="true" if a["needsNoteOff"] else "false",
                cat=CATEGORY_ENUM[a["category"]],
            ))
    lines.append("} };")
    lines.append("")
    lines.append("// Decode a keyswitch-zone MIDI note to the articulation it selects. A note outside the layout")
    lines.append('// falls back to Picked (normal), matching "unmapped keyswitch = back to normal".')
    lines.append("inline Articulation articulationForKeyswitchNote(int note)")
    lines.append("{")
    lines.append("    for (const auto& k : kKeyswitchLayout)")
    lines.append("        if (k.note == note)")
    lines.append("            return k.articulation;")
    lines.append("    return Articulation::Picked;")
    lines.append("}")
    lines.append("")
    lines.append("// Whether the keyswitch at this note is the Trill keyswitch. Trills are rendered as one sustained")
    lines.append("// tremolo on the main note, so the processor detects this note to drop the alternating upper note.")
    lines.append("inline bool keyswitchNoteIsTrill(int note)")
    lines.append("{")
    lines.append("    for (const auto& k : kKeyswitchLayout)")
    lines.append("        if (k.note == note)")
    lines.append('            return std::string_view(k.id) == "trill";')
    lines.append("    return false;")
    lines.append("}")
    lines.append("")
    lines.append("// Whether the keyswitch at this note is the Legato modifier. The processor uses it to arm the")
    lines.append("// per-channel legato latch (Trem vs P+T for tremolo), independently of the articulation latch.")
    lines.append("inline bool keyswitchNoteIsLegato(int note)")
    lines.append("{")
    lines.append("    for (const auto& k : kKeyswitchLayout)")
    lines.append("        if (k.note == note)")
    lines.append('            return std::string_view(k.id) == "legato";')
    lines.append("    return false;")
    lines.append("}")
    lines.append("")
    lines.append("} // namespace plectro")
    lines.append("")
    return "\n".join(lines)


def cpp_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


# ------------------------------------------------------------------------- Logic

# Logic Pro Articulation Set (.plist). Each articulation carries an Output block that
# Logic emits when the articulation is selected: a Note On of the keyswitch note at full
# velocity. Logic's plist schema is not officially published; this follows the community
# documented structure and is validated as a well formed plist.
LOGIC_OUTPUT_TYPE_NOTE_ON = 3


def gen_logic_plist(data, arts):
    articulations = []
    outputs = []
    for i, a in enumerate(arts):
        art_id = i + 1  # Logic articulation IDs are 1 based
        articulations.append({
            "ID": art_id,
            "Name": a["name"],
            "Symbol": 0,
        })
        outputs.append({
            "ID": art_id,
            "Type": LOGIC_OUTPUT_TYPE_NOTE_ON,
            "Value1": a["note"],
            "Value2": 127,
        })
    root = {
        "Name": data["name"],
        "Articulations": articulations,
        "Output": outputs,
    }
    return plistlib.dumps(root, fmt=plistlib.FMT_XML, sort_keys=False).decode("utf-8")


# ------------------------------------------------------------------------ Cubase

# Cubase VST Expression Map (.expressionmap). One PSlot per articulation; selecting a
# slot fires a PSoundAction (Note On of the keyswitch note at full velocity). Cubase can
# also auto discover these keyswitches via IKeyswitchController; this file is a ready made
# alternative for users who want fixed slots. Validated as well formed XML.
def gen_cubase_expressionmap(data, arts):
    doc = minidom.Document()
    root = doc.createElement("InstrumentMap")
    doc.appendChild(root)

    root.appendChild(_x_string(doc, "name", data["name"]))

    slots = doc.createElement("list")
    slots.setAttribute("name", "slots")
    slots.setAttribute("type", "obj")
    root.appendChild(slots)

    uid = 1000000000
    for i, a in enumerate(arts):
        pslot = _x_obj(doc, "PSlot", uid); uid += 1
        pslot.appendChild(_x_int(doc, "status", 0))

        sound_list = doc.createElement("list")
        sound_list.setAttribute("name", "pSoundSlot")
        sound_list.setAttribute("type", "obj")
        pslot.appendChild(sound_list)

        psound = _x_obj(doc, "PSoundSlot", uid); uid += 1

        # conditions: the articulation symbol this slot represents
        cond_member = doc.createElement("member")
        cond_member.setAttribute("name", "conditions")
        cond_list = doc.createElement("list")
        cond_list.setAttribute("name", "conditions")
        cond_list.setAttribute("type", "obj")
        partdef = _x_obj(doc, "PArtDef", uid); uid += 1
        partdef.appendChild(_x_int(doc, "type", 0))
        partdef.appendChild(_x_int(doc, "group", 0))
        partdef.appendChild(_x_int(doc, "art", i))
        partdef.appendChild(_x_int(doc, "ch", -1))
        cond_list.appendChild(partdef)
        cond_member.appendChild(cond_list)
        psound.appendChild(cond_member)

        # actions: emit the keyswitch note on
        act_member = doc.createElement("member")
        act_member.setAttribute("name", "actions")
        act_list = doc.createElement("list")
        act_list.setAttribute("name", "actions")
        act_list.setAttribute("type", "obj")
        action = _x_obj(doc, "PSoundAction", uid); uid += 1
        action.appendChild(_x_int(doc, "type", 0))
        action.appendChild(_x_int(doc, "data1", a["note"]))
        action.appendChild(_x_float(doc, "data2", 1))
        action.appendChild(_x_int(doc, "channel", 0))
        act_list.appendChild(action)
        act_member.appendChild(act_list)
        psound.appendChild(act_member)

        psound.appendChild(_x_string(doc, "name", a["name"]))
        psound.appendChild(_x_int(doc, "color", 0))
        psound.appendChild(_x_int(doc, "minPitch", 0))
        psound.appendChild(_x_int(doc, "maxPitch", 127))
        psound.appendChild(_x_int(doc, "minVelocity", 0))
        psound.appendChild(_x_int(doc, "maxVelocity", 127))

        sound_list.appendChild(psound)
        slots.appendChild(pslot)

    xml = doc.toprettyxml(indent="  ", encoding="utf-8").decode("utf-8")
    # Stable, no trailing whitespace lines.
    xml = "\n".join(line for line in xml.splitlines() if line.strip() != "")
    return xml + "\n"


def _x_obj(doc, cls, uid):
    o = doc.createElement("obj")
    o.setAttribute("class", cls)
    o.setAttribute("ID", str(uid))
    return o


def _x_int(doc, name, value):
    e = doc.createElement("int")
    e.setAttribute("name", name)
    e.setAttribute("value", str(value))
    return e


def _x_float(doc, name, value):
    e = doc.createElement("float")
    e.setAttribute("name", name)
    e.setAttribute("value", str(value))
    return e


def _x_string(doc, name, value):
    e = doc.createElement("string")
    e.setAttribute("name", name)
    e.setAttribute("value", value)
    e.setAttribute("wide", "true")
    return e


# ------------------------------------------------------------------------ Dorico

# Dorico library (.doricolib) carrying an Expression Map. Dorico libraries are JSON.
# Dorico can also import the Cubase .expressionmap; this file expresses the same map in
# Dorico's own vocabulary (playing techniques mapped to the keyswitch notes). Validated as
# syntactically valid JSON. Playing technique ids follow Dorico's built in vocabulary
# where one exists; unmatched articulations reuse the natural technique.
DORICO_TECHNIQUE = {
    "standard": "pt.natural",
    "pizzicato": "pt.pizzicato",
    "snap_pizzicato": "pt.snappizzicato",
    "random_pizzicato": "pt.pizzicato",
    "harmonic": "pt.harmonic",
    "mute": "pt.mute",
    "palm_mute": "pt.mute",
    "tremolo_8th": "pt.tremolo",
    "tremolo_16th": "pt.tremolo",
    "tremolo_32nd": "pt.tremolo",
    "tremolo_64th": "pt.tremolo",
    "trill": "pt.trill",
    "legato": "pt.legato",
}


def gen_dorico_doricolib(data, arts):
    switches = []
    for a in arts:
        switches.append({
            "techniques": [
                {"type": "kAddTechnique", "id": DORICO_TECHNIQUE.get(a["id"], "pt.natural")}
            ],
            "actions": [
                {
                    "type": "kNoteOn",
                    "data": {"note": a["note"], "velocity": 127, "channel": 0},
                }
            ],
            "name": a["name"],
        })
    doc = {
        "com.steinberg.doric.libraryexport": {
            "expressionMaps": {
                "expressionMap": [
                    {
                        "name": data["name"],
                        "id": "plectro.expressionmap",
                        "description": f"{data['name']} articulation keyswitches by {data['vendor']}.",
                        "switches": switches,
                    }
                ]
            }
        }
    }
    return json.dumps(doc, indent=2, ensure_ascii=False) + "\n"


# ----------------------------------------------------------------------- Generic

def gen_generic_json(data, arts):
    doc = {
        "name": data["name"],
        "vendor": data["vendor"],
        "version": data["version"],
        "keyswitchZone": {"base": data["keyswitchZone"]["base"],
                          "top": data["keyswitchZone"]["top"]},
        "articulations": [
            {
                "note": a["note"],
                "id": a["id"],
                "name": a["name"],
                "shortTitle": a["shortTitle"],
                "articulation": a["articulation"],
                "behavior": a["behavior"],
                "needsNoteOff": a["needsNoteOff"],
                "category": a["category"],
                "canCombineWith": a.get("canCombineWith", []),
            }
            for a in arts
        ],
    }
    return json.dumps(doc, indent=2, ensure_ascii=False) + "\n"


# -------------------------------------------------------------------------- Docs

def note_label(n):
    """Label a MIDI note in the C-1 = 0 convention (Cubase/MuseScore/Dorico)."""
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    return f"{names[n % 12]}{n // 12 - 1}"


def gen_docs_markdown(data, arts):
    z = data["keyswitchZone"]
    L = []
    L.append("# Plectro articulations and keyswitches")
    L.append("")
    L.append("<!-- GENERATED FILE - do not edit by hand. -->")
    L.append("<!-- Source of truth: articulations/plectro-articulations.json -->")
    L.append("<!-- Regenerate with: python3 tools/generate_articulation_maps.py -->")
    L.append("")
    L.append("Plectro is a VST3 and Audio Unit instrument. It selects its playing techniques")
    L.append("(pizzicato, tremolo, harmonic, mute, legato) through **keyswitches**: low MIDI notes")
    L.append("that pick an articulation instead of sounding a pitch.")
    L.append("")
    L.append("## What a keyswitch is")
    L.append("")
    L.append("A keyswitch is a MIDI note reserved to switch articulation rather than play a note.")
    L.append("When Plectro receives a note in its reserved low zone (MIDI "
             f"{z['base']} to {z['top']}), it does not sound; it selects the mapped articulation, and every")
    L.append("following musical note plays with that articulation until another keyswitch changes it.")
    L.append("")
    L.append("**There is no universal keyswitch standard.** Different libraries put different")
    L.append("articulations on different notes. Plectro publishes one stable map (below) and never")
    L.append("changes the note numbers, so saved projects keep working. Plectro is a self contained")
    L.append("instrument, not a Kontakt library, so it needs no external sample engine.")
    L.append("")
    L.append("## The map")
    L.append("")
    L.append("The MIDI **number** is the reference. Hosts label the same number differently by octave")
    L.append("convention: MIDI 0 shows as **C-1** in Cubase, Dorico and MuseScore, and as **C-2** in")
    L.append("Logic Pro and GarageBand. Always trust the number.")
    L.append("")
    L.append("| MIDI | Note (C-1=0) | Articulation | Internal | Behavior | Note Off | Category |")
    L.append("|-----:|:-------------|:-------------|:---------|:---------|:--------:|:---------|")
    for a in arts:
        L.append("| {n} | {lbl} | {name} | {internal} | {beh} | {noff} | {cat} |".format(
            n=a["note"],
            lbl=note_label(a["note"]),
            name=a["name"],
            internal=a["articulation"],
            beh=a["behavior"],
            noff="yes" if a["needsNoteOff"] else "no",
            cat=a["category"],
        ))
    L.append("")
    L.append("Legato (MIDI 12) is a **modifier**: it is held between its Note On and Note Off and")
    L.append("layers over whichever timbre is active (it does not replace it). Every other keyswitch")
    L.append("**latches**: one Note On selects it, no Note Off, held until the next keyswitch.")
    L.append("")
    L.append("## Host integration tiers")
    L.append("")
    L.append("| Tier | How articulations reach Plectro | Hosts |")
    L.append("|:-----|:--------------------------------|:------|")
    L.append("| A. Auto discovery | Host queries the plugin over VST3 `IKeyswitchController` and builds its own mapping | Cubase, Dorico, MuseScore (fork) |")
    L.append("| B. Official file | Host imports a map file Plectro ships | Logic Pro (`.plist`), Cubase (`.expressionmap`), Dorico (`.doricolib`) |")
    L.append("| C. Manual MIDI | User places the keyswitch notes by hand | GarageBand, Ableton Live, simple AU/VST3 hosts |")
    L.append("")
    L.append("## Per host compatibility")
    L.append("")
    L.append("| Host | Format | Discovery | Articulations | Legato | Official artifact |")
    L.append("|:-----|:-------|:----------|:--------------|:-------|:------------------|")
    L.append("| Cubase | VST3 | Yes (`IKeyswitchController`) | Yes | Manual note | `generated/cubase/Plectro.expressionmap` (optional) |")
    L.append("| Dorico | VST3 | Yes (`IKeyswitchController`) | Yes | Playing technique | `generated/dorico/Plectro.doricolib` or import the Cubase map |")
    L.append("| Logic Pro | Audio Unit | No | Yes (Articulation Set) | Manual, see note | `generated/logic/Plectro.plist` |")
    L.append("| GarageBand (macOS) | Audio Unit | No | Manual MIDI notes | Manual note | none (manual) |")
    L.append("| GarageBand (iOS) | AUv3 | n/a | n/a | n/a | not supported yet (future work) |")
    L.append("| MuseScore (fork) | VST3 | Yes (`IKeyswitchController`) | Yes | Slur span | none needed |")
    L.append("| Studio One | VST3 | Yes (Sound Variations read KS) | Yes | Manual note | none (manual) |")
    L.append("| REAPER | VST3/AU | No | Manual / Reaticulate | Manual note | none (manual) |")
    L.append("| Ableton Live | VST3/AU | No | Manual MIDI notes | Manual note | none (manual) |")
    L.append("")
    L.append("## Installing the map files")
    L.append("")
    L.append("**Logic Pro** (`generated/logic/Plectro.plist`): copy to")
    L.append("`~/Music/Audio Music Apps/Articulation Settings/`, then on the Plectro track open the")
    L.append("Track inspector and pick **Plectro** under Articulation Set. Selecting an articulation")
    L.append("sends its keyswitch note. Logic labels MIDI 0 as C-2.")
    L.append("")
    L.append("**Cubase** (`generated/cubase/Plectro.expressionmap`): copy to")
    L.append("`~/Documents/Steinberg/Cubase/Expression Maps/`, then in the Inspector Expression Map")
    L.append("field choose **Load Expression Map**. Cubase can also build the map itself from the")
    L.append("plugin's advertised keyswitches, so this file is optional.")
    L.append("")
    L.append("**Dorico** (`generated/dorico/Plectro.doricolib`): Library > Library Manager >")
    L.append("import, or double click the file. Alternatively import the Cubase expression map from")
    L.append("Play > Expression Maps. Dorico can also read the plugin's advertised keyswitches.")
    L.append("")
    L.append("**GarageBand / Ableton / REAPER / others**: no articulation file. Place the keyswitch")
    L.append("notes from the map above on the track just before the notes they affect. In pianoroll")
    L.append("hosts, draw the keyswitch note (for example MIDI 7 for Tremolo8th) a little before the")
    L.append("passage; it will not sound, it only switches the articulation.")
    L.append("")
    L.append("## Legato limitations by host")
    L.append("")
    L.append("Legato is a held modifier (Note On at the slur start, Note Off at the end). Over VST3")
    L.append("`IKeyswitchController` it is advertised as a held key-range (`kKeyRangeTypeID`) while the")
    L.append("other techniques are press-before switches (`kNoteOnKeyswitchTypeID`), so an auto")
    L.append("discovery host holds it across the passage. Auto discovery hosts and the MuseScore fork")
    L.append("drive it from the score's slurs. In file based")
    L.append("hosts you must hold the Legato keyswitch (MIDI 12) down across the slurred notes and")
    L.append("release it at the end. Logic Articulation Sets model momentary switches awkwardly, so")
    L.append("legato in Logic is best done by holding the manual keyswitch note. Slur aware tremolo")
    L.append("is a Pro edition feature; the free edition always uses the pick plus tremolo attack.")
    L.append("")
    L.append("## Manual smoke test")
    L.append("")
    L.append("On any host, play a sustained note while placing each keyswitch just before it and")
    L.append("confirm the timbre changes:")
    L.append("")
    L.append("- **Standard** (MIDI 0): normal picked tone.")
    L.append("- **Pizzicato** (MIDI 1): short plucked tone.")
    L.append("- **Tremolo8th** (MIDI 7): one sustained tremolo, not a machine gun of repeats.")
    L.append("- **Trill** (MIDI 11): a sustained tremolo roll on the written pitch.")
    L.append("- **Legato** (MIDI 12): hold it across two slurred notes; the second connects without a")
    L.append("  fresh attack (Pro edition).")
    L.append("")
    L.append("## GarageBand for iOS")
    L.append("")
    L.append("The current Audio Unit is a macOS AU (v2/v3 for macOS). GarageBand for iOS requires an")
    L.append("**iOS AUv3** build, a different target and packaging. It is not included here and is")
    L.append("tracked as future work. macOS AU support does not imply iOS support.")
    L.append("")
    return "\n".join(L)


# ------------------------------------------------------------------------ Driver

def build_all():
    """Return {relative_path: (text, kind)} for every generated file."""
    data, arts = load_source()
    return {
        "source/core/KeyswitchLayout.h": (gen_cpp_header(data, arts), "cpp"),
        "generated/logic/Plectro.plist": (gen_logic_plist(data, arts), "plist"),
        "generated/cubase/Plectro.expressionmap": (gen_cubase_expressionmap(data, arts), "xml"),
        "generated/dorico/Plectro.doricolib": (gen_dorico_doricolib(data, arts), "json"),
        "generated/generic/Plectro-articulations.json": (gen_generic_json(data, arts), "json"),
        "docs/articulations.md": (gen_docs_markdown(data, arts), "md"),
    }


def validate(kind, text, rel):
    """Parse the generated text to confirm it is syntactically valid."""
    try:
        if kind == "plist":
            plistlib.loads(text.encode("utf-8"))
        elif kind == "xml":
            minidom.parseString(text)
        elif kind == "json":
            json.loads(text)
        # cpp and md have no cheap parser; the C++ build and tests cover the header.
    except Exception as exc:  # noqa: BLE001
        raise SystemExit(f"invalid generated {kind} for {rel}: {exc}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="verify generated files are up to date and valid; exit non-zero if not")
    args = ap.parse_args()

    files = build_all()
    stale = []
    for rel, (text, kind) in files.items():
        validate(kind, text, rel)
        path = REPO_ROOT / rel
        if args.check:
            current = path.read_text(encoding="utf-8") if path.exists() else None
            if current != text:
                stale.append(rel)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")

    if args.check:
        if stale:
            print("Out of date (run: python3 tools/generate_articulation_maps.py):", file=sys.stderr)
            for rel in stale:
                print(f"  {rel}", file=sys.stderr)
            return 1
        print("All articulation artifacts are up to date and valid.")
        return 0

    for rel in files:
        print(f"wrote {rel}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
