// Thorax: an 8-track algorithmic sequencer in the spirit of the Torso T-1,
// with every control given its own clearly labelled knob.
//
// Like the T-1, one set of knobs edits whichever track is selected with the
// TRACK buttons: pressing a track button loads that track's settings onto the
// knobs, and turning a knob changes only that track. All the sequencing is in
// ThoraxEngine.hpp; this file is the panel, clock, outputs and the screen.
#include "plugin.hpp"
#include "ThoraxEngine.hpp"
#include <cctype>

using namespace thorax;

// ---- LAYOUT (same numbers in res/make_panel.py) ----
static const float COL_X[6] = {15.0f, 34.0f, 53.0f, 72.0f, 91.0f, 110.0f};
static const float ROW_Y[5] = {27.0f, 48.0f, 69.0f, 90.0f, 111.0f};
static const float DISP_X = 124.0f, DISP_Y = 12.5f, DISP_W = 75.0f, DISP_H = 18.0f;
static const float TRK_Y0 = 41.0f, TRK_DY = 9.6f;
static const float X_LED = 131.4f, X_SEL = 137.5f, X_MUTE = 148.5f, X_GATE = 162.5f, X_PITCH = 175.5f, X_VEL = 188.5f;
static const float GJ_Y = 121.5f;
static const float X_CLKIN = 131.0f, X_RSTIN = 144.0f, X_RUNIN = 157.0f, X_TRANS = 170.0f, X_CLKOUT = 183.0f;

// Where each track knob sits on the panel: {row, column}, in TrackParam order.
static const int KNOB_POS[NUM_TP][2] = {
	{0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5},   // steps pulses rotate length divide chance
	{1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5},   // velocity accent groove sustain swing delay
	{2, 0}, {2, 1}, {2, 2}, {2, 3}, {2, 4}, {2, 5},   // repeats time pace ramp random rate
	{3, 0}, {3, 1}, {3, 2}, {3, 3}, {3, 4}, {3, 5},   // pitch harmony voicing style range phrase
	{4, 0}, {4, 1},                                   // scale root
};

static const int PPQN_CHOICES[4] = {4, 24, 48, 96};

// LENGTH shows "off" at 0
struct LengthQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		int v = (int) std::round(getValue());
		return v <= 0 ? "off" : string::f("%d", v);
	}
	std::string getUnit() override {
		return (int) std::round(getValue()) <= 0 ? " (pattern never restarts)" : " steps, then restart";
	}
};

// ROOT, PITCH etc. use plain configParam; choice knobs use configSwitch.

struct Thorax : Module {
	enum ParamIds {
		// the per-track knobs come first, in TrackParam order
		TEMPO_PARAM = NUM_TP,
		RUN_PARAM,
		RESET_PARAM,
		DICE_PARAM,
		ENUMS(SELECT_PARAM, NUM_TRACKS),
		ENUMS(MUTE_PARAM, NUM_TRACKS),
		NUM_PARAMS
	};
	enum InputIds {
		CLOCK_INPUT,
		RESET_INPUT,
		RUN_INPUT,
		TRANSPOSE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(GATE_OUTPUT, NUM_TRACKS),
		ENUMS(PITCH_OUTPUT, NUM_TRACKS),
		ENUMS(VEL_OUTPUT, NUM_TRACKS),
		CLOCK_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(SELECT_LIGHT, NUM_TRACKS),
		ENUMS(MUTE_LIGHT, NUM_TRACKS),
		ENUMS(HIT_LIGHT, NUM_TRACKS),
		RUN_LIGHT,
		RESET_LIGHT,
		DICE_LIGHT,
		NUM_LIGHTS
	};

	Engine engine;
	int selected = 0;
	float prev[NUM_PARAMS] = {};
	int lastTouched = -1;        // param shown on the screen
	float touchAge = 99.f;       // seconds since it was turned

	// clock
	double pos = 0.0;            // position in 16ths
	bool started = false;        // external clock: has the first pulse arrived since reset?
	long pulseCount = 0;
	double sinceClock = 0.0, clockPeriod = 0.125;
	int ppqnIndex = 0;           // index into PPQN_CHOICES
	bool wasRunning = false;

	dsp::SchmittTrigger clockTrigger, resetTrigger, resetButton, diceButton;
	dsp::BooleanTrigger selectTrig[NUM_TRACKS];
	dsp::PulseGenerator resetFlash, diceFlash;
	dsp::ClockDivider uiDivider;

	Thorax() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		auto snap = [&](int id) { getParamQuantity(id)->snapEnabled = true; };
		auto desc = [&](int id, const char* d) { getParamQuantity(id)->description = d; };

		configParam(TP_STEPS, 1.f, 64.f, 16.f, "Steps", " steps");
		snap(TP_STEPS);
		desc(TP_STEPS, "T-1: STEPS knob.\nHow many steps in one cycle of the pattern.");
		configParam(TP_PULSES, 0.f, 64.f, 4.f, "Pulses", " hits");
		snap(TP_PULSES);
		desc(TP_PULSES, "T-1: PULSES knob.\nHow many hits, spread as evenly as possible over the steps (euclidean).");
		configParam(TP_ROTATE, 0.f, 63.f, 0.f, "Rotate", " steps");
		snap(TP_ROTATE);
		desc(TP_ROTATE, "T-1: hold CTRL and turn PULSES.\nSlides the hits along so the pattern starts somewhere else.");
		configParam<LengthQuantity>(TP_LENGTH, 0.f, 64.f, 0.f, "Length");
		snap(TP_LENGTH);
		desc(TP_LENGTH, "T-1: LENGTH knob.\nRestart the pattern after this many steps, even mid-cycle. Off = never.");
		configSwitch(TP_DIVIDE, 0.f, NUM_LENGTHS - 1, 7.f, "Divide (step length)",
			std::vector<std::string>(LENGTH_NAMES, LENGTH_NAMES + NUM_LENGTHS));
		desc(TP_DIVIDE, "T-1: DIVISION knob.\nHow long each step lasts. 1/16 = one sixteenth note; 1/8 = half speed; 1/32 = double speed. 1/3, 1/6... are triplets.");
		configParam(TP_CHANCE, 0.f, 1.f, 1.f, "Chance", "%", 0.f, 100.f);
		desc(TP_CHANCE, "T-1: hold CTRL and turn VELOCITY (probability).\nChance that each hit actually plays.");

		configParam(TP_VELOCITY, 0.f, 1.f, 0.8f, "Velocity", "%", 0.f, 100.f);
		desc(TP_VELOCITY, "T-1: VELOCITY knob.\nHow loud the notes are (VEL output, 0-10 V).");
		configParam(TP_ACCENT, 0.f, 1.f, 0.f, "Accent", "%", 0.f, 100.f);
		desc(TP_ACCENT, "T-1: ACCENT knob.\nHow strongly the GROOVE shape is applied: notes off the accent get quieter.");
		configSwitch(TP_GROOVE, 0.f, NUM_GROOVES - 1, 0.f, "Groove (accent shape)",
			std::vector<std::string>(GROOVE_NAMES, GROOVE_NAMES + NUM_GROOVES));
		desc(TP_GROOVE, "T-1: hold CTRL and turn ACCENT.\nWhich notes get the accent.");
		configParam(TP_SUSTAIN, 0.05f, 4.f, 0.5f, "Sustain (note length)", " steps");
		desc(TP_SUSTAIN, "T-1: SUSTAIN knob.\nHow long each gate stays open, in steps.");
		configParam(TP_SWING, 0.5f, 0.75f, 0.5f, "Swing", "%", 0.f, 100.f);
		desc(TP_SWING, "T-1: TIMING knob.\nPushes every second step late. 50% = straight, 66% = triplet feel, 75% = hard shuffle.");
		configParam(TP_DELAY, 0.f, 0.95f, 0.f, "Delay", "% of a step", 0.f, 100.f);
		desc(TP_DELAY, "T-1: hold CTRL and turn TIMING.\nPlays the whole track late by part of a step (laid-back feel, flams).");

		configParam(TP_REPEATS, 0.f, 16.f, 0.f, "Repeats", " extra notes");
		snap(TP_REPEATS);
		desc(TP_REPEATS, "T-1: REPEATS knob.\nExtra notes after each hit (ratchets / echoes). A new hit cuts off unfinished repeats.");
		configSwitch(TP_TIME, 0.f, NUM_LENGTHS - 1, 9.f, "Time (gap between repeats)",
			std::vector<std::string>(LENGTH_NAMES, LENGTH_NAMES + NUM_LENGTHS));
		desc(TP_TIME, "T-1: TIME knob.\nHow far apart the repeats are.");
		configParam(TP_PACE, -1.f, 1.f, 0.f, "Pace", "%", 0.f, 100.f);
		desc(TP_PACE, "T-1: hold CTRL and turn TIME.\nLeft: repeats speed up like a bouncing ball. Right: they slow down. Middle: even.");
		configParam(TP_RAMP, -1.f, 1.f, 0.f, "Ramp (repeat loudness)", "%", 0.f, 100.f);
		desc(TP_RAMP, "T-1: hold CTRL and turn REPEATS.\nLeft: repeats fade out. Right: they get louder.");
		configParam(TP_RANDOM, 0.f, 1.f, 0.f, "Random", "%", 0.f, 100.f);
		desc(TP_RANDOM, "T-1: RANDOM knob.\nRandom wobble in pitch (up to a scale octave) and velocity.");
		configSwitch(TP_RATE, 0.f, NUM_RATES - 1, 0.f, "Rate (how often the random changes)",
			std::vector<std::string>(RATE_NAMES, RATE_NAMES + NUM_RATES));
		desc(TP_RATE, "T-1: hold CTRL and turn RANDOM.\nA new random value every note, or held for 2, 4... notes.");

		configParam(TP_PITCH, -24.f, 24.f, 0.f, "Pitch", " semitones");
		snap(TP_PITCH);
		desc(TP_PITCH, "T-1: PITCH knob (the T-1 enters notes on its pads).\nThe track's note above the ROOT, snapped to the SCALE. 0 = root at 0 V.");
		configParam(TP_HARMONY, -7.f, 7.f, 0.f, "Harmony", " scale steps");
		snap(TP_HARMONY);
		desc(TP_HARMONY, "T-1: hold CTRL and turn PITCH.\nMoves the note (and its chord) up or down the scale, so chords change type with the key.");
		configParam(TP_VOICING, 1.f, 8.f, 1.f, "Voicing", " notes");
		snap(TP_VOICING);
		desc(TP_VOICING, "T-1: VOICING knob.\nHow many chord notes (stacked thirds in the scale). 1 = single notes.");
		configSwitch(TP_STYLE, 0.f, NUM_STYLES - 1, 0.f, "Style (how the chord plays)",
			std::vector<std::string>(STYLE_NAMES, STYLE_NAMES + NUM_STYLES));
		desc(TP_STYLE, "T-1: hold CTRL and turn VOICING.\nChord: all at once. Chord build: grows with each repeat. Chord climb: inverts upward with each repeat. Arp per hit: one chord note per hit. Arp up: walks up the chord through the repeats. Arp climb: keeps walking up across octaves.");
		configParam(TP_RANGE, -14.f, 14.f, 0.f, "Range (phrase depth)", " scale steps");
		snap(TP_RANGE);
		desc(TP_RANGE, "T-1: RANGE knob.\nHow far the PHRASE moves the notes. +7 makes the cadences land on exact chords. Negative = downwards.");
		configSwitch(TP_PHRASE, 0.f, NUM_PHRASES - 1, 0.f, "Phrase (melody shape)",
			std::vector<std::string>(PHRASE_NAMES, PHRASE_NAMES + NUM_PHRASES));
		desc(TP_PHRASE, "T-1: hold CTRL and turn RANGE.\nCadences change chord once per pattern cycle (four cycles). LFO shapes sweep over four cycles.");
		configSwitch(TP_SCALE, 0.f, NUM_SCALES - 1, 2.f, "Scale",
			std::vector<std::string>(SCALE_NAMES, SCALE_NAMES + NUM_SCALES));
		desc(TP_SCALE, "T-1: SCALE.\nThe scale every note is snapped to.");
		configSwitch(TP_ROOT, 0.f, 11.f, 0.f, "Root", std::vector<std::string>(ROOT_NAMES, ROOT_NAMES + 12));
		desc(TP_ROOT, "T-1: hold CTRL and turn SCALE.\nThe key's home note.");

		configParam(TEMPO_PARAM, 24.f, 280.f, 120.f, "Tempo", " BPM");
		snap(TEMPO_PARAM);
		desc(TEMPO_PARAM, "T-1: TEMPO.\nUsed when nothing is patched into CLOCK.");
		configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run", {"Stopped", "Running"});
		desc(RUN_PARAM, "T-1: PLAY. Starting again begins from step 1.");
		configButton(RESET_PARAM, "Reset (every track back to step 1)");
		configButton(DICE_PARAM, "Dice: random rhythm, repeats, accent and phrase for the selected track");
		for (int t = 0; t < NUM_TRACKS; t++) {
			configButton(SELECT_PARAM + t, string::f("Edit track %d", t + 1));
			configSwitch(MUTE_PARAM + t, 0.f, 1.f, 0.f, string::f("Mute track %d", t + 1), {"Playing", "Muted"});
			configOutput(GATE_OUTPUT + t, string::f("Track %d gate (one channel per chord note)", t + 1));
			configOutput(PITCH_OUTPUT + t, string::f("Track %d pitch, V/OCT (one channel per chord note)", t + 1));
			configOutput(VEL_OUTPUT + t, string::f("Track %d velocity, 0-10 V", t + 1));
		}
		configInput(CLOCK_INPUT, "Clock (4 pulses per beat = sixteenths; change in the right-click menu)");
		configInput(RESET_INPUT, "Reset");
		configInput(RUN_INPUT, "Run gate (high = running; overrides the RUN button)");
		configInput(TRANSPOSE_INPUT, "Transpose, V/OCT (stays in the scale)");
		configOutput(CLOCK_OUTPUT, "Clock out (sixteenths)");

		for (int i = 0; i < NUM_PARAMS; i++)
			if (getParamQuantity(i))
				getParamQuantity(i)->randomizeEnabled = false;

		uiDivider.setDivision(32);
		factoryTracks();
		loadTrack(0);
	}

	// Starting point: a small drum pattern on tracks 1-4, the rest empty.
	void factoryTracks() {
		for (int t = 0; t < NUM_TRACKS; t++)
			for (int k = 0; k < NUM_TP; k++)
				engine.p[t][k] = getParamQuantity(k)->getDefaultValue();
		const int pul[NUM_TRACKS] = {4, 2, 7, 3, 0, 0, 0, 0};
		const int rot[NUM_TRACKS] = {0, 4, 0, 2, 0, 0, 0, 0};
		for (int t = 0; t < NUM_TRACKS; t++) {
			engine.p[t][TP_PULSES] = pul[t];
			engine.p[t][TP_ROTATE] = rot[t];
		}
		engine.p[1][TP_PULSES] = 2;   // backbeat on 5 and 13
		engine.p[2][TP_VELOCITY] = 0.6f;
		engine.p[3][TP_STEPS] = 8;
		engine.p[3][TP_VOICING] = 3;
		engine.p[3][TP_PITCH] = -12;
	}

	// Put track t's settings onto the knobs.
	void loadTrack(int t) {
		selected = t;
		for (int k = 0; k < NUM_TP; k++) {
			params[k].setValue(engine.p[t][k]);
			prev[k] = engine.p[t][k];
		}
		lastTouched = -1;
	}

	void doReset() {
		pos = 0.0;
		started = false;
		pulseCount = 0;
		engine.reset();
	}

	void onReset() override {
		factoryTracks();
		loadTrack(0);
		doReset();
	}

	void dice() {
		Rng& r = engine.rng;
		auto pick = [&](int lo, int hi) { return (float) (lo + (int) (r.uniform() * (hi - lo + 1) - 1e-9)); };
		int n = (int) std::round(params[TP_STEPS].getValue());
		params[TP_PULSES].setValue(pick(1, std::max(1, n * 3 / 4)));
		params[TP_ROTATE].setValue(pick(0, n - 1));
		params[TP_REPEATS].setValue(r.uniform() < 0.6 ? 0.f : pick(1, 4));
		params[TP_TIME].setValue(pick(5, 11));
		params[TP_PACE].setValue(r.uniform() < 0.5 ? 0.f : (float) (r.uniform() * 2.0 - 1.0));
		params[TP_ACCENT].setValue((float) r.uniform());
		params[TP_GROOVE].setValue(pick(0, NUM_GROOVES - 1));
		params[TP_PHRASE].setValue(pick(0, NUM_PHRASES - 1));
		params[TP_RANGE].setValue(r.uniform() < 0.4 ? 0.f : pick(-7, 7));
	}

	void process(const ProcessArgs& args) override {
		// track buttons: load the chosen track onto the knobs
		for (int t = 0; t < NUM_TRACKS; t++)
			if (selectTrig[t].process(params[SELECT_PARAM + t].getValue() > 0.f) && t != selected)
				loadTrack(t);
		// knobs write into the selected track
		for (int k = 0; k < NUM_TP; k++)
			engine.p[selected][k] = params[k].getValue();
		for (int t = 0; t < NUM_TRACKS; t++)
			engine.mute[t] = params[MUTE_PARAM + t].getValue() > 0.5f;
		engine.transposeSemis = inputs[TRANSPOSE_INPUT].getVoltage() * 12.f;

		if (diceButton.process(params[DICE_PARAM].getValue())) {
			dice();
			diceFlash.trigger(0.2f);
		}

		// run / reset
		bool running = inputs[RUN_INPUT].isConnected() ? inputs[RUN_INPUT].getVoltage() >= 1.f
		                                               : params[RUN_PARAM].getValue() > 0.5f;
		bool resetNow = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		resetNow |= resetButton.process(params[RESET_PARAM].getValue());
		if (running && !wasRunning)
			resetNow = true;   // starting again begins from step 1, like PLAY on the T-1
		if (!running && wasRunning)
			engine.allGatesOff();
		wasRunning = running;
		if (resetNow) {
			doReset();
			resetFlash.trigger(0.15f);
		}

		// clock
		bool ext = inputs[CLOCK_INPUT].isConnected();
		bool pulse = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		bool advance = false;
		if (ext) {
			double unit = 4.0 / PPQN_CHOICES[ppqnIndex];   // 16ths per pulse
			sinceClock += args.sampleTime;
			if (pulse) {
				if (sinceClock > 1e-4 && sinceClock < 4.0)
					clockPeriod = sinceClock;
				sinceClock = 0.0;
				if (running) {
					if (!started) { started = true; pulseCount = 0; }
					else pulseCount++;
				}
			}
			if (running && started) {
				double frac = std::min(sinceClock / clockPeriod, 0.999);
				pos = (pulseCount + frac) * unit;
				advance = true;
			}
		}
		else if (running) {
			advance = true;
			// the first sample after a reset plays step 1, then time moves on
			engine.process(pos, (int) (args.sampleRate * 0.001f));
			pos += args.sampleTime * params[TEMPO_PARAM].getValue() / 15.0;
		}
		if (ext && advance)
			engine.process(pos, (int) (args.sampleRate * 0.001f));

		// outputs
		for (int t = 0; t < NUM_TRACKS; t++) {
			Track& tr = engine.tracks[t];
			bool g = engine.gateOut(t) && running;
			int ch = std::max(1, tr.voices);
			outputs[GATE_OUTPUT + t].setChannels(ch);
			outputs[PITCH_OUTPUT + t].setChannels(ch);
			for (int c = 0; c < ch; c++) {
				outputs[GATE_OUTPUT + t].setVoltage(g ? 10.f : 0.f, c);
				outputs[PITCH_OUTPUT + t].setVoltage(tr.volts[c], c);
			}
			outputs[VEL_OUTPUT + t].setVoltage(tr.vel * 10.f);
		}
		bool clkHigh = running && (!ext || started) && (pos - std::floor(pos)) < 0.5;
		outputs[CLOCK_OUTPUT].setVoltage(clkHigh ? 10.f : 0.f);

		// screen + lights, a few hundred times a second
		bool rf = resetFlash.process(args.sampleTime);
		bool df = diceFlash.process(args.sampleTime);
		if (uiDivider.process()) {
			float dt = args.sampleTime * uiDivider.getDivision();
			touchAge += dt;
			for (int k = 0; k < NUM_TP; k++) {
				float v = params[k].getValue();
				if (v != prev[k]) {
					prev[k] = v;
					lastTouched = k;
					touchAge = 0.f;
				}
			}
			float tv = params[TEMPO_PARAM].getValue();
			if (tv != prev[TEMPO_PARAM]) {
				prev[TEMPO_PARAM] = tv;
				lastTouched = TEMPO_PARAM;
				touchAge = 0.f;
			}
			for (int t = 0; t < NUM_TRACKS; t++) {
				Track& tr = engine.tracks[t];
				lights[HIT_LIGHT + t].setBrightness(tr.flash);
				tr.flash = std::max(0.f, tr.flash - dt * 8.f);
				lights[SELECT_LIGHT + t].setBrightness(t == selected ? 1.f : 0.f);
				lights[MUTE_LIGHT + t].setBrightness(engine.mute[t] ? 1.f : 0.f);
			}
			lights[RUN_LIGHT].setBrightness(running ? 1.f : 0.f);
			lights[RESET_LIGHT].setBrightnessSmooth(rf ? 1.f : 0.f, dt, 30.f);
			lights[DICE_LIGHT].setBrightnessSmooth(df ? 1.f : 0.f, dt, 30.f);
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* tracks = json_array();
		for (int t = 0; t < NUM_TRACKS; t++) {
			json_t* a = json_array();
			for (int k = 0; k < NUM_TP; k++)
				json_array_append_new(a, json_real(engine.p[t][k]));
			json_array_append_new(tracks, a);
		}
		json_object_set_new(root, "tracks", tracks);
		json_object_set_new(root, "selected", json_integer(selected));
		json_object_set_new(root, "ppqn", json_integer(ppqnIndex));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "tracks"))) {
			for (int t = 0; t < NUM_TRACKS && t < (int) json_array_size(j); t++) {
				json_t* a = json_array_get(j, t);
				for (int k = 0; k < NUM_TP && k < (int) json_array_size(a); k++)
					engine.p[t][k] = (float) json_number_value(json_array_get(a, k));
			}
		}
		if ((j = json_object_get(root, "selected"))) selected = clamp((int) json_integer_value(j), 0, NUM_TRACKS - 1);
		if ((j = json_object_get(root, "ppqn"))) ppqnIndex = clamp((int) json_integer_value(j), 0, 3);
		loadTrack(selected);
	}
};

// ---- the screen: selected track, the knob you just turned, and the pattern
struct ThoraxDisplay : Widget {
	Thorax* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		NVGcolor amber = nvgRGB(0xE2, 0xA9, 0x3B), cream = nvgRGB(0xF1, 0xE9, 0xDA), dim = nvgRGB(0x3A, 0x33, 0x2A);
		std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		float w = box.size.x, h = box.size.y;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, w, h, 3);
		nvgFillColor(args.vg, nvgRGB(0x14, 0x11, 0x0D));
		nvgFill(args.vg);

		int sel = module ? module->selected : 0;
		float defaults[NUM_TP] = {16, 4, 0, 0, 7, 1, 0.8f, 0, 0, 0.5f, 0.5f, 0, 0, 9, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2, 0};
		const float* p = module ? module->engine.p[sel] : defaults;

		if (font && font->handle >= 0) {
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, 13.f);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
			nvgFillColor(args.vg, amber);
			nvgText(args.vg, 6, 15, string::f("TRACK %d", sel + 1).c_str(), NULL);

			std::string info;
			int lt = module ? module->lastTouched : -1;
			if (module && lt >= 0 && module->touchAge < 4.f) {
				ParamQuantity* pq = module->getParamQuantity(lt);
				std::string name = pq->name;
				size_t cut = name.find(" (");
				if (cut != std::string::npos) name = name.substr(0, cut);
				for (auto& c : name) c = std::toupper(c);
				if (lt == TP_PULSES)
					info = string::f("%s %d of %d", name.c_str(), pulses(p), steps(p));
				else
					info = name + " " + pq->getDisplayValueString() + pq->getUnit();
			}
			else {
				info = string::f("%d hits / %d steps", pulses(p), steps(p));
			}
			if (info.size() > 30) info = info.substr(0, 30);
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
			nvgFillColor(args.vg, cream);
			nvgText(args.vg, w - 6, 15, info.c_str(), NULL);
		}

		// steps: one row of up to 32 cells, a second row above 32
		int n = steps(p);
		int cur = -1;
		if (module && module->engine.tracks[sel].lastStep >= 0) {
			long s = module->engine.tracks[sel].lastStep;
			int len = (int) std::round(p[TP_LENGTH]);
			long se = len > 0 ? s % len : s;
			cur = (int) (se % n);
		}
		int perRow = n > 32 ? 32 : n;
		int rows = n > 32 ? 2 : 1;
		float x0 = 6, y0 = 22, gw = w - 12, gh = h - 28;
		float cw = gw / perRow, rh = (gh - (rows - 1) * 2) / rows;
		for (int i = 0; i < n; i++) {
			int r = i / 32, c = i % 32;
			float x = x0 + c * cw, y = y0 + r * (rh + 2);
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x + 0.6f, y, cw - 1.2f, rh, 1.2f);
			nvgFillColor(args.vg, isHit(p, i) ? amber : dim);
			nvgFill(args.vg);
			if (i == cur) {
				nvgBeginPath(args.vg);
				nvgRect(args.vg, x + 0.6f, y + rh + 0.6f, cw - 1.2f, 1.6f);
				nvgFillColor(args.vg, cream);
				nvgFill(args.vg);
			}
		}
	}
};

struct ThoraxWidget : ModuleWidget {
	ThoraxWidget(Thorax* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Thorax.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		static const bool SNAPPY[NUM_TP] = {
			true, true, true, true, true, false,
			false, false, true, false, false, false,
			true, true, false, false, false, true,
			true, true, true, true, true, true,
			true, true};
		for (int k = 0; k < NUM_TP; k++) {
			Vec v = mm2px(Vec(COL_X[KNOB_POS[k][1]], ROW_Y[KNOB_POS[k][0]]));
			if (SNAPPY[k])
				addParam(createParamCentered<RoundBlackSnapKnob>(v, module, k));
			else
				addParam(createParamCentered<RoundBlackKnob>(v, module, k));
		}
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COL_X[2], ROW_Y[4])), module, Thorax::TEMPO_PARAM));
		addParam(createLightParamCentered<VCVLightBezelLatch<YellowLight>>(mm2px(Vec(COL_X[3], ROW_Y[4])), module, Thorax::RUN_PARAM, Thorax::RUN_LIGHT));
		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(COL_X[4], ROW_Y[4])), module, Thorax::RESET_PARAM, Thorax::RESET_LIGHT));
		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(COL_X[5], ROW_Y[4])), module, Thorax::DICE_PARAM, Thorax::DICE_LIGHT));

		ThoraxDisplay* disp = createWidget<ThoraxDisplay>(mm2px(Vec(DISP_X, DISP_Y)));
		disp->box.size = mm2px(Vec(DISP_W, DISP_H));
		disp->module = module;
		addChild(disp);

		for (int t = 0; t < NUM_TRACKS; t++) {
			float y = TRK_Y0 + t * TRK_DY;
			addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(X_LED, y)), module, Thorax::HIT_LIGHT + t));
			addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(X_SEL, y)), module, Thorax::SELECT_PARAM + t, Thorax::SELECT_LIGHT + t));
			addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(mm2px(Vec(X_MUTE, y)), module, Thorax::MUTE_PARAM + t, Thorax::MUTE_LIGHT + t));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_GATE, y)), module, Thorax::GATE_OUTPUT + t));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_PITCH, y)), module, Thorax::PITCH_OUTPUT + t));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_VEL, y)), module, Thorax::VEL_OUTPUT + t));
		}
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_CLKIN, GJ_Y)), module, Thorax::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_RSTIN, GJ_Y)), module, Thorax::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_RUNIN, GJ_Y)), module, Thorax::RUN_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_TRANS, GJ_Y)), module, Thorax::TRANSPOSE_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_CLKOUT, GJ_Y)), module, Thorax::CLOCK_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Thorax* module = dynamic_cast<Thorax*>(this->module);
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("CLOCK input pulses per beat",
			{"4 (sixteenths)", "24 (MIDI clock)", "48", "96"}, &module->ppqnIndex));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(string::f("Track %d", module->selected + 1)));
		menu->addChild(createMenuItem("Copy this track's settings to all tracks", "", [=]() {
			for (int t = 0; t < NUM_TRACKS; t++)
				for (int k = 0; k < NUM_TP; k++)
					module->engine.p[t][k] = module->params[k].getValue();
		}));
		menu->addChild(createMenuItem("Reset this track's knobs to default", "", [=]() {
			for (int k = 0; k < NUM_TP; k++)
				module->params[k].setValue(module->getParamQuantity(k)->getDefaultValue());
		}));
		menu->addChild(createMenuItem("Silence this track (0 pulses)", "", [=]() {
			module->params[TP_PULSES].setValue(0.f);
		}));
	}
};

Model* modelThorax = createModel<Thorax, ThoraxWidget>("Thorax");
