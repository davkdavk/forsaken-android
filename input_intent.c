/*===================================================================
	input_intent.c

	The one place that translates neutral input intent into the
	engine's SHIPCONTROL fields.

	Scaling mirrors the keyboard block in control_ship() exactly:
	a fully deflected analog axis produces the same per-frame delta a
	held key does, so the pad and the keyboard feel identical and the
	CLAMP() calls at the end of control_ship() still bound everything.

	Keyboard, for reference (controls.c):
	    ctrl->right   += MoveAccell * MaxMoveSpeed  * framelag;
	    ctrl->up      += MoveAccell * MaxMoveSpeed  * framelag;
	    ctrl->forward += MoveAccell * MaxMoveSpeed  * framelag;
	    ctrl->pitch   += TurnAccell * MaxTurnSpeed  * framelag;
	    ctrl->yaw     += TurnAccell * MaxTurnSpeed  * framelag;
	    ctrl->roll    += RollAccell * MaxRollSpeed  * framelag;
	    ctrl->bank    -= BankAccell * MaxBankAngle  * framelag;  (with yaw)
===================================================================*/

#include "main.h"
#include "controls.h"
#include "input_intent.h"

extern float MoveAccell, MaxMoveSpeed;
extern float TurnAccell, MaxTurnSpeed;
extern float RollAccell, MaxRollSpeed;
extern float BankAccell, MaxBankAngle;
extern float TurboAccell, MaxTurboSpeed;
extern float NitroFuel;

void intent_clear( INPUT_INTENT *in )
{
	if ( !in )
		return;
	memset( in, 0, sizeof( *in ) );
}

void apply_intent( SHIPCONTROL *ctrl, INPUT_INTENT *in, float framelag_ )
{
	float move, turn, roll, bank;

	if ( !ctrl || !in )
		return;

	move = MoveAccell * MaxMoveSpeed  * framelag_;
	turn = TurnAccell * MaxTurnSpeed  * framelag_;
	roll = RollAccell * MaxRollSpeed  * framelag_;
	bank = BankAccell * MaxBankAngle  * framelag_;

	/* Translation. */
	ctrl->right += in->right * move;
	ctrl->up    += in->up    * move;

	/* Turbo uses the larger accel budget, exactly as the keyboard does,
	 * but only while there is nitro left to burn. */
	if ( in->turbo && in->forward > 0.0f && NitroFuel > 0.0f )
		ctrl->forward += in->forward * TurboAccell * MaxTurboSpeed * framelag_;
	else
		ctrl->forward += in->forward * move;

	/* Rotation. Yaw drags bank with it so the ship leans into a turn,
	 * matching the keyboard and mouse behaviour. */
	ctrl->pitch += in->pitch * turn;
	ctrl->yaw   += in->yaw   * turn;
	ctrl->bank  -= in->yaw   * bank;
	ctrl->roll  += in->roll  * roll;

	/* Buttons are OR-ed so a device can only ever add input. */
	if ( in->fire_primary )   ctrl->fire_primary   = 1;
	if ( in->fire_secondary ) ctrl->fire_secondary = 1;
	if ( in->fire_mine )      ctrl->fire_mine      = 1;
	if ( in->turbo )          ctrl->turbo          = 1;

	if ( in->select_next_primary )   ctrl->select_next_primary   = 1;
	if ( in->select_prev_primary )   ctrl->select_prev_primary   = 1;
	if ( in->select_next_secondary ) ctrl->select_next_secondary = 1;
	if ( in->select_prev_secondary ) ctrl->select_prev_secondary = 1;

	if ( in->slide_mode ) ctrl->slide_mode = 1;
}
