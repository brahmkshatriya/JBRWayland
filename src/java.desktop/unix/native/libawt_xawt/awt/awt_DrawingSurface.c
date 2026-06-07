/*
 * Copyright (c) 1996, 2016, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#ifdef HEADLESS
    #error This file should not be included in headless library
#endif

#include "awt_p.h"
#include "java_awt_Component.h"

#include "awt_Component.h"

#include <jni.h>
#include <jni_util.h>
#include <jawt_md.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern struct ComponentIDs componentIDs;

#include "awt_GraphicsEnv.h"
extern jfieldID windowID;
extern jfieldID targetID;
extern jfieldID graphicsConfigID;
extern jfieldID drawStateID;
extern struct X11GraphicsConfigIDs x11GraphicsConfigIDs;

typedef struct awt_DrawingSurface {
    JAWT_DrawingSurface jawt;
    jboolean waylandLocked;
} awt_DrawingSurface;

static jlongArray
awt_GetWaylandDrawingSurfaceData(JNIEnv* env, jobject target)
{
    jvalue result = JNU_CallStaticMethodByName(env, NULL,
                                              "sun/awt/wl/WLToolkit",
                                              "getWaylandDrawingSurfaceInfo",
                                              "(Ljava/awt/Component;)[J",
                                              target);
    if ((*env)->ExceptionCheck(env)) {
        return NULL;
    }
    return (jlongArray)result.l;
}

static jboolean
awt_IsWLToolkit(JNIEnv* env)
{
    jclass toolkitClass;
    jmethodID getDefaultToolkit;
    jobject toolkit;
    jclass classClass;
    jmethodID getName;
    jclass actualClass;
    jstring name;
    const char* chars;
    jboolean result;

    toolkitClass = (*env)->FindClass(env, "java/awt/Toolkit");
    if (toolkitClass == NULL) {
        return JNI_FALSE;
    }
    getDefaultToolkit = (*env)->GetStaticMethodID(env, toolkitClass,
                                                  "getDefaultToolkit",
                                                  "()Ljava/awt/Toolkit;");
    if (getDefaultToolkit == NULL) {
        return JNI_FALSE;
    }
    toolkit = (*env)->CallStaticObjectMethod(env, toolkitClass, getDefaultToolkit);
    if ((*env)->ExceptionCheck(env) || toolkit == NULL) {
        return JNI_FALSE;
    }

    classClass = (*env)->FindClass(env, "java/lang/Class");
    if (classClass == NULL) {
        return JNI_FALSE;
    }
    getName = (*env)->GetMethodID(env, classClass, "getName", "()Ljava/lang/String;");
    if (getName == NULL) {
        return JNI_FALSE;
    }

    actualClass = (*env)->GetObjectClass(env, toolkit);
    name = (jstring)(*env)->CallObjectMethod(env, actualClass, getName);
    if ((*env)->ExceptionCheck(env) || name == NULL) {
        return JNI_FALSE;
    }

    chars = (*env)->GetStringUTFChars(env, name, NULL);
    if (chars == NULL) {
        return JNI_FALSE;
    }
    result = strcmp(chars, "sun.awt.wl.WLToolkit") == 0 ? JNI_TRUE : JNI_FALSE;
    (*env)->ReleaseStringUTFChars(env, name, chars);
    return result;
}

static jboolean
awt_IsWaylandDrawingSurface(JNIEnv* env, jobject target)
{
    jlongArray data = awt_GetWaylandDrawingSurfaceData(env, target);
    if (data == NULL) {
        return JNI_FALSE;
    }
    (*env)->DeleteLocalRef(env, data);
    return JNI_TRUE;
}

static void
awt_WaylandLock(JNIEnv* env)
{
    JNU_CallStaticMethodByName(env, NULL, "sun/awt/wl/WLToolkit", "awtLock", "()V");
}

static void
awt_WaylandUnlock(JNIEnv* env)
{
    JNU_CallStaticMethodByName(env, NULL, "sun/awt/wl/WLToolkit", "awtUnlock", "()V");
}

/*
 * Lock the surface of the target component for native rendering.
 * When finished drawing, the surface must be unlocked with
 * Unlock().  This function returns a bitmask with one or more of the
 * following values:
 *
 * JAWT_LOCK_ERROR - When an error has occurred and the surface could not
 * be locked.
 *
 * JAWT_LOCK_CLIP_CHANGED - When the clip region has changed.
 *
 * JAWT_LOCK_BOUNDS_CHANGED - When the bounds of the surface have changed.
 *
 * JAWT_LOCK_SURFACE_CHANGED - When the surface itself has changed
 */
JNIEXPORT jint JNICALL awt_DrawingSurface_Lock(JAWT_DrawingSurface* ds)
{
    JNIEnv* env;
    jobject target, peer;
    jclass componentClass;
    jint drawState;
    awt_DrawingSurface* pds;

    if (ds == NULL) {
#ifdef DEBUG
        fprintf(stderr, "Drawing Surface is NULL\n");
#endif
        return (jint)JAWT_LOCK_ERROR;
    }
    env = ds->env;
    target = ds->target;

    /* Make sure the target is a java.awt.Component */
    componentClass = (*env)->FindClass(env, "java/awt/Component");
    CHECK_NULL_RETURN(componentClass, (jint)JAWT_LOCK_ERROR);

    if (!(*env)->IsInstanceOf(env, target, componentClass)) {
#ifdef DEBUG
            fprintf(stderr, "Target is not a component\n");
#endif
        return (jint)JAWT_LOCK_ERROR;
        }

    pds = (awt_DrawingSurface*)ds;
    if (awt_IsWLToolkit(env)) {
        awt_WaylandLock(env);
        if ((*env)->ExceptionCheck(env)) {
            return (jint)JAWT_LOCK_ERROR;
        }
        if (awt_IsWaylandDrawingSurface(env, target)) {
            pds->waylandLocked = JNI_TRUE;
            return (jint)(JAWT_LOCK_SURFACE_CHANGED |
                          JAWT_LOCK_BOUNDS_CHANGED |
                          JAWT_LOCK_CLIP_CHANGED);
        }
        awt_WaylandUnlock(env);
        if ((*env)->ExceptionCheck(env)) {
            return (jint)JAWT_LOCK_ERROR;
        }
    }

    if (!awtLockInited) {
        return (jint)JAWT_LOCK_ERROR;
    }
    AWT_LOCK();

    /* Get the peer of the target component */
    peer = (*env)->GetObjectField(env, target, componentIDs.peer);
    if (JNU_IsNull(env, peer)) {
#ifdef DEBUG
        fprintf(stderr, "Component peer is NULL\n");
#endif
                AWT_FLUSH_UNLOCK();
        return (jint)JAWT_LOCK_ERROR;
    }

   drawState = (*env)->GetIntField(env, peer, drawStateID);
    (*env)->SetIntField(env, peer, drawStateID, 0);
    return drawState;
}

JNIEXPORT int32_t JNICALL
    awt_GetColor(JAWT_DrawingSurface* ds, int32_t r, int32_t g, int32_t b)
{
    JNIEnv* env;
    jobject target, peer;
    jclass componentClass;
    AwtGraphicsConfigDataPtr adata;
    int32_t result;
     jobject gc_object;
    if (ds == NULL) {
#ifdef DEBUG
        fprintf(stderr, "Drawing Surface is NULL\n");
#endif
        return (int32_t) 0;
    }

    env = ds->env;
    target = ds->target;

    /* Make sure the target is a java.awt.Component */
    componentClass = (*env)->FindClass(env, "java/awt/Component");
    CHECK_NULL_RETURN(componentClass, (int32_t) 0);

    if (!(*env)->IsInstanceOf(env, target, componentClass)) {
#ifdef DEBUG
        fprintf(stderr, "DrawingSurface target must be a component\n");
#endif
        return (int32_t) 0;
    }

    if (!awtLockInited) {
        return (int32_t) 0;
    }

    AWT_LOCK();

    /* Get the peer of the target component */
    peer = (*env)->GetObjectField(env, target, componentIDs.peer);
    if (JNU_IsNull(env, peer)) {
#ifdef DEBUG
        fprintf(stderr, "Component peer is NULL\n");
#endif
        AWT_UNLOCK();
        return (int32_t) 0;
    }
     /* GraphicsConfiguration object of MComponentPeer */
    gc_object = (*env)->GetObjectField(env, peer, graphicsConfigID);

    if (gc_object != NULL) {
        adata = (AwtGraphicsConfigDataPtr)
            JNU_GetLongFieldAsPtr(env, gc_object,
                                  x11GraphicsConfigIDs.aData);
    } else {
        adata = getDefaultConfig(DefaultScreen(awt_display));
    }

    result = adata->AwtColorMatch(r, g, b, adata);
        AWT_UNLOCK();
        return result;
}

/*
 * Get the drawing surface info.
 * The value returned may be cached, but the values may change if
 * additional calls to Lock() or Unlock() are made.
 * Lock() must be called before this can return a valid value.
 * Returns NULL if an error has occurred.
 * When finished with the returned value, FreeDrawingSurfaceInfo must be
 * called.
 */
JNIEXPORT JAWT_DrawingSurfaceInfo* JNICALL
awt_DrawingSurface_GetDrawingSurfaceInfo(JAWT_DrawingSurface* ds)
{
    JNIEnv* env;
    jobject target, peer;
    jclass componentClass;
    JAWT_X11DrawingSurfaceInfo* px;
    JAWT_WaylandDrawingSurfaceInfo* pw;
    JAWT_DrawingSurfaceInfo* p;
    XWindowAttributes attrs;
    jlongArray wlData;
    jlong* wl;
    union {
        jlong bits;
        double value;
    } wlDouble;

    if (ds == NULL) {
#ifdef DEBUG
        fprintf(stderr, "Drawing Surface is NULL\n");
#endif
        return NULL;
    }

    env = ds->env;
    target = ds->target;

    /* Make sure the target is a java.awt.Component */
    componentClass = (*env)->FindClass(env, "java/awt/Component");
    CHECK_NULL_RETURN(componentClass, NULL);

    if (!(*env)->IsInstanceOf(env, target, componentClass)) {
#ifdef DEBUG
        fprintf(stderr, "DrawingSurface target must be a component\n");
#endif
        return NULL;
        }

    wlData = awt_IsWLToolkit(env) ? awt_GetWaylandDrawingSurfaceData(env, target) : NULL;
    if (wlData != NULL) {
        wl = (*env)->GetLongArrayElements(env, wlData, NULL);
        if (wl == NULL) {
            (*env)->DeleteLocalRef(env, wlData);
            return NULL;
        }

        pw = (JAWT_WaylandDrawingSurfaceInfo*)
            malloc(sizeof(JAWT_WaylandDrawingSurfaceInfo));
        if (pw == NULL) {
            (*env)->ReleaseLongArrayElements(env, wlData, wl, JNI_ABORT);
            (*env)->DeleteLocalRef(env, wlData);
            return NULL;
        }
        pw->display = (struct wl_display*)(intptr_t)wl[0];
        pw->parentSurface = (struct wl_surface*)(intptr_t)wl[1];
        pw->x = (int)wl[2];
        pw->y = (int)wl[3];
        pw->width = (int)wl[4];
        pw->height = (int)wl[5];
        pw->scale = (int)wl[6];
        wlDouble.bits = wl[7];
        pw->effectiveScale = wlDouble.value;
        pw->javaX = (int)wl[8];
        pw->javaY = (int)wl[9];
        pw->javaWidth = (int)wl[10];
        pw->javaHeight = (int)wl[11];

        (*env)->ReleaseLongArrayElements(env, wlData, wl, JNI_ABORT);
        (*env)->DeleteLocalRef(env, wlData);

        p = (JAWT_DrawingSurfaceInfo*)malloc(sizeof(JAWT_DrawingSurfaceInfo));
        if (p == NULL) {
            free(pw);
            return NULL;
        }
        p->platformInfo = pw;
        p->ds = ds;
        p->bounds.x = (*env)->GetIntField(env, target, componentIDs.x);
        p->bounds.y = (*env)->GetIntField(env, target, componentIDs.y);
        p->bounds.width = (*env)->GetIntField(env, target, componentIDs.width);
        p->bounds.height = (*env)->GetIntField(env, target, componentIDs.height);
        p->clipSize = 1;
        p->clip = &(p->bounds);
        return p;
    }

    if (!awtLockInited) {
        return NULL;
    }

    AWT_LOCK();

    /* Get the peer of the target component */
    peer = (*env)->GetObjectField(env, target, componentIDs.peer);
    if (JNU_IsNull(env, peer)) {
#ifdef DEBUG
        fprintf(stderr, "Component peer is NULL\n");
#endif
                AWT_UNLOCK();
        return NULL;
    }

    AWT_UNLOCK();

    /* Allocate platform-specific data */
    px = (JAWT_X11DrawingSurfaceInfo*)
        malloc(sizeof(JAWT_X11DrawingSurfaceInfo));

    /* Set drawable and display */
    px->drawable = (*env)->GetLongField(env, peer, windowID);
    px->display = awt_display;

    /* Get window attributes to set other values */
    XGetWindowAttributes(awt_display, (Window)(px->drawable), &attrs);

    /* Set the other values */
    px->visualID = XVisualIDFromVisual(attrs.visual);
    px->colormapID = attrs.colormap;
    px->depth = attrs.depth;
    px->GetAWTColor = awt_GetColor;

    /* Allocate and initialize platform-independent data */
    p = (JAWT_DrawingSurfaceInfo*)malloc(sizeof(JAWT_DrawingSurfaceInfo));
    p->platformInfo = px;
    p->ds = ds;
    p->bounds.x = (*env)->GetIntField(env, target, componentIDs.x);
    p->bounds.y = (*env)->GetIntField(env, target, componentIDs.y);
    p->bounds.width = (*env)->GetIntField(env, target, componentIDs.width);
    p->bounds.height = (*env)->GetIntField(env, target, componentIDs.height);
    p->clipSize = 1;
    p->clip = &(p->bounds);

    /* Return our new structure */
    return p;
}

/*
 * Free the drawing surface info.
 */
JNIEXPORT void JNICALL
awt_DrawingSurface_FreeDrawingSurfaceInfo(JAWT_DrawingSurfaceInfo* dsi)
{
    if (dsi == NULL ) {
#ifdef DEBUG
        fprintf(stderr, "Drawing Surface Info is NULL\n");
#endif
        return;
    }
    free(dsi->platformInfo);
    free(dsi);
}

/*
 * Unlock the drawing surface of the target component for native rendering.
 */
JNIEXPORT void JNICALL awt_DrawingSurface_Unlock(JAWT_DrawingSurface* ds)
{
    JNIEnv* env;
    awt_DrawingSurface* pds;
    if (ds == NULL) {
#ifdef DEBUG
        fprintf(stderr, "Drawing Surface is NULL\n");
#endif
        return;
    }
    env = ds->env;
    pds = (awt_DrawingSurface*)ds;
    if (pds->waylandLocked) {
        pds->waylandLocked = JNI_FALSE;
        awt_WaylandUnlock(env);
        return;
    }
    AWT_FLUSH_UNLOCK();
}

JNIEXPORT JAWT_DrawingSurface* JNICALL
    awt_GetDrawingSurface(JNIEnv* env, jobject target)
{
    jclass componentClass;
    JAWT_DrawingSurface* p;
    awt_DrawingSurface* pds;

    /* Make sure the target component is a java.awt.Component */
    componentClass = (*env)->FindClass(env, "java/awt/Component");
    CHECK_NULL_RETURN(componentClass, NULL);

    if (!(*env)->IsInstanceOf(env, target, componentClass)) {
#ifdef DEBUG
        fprintf(stderr,
            "GetDrawingSurface target must be a java.awt.Component\n");
#endif
        return NULL;
    }

    pds = (awt_DrawingSurface*)calloc(1, sizeof(awt_DrawingSurface));
    if (pds == NULL) {
        return NULL;
    }
    p = (JAWT_DrawingSurface*)pds;
    p->env = env;
    p->target = (*env)->NewGlobalRef(env, target);
    p->Lock = awt_DrawingSurface_Lock;
    p->GetDrawingSurfaceInfo = awt_DrawingSurface_GetDrawingSurfaceInfo;
    p->FreeDrawingSurfaceInfo = awt_DrawingSurface_FreeDrawingSurfaceInfo;
    p->Unlock = awt_DrawingSurface_Unlock;
    return p;
}

JNIEXPORT void JNICALL
    awt_FreeDrawingSurface(JAWT_DrawingSurface* ds)
{
    JNIEnv* env;

    if (ds == NULL ) {
#ifdef DEBUG
        fprintf(stderr, "Drawing Surface is NULL\n");
#endif
        return;
    }
    env = ds->env;
    (*env)->DeleteGlobalRef(env, ds->target);
    free(ds);
}

JNIEXPORT void JNICALL
    awt_Lock(JNIEnv* env)
{
    if (awtLockInited) {
        AWT_LOCK();
    }
}

JNIEXPORT void JNICALL
    awt_Unlock(JNIEnv* env)
{
    if (awtLockInited) {
        AWT_FLUSH_UNLOCK();
    }
}

JNIEXPORT jobject JNICALL
    awt_GetComponent(JNIEnv* env, void* platformInfo)
{
    Window window = (Window)platformInfo;
    jobject peer = NULL;
    jobject target = NULL;

    AWT_LOCK();

    if (window != None) {
        peer = JNU_CallStaticMethodByName(env, NULL, "sun/awt/X11/XToolkit",
            "windowToXWindow", "(J)Lsun/awt/X11/XBaseWindow;", (jlong)window).l;
        if ((*env)->ExceptionCheck(env)) {
            AWT_UNLOCK();
            return (jobject)NULL;
        }
    }
    if ((peer != NULL) &&
        (JNU_IsInstanceOfByName(env, peer, "sun/awt/X11/XWindow") == 1)) {
        target = (*env)->GetObjectField(env, peer, targetID);
    }

    if (target == NULL) {
        (*env)->ExceptionClear(env);
        JNU_ThrowNullPointerException(env, "NullPointerException");
        AWT_UNLOCK();
        return (jobject)NULL;
    }

    AWT_UNLOCK();

    return target;
}

// EmbeddedFrame support

static char *const embeddedClassName = "sun/awt/X11/XEmbeddedFrame";

JNIEXPORT jobject JNICALL awt_CreateEmbeddedFrame
(JNIEnv* env, void* platformInfo)
{
    static jmethodID mid = NULL;
    static jclass cls;
    if (mid == NULL) {
        cls = (*env)->FindClass(env, embeddedClassName);
        CHECK_NULL_RETURN(cls, NULL);
        mid = (*env)->GetMethodID(env, cls, "<init>", "(JZ)V");
        CHECK_NULL_RETURN(mid, NULL);
    }
    return (*env)->NewObject(env, cls, mid, platformInfo, JNI_TRUE);
}


JNIEXPORT void JNICALL awt_SetBounds
(JNIEnv *env, jobject embeddedFrame, jint x, jint y, jint w, jint h)
{
    static jmethodID mid = NULL;
    if (mid == NULL) {
        jclass cls = (*env)->FindClass(env, embeddedClassName);
        CHECK_NULL(cls);
        mid = (*env)->GetMethodID(env, cls, "setBoundsPrivate", "(IIII)V");
        CHECK_NULL(mid);
    }
    (*env)->CallVoidMethod(env, embeddedFrame, mid, x, y, w, h);
}

JNIEXPORT void JNICALL awt_SynthesizeWindowActivation
(JNIEnv *env, jobject embeddedFrame, jboolean doActivate)
{
    static jmethodID mid = NULL;
    if (mid == NULL) {
        jclass cls = (*env)->FindClass(env, embeddedClassName);
        CHECK_NULL(cls);
        mid = (*env)->GetMethodID(env, cls, "synthesizeWindowActivation", "(Z)V");
        CHECK_NULL(mid);
    }
    (*env)->CallVoidMethod(env, embeddedFrame, mid, doActivate);
}
