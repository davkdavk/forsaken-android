#ifndef INPUT_INTENT_H
#define INPUT_INTENT_H

/*===================================================================
	input_intent.h

	Neutral input layer.

	Device code (gamepad today, touch tomorrow) never writes SHIPCONTROL
	directly. It fills an INPUT_INTENT with normalised, device-agnostic
	values and hands that to apply_intent(), which is the single place
	that knows how the engine's SHIPCONTROL fields are scaled.

	Analog fields are -1..+1 (triggers 0..+1) in ENGINE sign convention,
	which is taken from the keyboard block in control_ship():

	  forward  +  = thrust forward       (move_forward key)
	  right    +  = strafe right         (move_right key)
	  up       +  = slide up             (move_up key)
	  pitch    +  = nose up              (up key)
	  yaw      +  = turn right           (right key)
	  roll     +  = roll left            (roll_left key)

	Button fields are 0/1. The select_* fields are edge-triggered by the
	device layer, so one press yields exactly one step.

	apply_intent() SUMS into SHIPCONTROL - it never assigns - so several
	devices plus the keyboard can contribute in the same frame, and the
	existing CLAMP() at the end of control_ship() still bounds the result.
===================================================================*/

#include "controls.h"

typedef struct {
	/* analog, -1..+1 (engine sign convention above) */
	float forward;
	float right;
	float up;
	float pitch;
	float yaw;
	float roll;

	/* momentary buttons, 0/1 */
	int fire_primary;
	int fire_secondary;
	int fire_mine;
	int turbo;

	/* edge-triggered, one step per press */
	int select_next_primary;
	int select_prev_primary;
	int select_next_secondary;
	int select_prev_secondary;

	/* set when the device wants the ship to slide rather than rotate.
	 * mirrors SHIPCONTROL.slide_mode semantics. */
	int slide_mode;
} INPUT_INTENT;

void intent_clear( INPUT_INTENT *in );

/* Sum an intent into a SHIPCONTROL using the engine's framelag scaling. */
void apply_intent( SHIPCONTROL *ctrl, INPUT_INTENT *in, float framelag_ );

#endif /* INPUT_INTENT_H */
