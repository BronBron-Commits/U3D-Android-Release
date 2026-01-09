#include <jni.h>
#include <android/log.h>
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <cmath>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "U3D", __VA_ARGS__)

/* ================= ROTATION + INERTIA ================= */

static float g_rotX = 0.0f;
static float g_rotY = 0.0f;

static float g_velX = 0.0f;
static float g_velY = 0.0f;

static constexpr float INERTIA_SCALE = 0.075f;
static constexpr float DAMPING = 0.94f;

/* ================= JNI ================= */

extern "C"
JNIEXPORT void JNICALL
Java_com_example_u3d_MainActivity_nativeRotate(
        JNIEnv*, jobject, float dx, float dy) {
    g_velY += dx * INERTIA_SCALE;
    g_velX += dy * INERTIA_SCALE;
}

/* ================= SHADERS ================= */

const char* VS =
        "attribute vec3 aPos;\n"
        "attribute vec3 aColor;\n"
        "uniform mat4 uMVP;\n"
        "varying vec3 vColor;\n"
        "void main(){\n"
        "  vColor = aColor;\n"
        "  gl_Position = uMVP * vec4(aPos,1.0);\n"
        "}\n";

const char* FS =
        "precision mediump float;\n"
        "varying vec3 vColor;\n"
        "void main(){\n"
        "  gl_FragColor = vec4(vColor,1.0);\n"
        "}\n";

GLuint compile(GLenum t, const char* s) {
    GLuint sh = glCreateShader(t);
    glShaderSource(sh, 1, &s, nullptr);
    glCompileShader(sh);
    return sh;
}

/* ================= MATH ================= */

void mat4_identity(float* m) {
    memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void mat4_rotate_x(float* m, float a) {
    mat4_identity(m);
    m[5]  = cosf(a);
    m[6]  = sinf(a);
    m[9]  = -sinf(a);
    m[10] = cosf(a);
}

void mat4_rotate_y(float* m, float a) {
    mat4_identity(m);
    m[0]  = cosf(a);
    m[2]  = -sinf(a);
    m[8]  = sinf(a);
    m[10] = cosf(a);
}

void mat4_translate(float* m, float x, float y, float z) {
    mat4_identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

void mat4_mul(float* o, const float* a, const float* b) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            o[c*4+r] =
                    a[0*4+r]*b[c*4+0] +
                    a[1*4+r]*b[c*4+1] +
                    a[2*4+r]*b[c*4+2] +
                    a[3*4+r]*b[c*4+3];
}

void mat4_perspective(float* m, float fov, float asp, float n, float f) {
    float t = tanf(fov * 0.5f);
    memset(m, 0, sizeof(float) * 16);
    m[0]  = 1.0f / (asp * t);
    m[5]  = 1.0f / t;
    m[10] = -(f + n) / (f - n);
    m[11] = -1.0f;
    m[14] = -(2.0f * f * n) / (f - n);
}

/* ================= ANDROID ================= */

struct State {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint program = 0;
    GLint uMVP = -1;
    float proj[16];
};

void handle_cmd(android_app* app, int32_t cmd) {
    auto* s = (State*)app->userData;

    if (cmd == APP_CMD_INIT_WINDOW) {
        s->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        eglInitialize(s->display, nullptr, nullptr);

        EGLint cfgAttr[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_DEPTH_SIZE, 16,
                EGL_NONE
        };

        EGLConfig cfg;
        EGLint n;
        eglChooseConfig(s->display, cfgAttr, &cfg, 1, &n);

        s->surface = eglCreateWindowSurface(
                s->display, cfg, app->window, nullptr);

        EGLint ctxAttr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        s->context = eglCreateContext(
                s->display, cfg, EGL_NO_CONTEXT, ctxAttr);

        eglMakeCurrent(s->display, s->surface, s->surface, s->context);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        GLuint vs = compile(GL_VERTEX_SHADER, VS);
        GLuint fs = compile(GL_FRAGMENT_SHADER, FS);

        s->program = glCreateProgram();
        glAttachShader(s->program, vs);
        glAttachShader(s->program, fs);
        glLinkProgram(s->program);
        glUseProgram(s->program);

        s->uMVP = glGetUniformLocation(s->program, "uMVP");

        EGLint w, h;
        eglQuerySurface(s->display, s->surface, EGL_WIDTH, &w);
        eglQuerySurface(s->display, s->surface, EGL_HEIGHT, &h);
        glViewport(0, 0, w, h);

        mat4_perspective(s->proj, 1.0f, (float)w / h, 0.1f, 50.0f);
    }
}

void android_main(android_app* app) {
    State s{};
    app->userData = &s;
    app->onAppCmd = handle_cmd;

    float cube[] = {
            // Front
            -0.5,-0.5, 0.5, 1,0,0,  0.5,-0.5, 0.5, 1,0,0,  0.5, 0.5, 0.5, 1,0,0,
            -0.5,-0.5, 0.5, 1,0,0,  0.5, 0.5, 0.5, 1,0,0, -0.5, 0.5, 0.5, 1,0,0,
            // Back
            -0.5,-0.5,-0.5, 0,1,0, -0.5, 0.5,-0.5, 0,1,0,  0.5, 0.5,-0.5, 0,1,0,
            -0.5,-0.5,-0.5, 0,1,0,  0.5, 0.5,-0.5, 0,1,0,  0.5,-0.5,-0.5, 0,1,0,
            // Left
            -0.5,-0.5,-0.5, 0,0,1, -0.5,-0.5, 0.5, 0,0,1, -0.5, 0.5, 0.5, 0,0,1,
            -0.5,-0.5,-0.5, 0,0,1, -0.5, 0.5, 0.5, 0,0,1, -0.5, 0.5,-0.5, 0,0,1,
            // Right
            0.5,-0.5,-0.5, 1,1,0,  0.5, 0.5,-0.5, 1,1,0,  0.5, 0.5, 0.5, 1,1,0,
            0.5,-0.5,-0.5, 1,1,0,  0.5, 0.5, 0.5, 1,1,0,  0.5,-0.5, 0.5, 1,1,0,
            // Top
            -0.5, 0.5,-0.5, 0,1,1, -0.5, 0.5, 0.5, 0,1,1,  0.5, 0.5, 0.5, 0,1,1,
            -0.5, 0.5,-0.5, 0,1,1,  0.5, 0.5, 0.5, 0,1,1,  0.5, 0.5,-0.5, 0,1,1,
            // Bottom
            -0.5,-0.5,-0.5, 1,0,1,  0.5,-0.5,-0.5, 1,0,1,  0.5,-0.5, 0.5, 1,0,1,
            -0.5,-0.5,-0.5, 1,0,1,  0.5,-0.5, 0.5, 1,0,1, -0.5,-0.5, 0.5, 1,0,1
    };

    GLuint vbo = 0;

    while (true) {
        int events;
        android_poll_source* src;

        while (ALooper_pollOnce(0, nullptr, &events, (void**)&src) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) return;
        }

        if (s.display == EGL_NO_DISPLAY) continue;

        if (!vbo) {
            glGenBuffers(1, &vbo);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW);

            GLint aPos = glGetAttribLocation(s.program, "aPos");
            GLint aColor = glGetAttribLocation(s.program, "aColor");

            glEnableVertexAttribArray(aPos);
            glVertexAttribPointer(aPos, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), 0);
            glEnableVertexAttribArray(aColor);
            glVertexAttribPointer(aColor, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float),
                                  (void*)(3*sizeof(float)));
        }

        /* ----- INERTIA UPDATE ----- */
        g_rotX += g_velX;
        g_rotY += g_velY;
        g_velX *= DAMPING;
        g_velY *= DAMPING;

        float rx[16], ry[16], t[16], model[16], mv[16], mvp[16];

        mat4_rotate_x(rx, g_rotX);
        mat4_rotate_y(ry, g_rotY);
        mat4_translate(t, 0.0f, 0.0f, -3.0f);

        mat4_mul(model, ry, rx);
        mat4_mul(mv, t, model);
        mat4_mul(mvp, s.proj, mv);

        glClearColor(0.05f, 0.05f, 0.1f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUniformMatrix4fv(s.uMVP, 1, GL_FALSE, mvp);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        eglSwapBuffers(s.display, s.surface);
    }
}
