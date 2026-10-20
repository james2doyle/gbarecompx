// mobile_platform.cpp — see mobile_platform.h.

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE  // pthread_getattr_np for the desktop stack guard
#endif

#include "mobile_platform.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#if defined(__linux__)
#include <pthread.h>
#endif

#if defined(__ANDROID__)
#include <pthread.h>
#include <unistd.h>

#if defined(GBARECOMP_HAVE_SDL2)
#include <SDL.h>
#include <SDL_system.h>
#endif

#include <android/log.h>
#endif

#if defined(__ANDROID__)
#include "android_native_app_glue.h"  // struct android_app (activity/vm/clazz)
#include <jni.h>
// raylib's Android platform owns the NativeActivity app object. The symbol is
// exported by rcore_android.c but deliberately absent from raylib.h (upstream
// note: "Add this to header (if apps really need it)"), so declare it here.
extern "C" struct android_app* GetAndroidApp(void);
// The shared Java shell's JNI bridge (org.gbarecomp.GbaNative): the Activity
// pushes cutout insets + the physical panel density. Ported from the retired
// shell's SDL region (Phase 4f) — same Java contract, the store above.
extern "C" JNIEXPORT void JNICALL
Java_org_gbarecomp_GbaNative_setSafeInsets(JNIEnv*, jclass, jint left, jint top,
                                           jint right, jint bottom) {
    auto& info = gbarecomp::android_display_info();
    info.inset_left.store(left > 0 ? left : 0);
    info.inset_top.store(top > 0 ? top : 0);
    info.inset_right.store(right > 0 ? right : 0);
    info.inset_bottom.store(bottom > 0 ? bottom : 0);
    info.insets_known.store(true);
}
extern "C" JNIEXPORT void JNICALL
Java_org_gbarecomp_GbaNative_setDisplayDpi(JNIEnv*, jclass, jfloat xdpi,
                                           jfloat ydpi) {
    auto& info = gbarecomp::android_display_info();
    info.xdpi.store(xdpi);
    info.ydpi.store(ydpi);
}

namespace {
// Attach the calling (native game) thread to the JVM if needed. Detaching is
// the caller's decision: detaching an already-Java thread would tear down the
// VM attachment the runtime does not own.
JNIEnv* attach_env(JavaVM* vm, bool* attached) {
    *attached = false;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
        return env;
    }
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    *attached = true;
    return env;
}

// Clear a pending JNI exception. Describe/log only the first one: a missing
// Java method would otherwise spam the log on every haptic call.
void clear_jni_exception(JNIEnv* env, const char* what) {
    if (!env->ExceptionCheck()) return;
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) {
        env->ExceptionDescribe();
        __android_log_print(ANDROID_LOG_WARN, "FFTARecomp",
                            "haptics: %s failed (JNI exception)", what);
    }
    env->ExceptionClear();
}
}  // namespace
#endif

namespace gbarecomp {

AndroidDisplayInfo& android_display_info() {
    static AndroidDisplayInfo info;
    return info;
}

bool android_haptics_pulse(int duration_ms, float strength) {
#if defined(__ANDROID__)
    struct android_app* app = GetAndroidApp();
    if (!app || !app->activity) return false;
    JavaVM* vm = app->activity->vm;
    if (!vm) return false;
    bool attached = false;
    JNIEnv* env = attach_env(vm, &attached);
    if (!env) return false;
    jobject activity = app->activity->clazz;
    bool ok = false;
    if (activity) {
        jclass cls = env->GetObjectClass(activity);
        if (cls) {
            // Method ids are stable for the Activity class; cache so the
            // per-impact path does one lookup, not a class scan.
            static std::atomic<jmethodID> cached_mid{nullptr};
            jmethodID mid = cached_mid.load(std::memory_order_relaxed);
            if (!mid) {
                mid = env->GetMethodID(cls, "vibrate", "(IF)V");
                if (mid) cached_mid.store(mid, std::memory_order_relaxed);
                clear_jni_exception(env, "GetMethodID(vibrate)");
            }
            if (mid) {
                env->CallVoidMethod(activity, mid,
                                    static_cast<jint>(duration_ms),
                                    static_cast<jfloat>(strength));
                ok = !env->ExceptionCheck();
                if (!ok) clear_jni_exception(env, "vibrate");
            }
            env->DeleteLocalRef(cls);
        }
    }
    if (attached) vm->DetachCurrentThread();
    return ok;
#else
    (void)duration_ms;
    (void)strength;
    return false;
#endif
}

const char* mobile_internal_storage_path() {
#if defined(__ANDROID__)
#if defined(GBARECOMP_HAVE_SDL2)
    return SDL_AndroidGetInternalStoragePath();
#elif defined(GBARECOMP_ANDROID_PACKAGE)
// Stringized so -DGBARECOMP_ANDROID_PACKAGE=org.example.game (no quotes)
// works from CMake/Gradle alike.
#define GBARECOMP__STR(x) #x
#define GBARECOMP__XSTR(x) GBARECOMP__STR(x)
    // NativeActivity build: no SDL to ask, and the path is a platform
    // convention — /data/user/0/<package>/files for the primary user.
    static const char kFiles[] = "/data/user/0/" GBARECOMP__XSTR(
                                     GBARECOMP_ANDROID_PACKAGE) "/files";
#undef GBARECOMP__XSTR
#undef GBARECOMP__STR
    return kFiles;
#else
    return nullptr;
#endif
#else
    return nullptr;
#endif
}

// Host-stack budget guard state (see mobile_stack_over_budget). File-local;
// armed per thread by run_stacked (mobile) or
// host_stack_guard_arm_current (desktop finite-stack threads).
static std::uintptr_t g_stack_guard_lo = 0;
// Critical tier: fires even inside a live IRQ handler (which normally gates the
// guard off) when the stack is so low that the handler cannot possibly complete
// anyway. Covers the stuck-nest-depth state (g_irq_nest_depth never returns to
// 0 after a guest-return/IRQ interleave), where the primary gate would
// otherwise leave the nesting unbounded.
static std::uintptr_t g_stack_guard_crit_lo = 0;

#if defined(__ANDROID__)
namespace {
struct StackedCall {
    int (*entry)(int, char**) = nullptr;
    int argc = 0;
    char** argv = nullptr;
    std::size_t stack_bytes = 0;
    int result = 1;
};
void* run_stacked(void* arg) {
    auto* call = static_cast<StackedCall*>(arg);
    // Arm this thread's budget: keep kGuardMargin of headroom for the
    // runtime's own transient frames between two instruction-boundary
    // checks, then unwind through the standard yield path.
    constexpr std::size_t kGuardMargin = 32u * 1024u * 1024u;
    const std::size_t budget = call->stack_bytes > kGuardMargin
        ? call->stack_bytes - kGuardMargin
        : call->stack_bytes / 2u;
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(&call);
    g_stack_guard_lo = base - budget;
    // Critical tier: allow unwinding even inside a live IRQ handler when the
    // stack is within ~0.5 MiB of the bottom (a stuck nest depth otherwise
    // disables the primary gate; see host_stack_critically_low).
    const std::uintptr_t bottom = base - call->stack_bytes;
    g_stack_guard_crit_lo = bottom + 512u * 1024u;
    call->result = call->entry(call->argc, call->argv);
    g_stack_guard_lo = 0;
    return nullptr;
}
}  // namespace
#endif

int mobile_run_with_stack(int (*entry)(int, char**), int argc, char** argv,
                          std::size_t stack_bytes) {
#if defined(__ANDROID__)
    StackedCall call;
    call.entry = entry;
    call.argc = argc;
    call.argv = argv;
    call.stack_bytes = stack_bytes;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    // Reserved address space only; pages are committed as the stack grows.
    const bool sized = pthread_attr_setstacksize(&attr, stack_bytes) == 0;
    pthread_t thread;
    if (pthread_create(&thread, &attr, run_stacked, &call) != 0) {
        pthread_attr_destroy(&attr);
        std::fprintf(stderr, "mobile: could not create the %zu-byte game thread; "
                     "running on the calling thread\n", stack_bytes);
        return entry(argc, argv);
    }
    pthread_attr_destroy(&attr);
    std::fprintf(stderr, "mobile: game thread stack=%zu bytes%s\n", stack_bytes,
                 sized ? "" : " (size request rejected)");
    pthread_join(thread, nullptr);
    return call.result;
#else
    (void)stack_bytes;
    return entry(argc, argv);
#endif
}

bool mobile_stack_over_budget(const void* probe) {
    if (g_stack_guard_lo == 0) return false;
    return reinterpret_cast<std::uintptr_t>(probe) < g_stack_guard_lo;
}

bool host_stack_critically_low(const void* probe) {
    if (g_stack_guard_crit_lo == 0) return false;
    return reinterpret_cast<std::uintptr_t>(probe) < g_stack_guard_crit_lo;
}

// Desktop (and any pthread) arming: arm the guard for the CALLING thread
// from its real stack bounds, unwinding once the probe comes within
// `reserve_bytes` of the stack bottom. Used for the finite-stack TCP game
// thread, where the split vblank-wait loop's per-iteration host calls can
// nest past the default 8 MiB (see runtime_bus_bridge.cpp guard block).
void host_stack_guard_arm_current(std::size_t reserve_bytes) {
#if defined(__linux__)
    pthread_attr_t attr;
    void*  base = nullptr;
    std::size_t size = 0;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        if (pthread_attr_getstack(&attr, &base, &size) == 0 && base != nullptr) {
            const std::uintptr_t lo = reinterpret_cast<std::uintptr_t>(base);
            const std::size_t margin =
                reserve_bytes < size ? reserve_bytes : size / 2u;
            g_stack_guard_lo = lo + margin;
            const std::size_t crit =
                (size / 16u) < 512u * 1024u ? size / 16u : 512u * 1024u;
            g_stack_guard_crit_lo = lo + crit;
        }
        pthread_attr_destroy(&attr);
    }
#else
    (void)reserve_bytes;
#endif
}

bool mobile_prepare_process(std::vector<std::string>& args,
                            const MobileProcessOptions& options) {
#if defined(__ANDROID__)
    if (const char* storage = mobile_internal_storage_path())
        (void)chdir(storage);
    const char* log = options.log_file ? options.log_file : "android-runtime.log";
    // One open file description for both streams: separate freopen()s would
    // give stderr and stdout independent offsets, and each would overwrite
    // the other's lines in the shared log. The open is checked: a failed
    // freopen used to silence the session completely (game runs, zero log,
    // no error — observed 2026-10-11). On failure the session keeps working
    // but reports through logcat, the one channel that always exists.
    if (!std::freopen(log, "w", stderr)) {
        __android_log_print(ANDROID_LOG_WARN, "FFTARecomp",
            "mobile: cannot open log %s; logging to logcat only", log);
    }
    std::fflush(stdout);
    if (fileno(stderr) >= 0) dup2(fileno(stderr), fileno(stdout));
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // No compiler on the device: misses bridge loudly and are reported for
    // the offline heal loop instead of attempting an on-device rebuild.
    setenv("GBARECOMP_SELFHEAL_RECOMPILE", "0", 0);
    std::fprintf(stderr,
                 "mobile: storage=%s selfheal=report-only "
                 "(dispatch misses bridge loudly -> recomp_master_misses.toml.frag)\n",
                 mobile_internal_storage_path()
                     ? mobile_internal_storage_path() : "(unknown)");
    if (args.empty()) args.emplace_back("main");
    if (options.program_name) args[0] = options.program_name;
    if (options.game_config) args.emplace_back(options.game_config);
    args.emplace_back("--no-launcher");
    // Validation hook: extra runtime arguments (e.g. "--tcp-observe 19892")
    // from an app-private file only adb run-as can create. Absent in normal
    // installs; every argument taken is logged. ONE-SHOT: the file is
    // removed after reading, so a debug override (e.g. --launcher forcing
    // the pre-boot screen) applies to exactly the boot that asked for it —
    // otherwise a forced flag persists across the PickPending relaunch and
    // the launch screen reappears after a successful pick (observed on
    // device, 2026-10-16). Debug scripts rewrite the file per boot.
    if (std::FILE* f = std::fopen("debug-args.txt", "r")) {
        char word[256];
        while (std::fscanf(f, "%255s", word) == 1) {
            args.emplace_back(word);
            std::fprintf(stderr, "mobile: debug arg %s\n", word);
        }
        std::fclose(f);
        std::remove("debug-args.txt");
        std::fprintf(stderr, "mobile: debug args consumed (one-shot)\n");
    }
    return true;
#else
    (void)args;
    (void)options;
    return false;
#endif
}

}  // namespace gbarecomp
