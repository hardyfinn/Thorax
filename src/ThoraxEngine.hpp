// ThoraxEngine: the sequencing brain of Thorax, kept free of Rack code so it
// can be tested on its own (see tools/engine_test.cpp).
//
// Time is counted in sixteenth notes ("16ths") as a running position that the
// module advances from its own TEMPO or from the CLOCK input. Each track turns
// that position into steps, and each step may fire a note plus its repeats.
//
// In musical terms, for every track:
//   STEPS / PULSES / ROTATE  - a euclidean rhythm: PULSES hits spread as evenly
//                              as possible over STEPS, turned by ROTATE.
//   LENGTH                   - restart the pattern after this many steps (0 = never).
//   DIVIDE                   - how long one step is (1/16 = one sixteenth).
//   CHANCE                   - chance that a hit plays at all.
//   VELOCITY / ACCENT / GROOVE - loudness, and an accent shape across the hits.
//   SUSTAIN                  - note length, in steps.
//   SWING / DELAY            - push every second step late / push the whole track late.
//   REPEATS / TIME / PACE / RAMP - extra notes after each hit (a ratchet or echo):
//                              how many, how far apart, speeding up or slowing
//                              down like a bouncing ball, and getting quieter or louder.
//   PITCH / HARMONY          - the note, and a move up or down the scale.
//   VOICING / STYLE          - how many chord notes, and whether they play as a
//                              chord or one at a time across the repeats.
//   RANGE / PHRASE           - a melody shape (cadence or LFO) laid over the notes.
//   SCALE / ROOT             - the key.
//   RANDOM / RATE            - random wobble in pitch and velocity, and how often it changes.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <algorithm>

namespace thorax {

enum TrackParam {
	TP_STEPS, TP_PULSES, TP_ROTATE, TP_LENGTH, TP_DIVIDE, TP_CHANCE,
	TP_VELOCITY, TP_ACCENT, TP_GROOVE, TP_SUSTAIN, TP_SWING, TP_DELAY,
	TP_REPEATS, TP_TIME, TP_PACE, TP_RAMP, TP_RANDOM, TP_RATE,
	TP_PITCH, TP_HARMONY, TP_VOICING, TP_STYLE, TP_RANGE, TP_PHRASE,
	TP_SCALE, TP_ROOT,
	NUM_TP
};

static const int NUM_TRACKS = 8;
static const int MAX_VOICES = 8;

// Note lengths offered by DIVIDE and TIME, shortest last, in 16ths.
static const int NUM_LENGTHS = 12;
static const char* const LENGTH_NAMES[NUM_LENGTHS] = {
	"1/1", "1/2", "1/3", "1/4", "1/6", "1/8", "1/12", "1/16", "1/24", "1/32", "1/48", "1/64"};
static const double LENGTH_16THS[NUM_LENGTHS] = {
	16.0, 8.0, 16.0 / 3, 4.0, 8.0 / 3, 2.0, 4.0 / 3, 1.0, 2.0 / 3, 0.5, 1.0 / 3, 0.25};

static const int NUM_GROOVES = 8;
static const char* const GROOVE_NAMES[NUM_GROOVES] = {
	"Downbeats", "First hit", "Offbeats", "Every other", "Rise", "Fall", "Arch", "Random"};

static const int NUM_STYLES = 6;
static const char* const STYLE_NAMES[NUM_STYLES] = {
	"Chord", "Chord build", "Chord climb", "Arp per hit", "Arp up", "Arp climb"};

static const int NUM_PHRASES = 8;
static const char* const PHRASE_NAMES[NUM_PHRASES] = {
	"I-IV-V-I", "I-vi-IV-V", "ii-V-I-I", "I-V-vi-IV", "Triangle", "Saw", "Sine", "Square"};

static const int NUM_RATES = 6;
static const char* const RATE_NAMES[NUM_RATES] = {
	"Every note", "Every 2", "Every 4", "Every 8", "Every 16", "Every 32"};

static const int NUM_SCALES = 10;
static const char* const SCALE_NAMES[NUM_SCALES] = {
	"Chromatic", "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
	"Harmonic minor", "Major pentatonic", "Minor pentatonic"};
static const int SCALE_LEN[NUM_SCALES] = {12, 7, 7, 7, 7, 7, 7, 7, 5, 5};
static const int SCALE_SEMIS[NUM_SCALES][12] = {
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
	{0, 2, 4, 5, 7, 9, 11},
	{0, 2, 3, 5, 7, 8, 10},
	{0, 2, 3, 5, 7, 9, 10},
	{0, 1, 3, 5, 7, 8, 10},
	{0, 2, 4, 6, 7, 9, 11},
	{0, 2, 4, 5, 7, 9, 10},
	{0, 2, 3, 5, 7, 8, 11},
	{0, 2, 4, 7, 9},
	{0, 3, 5, 7, 10},
};
static const char* const ROOT_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Cadences: scale degree for each of four pattern cycles (with RANGE at +7 they
// land exactly on these chords; smaller RANGE squeezes them).
static const int CADENCES[4][4] = {{0, 3, 4, 0}, {0, 5, 3, 4}, {1, 4, 0, 0}, {0, 4, 5, 3}};

struct Rng {
	uint64_t s = 0x9E3779B97F4A7C15ull;
	double uniform() {   // 0..1
		s ^= s << 13; s ^= s >> 7; s ^= s << 17;
		return (double) (s >> 11) * (1.0 / 9007199254740992.0);
	}
};

inline int iround(float v) { return (int) std::lround(v); }

// ---- rhythm helpers (also used by the display)
inline int steps(const float* p) { return std::max(1, std::min(64, iround(p[TP_STEPS]))); }
inline int pulses(const float* p) { return std::max(0, std::min(steps(p), iround(p[TP_PULSES]))); }

// Is step i (0-based, within the cycle) a hit?
inline bool isHit(const float* p, int i) {
	int n = steps(p), k = pulses(p);
	if (k <= 0) return false;
	int rot = ((iround(p[TP_ROTATE]) % n) + n) % n;
	int j = ((i - rot) % n + n) % n;
	return (j * k) % n < k;
}

// Which hit of the cycle is step i (0 = first hit)?
inline int hitIndex(const float* p, int i) {
	int c = 0;
	for (int q = 0; q < i; q++)
		if (isHit(p, q)) c++;
	return c;
}

// Scale degree -> semitones above the root.
inline int degreeToSemis(int scale, int deg) {
	int n = SCALE_LEN[scale];
	int oct = (int) std::floor((double) deg / n);
	int idx = deg - oct * n;
	return oct * 12 + SCALE_SEMIS[scale][idx];
}

// Nearest scale degree to a semitone offset above the root.
inline int semisToDegree(int scale, int semis) {
	int n = SCALE_LEN[scale];
	int oct = (int) std::floor(semis / 12.0);
	int within = semis - oct * 12;
	int best = 0, bestD = 99;
	for (int d = 0; d <= n; d++) {
		int s = (d < n) ? SCALE_SEMIS[scale][d] : 12;
		int dist = std::abs(s - within);
		if (dist < bestD) { bestD = dist; best = d; }
	}
	return oct * n + best;
}

// Degrees above the chord root for chord tone k (stacked thirds).
inline int chordToneDegrees(int scale, int k) {
	if (scale == 0) {   // chromatic: stack major/minor thirds as semitones
		static const int T[MAX_VOICES] = {0, 4, 7, 11, 14, 17, 21, 24};
		return T[k];
	}
	return 2 * k;
}

struct NoteEvent {
	double t;              // when, in 16ths
	double len;            // gate length, in 16ths
	float vel;             // 0..1
	int voices;
	float volts[MAX_VOICES];
};

struct Track {
	long lastStep = -1;    // absolute step counter since reset
	long hits = 0;         // notes played since reset
	static const int QN = 24;
	NoteEvent q[QN];
	int qn = 0;
	// gate output state
	bool gate = false;
	double gateEnd = 0.0;
	int retrig = 0;        // samples of forced-low gate (so back-to-back notes re-trigger)
	float vel = 0.f;
	int voices = 1;
	float volts[MAX_VOICES] = {};
	// random wobble
	float randPitch = 0.f, randVel = 0.f;
	float flash = 0.f;     // for the panel light

	void reset() {
		lastStep = -1; hits = 0; qn = 0; gate = false; gateEnd = 0.0; retrig = 0;
		randPitch = randVel = 0.f;
	}
};

struct Sequencer {
	Track tracks[NUM_TRACKS];
	float p[NUM_TRACKS][NUM_TP] = {};
	bool mute[NUM_TRACKS] = {};
	float transposeSemis = 0.f;
	Rng rng;

	void reset() {
		for (auto& t : tracks) t.reset();
	}

	static double stepLen(const float* tp) {
		int d = std::max(0, std::min(NUM_LENGTHS - 1, iround(tp[TP_DIVIDE])));
		return LENGTH_16THS[d];
	}

	// Pitch (volts) for one voice of one note.
	float noteVolts(int ti, const float* tp, int cycle, int stepInCycle, int nSteps, int eventNo, int voice) {
		Track& tr = tracks[ti];
		int scale = std::max(0, std::min(NUM_SCALES - 1, iround(tp[TP_SCALE])));
		int root = std::max(0, std::min(11, iround(tp[TP_ROOT])));
		int n = SCALE_LEN[scale];
		int base = semisToDegree(scale, iround(tp[TP_PITCH]) + iround(transposeSemis));
		int deg = base + iround(tp[TP_HARMONY]);

		// phrase: melody shape over four cycles of the pattern
		float range = tp[TP_RANGE];
		if (std::fabs(range) > 0.01f) {
			int ph = std::max(0, std::min(NUM_PHRASES - 1, iround(tp[TP_PHRASE])));
			float v;
			if (ph < 4) v = CADENCES[ph][((cycle % 4) + 4) % 4] / 7.f;
			else {
				double x = ((((cycle % 4) + 4) % 4) * nSteps + stepInCycle) / (4.0 * nSteps);
				switch (ph) {
					case 4: v = (float) (1.0 - std::fabs(2.0 * x - 1.0)); break;
					case 5: v = (float) x; break;
					case 6: v = (float) (0.5 - 0.5 * std::cos(2.0 * M_PI * x)); break;
					default: v = x < 0.5 ? 0.f : 1.f; break;
				}
			}
			deg += iround(range * v);
		}
		deg += iround(tr.randPitch);

		// voicing / style
		int V = std::max(1, std::min(MAX_VOICES, iround(tp[TP_VOICING])));
		int style = std::max(0, std::min(NUM_STYLES - 1, iround(tp[TP_STYLE])));
		int semis;
		auto toneSemis = [&](int k) {   // chord tone k, may run past V into higher octaves
			int oct = k / V, kk = k % V;
			if (scale == 0) return degreeToSemis(scale, deg) + chordToneDegrees(0, kk) + 12 * oct;
			return degreeToSemis(scale, deg + chordToneDegrees(scale, kk) + n * oct);
		};
		switch (style) {
			case 2: {   // chord climb: each repeat lifts the lowest notes an octave (an inversion)
				int inv = eventNo % V;
				semis = toneSemis(voice) + (voice < inv ? 12 : 0);
				break;
			}
			case 3: semis = toneSemis((int) (tr.hits % V)); break;       // arp per hit
			case 4: semis = toneSemis(eventNo % V); break;               // arp up, back to the root each hit
			case 5: semis = toneSemis(eventNo % (V * 3)); break;         // arp climb: up through three octaves, then round again
			default: semis = toneSemis(voice); break;                    // chord / chord build
		}
		return (root + semis) / 12.f;
	}

	int voicesFor(const float* tp, int eventNo) {
		int V = std::max(1, std::min(MAX_VOICES, iround(tp[TP_VOICING])));
		int style = std::max(0, std::min(NUM_STYLES - 1, iround(tp[TP_STYLE])));
		if (style >= 3) return 1;
		if (style == 1) return std::min(V, eventNo + 1);
		return V;
	}

	float grooveShape(const float* tp, int stepInCycle, int hitNo, int nPulses) {
		int g = std::max(0, std::min(NUM_GROOVES - 1, iround(tp[TP_GROOVE])));
		float x = nPulses > 1 ? (float) hitNo / (nPulses - 1) : 1.f;
		switch (g) {
			case 0: return stepInCycle % 4 == 0 ? 1.f : 0.f;
			case 1: return hitNo == 0 ? 1.f : 0.f;
			case 2: return stepInCycle % 4 == 2 ? 1.f : 0.f;
			case 3: return hitNo % 2 == 0 ? 1.f : 0.f;
			case 4: return x;
			case 5: return 1.f - x;
			case 6: return 1.f - std::fabs(2.f * x - 1.f);
			default: return (float) rng.uniform();
		}
	}

	void push(Track& tr, const NoteEvent& e) {
		if (tr.qn < Track::QN) tr.q[tr.qn++] = e;
	}

	// A new step has begun on track ti: work out whether it plays and queue its notes.
	void onStep(int ti, long s, double L) {
		Track& tr = tracks[ti];
		const float* tp = p[ti];
		int n = steps(tp);
		int len = iround(tp[TP_LENGTH]);
		long se = len > 0 ? s % len : s;
		int i = (int) (se % n);
		int cycle = (int) (se / n);
		if (!isHit(tp, i)) return;
		if (rng.uniform() >= tp[TP_CHANCE]) return;

		// a new hit cuts off any repeats still waiting
		tr.qn = 0;

		// random wobble, refreshed every RATE notes
		int rate = std::max(0, std::min(NUM_RATES - 1, iround(tp[TP_RATE])));
		if (tr.hits % (1L << rate) == 0) {
			float amt = tp[TP_RANDOM];
			tr.randPitch = amt > 0.f ? (float) (rng.uniform() * 2.0 - 1.0) * amt * 7.f : 0.f;
			tr.randVel = amt > 0.f ? (float) (rng.uniform() * 2.0 - 1.0) * amt * 0.5f : 0.f;
		}

		float a = tp[TP_ACCENT];
		float g = grooveShape(tp, i, hitIndex(tp, i), pulses(tp));
		float vel = tp[TP_VELOCITY] * (1.f - 0.75f * a * (1.f - g));   // full ACCENT: off-accent notes drop to a quarter
		vel = std::max(0.f, std::min(1.f, vel * (1.f + tr.randVel)));

		double t0 = s * L + tp[TP_DELAY] * L;
		if (s % 2 == 1) t0 += (tp[TP_SWING] - 0.5) * 2.0 * L;
		double sus = std::max(0.02, (double) tp[TP_SUSTAIN]) * L;

		int reps = std::max(0, std::min(16, iround(tp[TP_REPEATS])));
		int ti_ = std::max(0, std::min(NUM_LENGTHS - 1, iround(tp[TP_TIME])));
		double interval = LENGTH_16THS[ti_];
		double r = std::pow(2.0, tp[TP_PACE] * 0.5);
		double t = t0;
		for (int e = 0; e <= reps; e++) {
			NoteEvent ev;
			ev.t = t;
			double ivl = std::max(1.0 / 32, interval);
			ev.len = reps > 0 ? std::min(sus, ivl * 0.9) : sus;
			float ramp = reps > 0 ? 1.f + tp[TP_RAMP] * (float) e / reps : 1.f;
			ev.vel = std::max(0.f, std::min(1.f, vel * ramp));
			ev.voices = voicesFor(tp, e);
			for (int v = 0; v < ev.voices; v++)
				ev.volts[v] = noteVolts(ti, tp, cycle, i, n, e, v);
			push(tr, ev);
			t += ivl;
			interval *= r;
		}
		tr.hits++;
	}

	// Advance to position pos (16ths). retrigSamples: length of the gap between
	// back-to-back notes. Call once per sample while running.
	void process(double pos, int retrigSamples) {
		for (int ti = 0; ti < NUM_TRACKS; ti++) {
			Track& tr = tracks[ti];
			const float* tp = p[ti];
			double L = stepLen(tp);
			double local = pos - tp[TP_DELAY] * L;   // steps begin late by DELAY, so do their notes
			long s = (long) std::floor(local / L + 1e-9);
			// catch up on steps (normally one at a time); never replay a huge backlog
			if (s - tr.lastStep > 64) tr.lastStep = s - 1;
			while (tr.lastStep < s) {
				tr.lastStep++;
				if (tr.lastStep >= 0) onStep(ti, tr.lastStep, L);
			}
			// fire due events (queue is in time order)
			int fired = 0;
			while (fired < tr.qn && tr.q[fired].t <= pos + 1e-9) {
				const NoteEvent& e = tr.q[fired];
				if (tr.gate) tr.retrig = retrigSamples;
				tr.gate = true;
				tr.gateEnd = e.t + e.len;
				tr.vel = e.vel;
				tr.voices = e.voices;
				for (int v = 0; v < e.voices; v++) tr.volts[v] = e.volts[v];
				tr.flash = 1.f;
				fired++;
			}
			if (fired) {
				for (int k = fired; k < tr.qn; k++) tr.q[k - fired] = tr.q[k];
				tr.qn -= fired;
			}
			if (tr.gate && pos >= tr.gateEnd) tr.gate = false;
		}
	}

	bool gateOut(int ti) {
		Track& tr = tracks[ti];
		if (tr.retrig > 0) { tr.retrig--; return false; }
		return tr.gate && !mute[ti];
	}

	void allGatesOff() {
		for (auto& t : tracks) { t.gate = false; t.qn = 0; }
	}
};

}  // namespace thorax
