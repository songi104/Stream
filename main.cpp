

#include "SSD.h"
#include "Block.h"
using namespace std;

int main() {

    long long G = 1 << 30;
    int M = 1 << 20;
    int K = 1 << 10;

    SSD ssd(8*G, 4*M, 4*K);
    ssd.processTraceFile("..\\test-fio-small_stream");


    return 0;
}


