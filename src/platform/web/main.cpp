#include <stdio.h>
#include <cstring>
#include <pthread.h>
#include <EGL/egl.h>

#include "game.h"

int lastJoy = -1;
EGLDisplay display;
EGLSurface surface;
EGLContext context;


// multi-threading
void* osMutexInit() {
    pthread_mutex_t *mutex = new pthread_mutex_t();
    pthread_mutex_init(mutex, NULL);
    return mutex;
}

void osMutexFree(void *obj) {
    pthread_mutex_destroy((pthread_mutex_t*)obj);
    delete (pthread_mutex_t*)obj;
}

void osMutexLock(void *obj) {
    pthread_mutex_lock((pthread_mutex_t*)obj);
}

void osMutexUnlock(void *obj) {
    pthread_mutex_unlock((pthread_mutex_t*)obj);
}

void* osRWLockInit() {
    pthread_rwlock_t *lock = new pthread_rwlock_t();
    pthread_rwlock_init(lock, NULL);
    return lock;
}

void osRWLockFree(void *obj) {
    pthread_rwlock_destroy((pthread_rwlock_t*)obj);
    delete (pthread_rwlock_t*)obj;
}

void osRWLockRead(void *obj) {
    pthread_rwlock_rdlock((pthread_rwlock_t*)obj);
}

void osRWUnlockRead(void *obj) {
    pthread_rwlock_unlock((pthread_rwlock_t*)obj);
}

void osRWLockWrite(void *obj) {
    pthread_rwlock_wrlock((pthread_rwlock_t*)obj);
}

void osRWUnlockWrite(void *obj) {
    pthread_rwlock_unlock((pthread_rwlock_t*)obj);
}


// timing
int osGetTime() {
    return (int)emscripten_get_now();
}

bool osSave(const char *name, const void *data, int size) {
// TODO cookie? idb?
    FILE *f = fopen(name, "wb");
    if (!f) return false;
    fwrite(data, size, 1, f);
    fclose(f);
    return true;
}

extern "C" {
    void EMSCRIPTEN_KEEPALIVE snd_fill(Sound::Frame *frames, int count) {
        Sound::fill(frames, count);
    }
    
    void EMSCRIPTEN_KEEPALIVE game_level_load(char *data, int size) {
        Game::startLevel(new Stream(data, size));
    }
}

JoyKey joyToInputKey(int code, int type) {
    static const JoyKey keys[2][16] = {
        { jkA, jkB, jkX, jkY, jkLB, jkRB, jkLT, jkRT, jkSelect, jkStart, jkL, jkR, jkNone, jkNone, jkNone, jkNone },
        { jkA, jkB, jkNone, jkX, jkY, jkNone, jkLB, jkRB, jkLT, jkRT, jkSelect, jkStart, jkNone, jkL, jkR, jkNone },
    };

    if (code >= 0 && code < 16)
        return keys[type][code];
    
    return jkNone;
}

int joyGetPOV(int mask) {
    switch (mask) {
        case 0b0001 : return 1;
        case 0b1001 : return 2;
        case 0b1000 : return 3;
        case 0b1010 : return 4;
        case 0b0010 : return 5;
        case 0b0110 : return 6;
        case 0b0100 : return 7;
        case 0b0101 : return 8;
    }
    return 0;
}

#define JOY_DEAD_ZONE_STICK      0.3f
#define JOY_DEAD_ZONE_TRIGGER    0.01f

vec2 joyAxis(float x, float y) {
    if (fabsf(x) > JOY_DEAD_ZONE_STICK || fabsf(y) > JOY_DEAD_ZONE_STICK)
        return vec2(x, y);
    return vec2(0.0f);
}

vec2 joyTrigger(float x) {
    return vec2(x > JOY_DEAD_ZONE_TRIGGER ? x : 0.0f, 0.0f);
}

int joyIndex[2] = { -1, -1 };
int joyType[2]  = { 0, 0 };

void joyUpdate(int index) {
    EmscriptenGamepadEvent state;
    
    if (joyIndex[index] == -1) {
        int count = emscripten_get_num_gamepads();
        for (int i = 0; i < count; i++) {
            if (emscripten_get_gamepad_status(i, &state) == EMSCRIPTEN_RESULT_SUCCESS && state.numButtons >= 12 && i != joyIndex[index ^ 1]) {

                if (state.digitalButton[9]) {
                    joyType[index] = 0;
                } else if (state.digitalButton[11]) {
                    joyType[index] = 1;
                } else
                    continue;
                joyIndex[index] = i;
                
                LOG("using gamepad %d as joy%d type%d\n", i, index, joyType[index]);
                break;
            }
        }
    }
    
    if (joyIndex[index] == -1)
        return;
    
    if (emscripten_get_gamepad_status(joyIndex[index], &state) != EMSCRIPTEN_RESULT_SUCCESS || state.numButtons < 12) {
        joyIndex[index] = -1;
        return;
    }

    for (int i = 0; i < state.numButtons; i++) {
        JoyKey key = joyToInputKey(i, joyType[index]);
        Input::setJoyDown(index, key, state.digitalButton[i]);
        if (key == jkLT || key == jkRT)
            Input::setJoyPos(index, key, joyTrigger(state.analogButton[i]));
    }

    if (joyType[index] == 0 && state.numButtons > 15) { // get POV
        auto &b = state.digitalButton;
        int pov = joyGetPOV(b[12] | (b[13] << 1) | (b[14] << 2) | (b[15] << 3));
        Input::setJoyPos(index, jkPOV, vec2((float)pov, 0.0f));
    }
    
    if (joyType[index] == 1) {
        // TODO
    }

    if (state.numAxes > 1) Input::setJoyPos(index, jkL, joyAxis(state.axis[0], state.axis[1]));
    if (state.numAxes > 3 && joyType[index] == 0) Input::setJoyPos(index, jkR, joyAxis(state.axis[2], state.axis[3]));
    if (state.numAxes > 5 && joyType[index] == 1) Input::setJoyPos(index, jkR, joyAxis(state.axis[3], state.axis[5]));
}

void main_loop() {
    joyUpdate(0);
    joyUpdate(1);

    if (Game::update()) {
		Game::render();
		eglSwapBuffers(display, surface);
	}
}

bool initGL() {
    EGLint vMajor, vMinor, cfgCount;
    EGLConfig cfg;

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE, EGL_NONE };
    EGLint attribList[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 8,
        EGL_STENCIL_SIZE, EGL_DONT_CARE,
        EGL_SAMPLE_BUFFERS, EGL_DONT_CARE,
        EGL_NONE
    };

    display = eglGetDisplay((EGLNativeDisplayType)EGL_DEFAULT_DISPLAY);
    eglInitialize(display, &vMajor, &vMinor);
    eglGetConfigs(display, NULL, 0, &cfgCount);
    eglChooseConfig(display, attribList, &cfg, 1, &cfgCount);
    surface = eglCreateWindowSurface(display, cfg, NULL, NULL);
    context = eglCreateContext(display, cfg, EGL_NO_CONTEXT, contextAttribs);
    eglMakeCurrent(display, surface, surface, context);
    return true;
}

void freeGL() {
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
}

EM_BOOL resize() {
    //int f;
    //emscripten_get_canvas_size(&Core::width, &Core::height, &f);
	double w, h;
	emscripten_get_element_css_size(NULL, &w, &h);
	Core::width  = int(w);
	Core::height = int(h);
	emscripten_set_canvas_size(Core::width, Core::height);
	LOG("resize %d %d\n", Core::width, Core::height);
    return 1;
}

EM_BOOL resizeCallback(int eventType, const EmscriptenUiEvent *e, void *userData) {
    return resize();
}

EM_BOOL fullscreenCallback(int eventType, const void *reserved, void *userData) {
    return resize();
}

bool isFullScreen() {
	EmscriptenFullscreenChangeEvent status;
	emscripten_get_fullscreen_status(&status);
	return status.isFullscreen;
}

void changeWindowMode() {
    if (!isFullScreen()) {
        EmscriptenFullscreenStrategy s;
        s.scaleMode                 = EMSCRIPTEN_FULLSCREEN_SCALE_STRETCH;
        s.canvasResolutionScaleMode = EMSCRIPTEN_FULLSCREEN_CANVAS_SCALE_HIDEF;
        s.filteringMode             = EMSCRIPTEN_FULLSCREEN_FILTERING_DEFAULT;
        s.canvasResizedCallback     = fullscreenCallback;
        emscripten_request_fullscreen_strategy(NULL, 1, &s);
    } else
        emscripten_exit_fullscreen();
}

InputKey keyToInputKey(int code) {
    static const int codes[] = {
        0x25, 0x27, 0x26, 0x28, 0x20, 0x09, 0x0D, 0x1B, 0x10, 0x11, 0x12,
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
        'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    };

    for (int i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
        if (codes[i] == code)
            return (InputKey)(ikLeft + i);
    return ikNone;
}

EM_BOOL keyCallback(int eventType, const EmscriptenKeyboardEvent *e, void *userData) {
    switch(eventType) {
        case EMSCRIPTEN_EVENT_KEYDOWN:
        case EMSCRIPTEN_EVENT_KEYUP:
            if (eventType == EMSCRIPTEN_EVENT_KEYDOWN && e->altKey && e->keyCode == 0x0D) {  // Alt + Enter
                changeWindowMode();
                break;
            }
            Input::setDown(keyToInputKey(e->keyCode), eventType == EMSCRIPTEN_EVENT_KEYDOWN);
            break;
    }
    return 1;
}

EM_BOOL touchCallback(int eventType, const EmscriptenTouchEvent *e, void *userData) {
    for (int i = 0; i < e->numTouches; i++) {
        if (!e->touches[i].isChanged) continue;
        InputKey key = Input::getTouch(e->touches[i].identifier);
        if (key == ikNone) continue;
        Input::setPos(key, vec2(e->touches[i].canvasX, e->touches[i].canvasY));

        if (eventType == EMSCRIPTEN_EVENT_TOUCHSTART || eventType == EMSCRIPTEN_EVENT_TOUCHEND || eventType == EMSCRIPTEN_EVENT_TOUCHCANCEL) 
            Input::setDown(key, eventType == EMSCRIPTEN_EVENT_TOUCHSTART);
    }
    return 1;
}

EM_BOOL mouseCallback(int eventType, const EmscriptenMouseEvent *e, void *userData) {
    switch (eventType) {
        case EMSCRIPTEN_EVENT_MOUSEDOWN :
        case EMSCRIPTEN_EVENT_MOUSEUP : {
            InputKey key = e->button == 0 ? ikMouseL : (e->button == 2 ? ikMouseR : ikMouseM);
            Input::setPos(key, vec2((float)e->clientX, (float)e->clientY));
            Input::setDown(key, eventType != EMSCRIPTEN_EVENT_MOUSEUP);
            break;
        }
        case EMSCRIPTEN_EVENT_MOUSEMOVE :
            Input::setPos(ikMouseL, vec2((float)e->clientX, (float)e->clientY));
            break;
    }
    return 1;
}

const char *IDB = "db";

void onError(void *arg) {
    Stream *stream = (Stream*)arg;
    LOG("! IDB error for %s\n", stream->name);
    stream->callback(NULL, stream->userData);
    delete stream;
}

void onLoad(void *arg, void *data, int size) {
    Stream *stream = (Stream*)arg;

    FILE *f = fopen(stream->name, "wb");
    fwrite(data, size, 1, f);
    fclose(f);

    stream->callback(new Stream(stream->name), stream->userData);	
    delete stream;
}

void onLoadAndStore(void *arg, void *data, int size) {
    emscripten_idb_async_store(IDB, ((Stream*)arg)->name, data, size, NULL, NULL, onError);
    onLoad(arg, data, size);
}

void onExists(void *arg, int exists) {
    if (exists)
        emscripten_idb_async_load(IDB, ((Stream*)arg)->name, arg, onLoad, onError);
    else
        emscripten_async_wget_data(((Stream*)arg)->name, arg, onLoadAndStore, onError);
}

void osDownload(Stream *stream) {
    emscripten_idb_async_exists(IDB, stream->name, stream, onExists, onError);
}

char Stream::cacheDir[255];
char Stream::contentDir[255];

int main() {
    Stream::contentDir[0] = Stream::cacheDir[0] = 0;
    
    initGL(); 

    emscripten_set_keydown_callback(0, 0, 1, keyCallback);
    emscripten_set_keyup_callback(0, 0, 1, keyCallback);
    emscripten_set_resize_callback(0, 0, 0, resizeCallback);

    emscripten_set_touchstart_callback(0, 0, 0, touchCallback);
    emscripten_set_touchend_callback(0, 0, 0, touchCallback);
    emscripten_set_touchmove_callback(0, 0, 0, touchCallback);
    emscripten_set_touchcancel_callback(0, 0, 0, touchCallback);

    emscripten_set_mousedown_callback(0, 0, 1, mouseCallback);
    emscripten_set_mouseup_callback(0, 0, 1, mouseCallback);
    emscripten_set_mousemove_callback(0, 0, 1, mouseCallback);
    
    Game::init();
    emscripten_run_script("snd_init()");
    resize();

    emscripten_set_main_loop(main_loop, 0, true);

    Game::deinit();
    freeGL();

    return 0;
}