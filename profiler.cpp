#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <jvmti.h>

// Global variables
int perf_fd = -1;
jvmtiEnv* jvmti;
JavaVM* jvm;

// Signal handler for SIGIO
void signal_handler(int signum, siginfo_t* info, void* context) {
    std::cout << "Signal handler triggered, signum: " << signum << std::endl;

    if (info->si_fd != perf_fd) {
        std::cout << "Signal from unknown source, not perf_event." << std::endl;
        return;
    }

    std::cout << "Perf event triggered!" << std::endl;

    JNIEnv* env;
    int res = jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    
    if (res == JNI_EDETACHED) {
        if (jvm->AttachCurrentThread((void**)&env, NULL) != 0) {
            std::cerr << "Failed to attach thread to JVM." << std::endl;
            return;
        }
    }

    // Try to capture the Java stack trace
    jvmtiFrameInfo frames[10];
    jint count;

    jvmtiError err = jvmti->GetStackTrace(NULL, 0, 10, frames, &count);
    if (err != JVMTI_ERROR_NONE) {
        std::cerr << "Failed to get stack trace: " << err << std::endl;
        return;
    }

    std::cout << "Captured stack trace:" << std::endl;
    for (int i = 0; i < count; ++i) {
        std::cout << frames[i].method << std::endl;
    }
}

int setup_perf_event() {
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.type = PERF_TYPE_SOFTWARE;
    pe.config = PERF_COUNT_SW_CPU_CLOCK;
    pe.size = sizeof(struct perf_event_attr);
    pe.sample_period = 10000; // Adjusted for more frequent sampling
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;

    int fd = syscall(SYS_perf_event_open, &pe, 0, -1, -1, 0);
    if (fd == -1) {
        perror("perf_event_open failed");
        return -1;
    }

    return fd;
}

extern "C" {

JNIEXPORT void JNICALL Java_TargetCode_startProfiling(JNIEnv* env, jobject obj) {
    if (perf_fd != -1) {
        std::cerr << "Profiling is already started." << std::endl;
        return;
    }

    // Register signal handler for SIGIO
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = signal_handler;
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGIO, &sa, NULL) == -1) {
        perror("sigaction failed");
        return;
    }

    // Setup perf event
    perf_fd = setup_perf_event();
    if (perf_fd == -1) {
        return;
    }

    std::cout << "perf_event_open success, fd: " << perf_fd << std::endl;

    // Set F_SETOWN and F_SETFL to enable async notification
    if (fcntl(perf_fd, F_SETOWN, getpid()) == -1) {
        perror("fcntl F_SETOWN failed");
        return;
    }
    if (fcntl(perf_fd, F_SETFL, fcntl(perf_fd, F_GETFL) | O_ASYNC) == -1) {
        perror("fcntl F_SETFL failed");
        return;
    }

    // Enable perf event
    if (ioctl(perf_fd, PERF_EVENT_IOC_RESET, 0) == -1) {
        perror("ioctl PERF_EVENT_IOC_RESET failed");
        return;
    }
    if (ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, 0) == -1) {
        perror("ioctl PERF_EVENT_IOC_ENABLE failed");
        return;
    }

    std::cout << "Profiling started..." << std::endl;
}

JNIEXPORT void JNICALL Java_TargetCode_stopProfiling(JNIEnv* env, jobject obj) {
    if (perf_fd == -1) {
        std::cerr << "Profiling is not started." << std::endl;
        return;
    }

    // Stop perf event
    if (ioctl(perf_fd, PERF_EVENT_IOC_DISABLE, 0) == -1) {
        perror("ioctl PERF_EVENT_IOC_DISABLE failed");
        return;
    }

    close(perf_fd);
    perf_fd = -1;

    std::cout << "Profiling stopped." << std::endl;
}

} // extern "C"
