// Quick check of the sequencing engine without Rack:
//   g++ -O2 -std=c++17 -I src tools/engine_test.cpp -o /tmp/et && /tmp/et
#include "ThoraxEngine.hpp"
#include <cstdio>
using namespace thorax;
static void defaults(float* p) {
	float d[NUM_TP] = {16, 4, 0, 0, 7, 1, 0.8f, 0, 0, 0.5f, 0.5f, 0, 0, 9, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2, 0};
	for (int i = 0; i < NUM_TP; i++) p[i] = d[i];
}
static void run(Engine& e, int ti, double sixteenths, const char* title) {
	printf("--- %s\n", title);
	e.reset();
	const double sr = 1000.0, per16 = 0.125;   // 120 bpm, 1 kHz test rate
	bool last = false;
	long nsamp = (long) (sixteenths * per16 * sr);
	for (long k = 0; k < nsamp; k++) {
		double pos = k / sr / per16;
		e.process(pos, 2);
		bool g = e.gateOut(ti);
		if (g && !last) {
			Track& t = e.tracks[ti];
			printf("  %6.3f  vel %.2f  notes:", pos, t.vel);
			for (int v = 0; v < t.voices; v++) printf(" %+.0f", t.volts[v] * 12);
			printf("\n");
		}
		last = g;
	}
}
int main() {
	Engine e;
	for (int t = 0; t < NUM_TRACKS; t++) { defaults(e.p[t]); e.p[t][TP_PULSES] = 0; }
	float* p = e.p[0];
	defaults(p);
	run(e, 0, 16, "4 of 16, plain (expect 0,4,8,12)");
	p[TP_PULSES] = 5; p[TP_ROTATE] = 1;
	run(e, 0, 16, "5 of 16 rotate 1");
	p[TP_PULSES] = 1; p[TP_REPEATS] = 3; p[TP_TIME] = 7; p[TP_PACE] = -1; p[TP_RAMP] = -0.75f;
	run(e, 0, 16, "1 hit, 3 repeats 1/16, pace -100%, ramp down");
	p[TP_REPEATS] = 2; p[TP_TIME] = 5; p[TP_PACE] = 0; p[TP_RAMP] = 0; p[TP_VOICING] = 3; p[TP_STYLE] = 0;
	run(e, 0, 16, "chord of 3, C minor, 2 repeats 1/8");
	p[TP_STYLE] = 4; p[TP_REPEATS] = 3; p[TP_TIME] = 7;
	run(e, 0, 16, "arp up 3 voices");
	p[TP_STYLE] = 0; p[TP_REPEATS] = 0; p[TP_VOICING] = 1; p[TP_PULSES] = 4; p[TP_ROTATE] = 0; p[TP_STEPS] = 4;
	p[TP_RANGE] = 7; p[TP_PHRASE] = 0; p[TP_SCALE] = 1;
	run(e, 0, 16, "cadence I-IV-V-I, major, one note per cycle of 4 (expect 0,5,7,0)");
	p[TP_RANGE] = 0; p[TP_STEPS] = 16; p[TP_SWING] = 0.75f; p[TP_ACCENT] = 1; p[TP_GROOVE] = 1; p[TP_PULSES] = 16;
	run(e, 0, 8, "8 of 16 swing 75%, accent first hit");
	return 0;
}
