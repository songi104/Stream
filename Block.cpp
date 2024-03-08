#include "SSD.h"
#include "Block.h"

#include <vector>
using namespace std;

long physicalAddress = 0;

Block::Block(int pagePerBlock) {
    pageInBlock.resize((pagePerBlock), nullptr);
    for (int i = 0; i < pagePerBlock; ++i) {
        pageInBlock[i] = new Page(physicalAddress, this);
        physicalAddress++;
    }
    invalidPageCount = 0;
    blockStreamNumber = -1; // when block in usedBlocksForGreedy, it should be not -1
}

Block::~Block() {
        for (int i = 0; i < pageInBlock.size(); ++i) {
            delete pageInBlock[i];
        }
    }

Page::Page(long physicalAddress, Block* block) : physicalAddress(physicalAddress), block(block), state(ERASED) {

}