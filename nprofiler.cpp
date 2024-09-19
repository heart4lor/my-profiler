#include <iostream>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <cstring>
#include <jvmti.h>
#include <signal.h>

extern "C" {
    // AsyncGetCallTrace API
    void AsyncGetCallTrace(JavaThreadState* trace, jint depth, void* ucontext);
}

struct JavaThreadState {
    JvmtiThreadState state;
    JvmtiEnv* jvmti;
    jmethodID method[128]; // Up to 128 frames of stack
    jint depth;
};

void handle_perf_event(int signo, siginfo_t* info, void* context) {
    ucontext_t* uctx = (ucontext_t*)context;

    JavaThreadState trace;
    trace.depth = 128;

    // Use AsyncGetCallTrace to collect stack traces
    AsyncGetCallTrace(&trace, trace.depth, uctx);

    // Process the collected stack trace
    if (trace.state == THREAD_STATE_OK) {
        for (int i = 0; i < trace.depth; i++) {
            // jmethodID -> method name resolving can be done using JVMTI
            std::cout << "Java method ID: " << trace.method[i] << std::endl;
        }
    } else {
        std::cerr << "Failed to capture stack trace" << std::endl;
    }
}

int setup_perf_event() {
    // Set up the perf event attributes (CPU cycles as an example)
    struct perf_event_attr pea;
    memset(&pea, 0, sizeof(struct perf_event_attr));
    pea.type = PERF_TYPE_HARDWARE;
    pea.size = sizeof(struct perf_event_attr);
    pea.config = PERF_COUNT_HW_CPU_CYCLES;
    pea.sample_period = 1000;  // Adjust as necessary
    pea.sample_type = PERF_SAMPLE_IP;
    pea.disabled = 1;
    pea.exclude_kernel = 1;
    pea.exclude_hv = 1;

    // Open the perf event file descriptor
    int fd = syscall(SYS_perf_event_open, &pea, 0, -1, -1, 0);
    if (fd == -1) {
        std::cerr << "Error opening perf event: " << strerror(errno) << std::endl;
        return -1;
    }

    return fd;
}

void install_signal_handler(int fd) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handle_perf_event;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGIO, &sa, nullptr);

    // Set the file descriptor to non-blocking and enable signal-driven IO
    fcntl(fd, F_SETFL, O_NONBLOCK | O_ASYNC);
    fcntl(fd, F_SETSIG, SIGIO);
    fcntl(fd, F_SETOWN, getpid());

    // Start the perf event counter
    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
}

int main() {
    // Initialize perf event
    int fd = setup_perf_event();
    if (fd == -1) {
        return 1;
    }

    // Install signal handler for perf event
    install_signal_handler(fd);

    // Profiler loop (you can adapt this for a specific use case)
    while (true) {
        pause(); // Wait for perf event interrupts
    }

    return 0;
}
