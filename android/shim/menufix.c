#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <android/log.h>
#include <string.h>
#include <stdbool.h>

#define SDLK_UP 1073741906
#define SDLK_DOWN 1073741905
#define SDLK_LEFT 1073741904
#define SDLK_RIGHT 1073741903
#define SDLK_RETURN 13
#define SDLK_ESCAPE 27

static float *p_lx=NULL, *p_ly=NULL, *p_rx=NULL, *p_ry=NULL;
static unsigned int *p_buttons=NULL;
static void (*p_send)(int)=NULL;

static void* thread_func(void* arg) {
    (void)arg;
    for (int i=0;i<50 && !p_send;i++) {
        p_send = dlsym(RTLD_DEFAULT, "input_buffer_send");
        if (!p_send) p_send = dlsym(RTLD_NEXT, "input_buffer_send");
        usleep(100000);
    }
    // resolve globals
    for (int i=0;i<50 && (!p_lx || !p_ly);i++) {
        p_lx = dlsym(RTLD_DEFAULT, "g_touch_lx");
        p_ly = dlsym(RTLD_DEFAULT, "g_touch_ly");
        p_rx = dlsym(RTLD_DEFAULT, "g_touch_rx");
        p_ry = dlsym(RTLD_DEFAULT, "g_touch_ry");
        p_buttons = dlsym(RTLD_DEFAULT, "g_touch_buttons");
        if (!p_lx) p_lx = dlsym(RTLD_NEXT, "g_touch_lx");
        if (!p_ly) p_ly = dlsym(RTLD_NEXT, "g_touch_ly");
        if (!p_rx) p_rx = dlsym(RTLD_NEXT, "g_touch_rx");
        if (!p_ry) p_ry = dlsym(RTLD_NEXT, "g_touch_ry");
        if (!p_buttons) p_buttons = dlsym(RTLD_NEXT, "g_touch_buttons");
        usleep(100000);
    }
    __android_log_print(ANDROID_LOG_INFO, "menufix", "p_send=%p lx=%p ly=%p btn=%p", p_send, p_lx, p_ly, p_buttons);
    bool latched=false;
    unsigned int prev_btn=0;
    while(1) {
        usleep(100000);
        if (!p_send || !p_lx || !p_ly || !p_buttons) continue;
        float lx = *p_lx, ly = *p_ly;
        unsigned int btn = *p_buttons;
        // stick menu nav (left stick)
        if (!latched) {
            if (ly < -0.5f) { p_send(SDLK_UP); latched=true; }
            else if (ly > 0.5f) { p_send(SDLK_DOWN); latched=true; }
            else if (lx < -0.5f) { p_send(SDLK_LEFT); latched=true; }
            else if (lx > 0.5f) { p_send(SDLK_RIGHT); latched=true; }
        } else if (lx > -0.35f && lx < 0.35f && ly > -0.35f && ly < 0.35f) {
            latched=false;
        }
        // button edge for FIRE -> RETURN, MINE -> ESCAPE (as example)
        // TB_FIRE=1, TB_MINE=0x80? Actually 1<<7=128
        // Use FIRE for RETURN, TURBO/MINE for ESCAPE
        if ((btn & 1) && !(prev_btn & 1)) p_send(SDLK_RETURN);
        if ((btn & 0x80) && !(prev_btn & 0x80)) p_send(SDLK_ESCAPE);
        if ((btn & 0x10) && !(prev_btn & 0x10)) p_send(SDLK_ESCAPE); // TURBO?
        prev_btn = btn;
    }
    return NULL;
}

__attribute__((constructor))
static void init(void) {
    pthread_t t;
    pthread_create(&t, NULL, thread_func, NULL);
    pthread_detach(t);
    __android_log_print(ANDROID_LOG_INFO, "menufix", "menufix loaded, thread started");
}

// Add SDL hint in constructor
__attribute__((constructor))
static void init2(void) {
    void* h = dlsym(RTLD_DEFAULT, "SDL_SetHint");
    if (!h) h = dlsym(RTLD_NEXT, "SDL_SetHint");
    if (h) {
        ((int(*)(const char*,const char*))h)("SDL_ACCELEROMETER_AS_JOYSTICK","0");
        __android_log_print(ANDROID_LOG_INFO, "menufix", "SDL hint set ACCEL 0");
    }
}
#include <SDL2/SDL_joystick.h>
SDL_Joystick* SDL_JoystickOpen(int index) {
    static SDL_Joystick* (*real)(int)=0;
    if (!real) real = dlsym(RTLD_NEXT, "SDL_JoystickOpen");
    const char* name = NULL;
    void* h2 = dlsym(RTLD_DEFAULT, "SDL_JoystickNameForIndex");
    if (!h2) h2 = dlsym(RTLD_NEXT, "SDL_JoystickNameForIndex");
    if (h2) name = ((const char*(*)(int))h2)(index);
    if (name && strcasestr(name, "Accelerometer")) {
        __android_log_print(ANDROID_LOG_INFO, "menufix", "blocked Accelerometer joystick %d", index);
        return NULL;
    }
    if (real) return real(index);
    return NULL;
}
