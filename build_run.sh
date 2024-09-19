export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64

g++ -shared -fPIC -I${JAVA_HOME}/include -I${JAVA_HOME}/include/linux profiler.cpp -o libmyprofiler.so -ldl
javac Profiler.java
java -Djava.library.path=. TargetCode