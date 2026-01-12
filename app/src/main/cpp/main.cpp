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

const char *vs_src =
        "attribute vec3 aPos;\n"
        "attribute vec3 aColor;\n"
        "attribute vec3 aNormal;\n"
        "uniform mat4 uMVP;\n"
        "uniform mat4 uWorld;\n"
        "varying vec3 vColor;\n"
        "varying vec3 vNormal;\n"
        "void main(){\n"
        "vColor=aColor;\n"
        "vNormal=mat3(uWorld)*aNormal;\n"
        "gl_Position=uMVP*vec4(aPos,1.0);\n"
        "}\n";

const char *fs_src =
        "precision mediump float;\n"
        "varying vec3 vColor;\n"
        "varying vec3 vNormal;\n"
        "uniform float uSelected;\n"
        "void main(){\n"
        "vec3 N=normalize(vNormal);\n"
        "vec3 L=normalize(vec3(-0.4,-1.0,-0.6));\n"
        "vec3 V=vec3(0.0,0.0,1.0);\n"
        "float d=max(dot(N,-L),0.0);\n"
        "vec3 H=normalize(-L+V);\n"
        "float s=pow(max(dot(N,H),0.0),24.0);\n"
        "vec3 base=vColor*(0.25+d*0.75)+vec3(s*0.35);\n"
        "vec3 highlight=mix(base,vec3(1.0,1.0,0.3),uSelected);\n"
        "gl_FragColor=vec4(highlight,1.0);\n"
        "}\n";

const char *axis_vs =
        "attribute vec3 aPos;\n"
        "attribute vec3 aColor;\n"
        "uniform mat4 uMVP;\n"
        "varying vec3 vColor;\n"
        "void main(){\n"
        "vColor=aColor;\n"
        "gl_Position=uMVP*vec4(aPos,1.0);\n"
        "}\n";

const char *axis_fs =
        "precision mediump float;\n"
        "varying vec3 vColor;\n"
        "void main(){\n"
        "gl_FragColor=vec4(vColor,1.0);\n"
        "}\n";

void mat4_identity(float *m){ memset(m,0,64); m[0]=m[5]=m[10]=m[15]=1; }
void mat4_translate(float *m,float x,float y,float z){ mat4_identity(m); m[12]=x; m[13]=y; m[14]=z; }
void mat4_scale(float *m,float x,float y,float z){ mat4_identity(m); m[0]=x; m[5]=y; m[10]=z; }
void mat4_rotate_y(float *m,float a){ mat4_identity(m); m[0]=cosf(a); m[2]=-sinf(a); m[8]=sinf(a); m[10]=cosf(a); }
void mat4_mul(float *o,float *a,float *b){
    for(int c=0;c<4;c++) for(int r=0;r<4;r++)
            o[c*4+r]=a[r]*b[c*4]+a[4+r]*b[c*4+1]+a[8+r]*b[c*4+2]+a[12+r]*b[c*4+3];
}
void mat4_perspective(float *m,float fov,float asp,float n,float f){
    float t=tanf(fov*0.5f); memset(m,0,64);
    m[0]=1/(asp*t); m[5]=1/t; m[10]=-(f+n)/(f-n); m[11]=-1; m[14]=-(2*f*n)/(f-n);
}

void draw_cube(GLint uMVP,GLint uWorld,float *proj,float *view,float *model){
    float t2[16],mvp[16];
    mat4_mul(t2,view,model);
    mat4_mul(mvp,proj,t2);
    glUniformMatrix4fv(uWorld,1,GL_FALSE,model);
    glUniformMatrix4fv(uMVP,1,GL_FALSE,mvp);
    glDrawArrays(GL_TRIANGLES,0,36); // ← FIX
}


typedef struct{ float x,y,rot,rot_vel; } Agent;
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
} engine;

static int32_t handle_input(struct android_app*,AInputEvent* e){
    if(AInputEvent_getType(e)!=AINPUT_EVENT_TYPE_MOTION) return 0;
    float x=AMotionEvent_getX(e,0);
    float y=AMotionEvent_getY(e,0);
    float wx=(x/engine.width)*8.0f-4.0f;
    float wy=((engine.height-y)/engine.height)*6.0f-3.0f;
    int a=AMotionEvent_getAction(e)&AMOTION_EVENT_ACTION_MASK;
    if(a==AMOTION_EVENT_ACTION_DOWN){
        engine.last_x=x; engine.last_y=y;
        engine.grabbed=-1; engine.selected=-1;
        for(int i=0;i<NUM_AGENTS;i++)
            if(fabsf(wx-agents[i].x)<PICK_RADIUS&&fabsf(wy-agents[i].y)<PICK_RADIUS)
                engine.grabbed=engine.selected=i;
    }
    if(a==AMOTION_EVENT_ACTION_MOVE&&engine.grabbed!=-1){
        float dx=x-engine.last_x; engine.last_x=x;
        agents[engine.grabbed].x=wx;
        agents[engine.grabbed].y=wy;
        agents[engine.grabbed].rot_vel+=dx*ROT_SENS;
    }
    if(a==AMOTION_EVENT_ACTION_UP) engine.grabbed=-1;
    return 1;
}

void android_main(struct android_app* app){
    app->onInputEvent=handle_input;
    while(!app->window){
        int ev; android_poll_source* src;
        ALooper_pollOnce(-1,NULL,&ev,(void**)&src);
        if(src) src->process(app,src);
    }

    engine.width=ANativeWindow_getWidth(app->window);
    engine.height=ANativeWindow_getHeight(app->window);

    engine.display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(engine.display,NULL,NULL);

    EGLConfig cfg; EGLint n;
    EGLint cfg_attr[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_DEPTH_SIZE,16,EGL_NONE};
    eglChooseConfig(engine.display,cfg_attr,&cfg,1,&n);
    engine.surface=eglCreateWindowSurface(engine.display,cfg,app->window,NULL);
    EGLint ctx_attr[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    engine.context=eglCreateContext(engine.display,cfg,EGL_NO_CONTEXT,ctx_attr);
    eglMakeCurrent(engine.display,engine.surface,engine.surface,engine.context);

    glEnable(GL_DEPTH_TEST);

    GLuint prog=glCreateProgram();
    glAttachShader(prog,compile(GL_VERTEX_SHADER,vs_src));
    glAttachShader(prog,compile(GL_FRAGMENT_SHADER,fs_src));
    glBindAttribLocation(prog,0,"aPos");
    glBindAttribLocation(prog,1,"aColor");
    glBindAttribLocation(prog,2,"aNormal");
    glLinkProgram(prog);

    GLint uMVP=glGetUniformLocation(prog,"uMVP");
    GLint uWorld=glGetUniformLocation(prog,"uWorld");
    GLint uSelected=glGetUniformLocation(prog,"uSelected");

    float cube[]={
            // +Z
            -0.5,-0.5,0.5, 1,0,0, 0,0,1,
            0.5,-0.5,0.5, 1,0,0, 0,0,1,
            0.5, 0.5,0.5, 1,0,0, 0,0,1,
            -0.5,-0.5,0.5, 1,0,0, 0,0,1,
            0.5, 0.5,0.5, 1,0,0, 0,0,1,
            -0.5, 0.5,0.5, 1,0,0, 0,0,1,

            // -Z
            -0.5,-0.5,-0.5, 0,1,0, 0,0,-1,
            -0.5, 0.5,-0.5, 0,1,0, 0,0,-1,
            0.5, 0.5,-0.5, 0,1,0, 0,0,-1,
            -0.5,-0.5,-0.5, 0,1,0, 0,0,-1,
            0.5, 0.5,-0.5, 0,1,0, 0,0,-1,
            0.5,-0.5,-0.5, 0,1,0, 0,0,-1,

            // -X
            -0.5,-0.5,-0.5, 0,0,1, -1,0,0,
            -0.5,-0.5, 0.5, 0,0,1, -1,0,0,
            -0.5, 0.5, 0.5, 0,0,1, -1,0,0,
            -0.5,-0.5,-0.5, 0,0,1, -1,0,0,
            -0.5, 0.5, 0.5, 0,0,1, -1,0,0,
            -0.5, 0.5,-0.5, 0,0,1, -1,0,0,

            // +X
            0.5,-0.5,-0.5, 1,1,0, 1,0,0,
            0.5, 0.5,-0.5, 1,1,0, 1,0,0,
            0.5, 0.5, 0.5, 1,1,0, 1,0,0,
            0.5,-0.5,-0.5, 1,1,0, 1,0,0,
            0.5, 0.5, 0.5, 1,1,0, 1,0,0,
            0.5,-0.5, 0.5, 1,1,0, 1,0,0,

            // +Y
            -0.5, 0.5,-0.5, 0,1,1, 0,1,0,
            -0.5, 0.5, 0.5, 0,1,1, 0,1,0,
            0.5, 0.5, 0.5, 0,1,1, 0,1,0,
            -0.5, 0.5,-0.5, 0,1,1, 0,1,0,
            0.5, 0.5, 0.5, 0,1,1, 0,1,0,
            0.5, 0.5,-0.5, 0,1,1, 0,1,0,

            // -Y
            -0.5,-0.5,-0.5, 1,0,1, 0,-1,0,
            0.5,-0.5,-0.5, 1,0,1, 0,-1,0,
            0.5,-0.5, 0.5, 1,0,1, 0,-1,0,
            -0.5,-0.5,-0.5, 1,0,1, 0,-1,0,
            0.5,-0.5, 0.5, 1,0,1, 0,-1,0,
            -0.5,-0.5, 0.5, 1,0,1, 0,-1,0
    };


    GLuint vbo;
    glGenBuffers(1,&vbo);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(cube),cube,GL_STATIC_DRAW);

    float axis[]={
            -5,0,0,1,0,0,  5,0,0,1,0,0,
            0,-5,0,0,1,0, 0,5,0,0,1,0,
            0,0,-5,0,0,1, 0,0,5,0,0,1
    };

    GLuint axis_vbo;
    glGenBuffers(1,&axis_vbo);
    glBindBuffer(GL_ARRAY_BUFFER,axis_vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(axis),axis,GL_STATIC_DRAW);

    GLuint axis_prog=glCreateProgram();
    glAttachShader(axis_prog,compile(GL_VERTEX_SHADER,axis_vs));
    glAttachShader(axis_prog,compile(GL_FRAGMENT_SHADER,axis_fs));
    glBindAttribLocation(axis_prog,0,"aPos");
    glBindAttribLocation(axis_prog,1,"aColor");
    glLinkProgram(axis_prog);
    GLint axis_uMVP=glGetUniformLocation(axis_prog,"uMVP");

    float proj[16],view[16],tmp[16],tr[16],ry[16];
    mat4_perspective(proj,1.1f,(float)engine.width/engine.height,0.1f,50);
    mat4_rotate_y(ry,0.6f);
    mat4_translate(tr,0,-0.6f,-7.5f);
    mat4_mul(view,tr,ry);

    agents[0].x=-1.3f; agents[1].x=1.3f;

    while(true){
        int ev; android_poll_source* src;
        while(ALooper_pollOnce(0,NULL,&ev,(void**)&src)>=0) if(src) src->process(app,src);
        for(int i=0;i<NUM_AGENTS;i++){ agents[i].rot+=agents[i].rot_vel; agents[i].rot_vel*=ROT_DAMP; }

        glClearColor(0.05f,0.05f,0.08f,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        glUseProgram(axis_prog);
        glBindBuffer(GL_ARRAY_BUFFER,axis_vbo);
        glDisableVertexAttribArray(2);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,6*sizeof(float),(void*)0);
        glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,6*sizeof(float),(void*)(3*sizeof(float)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);

        float id[16],axis_mvp[16];
        mat4_identity(id);
        mat4_mul(tmp,view,id);
        mat4_mul(axis_mvp,proj,tmp);
        glUniformMatrix4fv(axis_uMVP,1,GL_FALSE,axis_mvp);
        glDrawArrays(GL_LINES,0,6);

        glUseProgram(prog);
        glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,9*sizeof(float),(void*)0);
        glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,9*sizeof(float),(void*)(3*sizeof(float)));
        glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,9*sizeof(float),(void*)(6*sizeof(float)));

        for(int i=0;i<NUM_AGENTS;i++){
            float root[16],rot[16],scale[16],tmp2[16],model[16];
            mat4_translate(root,agents[i].x,agents[i].y,0);
            mat4_rotate_y(rot,agents[i].rot);
            mat4_scale(scale,0.8f,1.2f,0.8f);
            mat4_mul(tmp2,rot,scale);
            mat4_mul(model,root,tmp2);
            glUniform1f(uSelected,(float)(i==engine.selected));
            draw_cube(uMVP,uWorld,proj,view,model);
        }

        eglSwapBuffers(engine.display,engine.surface);
        usleep(16000);
    }
}
