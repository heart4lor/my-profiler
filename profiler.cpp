#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/syscall.h>   // 用于 syscall 函数和 __NR_perf_event_open
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include "jvmti.h"

// 定义 perf_event_open 系统调用的包装函数
static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                            int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

// 全局变量
static jvmtiEnv *jvmti = NULL;
static int perf_fd = -1;
static JavaVM *jvm = NULL;  // JavaVM 对象

// 信号处理函数，用于处理 perf 事件
void signal_handler(int signum, siginfo_t *info, void *ucontext) {
    printf("sig:%d from perf_event\n", signum);
    // 检查信号是否来自 perf_event
    if (info->si_fd != perf_fd) {
        printf("sig:%d not from perf_event", signum);
        return;
    }

    // 获取当前线程的调用栈
    JNIEnv *env = NULL;
    jint res = jvm->GetEnv((void **)&env, JNI_VERSION_1_6);
    if (res != JNI_OK) {
        printf("Failed to attach thread\n");
        return;
    }

    // 设置调用栈信息
    const int max_frames = 128;
    jvmtiFrameInfo frames[max_frames];
    jint count = 0;

    jvmtiError err = jvmti->GetStackTrace(NULL, 0, max_frames, frames, &count);
    if (err != JVMTI_ERROR_NONE) {
        fprintf(stderr, "Failed to get stack trace: %d\n", err);
        return;
    }

    // 输出调用栈
    printf("Call Trace (Thread ID: %ld):\n", syscall(__NR_gettid));
    for (int i = 0; i < count; i++) {
        char *method_name = NULL;
        char *method_signature = NULL;
        char *class_signature = NULL;

        // 获取方法名称
        err = jvmti->GetMethodName(frames[i].method, &method_name, &method_signature, NULL);
        if (err != JVMTI_ERROR_NONE) {
            continue;
        }

        // 获取类签名
        jclass declaring_class;
        err = jvmti->GetMethodDeclaringClass(frames[i].method, &declaring_class);
        if (err != JVMTI_ERROR_NONE) {
            jvmti->Deallocate((unsigned char *)method_name);
            jvmti->Deallocate((unsigned char *)method_signature);
            continue;
        }

        err = jvmti->GetClassSignature(declaring_class, &class_signature, NULL);
        if (err != JVMTI_ERROR_NONE) {
            jvmti->Deallocate((unsigned char *)method_name);
            jvmti->Deallocate((unsigned char *)method_signature);
            continue;
        }

        // 打印调用栈信息
        printf("  at %s%s%s (bci: %d)\n", class_signature, method_name, method_signature, frames[i].location);

        // 释放分配的内存
        jvmti->Deallocate((unsigned char *)method_name);
        jvmti->Deallocate((unsigned char *)method_signature);
        jvmti->Deallocate((unsigned char *)class_signature);
    }
}

extern "C" JNIEXPORT void JNICALL Java_TargetCode_startProfiling(JNIEnv *env, jobject obj) {
    // 获取 JavaVM 对象
    env->GetJavaVM(&jvm);

    // 设置 perf_event_attr
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));

    pe.type = PERF_TYPE_SOFTWARE;
    pe.size = sizeof(struct perf_event_attr);
    pe.config = PERF_COUNT_SW_CPU_CLOCK;
    pe.sample_period = 1000000000 / 19; // 19 Hz
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    pe.sample_type = PERF_SAMPLE_IP;

    // 调用 perf_event_open
    perf_fd = perf_event_open(&pe, 0, -1, -1, 0);
    if (perf_fd == -1) {
        fprintf(stderr, "Error opening perf event: %s\n", strerror(errno));
        return;
    } else {
        printf("perf_event_open success, fd:%d\n", perf_fd);
        long long count;
        int result = read(perf_fd, &count, sizeof(long long));
        if (result == -1) {
            perror("Error reading perf fd");
        } else {
            printf("perf event triggered %lld times\n", count);
        }
    }

    // 设置信号处理函数
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_sigaction = signal_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGIO, &sa, NULL) < 0) {
        perror("sigaction");
        close(perf_fd);
        perf_fd = -1;
        return;
    }

    // 设置文件描述符的所有者为当前进程，以接收 SIGIO 信号
    if (fcntl(perf_fd, F_SETOWN, getpid()) == -1) {
        perror("fcntl F_SETOWN");
        close(perf_fd);
        perf_fd = -1;
        return;
    }
    if (fcntl(perf_fd, F_SETFL, fcntl(perf_fd, F_GETFL) | O_ASYNC) == -1) {
        perror("fcntl F_SETFL");
        close(perf_fd);
        perf_fd = -1;
        return;
    }

    // 启动 perf 事件
    if (ioctl(perf_fd, PERF_EVENT_IOC_RESET, 0) == -1) {
        perror("ioctl PERF_EVENT_IOC_RESET");
        close(perf_fd);
        perf_fd = -1;
        return;
    }
    if (ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, 0) == -1) {
        perror("ioctl PERF_EVENT_IOC_ENABLE");
        close(perf_fd);
        perf_fd = -1;
        return;
    }
    
    sigset_t mask;
    sigemptyset(&mask);
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
        perror("sigprocmask failed");
    }

    printf("Profiling started...\n");
    long long count;
    int result = read(perf_fd, &count, sizeof(long long));
    if (result == -1) {
        perror("Error reading perf fd");
    } else {
        printf("perf event triggered %lld times\n", count);
    }

}

extern "C" JNIEXPORT void JNICALL Java_TargetCode_stopProfiling(JNIEnv *env, jobject obj) {
    if (perf_fd != -1) {
        long long count;
        int result = read(perf_fd, &count, sizeof(long long));
        if (result == -1) {
            perror("Error reading perf fd");
        } else {
            printf("close count perf event triggered %lld times\n", count);
        }
        // 停止 perf 事件
        ioctl(perf_fd, PERF_EVENT_IOC_DISABLE, 0);
        close(perf_fd);
        perf_fd = -1;
    }
    printf("Profiling stopped.\n");
}
