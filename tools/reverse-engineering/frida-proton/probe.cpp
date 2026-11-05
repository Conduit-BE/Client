#include <windows.h>
#include <cstdio>
extern "C" __declspec(dllexport) const unsigned int conduitFridaMarker = 0x434f4e44;
int main() {
 std::printf("ConduitFridaProbe Windows PID=%lu marker=%08x\n",GetCurrentProcessId(),conduitFridaMarker);
 std::fflush(stdout);
 for (int i=0;i<900;++i) Sleep(100);
 return 0;
}
