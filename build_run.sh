export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64

g++ -shared -fPIC -I${JAVA_HOME}/include -I${JAVA_HOME}/include/linux profiler.cpp -o libprofiler.so -ldl
javac Profiler.java
java -agentpath:./libprofiler.so Profiler