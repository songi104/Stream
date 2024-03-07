#pragma once

#include <vector>
#include "SSD.h"
using namespace std;

class Block;
class Page;


enum BlockState { FREE, USED };
enum PageState { VALID, INVALID, ERASED };


class Block {
public:
    Block(int pagePerBlock);
    ~Block();
    
    int blockNumber;
    int invalidPageCount;

    vector<Page*> pageInBlock;
    BlockState state;
    
    // Other block information...
};

class Page {
public:
    Page(long physicalAddress, Block* block);
    long physicalAddress;
    PageState state;
    Block* block;
};

