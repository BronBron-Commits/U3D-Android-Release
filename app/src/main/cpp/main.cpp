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
/* ================= UI GLOBALS ================= */

GLuint axis_btn_vbo[3] = {0, 0, 0};
GLuint cursor_prog = 0;
GLint  uCursor     = -1;



#define NUM_AGENTS 2
#define PICK_RADIUS 0.9f
#define ROT_SENS 0.005f
#define ROT_DAMP 0.82f
#define JOY_RADIUS   0.25f   // size in NDC
#define JOY_Y_OFFSET -0.75f  // bottom of screen
#define JOY_LEFT_X  -0.6f
#define JOY_RIGHT_X  0.6f
#define LOCK_Y  0.85f
#define LOCK_SPACING 0.18f
#define LOCK_SIZE 0.06f
#define GRID_SIZE     20      // half-extent in world units
#define GRID_STEP     1.0f    // spacing
#define GRID_COLOR_R  0.35f
#define GRID_COLOR_G  0.35f
#define GRID_COLOR_B  0.35f

#define CAM_LOCK_START_X  -0.3f
#define OBJ_LOCK_START_X   0.3f
#define SEL_SEGMENTS 64
#define AXIS_BTN_RADIUS   0.06f
#define AXIS_BTN_SPACING  0.15f
#define AXIS_BTN_Y        -0.85f
#define AXIS_BTN_START_X  0.70f
#define AXIS_BTN_SEGMENTS 32

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
        "  float diff = max(dot(N,-L),0.0);\n"
        "  vec3 base = vColor * (0.25 + diff * 0.75);\n"
        "  float rim = 1.0 - max(dot(N, V), 0.0);\n"
        "  rim = smoothstep(0.4, 0.8, rim);\n"
        "  vec3 outline = vec3(1.0, 0.9, 0.3) * rim * uSelected * 1.5;\n"
        "  gl_FragColor = vec4(base + outline, 1.0);\n"
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

/* ================= SKYBOX SHADERS ================= */

const char *sky_vs =
        "attribute vec3 aPos;\n"
        "uniform mat4 uMVP;\n"
        "varying float vY;\n"
        "void main(){\n"
        "  vY = aPos.y;\n"
        "  gl_Position = uMVP * vec4(aPos, 1.0);\n"
        "}\n";

const char *sky_fs =
        "precision mediump float;\n"
        "varying float vY;\n"
        "void main(){\n"
        "  vec3 horizon = vec3(0.45, 0.65, 0.95);\n"
        "  vec3 zenith  = vec3(0.05, 0.10, 0.25);\n"
        "  float t = clamp(1.0 - ((vY + 1.0) * 0.5), 0.0, 1.0);\n"
        "  vec3 col = mix(horizon, zenith, t);\n"
        "  gl_FragColor = vec4(col, 1.0);\n"
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
    float x, y, z;
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
    /* ===== AXIS LOCKS ===== */
    bool lock_cam_x;
    bool lock_cam_y;
    bool lock_cam_z;

    bool lock_obj_x;
    bool lock_obj_y;
    bool lock_obj_z;
    float pinch_start_dist;
    float pinch_start_cam_z;
    float pinch_last_cx;
    float pinch_last_cy;

} engine;



/* ================= INPUT ================= */
static bool hit_box(float x, float y, float bx, float by) {
    return fabsf(x - bx) < LOCK_SIZE && fabsf(y - by) < LOCK_SIZE;
}

static int32_t handle_input(struct android_app*, AInputEvent* e) {
    if (AInputEvent_getType(e) != AINPUT_EVENT_TYPE_MOTION)
        return 0;

    float x = AMotionEvent_getX(e, 0);
    float y = AMotionEvent_getY(e, 0);

    engine.cursor_ndc_x = (x / engine.width) * 2.0f - 1.0f;
    engine.cursor_ndc_y = 1.0f - (y / engine.height) * 2.0f;

    float cx = engine.cursor_ndc_x;
    float cy = engine.cursor_ndc_y;

    int pointers = AMotionEvent_getPointerCount(e);
    int action   = AMotionEvent_getAction(e) & AMOTION_EVENT_ACTION_MASK;

/* ================= TWO-FINGER CAMERA CONTROL ================= */
    if (pointers == 2) {
        float x0 = AMotionEvent_getX(e, 0);
        float y0 = AMotionEvent_getY(e, 0);
        float x1 = AMotionEvent_getX(e, 1);
        float y1 = AMotionEvent_getY(e, 1);

        /* Pinch distance */
        float dx = x0 - x1;
        float dy = y0 - y1;
        float dist = sqrtf(dx*dx + dy*dy);

        /* Centroid (screen space) */
        float cx2 = (x0 + x1) * 0.5f;
        float cy2 = (y0 + y1) * 0.5f;

        if (action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            engine.pinch_start_dist  = dist;
            engine.pinch_start_cam_z = engine.cam_z;
            engine.pinch_last_cx     = cx2;
            engine.pinch_last_cy     = cy2;
            return 1;
        }

        if (action == AMOTION_EVENT_ACTION_MOVE && engine.pinch_start_dist > 0.0f) {
            /* ----- ZOOM ----- */
            float zoom_delta = dist - engine.pinch_start_dist;
            engine.cam_z = engine.pinch_start_cam_z + zoom_delta * 0.015f;

            if (engine.cam_z > -2.0f)  engine.cam_z = -2.0f;
            if (engine.cam_z < -40.0f) engine.cam_z = -40.0f;

            /* ----- ROTATE ----- */
            float dxc = cx2 - engine.pinch_last_cx;
            float dyc = cy2 - engine.pinch_last_cy;

            float rot_sens = 0.0055f;

            if (!engine.lock_cam_x)
                engine.cam_yaw   += dxc * rot_sens;

            if (!engine.lock_cam_y)
                engine.cam_pitch += dyc * rot_sens;

            /* Clamp pitch */
            if (engine.cam_pitch > 1.4f)  engine.cam_pitch = 1.4f;
            if (engine.cam_pitch < -1.4f) engine.cam_pitch = -1.4f;

            engine.pinch_last_cx = cx2;
            engine.pinch_last_cy = cy2;

            return 1;
        }
    }





    if ((AMotionEvent_getAction(e) & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_DOWN) {

        /* ---- CAMERA LOCKS ---- */
        if (hit_box(cx, cy, CAM_LOCK_START_X + 0*LOCK_SPACING, LOCK_Y))
            engine.lock_cam_x = !engine.lock_cam_x;

        if (hit_box(cx, cy, CAM_LOCK_START_X + 1*LOCK_SPACING, LOCK_Y))
            engine.lock_cam_y = !engine.lock_cam_y;

        if (hit_box(cx, cy, CAM_LOCK_START_X + 2*LOCK_SPACING, LOCK_Y))
            engine.lock_cam_z = !engine.lock_cam_z;

        /* ---- OBJECT LOCKS ---- */
        if (hit_box(cx, cy, OBJ_LOCK_START_X + 0*LOCK_SPACING, LOCK_Y))
            engine.lock_obj_x = !engine.lock_obj_x;

        if (hit_box(cx, cy, OBJ_LOCK_START_X + 1*LOCK_SPACING, LOCK_Y))
            engine.lock_obj_y = !engine.lock_obj_y;

        if (hit_box(cx, cy, OBJ_LOCK_START_X + 2*LOCK_SPACING, LOCK_Y))
            engine.lock_obj_z = !engine.lock_obj_z;
    }



    float wx = (x / engine.width) * 8.0f - 4.0f;
    float wy = ((engine.height - y) / engine.height) * 4.0f - 2.0f;
    float wz = ((engine.height - y) / engine.height) * 10.0f - 5.0f;


    int a = AMotionEvent_getAction(e) & AMOTION_EVENT_ACTION_MASK;
    if (a == AMOTION_EVENT_ACTION_DOWN) {
        engine.selected = -1;
        engine.grabbed  = -1;

        for (int i = 0; i < NUM_AGENTS; i++) {
            if (fabsf(wx - agents[i].x) < PICK_RADIUS &&
                fabsf(wy - agents[i].y) < PICK_RADIUS) {
                engine.selected = i;
                engine.grabbed  = i;
                engine.last_x   = x;
                engine.last_y   = y;
                break;
            }
        }
    }


    if (a == AMOTION_EVENT_ACTION_MOVE && engine.grabbed != -1) {
        float dx = x - engine.last_x;
        engine.last_x = x;

        if (!engine.lock_obj_x)
            agents[engine.grabbed].x = wx;

        if (!engine.lock_obj_y)
            agents[engine.grabbed].y = wy;

        if (!engine.lock_obj_z)
            agents[engine.grabbed].z = wz;



        agents[engine.grabbed].rot_vel += dx * (ROT_SENS * 0.35f);
    }

    if (engine.grabbed != -1) {
        if (agents[engine.grabbed].rot_vel > 0.08f)
            agents[engine.grabbed].rot_vel = 0.08f;

        if (agents[engine.grabbed].rot_vel < -0.08f)
            agents[engine.grabbed].rot_vel = -0.08f;
    }


    if (a == AMOTION_EVENT_ACTION_UP) {
        engine.joyL_active = false;
        engine.joyL_x = engine.joyL_y = 0.0f;
        engine.grabbed = -1;
        if (action == AMOTION_EVENT_ACTION_UP ||
            action == AMOTION_EVENT_ACTION_POINTER_UP) {
            engine.pinch_start_dist = 0.0f;
        }

    }
    return 1;
}


static void build_circle(float *out, int segments, float r, float cr, float cg, float cb) {
    int k = 0;
    for (int i = 0; i < segments; i++) {
        float a0 = (float)i / segments * 2.0f * M_PI;
        float a1 = (float)(i + 1) / segments * 2.0f * M_PI;

        out[k++] = cosf(a0) * r;
        out[k++] = sinf(a0) * r;
        out[k++] = cr; out[k++] = cg; out[k++] = cb;

        out[k++] = cosf(a1) * r;
        out[k++] = sinf(a1) * r;
        out[k++] = cr; out[k++] = cg; out[k++] = cb;
    }
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
        
    float axis_x_btn[AXIS_BTN_SEGMENTS * 2 * 5];
    float axis_y_btn[AXIS_BTN_SEGMENTS * 2 * 5];
    float axis_z_btn[AXIS_BTN_SEGMENTS * 2 * 5];

    build_circle(axis_x_btn, AXIS_BTN_SEGMENTS, AXIS_BTN_RADIUS, 1.0f, 0.3f, 0.3f); // X = red
    build_circle(axis_y_btn, AXIS_BTN_SEGMENTS, AXIS_BTN_RADIUS, 0.3f, 1.0f, 0.3f); // Y = green
    build_circle(axis_z_btn, AXIS_BTN_SEGMENTS, AXIS_BTN_RADIUS, 0.3f, 0.6f, 1.0f); // Z = blue


    glGenBuffers(3, axis_btn_vbo);

    glBindBuffer(GL_ARRAY_BUFFER, axis_btn_vbo[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(axis_x_btn), axis_x_btn, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, axis_btn_vbo[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(axis_y_btn), axis_y_btn, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, axis_btn_vbo[2]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(axis_z_btn), axis_z_btn, GL_STATIC_DRAW);


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

    /* ================= SKYBOX GEOMETRY ================= */

    float sky_cube[] = {
            -1,-1,-1,  1,-1,-1,  1, 1,-1,
            -1,-1,-1,  1, 1,-1, -1, 1,-1,

            -1,-1, 1,  1,-1, 1,  1, 1, 1,
            -1,-1, 1,  1, 1, 1, -1, 1, 1,

            -1,-1,-1, -1, 1,-1, -1, 1, 1,
            -1,-1,-1, -1, 1, 1, -1,-1, 1,

            1,-1,-1,  1, 1,-1,  1, 1, 1,
            1,-1,-1,  1, 1, 1,  1,-1, 1,

            -1,-1,-1, -1,-1, 1,  1,-1, 1,
            -1,-1,-1,  1,-1, 1,  1,-1,-1,

            -1, 1,-1, -1, 1, 1,  1, 1, 1,
            -1, 1,-1,  1, 1, 1,  1, 1,-1
    };

    GLuint sky_vbo;
    glGenBuffers(1, &sky_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, sky_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(sky_cube), sky_cube, GL_STATIC_DRAW);


    GLuint sky_prog = glCreateProgram();
    glAttachShader(sky_prog, compile(GL_VERTEX_SHADER, sky_vs));
    glAttachShader(sky_prog, compile(GL_FRAGMENT_SHADER, sky_fs));
    glBindAttribLocation(sky_prog, 0, "aPos");
    glLinkProgram(sky_prog);

    GLint sky_uMVP = glGetUniformLocation(sky_prog, "uMVP");


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

    /* ================= SELECTION RING ================= */

    float sel_ring[SEL_SEGMENTS * 6 * 2];
    int si = 0;

    for (int i = 0; i < SEL_SEGMENTS; i++) {
        float a0 = (float)i / SEL_SEGMENTS * 2.0f * M_PI;
        float a1 = (float)(i + 1) / SEL_SEGMENTS * 2.0f * M_PI;

        // XZ ring
        sel_ring[si++] = cosf(a0) * PICK_RADIUS;
        sel_ring[si++] = 0.0f;
        sel_ring[si++] = sinf(a0) * PICK_RADIUS;
        sel_ring[si++] = 1.0f; sel_ring[si++] = 1.0f; sel_ring[si++] = 0.2f;

        sel_ring[si++] = cosf(a1) * PICK_RADIUS;
        sel_ring[si++] = 0.0f;
        sel_ring[si++] = sinf(a1) * PICK_RADIUS;
        sel_ring[si++] = 1.0f; sel_ring[si++] = 1.0f; sel_ring[si++] = 0.2f;
    }

    GLuint sel_vbo;
    glGenBuffers(1, &sel_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, sel_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(sel_ring), sel_ring, GL_STATIC_DRAW);

    /* ================= GRID FLOOR ================= */

    int grid_lines = (GRID_SIZE * 2 + 1) * 4;
    float *grid = (float*)malloc(sizeof(float) * grid_lines * 6);

    int gi = 0;
    for (int i = -GRID_SIZE; i <= GRID_SIZE; i++) {
        float v = i * GRID_STEP;

        // X lines (along Z)
        grid[gi++] = -GRID_SIZE * GRID_STEP; grid[gi++] = 0.0f; grid[gi++] = v;
        grid[gi++] = GRID_COLOR_R; grid[gi++] = GRID_COLOR_G; grid[gi++] = GRID_COLOR_B;

        grid[gi++] =  GRID_SIZE * GRID_STEP; grid[gi++] = 0.0f; grid[gi++] = v;
        grid[gi++] = GRID_COLOR_R; grid[gi++] = GRID_COLOR_G; grid[gi++] = GRID_COLOR_B;

        // Z lines (along X)
        grid[gi++] = v; grid[gi++] = 0.0f; grid[gi++] = -GRID_SIZE * GRID_STEP;
        grid[gi++] = GRID_COLOR_R; grid[gi++] = GRID_COLOR_G; grid[gi++] = GRID_COLOR_B;

        grid[gi++] = v; grid[gi++] = 0.0f; grid[gi++] =  GRID_SIZE * GRID_STEP;
        grid[gi++] = GRID_COLOR_R; grid[gi++] = GRID_COLOR_G; grid[gi++] = GRID_COLOR_B;
    }

    GLuint grid_vbo;
    glGenBuffers(1, &grid_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, grid_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * gi, grid, GL_STATIC_DRAW);
    free(grid);


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
/* ================= JOYSTICK THUMB CIRCLE ================= */
#define THUMB_RADIUS 0.06f
#define THUMB_SEGMENTS 32

    float joy_thumb[THUMB_SEGMENTS * 5 * 2];

    for (int i = 0; i < THUMB_SEGMENTS; i++) {
        float a0 = (float)i / THUMB_SEGMENTS * 2.0f * M_PI;
        float a1 = (float)(i + 1) / THUMB_SEGMENTS * 2.0f * M_PI;

        int o = i * 10;

        joy_thumb[o + 0] = cosf(a0) * THUMB_RADIUS;
        joy_thumb[o + 1] = sinf(a0) * THUMB_RADIUS;
        joy_thumb[o + 2] = 1.0f;
        joy_thumb[o + 3] = 0.2f;
        joy_thumb[o + 4] = 1.0f;

        joy_thumb[o + 5] = cosf(a1) * THUMB_RADIUS;
        joy_thumb[o + 6] = sinf(a1) * THUMB_RADIUS;
        joy_thumb[o + 7] = 1.0f;
        joy_thumb[o + 8] = 0.2f;
        joy_thumb[o + 9] = 1.0f;
    }

    GLuint joy_thumb_vbo;
    glGenBuffers(1, &joy_thumb_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, joy_thumb_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(joy_thumb), joy_thumb, GL_STATIC_DRAW);




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
    agents[0].y =  0.0f;
    agents[0].z =  0.0f;

    agents[1].x =  1.3f;
    agents[1].y =  0.0f;
    agents[1].z =  0.0f;

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

            // Angular damping
            agents[i].rot_vel *= ROT_DAMP;

            // Kill tiny drift
            if (fabsf(agents[i].rot_vel) < 0.0005f)
                agents[i].rot_vel = 0.0f;
        }



        /* ===== CAMERA UPDATE (EVERY FRAME) ===== */
/* ===== CHARACTER MOVE (LEFT JOYSTICK) ===== */
        if (engine.joyL_active) {
            float move_speed = 0.05f;

            float forward_x = sinf(engine.cam_yaw);
            float forward_z = cosf(engine.cam_yaw);

            float right_x = cosf(engine.cam_yaw);
            float right_z = -sinf(engine.cam_yaw);

            Agent *p = &agents[0]; // primary character

            // Strafe (left / right)
            p->x += right_x * engine.joyL_x * move_speed;
            p->z += right_z * engine.joyL_x * move_speed;

            // Forward / backward
            p->x += forward_x * engine.joyL_y * move_speed;
            p->z += forward_z * engine.joyL_y * move_speed;
        }


            mat4_rotate_y(ry, engine.cam_yaw);
            mat4_rotate_x(rx, engine.cam_pitch);
            mat4_mul(rot, rx, ry);
            mat4_translate(tr, engine.cam_x, engine.cam_y, engine.cam_z);
            mat4_mul(view, tr, rot);


            glClearColor(0.05f, 0.05f, 0.08f, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        /* ================= SKYBOX DRAW ================= */
        glDepthMask(GL_FALSE);      // do NOT write depth
        glUseProgram(sky_prog);

        glBindBuffer(GL_ARRAY_BUFFER, sky_vbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glEnableVertexAttribArray(0);

/* Build skybox view (rotation only — no translation) */
        float sky_view[16];
        float sky_mvp[16];
        float sky_tmp[16];

        mat4_rotate_y(ry, engine.cam_yaw);
        mat4_rotate_x(rx, engine.cam_pitch);
        mat4_mul(sky_view, rx, ry);

/* MVP = proj * sky_view */
        mat4_mul(sky_tmp, sky_view, (float[16]){
                1,0,0,0,
                0,1,0,0,
                0,0,1,0,
                0,0,0,1
        });
        mat4_mul(sky_mvp, proj, sky_tmp);

        glUniformMatrix4fv(sky_uMVP, 1, GL_FALSE, sky_mvp);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        glDepthMask(GL_TRUE);       // restore depth writes

        /* ================= GRID DRAW ================= */

        glUseProgram(axis_prog);
        glBindBuffer(GL_ARRAY_BUFFER, grid_vbo);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);

        float grid_id[16], grid_tmp[16], grid_mvp[16];
        mat4_identity(grid_id);
        mat4_mul(grid_tmp, view, grid_id);
        mat4_mul(grid_mvp, proj, grid_tmp);

        glUniformMatrix4fv(axis_uMVP, 1, GL_FALSE, grid_mvp);
        glLineWidth(1.0f);
        glDrawArrays(GL_LINES, 0, grid_lines);

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

        int char_index = 0; // currently only one character
        float sel = (engine.selected == char_index) ? 1.0f : 0.0f;
        glUniform1f(uSelected, sel);

        /* ================= CHARACTER (MULTI-PRIMITIVE) ================= */

        int i = 0;  // single character for now

/* Root transform */
        float root[16];
        mat4_translate(root, agents[i].x, agents[i].y, agents[i].z);

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
            draw_cube(uMVP, uWorld, proj, view, model);
        }

        /* ================= SELECTION RINGS ================= */
        glUseProgram(axis_prog);
        glBindBuffer(GL_ARRAY_BUFFER, sel_vbo);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);

/* ---- XZ RING (GROUND) ---- */
        float t[16], tmp2[16], mvp[16];
        mat4_translate(t, agents[i].x, agents[i].y, agents[i].z);
        mat4_mul(tmp2, view, t);
        mat4_mul(mvp, proj, tmp2);
        glUniformMatrix4fv(axis_uMVP, 1, GL_FALSE, mvp);
        glDrawArrays(GL_LINES, 0, SEL_SEGMENTS * 2);

/* ---- XY RING (VERTICAL) ---- */
        float rx[16], t2[16];
        mat4_rotate_x(rx, M_PI * 0.5f);
        mat4_mul(t2, t, rx);
        mat4_mul(tmp2, view, t2);
        mat4_mul(mvp, proj, tmp2);
        glUniformMatrix4fv(axis_uMVP, 1, GL_FALSE, mvp);
        glDrawArrays(GL_LINES, 0, SEL_SEGMENTS * 2);


        /* ================= LEFT ARM ================= */
        {
            float t[16], s[16], m1[16], model[16];

            // Position arm relative to torso
            mat4_translate(t, -0.8f, 0.7f, 0.0f);   // left side, upper torso height
            mat4_scale(s, 0.25f, 0.9f, 0.25f);      // thin and long

            mat4_mul(m1, rotY, t);
            mat4_mul(m1, m1, s);
            mat4_mul(model, root, m1);

            draw_cube(uMVP, uWorld, proj, view, model);
        }

        /* ================= RIGHT ARM ================= */
        {
            float t[16], s[16], m1[16], model[16];

            mat4_translate(t, 0.8f, 0.7f, 0.0f);    // right side
            mat4_scale(s, 0.25f, 0.9f, 0.25f);

            mat4_mul(m1, rotY, t);
            mat4_mul(m1, m1, s);
            mat4_mul(model, root, m1);

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



        glEnable(GL_DEPTH_TEST);

        /* ================= AXIS BUTTON UI ================= */

        glDisable(GL_DEPTH_TEST);
        glUseProgram(cursor_prog);

        float bx = AXIS_BTN_START_X;

        for (int i = 0; i < 3; i++) {
            glUniform2f(
                    uCursor,
                    bx + i * AXIS_BTN_SPACING,
                    AXIS_BTN_Y
            );

            glBindBuffer(GL_ARRAY_BUFFER, axis_btn_vbo[i]);

            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                                  (void*)(2 * sizeof(float)));

            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);

            glDrawArrays(GL_LINES, 0, AXIS_BTN_SEGMENTS * 2);
        }

        glEnable(GL_DEPTH_TEST);

        eglSwapBuffers(engine.display, engine.surface);
            usleep(16000);
        }
    }