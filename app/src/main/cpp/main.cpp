#include <android/native_activity.h>
#include <android/input.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <unistd.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define NUM_AGENTS 2
#define PICK_RADIUS 0.9f
#define ROT_SENS 0.005f
#define ROT_DAMP 0.96f
#define JOY_RADIUS   0.25f   // size in NDC
#define JOY_Y_OFFSET -0.75f  // bottom of screen
#define JOY_LEFT_X  -0.6f
#define JOY_RIGHT_X  0.6f

/* ================= WORLD SHADERS ================= */

const char *vs_src =
        "attribute vec3 aPos;\n"
        "attribute vec3 aColor;\n"
        "attribute vec3 aNormal;\n"
        "uniform mat4 uMVP;\n"
        "uniform mat4 uWorld;\n"
        "varying vec3 vColor;\n"
        "varying vec3 vNormal;\n"
        "void main(){\n"
        "  vColor = aColor;\n"
        "  vNormal = mat3(uWorld) * aNormal;\n"
        "  gl_Position = uMVP * vec4(aPos,1.0);\n"
        "}\n";

const char *fs_src =
        "precision mediump float;\n"
        "varying vec3 vColor;\n"
        "varying vec3 vNormal;\n"
        "uniform float uSelected;\n"
        "void main(){\n"
        "  vec3 N = normalize(vNormal);\n"
        "  vec3 L = normalize(vec3(-0.4,-1.0,-0.6));\n"
        "  vec3 V = vec3(0.0,0.0,1.0);\n"
        "  float d = max(dot(N,-L),0.0);\n"
        "  vec3 H = normalize(-L+V);\n"
        "  float s = pow(max(dot(N,H),0.0),24.0);\n"
        "  vec3 base = vColor*(0.25+d*0.75)+vec3(s*0.35);\n"
        "  vec3 highlight = base + uSelected * vec3(0.4, 0.4, 0.2);\n"
        "  gl_FragColor = vec4(highlight,1.0);\n"
        "}\n";

/* ================= AXIS SHADERS ================= */

const char *axis_vs =
        "attribute vec3 aPos;\n"
        "attribute vec3 aColor;\n"
        "uniform mat4 uMVP;\n"
        "varying vec3 vColor;\n"
        "void main(){\n"
        "  vColor = aColor;\n"
        "  gl_Position = uMVP * vec4(aPos,1.0);\n"
        "}\n";

const char *axis_fs =
        "precision mediump float;\n"
        "varying vec3 vColor;\n"
        "void main(){\n"
        "  gl_FragColor = vec4(vColor,1.0);\n"
        "}\n";

/* ================= CURSOR SHADERS (SCREEN SPACE) ================= */
const char *cursor_vs =
        "attribute vec2 aPos;\n"
        "attribute vec3 aColor;\n"
        "uniform vec2 uCursor;\n"
        "varying vec3 vColor;\n"
        "void main(){\n"
        "  vColor = aColor;\n"
        "  gl_Position = vec4(aPos + uCursor, 0.0, 1.0);\n"
        "}\n";


const char *cursor_fs =
        "precision mediump float;\n"
        "varying vec3 vColor;\n"
        "void main(){\n"
        "  vec3 glow = vColor * 2.5;\n"
        "  gl_FragColor = vec4(glow, 1.0);\n"
        "}\n";


/* ================= MATH ================= */

void mat4_identity(float *m){ memset(m,0,64); m[0]=m[5]=m[10]=m[15]=1; }
void mat4_translate(float *m,float x,float y,float z){ mat4_identity(m); m[12]=x; m[13]=y; m[14]=z; }
void mat4_scale(float *m,float x,float y,float z){ mat4_identity(m); m[0]=x; m[5]=y; m[10]=z; }
void mat4_rotate_y(float *m,float a){ mat4_identity(m); m[0]=cosf(a); m[2]=-sinf(a); m[8]=sinf(a); m[10]=cosf(a); }
void mat4_rotate_x(float *m,float a){
    mat4_identity(m);
    m[5] = cosf(a);
    m[6] = sinf(a);
    m[9] = -sinf(a);
    m[10]= cosf(a);
}
void mat4_mul(float *o,float *a,float *b){
    for(int c=0;c<4;c++) for(int r=0;r<4;r++)
            o[c*4+r]=a[r]*b[c*4]+a[4+r]*b[c*4+1]+a[8+r]*b[c*4+2]+a[12+r]*b[c*4+3];
}
void mat4_perspective(float *m,float fov,float asp,float n,float f){
    float t=tanf(fov*0.5f); memset(m,0,64);
    m[0]=1/(asp*t); m[5]=1/t; m[10]=-(f+n)/(f-n); m[11]=-1; m[14]=-(2*f*n)/(f-n);
}

/* ================= DRAW ================= */

void draw_cube(GLint uMVP,GLint uWorld,float *proj,float *view,float *model){
    float t2[16],mvp[16];
    mat4_mul(t2,view,model);
    mat4_mul(mvp,proj,t2);
    glUniformMatrix4fv(uWorld,1,GL_FALSE,model);
    glUniformMatrix4fv(uMVP,1,GL_FALSE,mvp);
    glDrawArrays(GL_TRIANGLES,0,36);
}

/* ================= DATA ================= */

typedef struct {
    float x, y;
    float rot, rot_vel;

    /* Procedural parameters */
    float height;
    float width;
    float depth;

    float r, g, b;

    float anim_phase;
} Agent;

Agent agents[NUM_AGENTS];

GLuint compile(GLenum t,const char *s){
    GLuint sh=glCreateShader(t);
    glShaderSource(sh,1,&s,NULL);
    glCompileShader(sh);
    return sh;
}

struct Engine{
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int width,height;
    int grabbed,selected;
    float last_x,last_y;
    float cursor_ndc_x;
    float cursor_ndc_y;
    float joyL_x, joyL_y;
    float joyR_x, joyR_y;
    bool  joyL_active;
    bool  joyR_active;
    float cam_yaw;
    float cam_pitch;
    float cam_x;
    float cam_y;
    float cam_z;

} engine;



/* ================= INPUT ================= */

static int32_t handle_input(struct android_app*,AInputEvent* e) {
    if (AInputEvent_getType(e) != AINPUT_EVENT_TYPE_MOTION) return 0;
    float x = AMotionEvent_getX(e, 0);
    float y = AMotionEvent_getY(e, 0);
    engine.cursor_ndc_x = (x / engine.width) * 2.0f - 1.0f;
    engine.cursor_ndc_y = 1.0f - (y / engine.height) * 2.0f;
    float cx = engine.cursor_ndc_x;
    float cy = engine.cursor_ndc_y;

/* Left joystick */
    float dxL = cx - JOY_LEFT_X;
    float dyL = cy - JOY_Y_OFFSET;
    if (dxL * dxL + dyL * dyL < JOY_RADIUS * JOY_RADIUS) {
        engine.joyL_active = true;
        engine.joyL_x = -dxL / JOY_RADIUS;
        engine.joyL_y = dyL / JOY_RADIUS;
    }

/* Right joystick */
    float dxR = cx - JOY_RIGHT_X;
    float dyR = cy - JOY_Y_OFFSET;
    if (dxR * dxR + dyR * dyR < JOY_RADIUS * JOY_RADIUS) {
        engine.joyR_active = true;
        engine.joyR_x = dxR / JOY_RADIUS;
        engine.joyR_y = dyR / JOY_RADIUS;
    }

    if (engine.joyR_active) {
        float look_sens = 0.035f;

        // Left / Right look
        engine.cam_yaw += engine.joyR_x * look_sens;

        // Up / Down look (invert if desired)
        engine.cam_pitch -= engine.joyR_y * look_sens;

        // Clamp pitch so camera never flips
        if (engine.cam_pitch > 1.4f)  engine.cam_pitch = 1.4f;
        if (engine.cam_pitch < -1.4f) engine.cam_pitch = -1.4f;
    }



    float wx = (x / engine.width) * 8.0f - 4.0f;
    float wy = ((engine.height - y) / engine.height) * 10.0f - 5.0f;
    int a = AMotionEvent_getAction(e) & AMOTION_EVENT_ACTION_MASK;
    if (a == AMOTION_EVENT_ACTION_DOWN) {
        engine.last_x = x;
        engine.last_y = y;
        engine.grabbed = -1;
        engine.selected = -1;
        for (int i = 0; i < NUM_AGENTS; i++)
            if (fabsf(wx - agents[i].x) < PICK_RADIUS && fabsf(wy - agents[i].y) < PICK_RADIUS)
                engine.grabbed = engine.selected = i;
    }
    if (a == AMOTION_EVENT_ACTION_MOVE && engine.grabbed != -1) {
        float dx = x - engine.last_x;
        engine.last_x = x;
        agents[engine.grabbed].x = wx;
        agents[engine.grabbed].y = wy;
        agents[engine.grabbed].rot_vel += dx * ROT_SENS;
    }
    if (a == AMOTION_EVENT_ACTION_UP) {
        engine.joyL_active = false;
        engine.joyR_active = false;
        engine.joyL_x = engine.joyL_y = 0.0f;
        engine.joyR_x = engine.joyR_y = 0.0f;
        engine.grabbed = -1;
    }
    return 1;
}

/* ================= MAIN ================= */

    void android_main(struct android_app *app) {
        app->onInputEvent = handle_input;
        while (!app->window) {
            int ev;
            android_poll_source *src;
            ALooper_pollOnce(-1, NULL, &ev, (void **) &src);
            if (src) src->process(app, src);
        }

        engine.width = ANativeWindow_getWidth(app->window);
        engine.height = ANativeWindow_getHeight(app->window);
    engine.cursor_ndc_x = 0.0f;
    engine.cursor_ndc_y = 0.0f;

/* ===== INITIAL CAMERA POSE (GOOD DEFAULT) ===== */
    engine.cam_yaw   = 0.0f;     // facing +Z
    engine.cam_pitch = -0.25f;   // slight downward tilt
    engine.cam_x     = 0.0f;
    engine.cam_y     = -0.3f;
    engine.cam_z     = -6.0f;


    engine.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        eglInitialize(engine.display, NULL, NULL);

        EGLConfig cfg;
        EGLint n;
        EGLint cfg_attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE,
                             EGL_WINDOW_BIT, EGL_DEPTH_SIZE, 16, EGL_NONE};
        eglChooseConfig(engine.display, cfg_attr, &cfg, 1, &n);
        engine.surface = eglCreateWindowSurface(engine.display, cfg, app->window, NULL);
        EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        engine.context = eglCreateContext(engine.display, cfg, EGL_NO_CONTEXT, ctx_attr);
        eglMakeCurrent(engine.display, engine.surface, engine.surface, engine.context);

        glEnable(GL_DEPTH_TEST);

        /* world program */
        GLuint prog = glCreateProgram();
        glAttachShader(prog, compile(GL_VERTEX_SHADER, vs_src));
        glAttachShader(prog, compile(GL_FRAGMENT_SHADER, fs_src));
        glBindAttribLocation(prog, 0, "aPos");
        glBindAttribLocation(prog, 1, "aColor");
        glBindAttribLocation(prog, 2, "aNormal");
        glLinkProgram(prog);

        GLint uMVP = glGetUniformLocation(prog, "uMVP");
        GLint uWorld = glGetUniformLocation(prog, "uWorld");
        GLint uSelected = glGetUniformLocation(prog, "uSelected");

        /* cube geometry */
        float cube[] = {
                -0.5, -0.5, 0.5, 1, 0, 0, 0, 0, 1, 0.5, -0.5, 0.5, 1, 0, 0, 0, 0, 1, 0.5, 0.5, 0.5,
                1, 0, 0, 0, 0, 1,
                -0.5, -0.5, 0.5, 1, 0, 0, 0, 0, 1, 0.5, 0.5, 0.5, 1, 0, 0, 0, 0, 1, -0.5, 0.5, 0.5,
                1, 0, 0, 0, 0, 1,
                -0.5, -0.5, -0.5, 0, 1, 0, 0, 0, -1, -0.5, 0.5, -0.5, 0, 1, 0, 0, 0, -1, 0.5, 0.5,
                -0.5, 0, 1, 0, 0, 0, -1,
                -0.5, -0.5, -0.5, 0, 1, 0, 0, 0, -1, 0.5, 0.5, -0.5, 0, 1, 0, 0, 0, -1, 0.5, -0.5,
                -0.5, 0, 1, 0, 0, 0, -1,
                -0.5, -0.5, -0.5, 0, 0, 1, -1, 0, 0, -0.5, -0.5, 0.5, 0, 0, 1, -1, 0, 0, -0.5, 0.5,
                0.5, 0, 0, 1, -1, 0, 0,
                -0.5, -0.5, -0.5, 0, 0, 1, -1, 0, 0, -0.5, 0.5, 0.5, 0, 0, 1, -1, 0, 0, -0.5, 0.5,
                -0.5, 0, 0, 1, -1, 0, 0,
                0.5, -0.5, -0.5, 1, 1, 0, 1, 0, 0, 0.5, 0.5, -0.5, 1, 1, 0, 1, 0, 0, 0.5, 0.5, 0.5,
                1, 1, 0, 1, 0, 0,
                0.5, -0.5, -0.5, 1, 1, 0, 1, 0, 0, 0.5, 0.5, 0.5, 1, 1, 0, 1, 0, 0, 0.5, -0.5, 0.5,
                1, 1, 0, 1, 0, 0,
                -0.5, 0.5, -0.5, 0, 1, 1, 0, 1, 0, -0.5, 0.5, 0.5, 0, 1, 1, 0, 1, 0, 0.5, 0.5, 0.5,
                0, 1, 1, 0, 1, 0,
                -0.5, 0.5, -0.5, 0, 1, 1, 0, 1, 0, 0.5, 0.5, 0.5, 0, 1, 1, 0, 1, 0, 0.5, 0.5, -0.5,
                0, 1, 1, 0, 1, 0,
                -0.5, -0.5, -0.5, 1, 0, 1, 0, -1, 0, 0.5, -0.5, -0.5, 1, 0, 1, 0, -1, 0, 0.5, -0.5,
                0.5, 1, 0, 1, 0, -1, 0,
                -0.5, -0.5, -0.5, 1, 0, 1, 0, -1, 0, 0.5, -0.5, 0.5, 1, 0, 1, 0, -1, 0, -0.5, -0.5,
                0.5, 1, 0, 1, 0, -1, 0
        };

        GLuint vbo;
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW);
    /* ================= JOYSTICK RING ================= */
#define RING_SEGMENTS 48

    float joy_ring[RING_SEGMENTS * 5 * 2];

    for (int i = 0; i < RING_SEGMENTS; i++) {
        float a0 = (float)i / RING_SEGMENTS * 2.0f * M_PI;
        float a1 = (float)(i + 1) / RING_SEGMENTS * 2.0f * M_PI;

        int o = i * 10;

        joy_ring[o + 0] = cosf(a0) * JOY_RADIUS;
        joy_ring[o + 1] = sinf(a0) * JOY_RADIUS;
        joy_ring[o + 2] = 1.0f;   // bright pink
        joy_ring[o + 3] = 0.0f;
        joy_ring[o + 4] = 0.7f;

        joy_ring[o + 5] = cosf(a1) * JOY_RADIUS;
        joy_ring[o + 6] = sinf(a1) * JOY_RADIUS;
        joy_ring[o + 7] = 1.0f;
        joy_ring[o + 8] = 0.0f;
        joy_ring[o + 9] = 0.7f;
    }

    GLuint joy_ring_vbo;
    glGenBuffers(1, &joy_ring_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, joy_ring_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(joy_ring), joy_ring, GL_STATIC_DRAW);

    /* axis */
        float axis[] = {
                -5, 0, 0, 1, 0, 0, 5, 0, 0, 1, 0, 0,
                0, -5, 0, 0, 1, 0, 0, 5, 0, 0, 1, 0,
                0, 0, -5, 0, 0, 1, 0, 0, 5, 0, 0, 1
        };

        GLuint axis_vbo;
        glGenBuffers(1, &axis_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, axis_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(axis), axis, GL_STATIC_DRAW);

        GLuint axis_prog = glCreateProgram();
        glAttachShader(axis_prog, compile(GL_VERTEX_SHADER, axis_vs));
        glAttachShader(axis_prog, compile(GL_FRAGMENT_SHADER, axis_fs));
        glBindAttribLocation(axis_prog, 0, "aPos");
        glBindAttribLocation(axis_prog, 1, "aColor");
        glLinkProgram(axis_prog);
        GLint axis_uMVP = glGetUniformLocation(axis_prog, "uMVP");

        /* cursor */
        float cursor[] = {
                -0.05f, 0.0f, 1, 1, 1, 0.05f, 0.0f, 1, 1, 1,
                0.0f, -0.05f, 1, 1, 1, 0.0f, 0.05f, 1, 1, 1
        };;

        GLuint cursor_vbo;
        glGenBuffers(1, &cursor_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, cursor_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cursor), cursor, GL_STATIC_DRAW);
    /* ================= JOYSTICK GEOMETRY ================= */
/* ================= JOYSTICK CROSS (FULL AXES) ================= */
    float joy_cross[] = {
            /* Horizontal bar */
            -0.08f,  0.0f,   1.0f, 0.0f, 0.7f,
            0.08f,  0.0f,   1.0f, 0.0f, 0.7f,

            /* Vertical bar */
            0.0f,  -0.08f,  1.0f, 0.0f, 0.7f,
            0.0f,   0.08f,  1.0f, 0.0f, 0.7f
    };

    GLuint joy_vbo;
    glGenBuffers(1, &joy_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, joy_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(joy_cross), joy_cross, GL_STATIC_DRAW);



    GLuint cursor_prog = glCreateProgram();
        glAttachShader(cursor_prog, compile(GL_VERTEX_SHADER, cursor_vs));
        glAttachShader(cursor_prog, compile(GL_FRAGMENT_SHADER, cursor_fs));
        glBindAttribLocation(cursor_prog, 0, "aPos");
        glBindAttribLocation(cursor_prog, 1, "aColor");
        glLinkProgram(cursor_prog);
        GLint uCursor = glGetUniformLocation(cursor_prog, "uCursor");


        float proj[16], view[16], tmp[16], tr[16], ry[16], rx[16], rot[16];

    float fov = 1.35f;  // ~77 degrees (wide-angle)
    mat4_perspective(
            proj,
            fov,
            (float)engine.width / (float)engine.height,
            0.1f,
            50.0f
    );



    agents[0].x = -1.3f;
        agents[1].x = 1.3f;
    /* ===== PROCEDURAL CHARACTER SETUP ===== */
    agents[0].height = 1.4f;
    agents[0].width  = 0.7f;
    agents[0].depth  = 0.6f;
    agents[0].r = 0.9f; agents[0].g = 0.3f; agents[0].b = 0.3f;
    agents[0].anim_phase = 0.0f;

    agents[1].height = 0.9f;
    agents[1].width  = 1.0f;
    agents[1].depth  = 1.0f;
    agents[1].r = 0.3f; agents[1].g = 0.8f; agents[1].b = 1.0f;
    agents[1].anim_phase = 1.6f;

    while (true) {
            int ev;
            android_poll_source *src;
            while (ALooper_pollOnce(0, NULL, &ev, (void **) &src) >= 0)
                if (src)
                    src->process(app, src);
        for (int i = 0; i < NUM_AGENTS; i++) {
            agents[i].rot += agents[i].rot_vel;
        }


        /* ===== CAMERA UPDATE (EVERY FRAME) ===== */
            mat4_rotate_y(ry, engine.cam_yaw);
            mat4_rotate_x(rx, engine.cam_pitch);
            mat4_mul(rot, rx, ry);
            mat4_translate(tr, engine.cam_x, engine.cam_y, engine.cam_z);
            mat4_mul(view, tr, rot);


            glClearColor(0.05f, 0.05f, 0.08f, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            /* axes */
            glUseProgram(axis_prog);
            glBindBuffer(GL_ARRAY_BUFFER, axis_vbo);
            glDisableVertexAttribArray(2);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *) 0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                  (void *) (3 * sizeof(float)));
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            float id[16], axis_mvp[16];
            mat4_identity(id);
            mat4_mul(tmp, view, id);
            mat4_mul(axis_mvp, proj, tmp);
            glUniformMatrix4fv(axis_uMVP, 1, GL_FALSE, axis_mvp);
            glDrawArrays(GL_LINES, 0, 6);

            /* cubes */
            glUseProgram(prog);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void *) 0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                                  (void *) (3 * sizeof(float)));
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                                  (void *) (6 * sizeof(float)));
        /* ================= CHARACTER (MULTI-PRIMITIVE) ================= */

        int i = 0;  // single character for now

/* Root transform */
        float root[16];
        mat4_translate(root, agents[i].x, agents[i].y, 0);

/* Shared rotation */
        float rotY[16];
        mat4_rotate_y(rotY, agents[i].rot);

/* ================= TORSO ================= */
        {
            float t[16], s[16], m1[16], model[16];

            mat4_translate(t, 0.0f, 0.6f, 0.0f);
            mat4_scale(s, 0.9f, 1.2f, 0.5f);

            mat4_mul(m1, rotY, t);
            mat4_mul(m1, m1, s);
            mat4_mul(model, root, m1);

            glUniform1f(uSelected, 0.0f);
            draw_cube(uMVP, uWorld, proj, view, model);
        }

/* ================= HEAD ================= */
        {
            float t[16], s[16], m1[16], model[16];

            mat4_translate(t, 0.0f, 1.5f, 0.0f);
            mat4_scale(s, 0.5f, 0.5f, 0.5f);

            mat4_mul(m1, rotY, t);
            mat4_mul(m1, m1, s);
            mat4_mul(model, root, m1);

            draw_cube(uMVP, uWorld, proj, view, model);
        }

/* ================= LEFT LEG ================= */
        {
            float t[16], s[16], m1[16], model[16];

            mat4_translate(t, -0.3f, -0.3f, 0.0f);
            mat4_scale(s, 0.3f, 0.8f, 0.3f);

            mat4_mul(m1, rotY, t);
            mat4_mul(m1, m1, s);
            mat4_mul(model, root, m1);

            draw_cube(uMVP, uWorld, proj, view, model);
        }

/* ================= RIGHT LEG ================= */
        {
            float t[16], s[16], m1[16], model[16];

            mat4_translate(t, 0.3f, -0.3f, 0.0f);
            mat4_scale(s, 0.3f, 0.8f, 0.3f);

            mat4_mul(m1, rotY, t);
            mat4_mul(m1, m1, s);
            mat4_mul(model, root, m1);

            draw_cube(uMVP, uWorld, proj, view, model);
        }


        /* cursor overlay */
            glDisable(GL_DEPTH_TEST);
            glUseProgram(cursor_prog);
            glUniform2f(uCursor,
                        engine.cursor_ndc_x,
                        engine.cursor_ndc_y);
            glBindBuffer(GL_ARRAY_BUFFER, cursor_vbo);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *) 0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                                  (void *) (2 * sizeof(float)));
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glDrawArrays(GL_LINES, 0, 4);
            glEnable(GL_DEPTH_TEST);

            /* ================= JOYSTICKS ================= */
            glDisable(GL_DEPTH_TEST);
            glUseProgram(cursor_prog);

/* ---- RINGS ---- */
            glBindBuffer(GL_ARRAY_BUFFER, joy_ring_vbo);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)(2*sizeof(float)));
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);

/* Left ring */
            glUniform2f(uCursor, JOY_LEFT_X, JOY_Y_OFFSET);
            glDrawArrays(GL_LINES, 0, RING_SEGMENTS * 2);

/* Right ring */
            glUniform2f(uCursor, JOY_RIGHT_X, JOY_Y_OFFSET);
            glDrawArrays(GL_LINES, 0, RING_SEGMENTS * 2);

/* ---- CROSSES ---- */
            glBindBuffer(GL_ARRAY_BUFFER, joy_vbo);

/* Left base */
            glUniform2f(uCursor, JOY_LEFT_X, JOY_Y_OFFSET);
            glDrawArrays(GL_LINES, 0, 4);

/* Left thumb */
            glUniform2f(
                    uCursor,
                    JOY_LEFT_X + engine.joyL_x * JOY_RADIUS,
                    JOY_Y_OFFSET + engine.joyL_y * JOY_RADIUS
            );
            glDrawArrays(GL_LINES, 0, 4);

/* Right base */
            glUniform2f(uCursor, JOY_RIGHT_X, JOY_Y_OFFSET);
            glDrawArrays(GL_LINES, 0, 4);

/* Right thumb */
            glUniform2f(
                    uCursor,
                    JOY_RIGHT_X + engine.joyR_x * JOY_RADIUS,
                    JOY_Y_OFFSET + engine.joyR_y * JOY_RADIUS
            );
            glDrawArrays(GL_LINES, 0, 4);

            glEnable(GL_DEPTH_TEST);



            eglSwapBuffers(engine.display, engine.surface);
            usleep(16000);
        }
    }