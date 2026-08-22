/*===================================================================
	android_input.c

	SDL_GameController -> INPUT_INTENT for the Android / Orange Pi port.

	Why SDL_GameController and not SDL_Joystick:
	the engine's existing raw-joystick path (input_sdl.c) indexes axes and
	buttons by number, and an Xbox pad enumerates differently over USB than
	over Bluetooth - so a binding made on one transport is wrong on the
	other. SDL_GameController resolves that with its internal mapping db:
	SDL_CONTROLLER_BUTTON_A is the A button on both. The raw joystick
	handlers are left in place and untouched; this is an additional path.

	This module never writes SHIPCONTROL. It fills an INPUT_INTENT and
	apply_intent() does the framelag scaling, so the touch overlay can
	reuse the same layer.

	Default map (all remappable via the pilot config):

	  RT / LT           throttle forward / reverse (analog)
	  Left stick  X     strafe          Y   vertical slide
	  Right stick X     yaw             Y   pitch
	  LB / RB           roll left / right
	  A                 fire primary    X   fire secondary
	  D-pad L/R         cycle primary   U/D cycle secondary (edge)
	  R3                turbo           Start  pause
===================================================================*/

#include "main.h"
#include "controls.h"
#include "config.h"
#include "input_intent.h"
#include "util.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

extern float framelag;
extern USERCONFIG *player_config;

/*-------------------------------------------------------------------
	Touch overlay state, written from Java through JNI. Kept separate
	from pad state so both can drive the ship at once.
-------------------------------------------------------------------*/
float     g_touch_lx = 0.0f, g_touch_ly = 0.0f;
float     g_touch_rx = 0.0f, g_touch_ry = 0.0f;
u_int32_t g_touch_buttons = 0;

/* Bit positions for g_touch_buttons - must match the Java overlay. */
#define TB_FIRE        (1u << 0)
#define TB_SECONDARY   (1u << 1)
#define TB_ROLL_LEFT   (1u << 2)
#define TB_ROLL_RIGHT  (1u << 3)
#define TB_UP          (1u << 4)
#define TB_DOWN        (1u << 5)
#define TB_TURBO       (1u << 6)
#define TB_MINE        (1u << 7)
#define TB_NEXT_GUN    (1u << 8)
#define TB_PREV_GUN    (1u << 9)
#define TB_NEXT_MSL    (1u << 10)
#define TB_PREV_MSL    (1u << 11)

static SDL_GameController *pad      = NULL;
static SDL_JoystickID      pad_inst = -1;

/*-------------------------------------------------------------------
	Tuning.

	These mirror the fields persisted in USERCONFIG. Defaults are used
	until a pilot config is loaded; PadApplyConfig() copies the pilot's
	values over them.
-------------------------------------------------------------------*/
static float cfg_deadzone_left  = 0.15f;
static float cfg_deadzone_right = 0.15f;
static float cfg_deadzone_trig  = 0.08f;
static float cfg_look_sens      = 1.0f;
static float cfg_move_sens      = 1.0f;
static int   cfg_invert_pitch   = 0;
static int   cfg_expo           = 1;     /* 0 = linear, 1 = mild expo */

/*-------------------------------------------------------------------
	Bindings. Defaults are the documented map; a pilot config may
	override any of them. -1 means unbound.
-------------------------------------------------------------------*/
static int cfg_bind[ PADBIND_MAX ] = {
	SDL_CONTROLLER_BUTTON_A,              /* PADBIND_FirePrimary    */
	SDL_CONTROLLER_BUTTON_X,              /* PADBIND_FireSecondary  */
	-1,                                   /* PADBIND_FireMine       */
	SDL_CONTROLLER_BUTTON_RIGHTSTICK,     /* PADBIND_Turbo          */
	SDL_CONTROLLER_BUTTON_LEFTSHOULDER,   /* PADBIND_RollLeft       */
	SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,  /* PADBIND_RollRight      */
	SDL_CONTROLLER_BUTTON_DPAD_RIGHT,     /* PADBIND_NextPrimary    */
	SDL_CONTROLLER_BUTTON_DPAD_LEFT,      /* PADBIND_PrevPrimary    */
	SDL_CONTROLLER_BUTTON_DPAD_DOWN,      /* PADBIND_NextSecondary  */
	SDL_CONTROLLER_BUTTON_DPAD_UP,        /* PADBIND_PrevSecondary  */
	SDL_CONTROLLER_BUTTON_START,          /* PADBIND_Pause          */
};

static int cfg_axis[ PADAXIS_MAX ] = {
	SDL_CONTROLLER_AXIS_TRIGGERRIGHT,     /* PADAXIS_Forward  */
	SDL_CONTROLLER_AXIS_TRIGGERLEFT,      /* PADAXIS_Reverse  */
	SDL_CONTROLLER_AXIS_LEFTX,            /* PADAXIS_Strafe   */
	SDL_CONTROLLER_AXIS_LEFTY,            /* PADAXIS_Vertical */
	SDL_CONTROLLER_AXIS_RIGHTX,           /* PADAXIS_Yaw      */
	SDL_CONTROLLER_AXIS_RIGHTY,           /* PADAXIS_Pitch    */
};

/* Mild expo: blend a cubic into the linear response. 6DOF aim is twitchy
 * and the remaster shipped with oversensitive, unremappable controls; this
 * keeps fine aim precise near centre without capping the top end. */
#define EXPO_AMOUNT 0.35f

static float curve( float v )
{
	if ( !cfg_expo )
		return v;
	return ( 1.0f - EXPO_AMOUNT ) * v + EXPO_AMOUNT * v * v * v;
}

/* Radial deadzone applied per stick, so the ignored region is a circle
 * rather than a cross - a diagonal push no longer needs more travel than
 * a straight one. Output is rescaled so there is no jump at the edge. */
static void stick_pair( SDL_GameControllerAxis ax, SDL_GameControllerAxis ay,
                        float dz, float *outx, float *outy )
{
	float x, y, mag, scale;

	*outx = *outy = 0.0f;

	if ( !pad )
		return;

	x = SDL_GameControllerGetAxis( pad, ax ) / 32767.0f;
	y = SDL_GameControllerGetAxis( pad, ay ) / 32767.0f;

	if ( x >  1.0f ) x =  1.0f;  if ( x < -1.0f ) x = -1.0f;
	if ( y >  1.0f ) y =  1.0f;  if ( y < -1.0f ) y = -1.0f;

	mag = (float) sqrt( x * x + y * y );
	if ( mag <= dz || mag <= 0.0f )
		return;

	if ( mag > 1.0f )
		mag = 1.0f;

	/* rescale magnitude out of the deadzone, keep direction */
	scale = ( ( mag - dz ) / ( 1.0f - dz ) ) / mag;

	*outx = x * scale;
	*outy = y * scale;
}

/* Triggers rest at -32768 and travel to +32767; normalise to 0..1. */
static float trigger( SDL_GameControllerAxis a )
{
	float v;

	if ( !pad )
		return 0.0f;

	v = ( SDL_GameControllerGetAxis( pad, a ) + 32768.0f ) / 65535.0f;

	if ( v < 0.0f ) v = 0.0f;
	if ( v > 1.0f ) v = 1.0f;

	if ( v <= cfg_deadzone_trig )
		return 0.0f;

	return ( v - cfg_deadzone_trig ) / ( 1.0f - cfg_deadzone_trig );
}

static bool held( SDL_GameControllerButton b )
{
	if ( !pad )
		return false;
	return SDL_GameControllerGetButton( pad, b ) != 0;
}

/* Rising-edge detect so "cycle weapon" steps exactly once per press. */
static bool pressed( SDL_GameControllerButton b )
{
	static bool prev[ SDL_CONTROLLER_BUTTON_MAX ];
	bool now, edge;

	if ( b < 0 || b >= SDL_CONTROLLER_BUTTON_MAX )
		return false;

	now  = held( b );
	edge = ( now && !prev[ b ] );
	prev[ b ] = now;
	return edge;
}

/* Binding-aware wrappers: look the action up in the current map. */
static bool bind_held( int action )
{
	if ( action < 0 || action >= PADBIND_MAX )
		return false;
	if ( cfg_bind[ action ] < 0 )
		return false;
	return held( (SDL_GameControllerButton) cfg_bind[ action ] );
}

static bool bind_pressed( int action )
{
	if ( action < 0 || action >= PADBIND_MAX )
		return false;
	if ( cfg_bind[ action ] < 0 )
		return false;
	return pressed( (SDL_GameControllerButton) cfg_bind[ action ] );
}

bool AndroidPadConnected( void )
{
	return pad != NULL;
}

const char * AndroidPadName( void )
{
	return pad ? SDL_GameControllerName( pad ) : NULL;
}

/*-------------------------------------------------------------------
	Report the pad's capabilities, and warn about anything in the
	default map the device does not actually provide.
-------------------------------------------------------------------*/
static void PadReport( void )
{
	static const struct {
		SDL_GameControllerAxis axis;
		const char *what;
	} want_axis[] = {
		{ SDL_CONTROLLER_AXIS_LEFTX,        "left stick X (strafe)"   },
		{ SDL_CONTROLLER_AXIS_LEFTY,        "left stick Y (vertical)" },
		{ SDL_CONTROLLER_AXIS_RIGHTX,       "right stick X (yaw)"     },
		{ SDL_CONTROLLER_AXIS_RIGHTY,       "right stick Y (pitch)"   },
		{ SDL_CONTROLLER_AXIS_TRIGGERLEFT,  "left trigger (reverse)"  },
		{ SDL_CONTROLLER_AXIS_TRIGGERRIGHT, "right trigger (forward)" },
	};
	static const struct {
		SDL_GameControllerButton btn;
		const char *what;
	} want_btn[] = {
		{ SDL_CONTROLLER_BUTTON_A,             "A (fire primary)"       },
		{ SDL_CONTROLLER_BUTTON_X,             "X (fire secondary)"     },
		{ SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  "LB (roll left)"         },
		{ SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, "RB (roll right)"        },
		{ SDL_CONTROLLER_BUTTON_RIGHTSTICK,    "R3 (turbo)"             },
		{ SDL_CONTROLLER_BUTTON_START,         "Start (pause)"          },
		{ SDL_CONTROLLER_BUTTON_DPAD_LEFT,     "D-pad left"             },
		{ SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    "D-pad right"            },
		{ SDL_CONTROLLER_BUTTON_DPAD_UP,       "D-pad up"               },
		{ SDL_CONTROLLER_BUTTON_DPAD_DOWN,     "D-pad down"             },
	};
	size_t i;

	if ( !pad )
		return;

	DebugPrintf( "AndroidInput: controller name '%s'\n",
		SDL_GameControllerName( pad ) ? SDL_GameControllerName( pad ) : "(null)" );
#ifdef __ANDROID__
	__android_log_print( ANDROID_LOG_INFO, "Forsaken",
		"controller name '%s'",
		SDL_GameControllerName( pad ) ? SDL_GameControllerName( pad ) : "(null)" );
#endif

	for ( i = 0; i < sizeof( want_axis ) / sizeof( want_axis[ 0 ] ); i++ )
	{
		if ( !SDL_GameControllerHasAxis( pad, want_axis[ i ].axis ) )
		{
			DebugPrintf( "AndroidInput: WARNING unmapped axis - %s\n",
				want_axis[ i ].what );
#ifdef __ANDROID__
			__android_log_print( ANDROID_LOG_WARN, "Forsaken",
				"unmapped axis: %s", want_axis[ i ].what );
#endif
		}
	}

	for ( i = 0; i < sizeof( want_btn ) / sizeof( want_btn[ 0 ] ); i++ )
	{
		if ( !SDL_GameControllerHasButton( pad, want_btn[ i ].btn ) )
		{
			DebugPrintf( "AndroidInput: WARNING unmapped button - %s\n",
				want_btn[ i ].what );
#ifdef __ANDROID__
			__android_log_print( ANDROID_LOG_WARN, "Forsaken",
				"unmapped button: %s", want_btn[ i ].what );
#endif
		}
	}
}

/* Pull the tuning values out of the pilot config. Called after a pilot
 * config is loaded so saved settings take effect without a restart. */
void PadApplyConfig( USERCONFIG *u )
{
	int i;

	if ( !u )
		return;

	if ( u->pad_deadzone_left  > 0.0f ) cfg_deadzone_left  = u->pad_deadzone_left;
	if ( u->pad_deadzone_right > 0.0f ) cfg_deadzone_right = u->pad_deadzone_right;
	if ( u->pad_deadzone_trigger > 0.0f ) cfg_deadzone_trig = u->pad_deadzone_trigger;
	if ( u->pad_look_sensitivity > 0.0f ) cfg_look_sens = u->pad_look_sensitivity;
	if ( u->pad_move_sensitivity > 0.0f ) cfg_move_sens = u->pad_move_sensitivity;

	cfg_invert_pitch = u->pad_invert_pitch;
	cfg_expo         = u->pad_expo;

	for ( i = 0; i < PADBIND_MAX; i++ )
		if ( u->pad_bind[ i ] >= -1 )
			cfg_bind[ i ] = u->pad_bind[ i ];
	for ( i = 0; i < PADAXIS_MAX; i++ )
		if ( u->pad_axis[ i ] >= -1 )
			cfg_axis[ i ] = u->pad_axis[ i ];

	DebugPrintf( "AndroidInput: config dz(%.2f/%.2f/%.2f) look %.2f move %.2f "
		"invert %d expo %d\n",
		cfg_deadzone_left, cfg_deadzone_right, cfg_deadzone_trig,
		cfg_look_sens, cfg_move_sens, cfg_invert_pitch, cfg_expo );
}

static bool HasRealAxes( SDL_GameController *c )
{
	return c && SDL_GameControllerHasAxis( c, SDL_CONTROLLER_AXIS_LEFTX )
	        && SDL_GameControllerHasAxis( c, SDL_CONTROLLER_AXIS_RIGHTX )
	        && SDL_GameControllerHasAxis( c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT );
}

static bool IsPreferredName( const char *n )
{
	if ( !n ) return false;
	if ( strcasestr( n, "xbox" ) ) return true;
	if ( strcasestr( n, "x-box" ) ) return true;
	if ( strcasestr( n, "xinput" ) ) return true;
	if ( strcasestr( n, "series" ) ) return true;
	return false;
}

void AndroidInputInit( void )
{
	int i;
	SDL_GameController *cand = NULL;
	int cand_idx = -1;

	if ( SDL_WasInit( SDL_INIT_GAMECONTROLLER ) == 0 )
		SDL_InitSubSystem( SDL_INIT_GAMECONTROLLER );

	for ( i = 0; i < SDL_NumJoysticks(); i++ )
	{
		const char *nm;
		if ( !SDL_IsGameController( i ) )
		{
			nm = SDL_JoystickNameForIndex( i );
			DebugPrintf( "AndroidInput: joystick %d ('%s') has no "
				"GameController mapping, ignoring\n",
				i, nm ? nm : "(null)" );
			continue;
		}
		nm = SDL_JoystickNameForIndex( i );
		if ( nm && strcasestr( nm, "YICHIP" ) )
		{
			DebugPrintf( "AndroidInput: ignoring YICHIP consumer device at %d\n", i );
			continue;
		}
		if ( IsPreferredName( nm ) )
		{
			pad = SDL_GameControllerOpen( i );
			if ( pad && HasRealAxes( pad ) )
			{
				pad_inst = SDL_JoystickInstanceID( SDL_GameControllerGetJoystick( pad ) );
				PadReport();
				PadApplyConfig( player_config );
				return;
			}
			if ( pad ) { SDL_GameControllerClose( pad ); pad = NULL; }
		}
		if ( !cand )
		{
			SDL_GameController *tmp = SDL_GameControllerOpen( i );
			if ( tmp )
			{
				if ( HasRealAxes( tmp ) )
				{
					cand = tmp; cand_idx = i;
				}
				else
				{
					SDL_GameControllerClose( tmp );
				}
			}
		}
	}
	if ( cand )
	{
		pad = cand;
		pad_inst = SDL_JoystickInstanceID( SDL_GameControllerGetJoystick( pad ) );
		PadReport();
		PadApplyConfig( player_config );
		return;
	}
	DebugPrintf( "AndroidInput: no game controller present\n" );
}

/* Called from the SDL event pump so hotplug works without a rescan. */
void AndroidInputDeviceEvent( int which, bool added )
{
	if ( added )
	{
		if ( !pad && SDL_IsGameController( which ) )
		{
			const char *nm = SDL_JoystickNameForIndex( which );
			if ( nm && strcasestr( nm, "YICHIP" ) )
			{
				DebugPrintf( "AndroidInput: ignoring YICHIP hotplug\n" );
				return;
			}
			pad = SDL_GameControllerOpen( which );
			if ( pad )
			{
				if ( !HasRealAxes( pad ) )
				{
					DebugPrintf( "AndroidInput: hotplugged pad has no real axes, ignoring\n" );
					SDL_GameControllerClose( pad );
					pad = NULL;
					return;
				}
				pad_inst = SDL_JoystickInstanceID(
					SDL_GameControllerGetJoystick( pad ) );
				DebugPrintf( "AndroidInput: pad attached '%s'\n",
					SDL_GameControllerName( pad ) ? SDL_GameControllerName( pad ) : "(null)" );
				PadReport();
				PadApplyConfig( player_config );
			}
		}
	}
	else
	{
		if ( pad && which == pad_inst )
		{
			DebugPrintf( "AndroidInput: pad detached\n" );
			SDL_GameControllerClose( pad );
			pad      = NULL;
			pad_inst = -1;
			AndroidInputInit();   /* fall back to another pad if present */
		}
	}
}

void AndroidInputShutdown( void )
{
	if ( pad )
	{
		SDL_GameControllerClose( pad );
		pad = NULL;
	}
	pad_inst = -1;
}

/*===================================================================
	Build the frame's intent from pad + touch, then hand it to the
	shared apply_intent().
===================================================================*/
void AndroidReadInput( SHIPCONTROL *ctrl )
{
	INPUT_INTENT in;
	float lx, ly, rx, ry;
	float fwd, rev;
	float pitch_sign;

	if ( !ctrl )
		return;

	intent_clear( &in );

	{
		SDL_GameControllerAxis sx = (SDL_GameControllerAxis) cfg_axis[ PADAXIS_Strafe ];
		SDL_GameControllerAxis sy = (SDL_GameControllerAxis) cfg_axis[ PADAXIS_Vertical ];
		SDL_GameControllerAxis yx = (SDL_GameControllerAxis) cfg_axis[ PADAXIS_Yaw ];
		SDL_GameControllerAxis yy = (SDL_GameControllerAxis) cfg_axis[ PADAXIS_Pitch ];
		if ( sx >= 0 && sy >= 0 )
			stick_pair( sx, sy, cfg_deadzone_left,  &lx, &ly );
		else
			lx = ly = 0.0f;
		if ( yx >= 0 && yy >= 0 )
			stick_pair( yx, yy, cfg_deadzone_right, &rx, &ry );
		else
			rx = ry = 0.0f;
	}

	rx += g_touch_rx;  ry += g_touch_ry;
	lx += g_touch_lx;

	if ( lx >  1.0f ) lx =  1.0f;  if ( lx < -1.0f ) lx = -1.0f;
	if ( ly >  1.0f ) ly =  1.0f;  if ( ly < -1.0f ) ly = -1.0f;
	if ( rx >  1.0f ) rx =  1.0f;  if ( rx < -1.0f ) rx = -1.0f;
	if ( ry >  1.0f ) ry =  1.0f;  if ( ry < -1.0f ) ry = -1.0f;

	pitch_sign = cfg_invert_pitch ? -1.0f : 1.0f;

	fwd = ( cfg_axis[ PADAXIS_Forward ] >= 0 )
		? trigger( (SDL_GameControllerAxis) cfg_axis[ PADAXIS_Forward ] ) : 0.0f;
	rev = ( cfg_axis[ PADAXIS_Reverse ] >= 0 )
		? trigger( (SDL_GameControllerAxis) cfg_axis[ PADAXIS_Reverse ] ) : 0.0f;
	in.forward = fwd - rev;

	in.forward -= g_touch_ly;

	if ( in.forward >  1.0f ) in.forward =  1.0f;
	if ( in.forward < -1.0f ) in.forward = -1.0f;

	/*---------------------------------------------------------------
		Left stick: strafe + vertical.
		SDL reports stick up and left as negative; engine 'right' is
		positive-right and engine 'up' is positive-up, so Y is negated.

		Vertical uses the slide semantics, not a rotation.
	---------------------------------------------------------------*/
	in.right = curve( lx ) * cfg_move_sens;
	in.up    = curve( -ly ) * cfg_move_sens;
	in.slide_mode = 1;

	/*---------------------------------------------------------------
		Right stick: aim. Engine yaw is positive-right and pitch is
		positive-nose-up, so Y is negated before the invert flag.
	---------------------------------------------------------------*/
	in.yaw   = curve( rx )  * cfg_look_sens;
	in.pitch = curve( -ry ) * cfg_look_sens * pitch_sign;

	if ( bind_held( PADBIND_RollLeft ) || ( g_touch_buttons & TB_ROLL_LEFT ) )
		in.roll += 1.0f;
	if ( bind_held( PADBIND_RollRight ) || ( g_touch_buttons & TB_ROLL_RIGHT ) )
		in.roll -= 1.0f;

	if ( bind_held( PADBIND_FirePrimary ) || ( g_touch_buttons & TB_FIRE ) )
		in.fire_primary = 1;
	if ( bind_held( PADBIND_FireSecondary ) || ( g_touch_buttons & TB_SECONDARY ) )
		in.fire_secondary = 1;
	if ( bind_held( PADBIND_FireMine ) || ( g_touch_buttons & TB_MINE ) )
		in.fire_mine = 1;

	if ( bind_held( PADBIND_Turbo ) || ( g_touch_buttons & TB_TURBO ) )
		in.turbo = 1;

	/* Touch-only vertical buttons still work alongside the stick. */
	if ( g_touch_buttons & TB_UP )   in.up += 1.0f;
	if ( g_touch_buttons & TB_DOWN ) in.up -= 1.0f;

	if ( in.up >  1.0f ) in.up =  1.0f;
	if ( in.up < -1.0f ) in.up = -1.0f;

	if ( bind_pressed( PADBIND_PrevPrimary ) )    in.select_prev_primary = 1;
	if ( bind_pressed( PADBIND_NextPrimary ) )    in.select_next_primary = 1;
	if ( bind_pressed( PADBIND_PrevSecondary ) )  in.select_prev_secondary = 1;
	if ( bind_pressed( PADBIND_NextSecondary ) )  in.select_next_secondary = 1;

	apply_intent( ctrl, &in, framelag );
}

/*===================================================================
	Menu navigation.

	The title/menu state machine consumes discrete keycodes from the
	legacy input buffer, so rather than duplicating it, translate pad
	edges into the keycodes it already understands.
===================================================================*/
extern void input_buffer_send( int code );

void AndroidPadMenuInput( void )
{
	float lx, ly;
	static bool stick_latched = false;
	SDL_GameControllerAxis sx, sy;

	if ( !pad )
		return;

	if ( pressed( SDL_CONTROLLER_BUTTON_DPAD_UP ) )    input_buffer_send( SDLK_UP );
	if ( pressed( SDL_CONTROLLER_BUTTON_DPAD_DOWN ) )  input_buffer_send( SDLK_DOWN );
	if ( pressed( SDL_CONTROLLER_BUTTON_DPAD_LEFT ) )  input_buffer_send( SDLK_LEFT );
	if ( pressed( SDL_CONTROLLER_BUTTON_DPAD_RIGHT ) ) input_buffer_send( SDLK_RIGHT );

	if ( bind_pressed( PADBIND_FirePrimary ) ) input_buffer_send( SDLK_RETURN );
	if ( pressed( SDL_CONTROLLER_BUTTON_B ) )  input_buffer_send( SDLK_ESCAPE );
	if ( bind_pressed( PADBIND_Pause ) )       input_buffer_send( SDLK_ESCAPE );

	sx = (SDL_GameControllerAxis) cfg_axis[ PADAXIS_Strafe ];
	sy = (SDL_GameControllerAxis) cfg_axis[ PADAXIS_Vertical ];
	if ( sx >= 0 && sy >= 0 )
		stick_pair( sx, sy, cfg_deadzone_left, &lx, &ly );
	else
		lx = ly = 0.0f;

	if ( !stick_latched )
	{
		if ( ly < -0.5f ) { input_buffer_send( SDLK_UP );    stick_latched = true; }
		else if ( ly >  0.5f ) { input_buffer_send( SDLK_DOWN );  stick_latched = true; }
		else if ( lx < -0.5f ) { input_buffer_send( SDLK_LEFT );  stick_latched = true; }
		else if ( lx >  0.5f ) { input_buffer_send( SDLK_RIGHT ); stick_latched = true; }
	}
	else if ( lx > -0.35f && lx < 0.35f && ly > -0.35f && ly < 0.35f )
	{
		stick_latched = false;
	}
}

/*===================================================================
	JNI glue for the touch overlay.
===================================================================*/
#include <jni.h>

JNIEXPORT void JNICALL
Java_org_forsakenx_forsaken_TouchOverlayView_nativeSetSticks(
    JNIEnv *env, jobject thiz, jfloat lx_, jfloat ly_, jfloat rx_, jfloat ry_) {
    (void) env; (void) thiz;
    g_touch_lx = lx_;
    g_touch_ly = ly_;
    g_touch_rx = rx_;
    g_touch_ry = ry_;
}

JNIEXPORT void JNICALL
Java_org_forsakenx_forsaken_TouchOverlayView_nativeSetButtons(
    JNIEnv *env, jobject thiz, jint mask) {
    (void) env; (void) thiz;
    g_touch_buttons = (u_int32_t) mask;
}

JNIEXPORT jboolean JNICALL
Java_org_forsakenx_forsaken_TouchOverlayView_nativeIsPadConnected(
    JNIEnv *env, jobject thiz) {
    (void) env; (void) thiz;
    return AndroidPadConnected() ? JNI_TRUE : JNI_FALSE;
}
