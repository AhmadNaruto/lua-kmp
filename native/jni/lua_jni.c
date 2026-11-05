/*
** JNI entry points for the lua-kmp JVM/Android backend.
**
** One JNIEXPORT per external fun on com.seanproctor.lua.LuaJni, operating on
** a lua_State* passed as jlong. Marshalling and registry logic live in
** Kotlin (jvmShared); this layer stays mechanical.
**
** Host-callback flow: kl_dispatch (lua_shim.c) invokes jni_host_callback,
** which calls the static LuaJni.nativeCallback(threadPtr, mainPtr, id). That
** method returns the number of results pushed, or -1 with an error message
** pushed, in which case kl_dispatch raises the Lua error only after every
** Java frame has returned — the longjmp never crosses a JNI frame.
*/
#include <jni.h>
#include <stdlib.h>
#include <stdint.h>

#include "lua_shim.h"

static JavaVM *g_vm = NULL;
static jclass g_luajni_class = NULL;
static jmethodID g_callback_mid = NULL;

#define STATE(ptr) ((lua_State *)(uintptr_t)(ptr))

static int jni_host_callback(lua_State *L) {
  JNIEnv *env = NULL;
  jint nresults;
  if (g_vm == NULL ||
      (*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK ||
      env == NULL) {
    lua_pushliteral(L, "lua-kmp: no JNI environment for host callback");
    return -1;
  }
  nresults = (*env)->CallStaticIntMethod(
      env, g_luajni_class, g_callback_mid,
      (jlong)(uintptr_t)L, (jlong)(uintptr_t)kl_getextra(L),
      (jint)kl_upvalue_int(L, 1));
  if ((*env)->ExceptionCheck(env)) {
    (*env)->ExceptionClear(env);
    lua_pushliteral(L, "lua-kmp: host function raised an unexpected JVM exception");
    return -1;
  }
  return (int)nresults;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  JNIEnv *env = NULL;
  jclass cls;
  (void)reserved;
  g_vm = vm;
  if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK)
    return JNI_ERR;
  cls = (*env)->FindClass(env, "com/seanproctor/lua/LuaJni");
  if (cls == NULL)
    return JNI_ERR;
  g_luajni_class = (jclass)(*env)->NewGlobalRef(env, cls);
  g_callback_mid =
      (*env)->GetStaticMethodID(env, g_luajni_class, "nativeCallback", "(JJI)I");
  if (g_callback_mid == NULL)
    return JNI_ERR;
  kl_set_callback(jni_host_callback);
  return JNI_VERSION_1_6;
}

/* Copies a jbyteArray into a NUL-terminated malloc'd buffer. */
static unsigned char *copy_bytes(JNIEnv *env, jbyteArray arr, size_t *len_out) {
  jsize len = (*env)->GetArrayLength(env, arr);
  unsigned char *buf = (unsigned char *)malloc((size_t)len + 1);
  if (buf == NULL)
    return NULL;
  (*env)->GetByteArrayRegion(env, arr, 0, len, (jbyte *)buf);
  buf[len] = '\0';
  *len_out = (size_t)len;
  return buf;
}

JNIEXPORT jlong JNICALL
Java_com_seanproctor_lua_LuaJni_newState(JNIEnv *env, jobject self) {
  lua_State *L = luaL_newstate();
  (void)env; (void)self;
  if (L == NULL)
    return 0;
  /* The main state's address rides in the extraspace so the dispatcher can
     recover it from coroutine threads (Lua copies extraspace to threads). */
  kl_setextra(L, L);
  return (jlong)(uintptr_t)L;
}

JNIEXPORT void JNICALL
Java_com_seanproctor_lua_LuaJni_openLibs(JNIEnv *env, jobject self, jlong state, jint mask) {
  (void)env; (void)self;
  kl_openlibs(STATE(state), (int)mask);
}

JNIEXPORT void JNICALL
Java_com_seanproctor_lua_LuaJni_close(JNIEnv *env, jobject self, jlong state) {
  (void)env; (void)self;
  lua_close(STATE(state));
}

JNIEXPORT jint JNICALL
Java_com_seanproctor_lua_LuaJni_loadBuffer(JNIEnv *env, jobject self, jlong state,
                                           jbyteArray code, jbyteArray chunkName) {
  size_t code_len = 0, name_len = 0;
  unsigned char *code_buf, *name_buf;
  int status;
  (void)self;
  code_buf = copy_bytes(env, code, &code_len);
  name_buf = copy_bytes(env, chunkName, &name_len);
  if (code_buf == NULL || name_buf == NULL) {
    free(code_buf);
    free(name_buf);
    lua_pushliteral(STATE(state), "not enough memory");
    return LUA_ERRMEM;
  }
  status = kl_loadbuffer(STATE(state), (const char *)code_buf, (int)code_len,
                         (const char *)name_buf);
  free(code_buf);
  free(name_buf);
  return status;
}

JNIEXPORT jbyteArray JNICALL
Java_com_seanproctor_lua_LuaJni_dump(JNIEnv *env, jobject self, jlong state) {
  int len = 0;
  char *bytes = kl_dump(STATE(state), &len);
  (void)self;
  if (bytes == NULL) return NULL;
  jbyteArray arr = (*env)->NewByteArray(env, len);
  if (arr != NULL) {
    (*env)->SetByteArrayRegion(env, arr, 0, len, (const jbyte *)bytes);
  }
  free(bytes);
  return arr;
}

JNIEXPORT jint JNICALL
Java_com_seanproctor_lua_LuaJni_pcall(JNIEnv *env, jobject self, jlong state,
                                      jint nargs, jint nresults) {
  (void)env; (void)self;
  return kl_pcall(STATE(state), (int)nargs, (int)nresults);
}

JNIEXPORT jint JNICALL
Java_com_seanproctor_lua_LuaJni_getTop(JNIEnv *env, jobject self, jlong state) {
  (void)env; (void)self;
  return lua_gettop(STATE(state));
}

JNIEXPORT void JNICALL
Java_com_seanproctor_lua_LuaJni_setTop(JNIEnv *env, jobject self, jlong state, jint idx) {
  (void)env; (void)self;
  lua_settop(STATE(state), (int)idx);
}

JNIEXPORT jboolean JNICALL
Java_com_seanproctor_lua_LuaJni_checkStack(JNIEnv *env, jobject self, jlong state, jint n) {
  (void)env; (void)self;
  return lua_checkstack(STATE(state), (int)n) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_seanproctor_lua_LuaJni_pushNil(JNIEnv *env, jobject self, jlong state) {
  (void)env; (void)self;
  lua_pushnil(STATE(state));
}

JNIEXPORT void JNICALL
Java_com_seanproctor_lua_LuaJni_pushBoolean(JNIEnv *env, jobject self, jlong state, jboolean value) {
  (void)env; (void)self;
  lua_pushboolean(STATE(state), value == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_seanproctor_lua_LuaJni_pushInteger(JNIEnv *env, jobject self, jlong state, jlong value) {
  (void)env; (void)self;
  lua_pushinteger(STATE(state), (lua_Integer)value);
}

JNIEXPORT void JNICALL
Java_com_seanproctor_lua_LuaJni_pushNumber(JNIEnv *env, jobject self, jlong state, jdouble value) {
  (void)env; (void)self;
  lua_pushnumber(STATE(state), (lua_Number)value);
}

JNIEXPORT void JNICALL
Java_com_seanproctor_lua_LuaJni_pushString(JNIEnv *env, jobject self, jlong state, jbyteArray bytes) {
  size_t len = 0;
  unsigned char *buf;
  (void)self;
  buf = copy_bytes(env, bytes, &len);
  if (buf == NULL) {
    lua_pushliteral(STATE(state), "");
    return;
  }
  lua_pushlstring(STATE(state), (const char *)buf, len);
  free(buf);
}

JNIEXPORT void JNICALL
Java_com_seanproctor_lua_LuaJni_pushValue(JNIEnv *env, jobject self, jlong state, jint idx) {
  (void)env; (void)self;
  lua_pushvalue(STATE(state), (int)idx);
}

JNIEXPORT void JNICALL
Java_com_seanproctor_lua_LuaJni_pushClosure(JNIEnv *env, jobject self, jlong state, jint id) {
  (void)env; (void)self;
  kl_push_dispatch_closure(STATE(state), (int)id);
}

JNIEXPORT jint JNICALL
Java_com_seanproctor_lua_LuaJni_type(JNIEnv *env, jobject self, jlong state, jint idx) {
  (void)env; (void)self;
  return lua_type(STATE(state), (int)idx);
}

JNIEXPORT jboolean JNICALL
Java_com_seanproctor_lua_LuaJni_isInteger(JNIEnv *env, jobject self, jlong state, jint idx) {
  (void)env; (void)self;
  return lua_isinteger(STATE(state), (int)idx) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_seanproctor_lua_LuaJni_toBoolean(JNIEnv *env, jobject self, jlong state, jint idx) {
  (void)env; (void)self;
  return lua_toboolean(STATE(state), (int)idx) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_com_seanproctor_lua_LuaJni_toInteger(JNIEnv *env, jobject self, jlong state, jint idx) {
  (void)env; (void)self;
  return (jlong)lua_tointegerx(STATE(state), (int)idx, NULL);
}

JNIEXPORT jdouble JNICALL
Java_com_seanproctor_lua_LuaJni_toNumber(JNIEnv *env, jobject self, jlong state, jint idx) {
  (void)env; (void)self;
  return (jdouble)lua_tonumberx(STATE(state), (int)idx, NULL);
}

JNIEXPORT jbyteArray JNICALL
Java_com_seanproctor_lua_LuaJni_toStringBytes(JNIEnv *env, jobject self, jlong state, jint idx) {
  size_t len = 0;
  const char *s = lua_tolstring(STATE(state), (int)idx, &len);
  jbyteArray arr;
  (void)self;
  if (s == NULL)
    return NULL;
  arr = (*env)->NewByteArray(env, (jsize)len);
  if (arr == NULL)
    return NULL;
  (*env)->SetByteArrayRegion(env, arr, 0, (jsize)len, (const jbyte *)s);
  return arr;
}

JNIEXPORT void JNICALL
Java_com_seanproctor_lua_LuaJni_newTable(JNIEnv *env, jobject self, jlong state) {
  (void)env; (void)self;
  lua_createtable(STATE(state), 0, 0);
}

JNIEXPORT jint JNICALL
Java_com_seanproctor_lua_LuaJni_next(JNIEnv *env, jobject self, jlong state, jint idx) {
  (void)env; (void)self;
  return lua_next(STATE(state), (int)idx);
}

JNIEXPORT jint JNICALL
Java_com_seanproctor_lua_LuaJni_getGlobal(JNIEnv *env, jobject self, jlong state, jbyteArray name) {
  size_t len = 0;
  unsigned char *buf;
  int type;
  (void)self;
  buf = copy_bytes(env, name, &len);
  if (buf == NULL) {
    lua_pushnil(STATE(state));
    return LUA_TNIL;
  }
  type = lua_getglobal(STATE(state), (const char *)buf);
  free(buf);
  return type;
}

JNIEXPORT void JNICALL
Java_com_seanproctor_lua_LuaJni_setGlobal(JNIEnv *env, jobject self, jlong state, jbyteArray name) {
  size_t len = 0;
  unsigned char *buf;
  (void)self;
  buf = copy_bytes(env, name, &len);
  if (buf == NULL) {
    lua_pop(STATE(state), 1);
    return;
  }
  lua_setglobal(STATE(state), (const char *)buf);
  free(buf);
}

JNIEXPORT jint JNICALL
Java_com_seanproctor_lua_LuaJni_ref(JNIEnv *env, jobject self, jlong state) {
  (void)env; (void)self;
  return kl_ref(STATE(state));
}

JNIEXPORT void JNICALL
Java_com_seanproctor_lua_LuaJni_unref(JNIEnv *env, jobject self, jlong state, jint ref) {
  (void)env; (void)self;
  kl_unref(STATE(state), (int)ref);
}

JNIEXPORT jint JNICALL
Java_com_seanproctor_lua_LuaJni_getRef(JNIEnv *env, jobject self, jlong state, jint ref) {
  (void)env; (void)self;
  return kl_getref(STATE(state), (int)ref);
}

JNIEXPORT jint JNICALL
Java_com_seanproctor_lua_LuaJni_pGetTable(JNIEnv *env, jobject self, jlong state) {
  (void)env; (void)self;
  return kl_pgettable(STATE(state));
}

JNIEXPORT jint JNICALL
Java_com_seanproctor_lua_LuaJni_pSetTable(JNIEnv *env, jobject self, jlong state) {
  (void)env; (void)self;
  return kl_psettable(STATE(state));
}

JNIEXPORT jint JNICALL
Java_com_seanproctor_lua_LuaJni_pLen(JNIEnv *env, jobject self, jlong state) {
  (void)env; (void)self;
  return kl_plen(STATE(state));
}
