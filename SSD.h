#pragma once


#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <utility>
#include <deque>
#include <algorithm>

#include "Block.h"

using namespace std;


class SSD;
class FTL;
class LogStructuredFTL;
class MappingTable;



//-------------------SSD Part-------------------
class SSD {
public:
    SSD(long long ssdCapacity, int blockCapacity, int pageCapacity);
    ~SSD();
    void processTraceFile(const string& traceFilePath);

    vector<Block*> blockInSSD;
    int blockPerSSD;
    int pagePerBlock;
    MappingTable* mappingTable;
    LogStructuredFTL* ftl;

private:

    int ssdCapacity;
    int blockCapacity;
    int pageCapacity;
};





//-------------------FTL Part-------------------
class FTL {
public:
    virtual ~FTL() {};
    virtual void write(double timeStamp, int logicalAddress) =0;
    //virtual void updateMappingTable(int logicalAddress, int physicalAddress) = 0;
};

class LogStructuredFTL : public FTL { //FTL에서 상속받음
public:
    enum GarbageCollectionStrategy {
        FIFO,
        Greedy
    };

    LogStructuredFTL(SSD* ssd, GarbageCollectionStrategy strategy);

    void write(double timeStamp, int logicalAddress) override;
    void updatePageState(int physicalAddress, PageState pageState);
    void updateLogBlock();
    void updateBlockState(int physicalAddress, BlockState newState);
    
    void garbageCollect(int idx);
    void garbageCollectGreedy();
    double getCurrentTimeStamp();
    
    void trimPage(Page* page);

    void printOutput();
    
    MappingTable* mappingTable;
    deque<int> freeBlocksIndex;

    long userWriteCount;
    long totalWriteCount; 
    long userWriteCount_tmp;
    long totalWriteCount_tmp;

    int currentStreamNumber;

private:
    double currentTimestamp = -1;
    SSD* ssd;
    int eraseCount;
    double ratio ;
    
    vector<pair<double, int>> usedBlocks; //Timestamp, block number for FIFO
    vector<pair<int, int*>> usedBlocksForGreedy; //Block number, invalidPageCounter for Greedy
    GarbageCollectionStrategy strategy;

    int logBlockNumbers[5] = {0,1,2,3,4};
    int logPageOffsets[5];
};




//-------------------MappingTable Part-------------------
class MappingTable {
public:
    MappingTable(SSD* ssd);

    // check=0: already exists, check=1: add a new
    void update(int logicalAddress, int physicalAddress, int check);
    unordered_map<int, int> logicalToPhysical;

private:
    SSD* ssd;
};
