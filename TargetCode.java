// File: TargetCode.java
public class TargetCode {
    static {
        System.loadLibrary("myprofiler"); // 加载本地库
    }

    // 声明本地方法
    public native void startProfiling();
    public native void stopProfiling();

    public void runTest() {
        // 目标代码逻辑
        for (int i = 0; i < 1000000; i++) {
            performWork();
        }
    }

    private void performWork() {
        // 模拟工作负载
        try {
            Thread.sleep(1);
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
    }

    public static void main(String[] args) {
        TargetCode tc = new TargetCode();
        tc.startProfiling();
        tc.runTest();
        tc.stopProfiling();
    }
}

