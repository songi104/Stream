#include "SSD.h"
#include "Block.h"

// �ӽ� GC ����. Greedy������ �򰥷��� ���� ������

void LogStructuredFTL::garbageCollect(int idx) {

	//���߿� �׸���� ��ġ�� ����
	int garbageCollectCount = 0;
	int validPageNumber = 0;

	//FIFO
	pair<double, int> victim = usedBlocks[idx];
	int blockNumber = victim.second;
	Block* block = ssd->blockInSSD[blockNumber];

	// Check all page states in the block
	bool allInvalid = true;
	bool allValid = true;

	for (const auto& page : block->pageInBlock) {
		if (page->state == PageState::VALID) {
			allInvalid = false;
			if (allValid == false) { break; }
		}
		else {
			allValid = false;
			if (allInvalid == false) { break; }
		}
	}

	if (allInvalid) {
		// Case 1: Block has all invalid pages, ERASE the block
		block->state = BlockState::FREE;
		eraseCount++;
		usedBlocks.erase(usedBlocks.begin());
		garbageCollectCount++;
		freeBlocksIndex.push_back(block->blockNumber);
	}
	else if (allValid) { //TODO: how to handle this case? -> �ϴ� �׳� �ű�⸸�Ѵ�
		// Case 2: Block has all valid pages
		usedBlocks.erase(usedBlocks.begin());
		double timeStamp = this->currentTimestamp;
		usedBlocks.emplace_back(timeStamp, block->blockNumber);
		garbageCollectCount++;
		return;
	}
	else {
		// Case 3: Block has part of valid pages
		// trim the pages

		for (auto& page : block->pageInBlock) {
			if (page->state == PageState::VALID) { //trim part
				// 4. We need to update logPage
				logPageOffsets[currentStreamNumber]++;
				if (logPageOffsets[currentStreamNumber] >= ssd->pagePerBlock) {
					logPageOffsets[currentStreamNumber] = 0;
					logBlockNumbers[currentStreamNumber] = freeBlocksIndex.front();
					freeBlocksIndex.pop_front();
				}

				// 1. We already know physicalAddress
				int originalPhysicalAddress = page->physicalAddress;
				int newPhysicalAddress = logBlockNumbers[currentStreamNumber] * ssd->pagePerBlock + logPageOffsets[currentStreamNumber];

				// 2. update mappingTable
				mappingTable->update(originalPhysicalAddress, newPhysicalAddress, 0); //mapingTable update with new physicalAddress

				/* 3. update State */



				totalWriteCount++;
				totalWriteCount_tmp++;
				validPageNumber++;
			}
		}
		block->state = BlockState::FREE;
		eraseCount++;
		usedBlocks.erase(usedBlocks.begin());
		freeBlocksIndex.push_back(block->blockNumber);
		double timeStamp = this->currentTimestamp;
		garbageCollectCount++;
		this->ratio = double(validPageNumber) / (garbageCollectCount * (ssd->pagePerBlock));
	}
}