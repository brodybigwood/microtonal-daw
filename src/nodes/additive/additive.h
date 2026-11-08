#pragma once

#include "Node.h"
#include <array>

// Polyphonic additive sine instrument. Each note is a sum of kPartials sinusoids:
// frequency = base * ratio, phase offset = phase (0..1 of a turn), amplitude = amp.
// Base pitch comes from the note (full fractional pitch) or, in length mode, from
// the note's beat length reinterpreted as an exponent: pitch = 69 + 12 * beats.
// Unison stacks up to kMaxUnison slightly detuned copies per note for analog weight.
class AdditiveNode : public Node {
    public:
        static constexpr int kPartials = 16;
        static constexpr int kVoices = 64;
        static constexpr int kMaxUnison = 4;
        static constexpr int kTimbreMax = 32;

        AdditiveNode(uint16_t, NodeManager*);

        Connection* inputN = nullptr;   // melody: notes that sound
        Connection* inputT = nullptr;   // timbre: notes that only add partials
        Connection* output = nullptr;

        void process() override;
        void setup() override {}

        Knob volume = Knob(0.72, NODE_W / 4, NODE_H / 2, 16, "assets/knobs/1.png", -135, 135, "Level");
        Knob pitchFromLength = Knob(0.0, NODE_W * 3 / 4, NODE_H / 2, 16, "assets/knobs/1.png", -135, 135, "Pitch From Length");
        Knob unison = Knob(2.0, NODE_W / 2, NODE_H / 2, 16, "assets/knobs/1.png", -135, 135, "Unison");
        Knob detune = Knob(10.0, NODE_W / 2, NODE_H * 3 / 4, 16, "assets/knobs/1.png", -135, 135, "Detune (cents)");

        // 3 plain parameters per partial (ratio, phase, amp), no knob UI; settable via
        // the control API (set_param_value) and automation like any parameter.
        std::array<Parameter, kPartials * 3> partialParams;

        struct Voice {
            bool active = false;
            int noteId = -1;
            float baseFreq = 440.f;
            float amp = 1.f;
            int waitOn = 0;
            int waitOff = -1;
            int unisonCount = 1;
            std::array<float, kMaxUnison> detuneMult{};
            std::array<std::array<double, kPartials>, kMaxUnison> phase{};
            // Per-voice oscillators for the timbre-region partials.
            std::array<std::array<double, kTimbreMax>, kMaxUnison> timbrePhase{};
        };
        std::array<Voice, kVoices> voices;

        // Timbre region: pitch relative to 69 selects the partial ratio, velocity its
        // level. These partials are added to every sounding melody voice.
        struct TimbrePartial {
            bool active = false;
            int noteId = -1;
            float ratio = 1.f;
            float amp = 0.f;
        };
        std::array<TimbrePartial, kTimbreMax> timbrePartials;
};
