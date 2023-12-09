
#define DAT 13
#define LAT 12
#define CLK 11
#define SERVOPWM 10
#define VX 9
#define VY 8
#define SW 7

#include <ctype.h>
#include <stdio.h>
#include <Servo.h>

void setDPin(int pin, bool high);
bool getDPin(int pin);

int volumes[] = {0, 3729, 3322, 4181};
double bpm = 60; // 1 <= x <= 512

uint8_t beats[] = {3, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
char beatsText[] = {3, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\0'};

uint8_t beatLimit = 4;
uint8_t nextBeat = 0;

uint64_t bpmStartDelay = 0;
uint64_t bpmEndDelay = 0;

uint64_t bpmHoldingMillis = 0;
bool bpmHolding;
bool bpmSubtraction;

bool lastChange;
bool BPMChange = true;


Servo servo;
// formatteed as 0bABCDEFGhijk
// A-G = thinga to light up
// h-k are the displays

int characters[] = {
	[0] = 0, [1] = 0, [2] = 0, [3] = 0, [4] = 0, [5] = 0, [6] = 0, [7] = 0, [8] = 0, [9] = 0, [10] = 0, [11] = 0, [12] = 0, [13] = 0, [14] = 0, [15] = 0, [16] = 0, [17] = 0, [18] = 0, [19] = 0, [20] = 0, [21] = 0, [22] = 0, [23] = 0, [24] = 0, [25] = 0, [26] = 0, [27] = 0, [28] = 0, [29] = 0, [30] = 0, [31] = 0, [32] = 0, [33] = 0, [34] = 0, [35] = 0, [36] = 0, [37] = 0, [38] = 0, [39] = 0, [40] = 0, [41] = 0, [42] = 0, [43] = 0, [44] = 0, [45] = 0, [46] = 0, [47] = 0,

	['0'] = 0b1111110,
	['1'] = 0b0110000,
	['2'] = 0b1101101,
	['3'] = 0b1111001,
	['4'] = 0b1110011,
	['5'] = 0b1011011,
	['6'] = 0b1011111,
	['7'] = 0b1110000,
	['8'] = 0b1111111,
	['9'] = 0b1110011,

	/*['A'] = 0b1110111,
	['B'] = 0b0011111,
	['C'] = 0b1001110,
	['D'] = 0b0111101,
	['E'] = 0b1001111,
	['F'] = 0b1000111,
	['G'] = 0b1011110,
	['H'] = 0b0110111,
	['I'] = 0b1010000,
	['J'] = 0b1011000,
	['K'] = 0b1010111,
	['L'] = 0b0001110,
	['M'] = 0b1010101,
	['N'] = 0b0010101,
	['O'] = 0b0011101,
	['P'] = 0b1100111,
	['Q'] = 0b1110011,
	['R'] = 0b0000101,
	['S'] = 0b1011011,
	['T'] = 0b0001111,
	['U'] = 0b0111110,
	['V'] = 0b0011100,
	['W'] = 0b1011100,
	['X'] = 0b1101100,
	['Y'] = 0b0110011,
	['Z'] = 0b1101101,*/
};

void setup() {
	DDRB = DDRB | 0b11110000; // set pins 0x8-0xD to output
	DDRD = DDRD | 0b00001000; // set pins 0x2-0x7 to output
	Serial.begin(115200);     // begin serial output
	//clrSReg();
	servo.attach(SERVOPWM);
	servo.write(90);
}

int displays[] = {
	[0] = 0,
	[1] = 0,
	[2] = 0,
	[3] = 0,
};

int nextDisplay = 0;


void pushInputCooldown() {

}
void setDisplay(uint8_t display, int abcdefg) {
	displays[display] = (abcdefg << 4) | (1 << (3-display));
}

void setNextDisplay(int abcdefg) {
	char emptyDisplay = -1;
	for (int i = 0; i <= 3; i++) {
		if (displays[i] == 0) {
			emptyDisplay = i;
			break;
		}
	}

	if (emptyDisplay == -1) {
		displays[0] = displays[1];
		displays[1] = displays[2];
		displays[2] = displays[3];
		setDisplay(3, abcdefg);
	} else {
		setDisplay(emptyDisplay, abcdefg);
	}
}

void renderDisplays() {
	if (displays[0] == 0) {
		return;
	}

	for (int i = 0; i <= 3; i++) {
		digitalWrite(LAT, 1);
		int data = displays[i];
		for (int j = 0; j <= 10; j++) {
			digitalWrite(CLK, 1);
			digitalWrite(DAT, (data & (1 << j)) >> j);
			digitalWrite(CLK, 0);
		}
		digitalWrite(LAT, 0);
	}
}

char* textToRender;
int delayPerCharMS;
int textIndex = 0;
bool renderingText = false;
uint64_t textCharDelay = 0;

void renderText(char* text, int delayPerCharMS) {
	setDisplay(0, 0);
	setDisplay(1, 0);
	setDisplay(2, 0);
	setDisplay(3, 0);

	textToRender = text;
	delayPerCharMS = delayPerCharMS;
	textIndex = 0;
	renderingText = true;
	textCharDelay = millis();
}

char bpmText[] = {0, 0, 0, 0};
void bpmHoldFn(uint64_t mil) {
	if (bpmHolding) {
		if (bpmSubtraction) {
			if (bpm <= 1) return;
		} else {
			if (bpm >= 512) return;
		}

		uint64_t elapsed = mil - bpmHoldingMillis;
		double incrPercentage = (elapsed) / (elapsed + 4) - .2;
		if (incrPercentage >= 3.0) incrPercentage = 3;
		
		if (mil + 1000 <+ bpmEndDelay) {
			bpmEndDelay = mil + 1000;
		}

		double incr = 1 * incrPercentage;

		if (bpmSubtraction) {
			bpm -= incr;
		} else {
			bpm += incr;
		}

		sprintf(bpmText, "%d", (int)bpm);
		lastChange = BPMChange;
	}
}

void bpmCountFn(uint64_t mil) {
	if (mil >= bpmEndDelay) {
		tone(3, volumes[beats[nextBeat]], 8);
		nextBeat += 1;

		uint32_t beatSwitchLength = 60.0 / bpm * 1000 / 2;
		bpmStartDelay = mil + beatSwitchLength;
		bpmEndDelay = mil + beatSwitchLength * 2;
		servo.write(90+45);
	} else if (mil >= bpmStartDelay) {
		bpmStartDelay = 0;
		noTone(3);
		servo.write(90-45);
	}
}

void loop() {
	Serial.println("loop");
	if (nextBeat >= beatLimit) {
		nextBeat = 0;
	}

	//! joystick stuff
	double vX = (analogRead(A0) - 512) / 512.0; // -1, 0, +1
	double vY = (analogRead(A1) - 512) / 512.0; // -1, 0, +1

	if (getDPin(SW) == HIGH) {
		nextBeatVolume();
	}

	if (vY <= -.3) {
		Serial.print("bpm ");
		holdBPMNegIncrement();
		Serial.println(bpm);
	} else if (vY >= .3) {
		Serial.print("bpm ");
		holdBPMIncrement();
		Serial.println(bpm);
	} else {
		bpmHolding = false;
		bpmSubtraction = false;
	}

	uint64_t mil = millis();

	//! bpm incrementing code
	bpmHoldFn(mil);
	
	//! beat code, no yielding
	bpmCountFn(mil);

	//! text render code
	if (renderingText) {
		if (millis() >= textCharDelay) {
			if (textToRender[textIndex] == '\0') {
				renderingText = false;
				return;
			}

			int charIndex = toupper(textToRender[textIndex]);
			if (characters[charIndex]) {
				textCharDelay = millis() + delayPerCharMS;
				setNextDisplay(characters[charIndex]);
			}

			textIndex++;
		}
	} else {
		if (millis() >= textCharDelay + 1000) {
			if (lastChange == BPMChange) {
				setDisplay(0, bpmText[0]);
				setDisplay(1, bpmText[1]);
				setDisplay(2, bpmText[2]);
				setDisplay(3, '0');

				// show bpm
			} else {
				// show beats

				// this only shows when the beats are done rendering
				for (int i = 0; i <= 15; i++) {
					beatsText[i] = beats[i] + '0';
				}
				renderText(beatsText, 1000);
			}
		}
	}

	renderDisplays();
}

void holdBPMIncrement() {
	if (!bpmHolding) {
		bpmHoldingMillis = millis();
		bpmHolding = true;
		bpmSubtraction = false;
	}
}

void holdBPMNegIncrement() {
	if (!bpmHolding) {
		bpmHoldingMillis = millis();
		bpmHolding = true;
		bpmSubtraction = true;
	}
}

void nextBeatVolume() {
	uint8_t next;
	switch(beats[beatLimit-1]) {
		case 0:
			next = 1;
		case 1:
			next = 2;
		case 2:
			next = 3;
		case 3:
			next = 1;
	}
	beats[beatLimit-1] = next;
}

void addBeat(uint8_t beatVolume) {
	if (beatLimit >= 16) return;
	lastChange = not BPMChange;
	beats[++beatLimit] = beatVolume;
}

void removeBeat() {
	if (beatLimit <= 1) return;
	lastChange = not BPMChange;
	beats[beatLimit--] = 0;
}

void clrSReg() {
	setDPin(DAT, 0);
	for (uint8_t j = 0; j <= 15; j++) {
        	setDPin(CLK, 1);
        	setDPin(CLK, 0);
    	}
	setDPin(LAT, 1);
	setDPin(LAT, 0);
}

void postSReg(uint16_t binarySeq) {
	setDPin(LAT, 1);
	for (int8_t j = 15; j>=0; j--) {
		setDPin(CLK, 1);
		setDPin(DAT, !!(binarySeq & (1 << j)));
        	setDPin(CLK, 0);
	}
	
	setDPin(LAT, 0);
}

void setDPin(int pin, bool high) {
	if (pin >= 8 && pin <= 13) {
		uint8_t relPin = pin - 8;
		if (high)
			PORTB |= 1 << relPin;
		else
			PORTB &= ~(1 << relPin);
	} else if (pin <= 7) {
		if (high)
			PORTD |= 1 << pin;
		else
			PORTD &= ~(1 << pin);
	}
}

bool getDPin(int pin) {
	if (pin >= 8 && pin <= 13) {
		uint8_t relPin = pin - 8;
		return (PORTB & (1 << relPin)) >> relPin;
	} else if (pin <= 7) {
		return (PORTD & (1 << pin)) >> pin;
	}
}