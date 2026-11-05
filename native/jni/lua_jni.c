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

/* Helper for identifier strings (globals, chunk names, etc) which are usually short and need NUL termination */
static inline int get_ident_string(JNIEnv *env, jbyteArray arr, char *stack_buf, size_t stack_buf_size, char **out_ptr, size_t *out_len) {
  if (arr == NULL) return 0;
  jsize len = (*env)->GetArrayLength(env, arr);
  *out_len = (size_t)len;
  if ((size_t)len < stack_buf_size) {
    (*env)->GetByteArrayRegion(env, arr, 0, len, (jbyte *)stack_buf);
    stack_buf[len] = '\0';
    *out_ptr = stack_buf;
    return 1;
  }
  char *heap = (char *)malloc((size_t)len + 1);
  if (heap == NULL) return 0;
  (*env)->GetByteArrayRegion(env, arr, 0, len, (jbyte *)heap);
  heap[len] = '\0';
  *out_ptr = heap;
  return 2; /* 2 indicates heap allocated, caller must free */
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
  (void)self;
  if (code == NULL) {
    lua_pushliteral(STATE(state), "null code buffer");
    return LUA_ERRMEM;
  }

  char name_buf[256];
  char *name_str = NULL;
  size_t name_len = 0;
  int name_alloc = get_ident_string(env, chunkName, name_buf, sizeof(name_buf), &name_str, &name_len);
  const char *final_name = name_alloc ? name_str : "";

  jsize code_len = (*env)->GetArrayLength(env, code);
  int status;

  /* Zero-copy critical pin to avoid intermediate malloc/memcpy of large Lua code */
  void *code_ptr = (*env)->GetPrimitiveArrayCritical(env, code, NULL);
  if (code_ptr != NULL) {
    status = kl_loadbuffer(STATE(state), (const char *)code_ptr, (int)code_len, final_name);
    (*env)->ReleasePrimitiveArrayCritical(env, code, code_ptr, JNI_ABORT);
  } else {
    /* Fallback if critical section unavailable */
    jbyte *elems = (*env)->GetByteArrayElements(env, code, NULL);
    if (elems != NULL) {
      status = kl_loadbuffer(STATE(state), (const char *)elems, (int)code_len, final_name);
      (*env)->ReleaseByteArrayElements(env, code, elems, JNI_ABORT);
    } else {
      lua_pushliteral(STATE(state), "not enough memory");
      status = LUA_ERRMEM;
    }
  }

  if (name_alloc == 2) {
    free(name_str);
  }
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
  (void)self;
  if (bytes == NULL) {
    lua_pushnil(STATE(state));
    return;
  }
  jsize len = (*env)->GetArrayLength(env, bytes);
  if (len == 0) {
    lua_pushliteral(STATE(state), "");
    return;
  }
  if (len < 256) {
    char stack_buf[256];
    (*env)->GetByteArrayRegion(env, bytes, 0, len, (jbyte *)stack_buf);
    lua_pushlstring(STATE(state), stack_buf, (size_t)len);
  } else {
    /* Large string / HTML / response body: pin directly in JVM heap without intermediate malloc */
    void *ptr = (*env)->GetPrimitiveArrayCritical(env, bytes, NULL);
    if (ptr != NULL) {
      lua_pushlstring(STATE(state), (const char *)ptr, (size_t)len);
      (*env)->ReleasePrimitiveArrayCritical(env, bytes, ptr, JNI_ABORT);
    } else {
      jbyte *elems = (*env)->GetByteArrayElements(env, bytes, NULL);
      if (elems != NULL) {
        lua_pushlstring(STATE(state), (const char *)elems, (size_t)len);
        (*env)->ReleaseByteArrayElements(env, bytes, elems, JNI_ABORT);
      } else {
        lua_pushliteral(STATE(state), "");
      }
    }
  }
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
  char stack_buf[128];
  char *name_str = NULL;
  size_t len = 0;
  int type;
  (void)self;
  int alloc = get_ident_string(env, name, stack_buf, sizeof(stack_buf), &name_str, &len);
  if (!alloc) {
    lua_pushnil(STATE(state));
    return LUA_TNIL;
  }
  type = lua_getglobal(STATE(state), name_str);
  if (alloc == 2) {
    free(name_str);
  }
  return type;
}

JNIEXPORT void JNICALL
Java_com_seanproctor_lua_LuaJni_setGlobal(JNIEnv *env, jobject self, jlong state, jbyteArray name) {
  char stack_buf[128];
  char *name_str = NULL;
  size_t len = 0;
  (void)self;
  int alloc = get_ident_string(env, name, stack_buf, sizeof(stack_buf), &name_str, &len);
  if (!alloc) {
    lua_pop(STATE(state), 1);
    return;
  }
  lua_setglobal(STATE(state), name_str);
  if (alloc == 2) {
    free(name_str);
  }
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
