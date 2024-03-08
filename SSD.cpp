#include "SSD.h"
#include "Block.h"


//setting is strategy
LogStructuredFTL::GarbageCollectionStrategy setting = LogStructuredFTL::GarbageCollectionStrategy::Greedy;
using namespace std;



SSD::SSD(long long ssdCapacity, int blockCapacity, int pageCapacity)
	: ssdCapacity(ssdCapacity), blockCapacity(blockCapacity), pageCapacity(pageCapacity)
{
	blockPerSSD = ssdCapacity / blockCapacity;
	pagePerBlock = blockCapacity / pageCapacity;
	blockInSSD.resize(blockPerSSD, nullptr);
	for (int i = 0; i < blockPerSSD; ++i) {
		blockInSSD[i] = new Block(pagePerBlock);
		blockInSSD[i]->blockNumber = i;
	}
	for (int j = 0; j < 5; j++) {
		blockInSSD[j]->blockStreamNumber = j;
	}

	mappingTable = new MappingTable(this);
	ftl = new LogStructuredFTL(this, setting);
};

SSD::~SSD() {
	for (Block* block : blockInSSD) {
		delete block;
	}
	delete mappingTable;
}



long long tmp_ioSize = 0;
long long printout_ioSize = 0;

void SSD::processTraceFile(const std::string& traceFilePath) {

	ifstream traceFile(traceFilePath);
	if (!traceFile.is_open()) {
		cerr << "Error opening trace file!" << endl;
		return;
	}

	cout << "TimeStamp IOtype LBA IOsize StreamNumber\n------------------------------" << endl;

	string line;
	int i = 0;
	while (getline(traceFile, line)) {
		//cout << line << endl;
		istringstream iss(line);
		double timeStamp;
		int ioType, logicalAddress, ioSize, streamNumber;


		// Parse the values from the line
		if (!(iss >> timeStamp >> ioType >> logicalAddress >> ioSize >> streamNumber)) {
			cerr << "Error parsing line: " << line << endl;
			return;
		}


		switch (ioType) {
		case 0: //read
			//cout << "read: " << logicalAddress << ioSize << endl; //
			break;
		case 1: //write
			ftl->currentStreamNumber = streamNumber;
			ftl->write(timeStamp, logicalAddress);
			break;
		case 2: break;
		case 3: //trim
			cout << "trim" << endl;
			break;
		default:
			cerr << "Unknown I/O type in line: " << line << endl;
			break;
		}

		//Check if we need to print out the output
		printout_ioSize += ioSize;
		if (printout_ioSize >= 8LL * (1LL << 30)) {
			printout_ioSize = 0;
			ftl->printOutput();
		}

		tmp_ioSize += ioSize;
		if (tmp_ioSize >= 50LL * (1LL << 30)) {
			tmp_ioSize = 0;
			ftl->userWriteCount_tmp = 0;
			ftl->totalWriteCount_tmp = 0;
		}

		i++;

	}
	traceFile.close();
}


//-------------------FTL Part-------------------


LogStructuredFTL::LogStructuredFTL(SSD* ssd, GarbageCollectionStrategy strategy)
	: ssd(ssd), strategy(strategy) {
	this->mappingTable = ssd->mappingTable;

	logPageOffsets[currentStreamNumber] = 0;

	userWriteCount = 0;
	totalWriteCount = 0;

	userWriteCount_tmp = 0;
	totalWriteCount_tmp = 0;
	//utilization = 0.0;
	eraseCount = 0;
	ratio = 0.0;

	for (int i = 5; i < ssd->blockPerSSD; i++) {
		freeBlocksIndex.push_back(i);
	}

};


void LogStructuredFTL::write(double timeStamp, int logicalAddress) {

	this->currentTimestamp = timeStamp;

	// 1. look to a mappingTable

	auto it = this->mappingTable->logicalToPhysical.find(logicalAddress);
	int physicalAddress = (it != mappingTable->logicalToPhysical.end()) ? it->second : -1;

	// 2. update mappingTable, pageState, and BlockState
	if (physicalAddress == -1) {
		// Case1: No original write
		int physicalAddress = logBlockNumbers[currentStreamNumber] * ssd->pagePerBlock + logPageOffsets[currentStreamNumber];
		mappingTable->logicalToPhysical.emplace(make_pair(logicalAddress, physicalAddress));
		updatePageState(physicalAddress, PageState::VALID); //updatePageState with VALID

	}
	else {
		// Case2: Original Write exists
		int originalPhysicalAddress = physicalAddress;
		int newPhysicalAddress = logBlockNumbers[currentStreamNumber] * ssd->pagePerBlock + logPageOffsets[currentStreamNumber];

		// update mappingTable
		mappingTable->logicalToPhysical[logicalAddress] = physicalAddress;

		//change old page's state to INVALID
		int oldBlockNumber = originalPhysicalAddress / ssd->pagePerBlock;
		int oldPageNumber = originalPhysicalAddress % ssd->pagePerBlock;
		Page* oldPage = ssd->blockInSSD[oldBlockNumber]->pageInBlock[oldPageNumber];
		if (oldPage->state == PageState::VALID) {
			oldPage->state = PageState::INVALID;
			ssd->blockInSSD[oldBlockNumber]->invalidPageCount++;
		}

		//change new page's state to VALID
		Page* newPage = ssd->blockInSSD[logBlockNumbers[currentStreamNumber]]->pageInBlock[logPageOffsets[currentStreamNumber]];
		newPage->state = PageState::VALID;

	}

	// 3. We need to update pageOffset
	logPageOffsets[currentStreamNumber]++;
	if (logPageOffsets[currentStreamNumber] >= ssd->pagePerBlock) {
		logPageOffsets[currentStreamNumber] = 0;

		Block* oldBlock = ssd->blockInSSD[logBlockNumbers[currentStreamNumber]];
		usedBlocksForGreedy.emplace_back(oldBlock->blockNumber, &oldBlock->invalidPageCount);
		ssd->blockInSSD[oldBlock->blockNumber]->blockStreamNumber = currentStreamNumber;

		logBlockNumbers[currentStreamNumber] = freeBlocksIndex.front();
		freeBlocksIndex.pop_front();
	}

	// 4. garbage collection occurs
	while (freeBlocksIndex.size() < 2) {
		if (strategy == GarbageCollectionStrategy::FIFO)
		{
			garbageCollect(0);
		}
		else if (strategy == GarbageCollectionStrategy::Greedy) {
			garbageCollectGreedy();
		}
	}


	userWriteCount++;
	totalWriteCount++;
	userWriteCount_tmp++;
	totalWriteCount_tmp++;

}

int validPageNumber = 0;
int garbageCollectCount = 0;

void LogStructuredFTL::garbageCollect(int idx) {
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
	else if (allValid) { //TODO: how to handle this case? -> 일단 그냥 옮기기만한다
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

				// 3. update State
				updatePageState(newPhysicalAddress, PageState::VALID);
				updateBlockState(newPhysicalAddress, BlockState::USED);



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

void LogStructuredFTL::garbageCollectGreedy() {

	// 1. sort the usedBlocks based on invalidPageCount in descending order
	sort(usedBlocksForGreedy.begin(), usedBlocksForGreedy.end(), [this](const auto& a, const auto& b) {
		return *(a.second) > *(b.second);
		});

	int victimBlockNumber = usedBlocksForGreedy[0].first;
	Block* victimBlock = ssd->blockInSSD[victimBlockNumber];

	// 2. Check if the block has all invalid pages
	if (victimBlock->invalidPageCount == ssd->pagePerBlock) {
	// Case 1) Block has all invalid pages
		
	}

	else {
	// Case 2) Block has partial valid pages
		int victimStreamNumber = victimBlock->blockStreamNumber;
		int victimBlockNumber = victimBlock->blockNumber;

		//Error handling
		try {
			if (victimStreamNumber == -1) {
				throw runtime_error("victimStreamNumber is -1");
			}
		}
		catch (const std::runtime_error& e) {
			cerr << "Error: " << e.what() << endl;
		}

		for (auto& page : victimBlock->pageInBlock) {

			if (page->state == PageState::VALID) {
				// Trim Page
				
				// 1. We already know physicalAddress
				int victimPhysicalAddress = page->physicalAddress;
				
				int newBlockNumber = logBlockNumbers[victimStreamNumber];
				int newPageOffset = logPageOffsets[victimStreamNumber];
				int newPhysicalAddress = newBlockNumber* ssd->pagePerBlock + newPageOffset;

				// 2. update mappingTable
				mappingTable->logicalToPhysical[victimPhysicalAddress] = newPhysicalAddress;

				// 3. update State of new
				ssd->blockInSSD[newBlockNumber]->pageInBlock[newPageOffset]->state = PageState::VALID;

				// 4. We need to update logPage
				logPageOffsets[victimStreamNumber]++;
				if (logPageOffsets[victimStreamNumber] >= ssd->pagePerBlock) {
					usedBlocksForGreedy.emplace_back(victimBlockNumber, &victimBlock->invalidPageCount);
					ssd->blockInSSD[victimBlockNumber]->blockStreamNumber = victimStreamNumber;

					logPageOffsets[victimStreamNumber] = 0;
					logBlockNumbers[victimStreamNumber] = freeBlocksIndex.front();
					freeBlocksIndex.pop_front();
				}

				totalWriteCount++;
				totalWriteCount_tmp++;
				validPageNumber++;
			}
		}
	}

	usedBlocksForGreedy.erase(usedBlocksForGreedy.begin());
	for (auto page : victimBlock->pageInBlock) {
		page->state = PageState::ERASED;
	}

	victimBlock->state = BlockState::FREE;
	victimBlock->invalidPageCount = 0;
	victimBlock->blockStreamNumber = -1;
	freeBlocksIndex.push_back(victimBlock->blockNumber);

	eraseCount++;
	garbageCollectCount++;
	this->ratio = double(validPageNumber) / (garbageCollectCount * (ssd->pagePerBlock));

};




void LogStructuredFTL::updateBlockState(int physicalAddress, BlockState newState) {
	int blockNumber = physicalAddress / (ssd->pagePerBlock);
	Block* block = ssd->blockInSSD[blockNumber];

	if (block->state == newState) {
		return;
	}
	else { //다를 시에
		if (newState == BlockState::FREE) {

			//kick out from used Block
			auto it = remove_if(usedBlocks.begin(), usedBlocks.end(),
				[blockNumber](const auto& pair) {
					return pair.second == blockNumber;
				});
			usedBlocks.erase(it);
			freeBlocksIndex.push_back(blockNumber);

		}

		else if (newState == BlockState::USED) {
			block->state = newState;
			if (strategy == FIFO) {
				double timestamp = this->currentTimestamp;
				usedBlocks.emplace_back(timestamp, blockNumber);
			}
			else {//Greedy
			}
		}
	}
}

double LogStructuredFTL::getCurrentTimeStamp() {
	return this->currentTimestamp;
}

void LogStructuredFTL::updatePageState(int physicalAddress, PageState pageState) {
	int blockNumber = physicalAddress / ssd->pagePerBlock;
	int pageNumber = physicalAddress % ssd->pagePerBlock;
	PageState* originalState = &ssd->blockInSSD[blockNumber]->pageInBlock[pageNumber]->state;
	if (*originalState == PageState::VALID) {
		if (pageState == PageState::INVALID) {
		}
	}
	*originalState = pageState;
}


void LogStructuredFTL::printOutput() {
	// TODO: type Check
	double waf = (double)(totalWriteCount) / (userWriteCount);
	double waf_tmp = (double)(totalWriteCount_tmp) / (userWriteCount_tmp);
	double utilization = 1.00;
	cout << "[Progress:8GiB] WAF: " << waf
		<< ", TMP_WAF: " << waf_tmp
		<< ", Utilization: " << utilization
		<< "\nGroup 0[" << (ssd->blockPerSSD - freeBlocksIndex.size())
		<< "]: " << ratio
		<< "(ERASE: " << eraseCount << ")" << endl;
}


//-------------------MappingTable Part-------------------
MappingTable::MappingTable(SSD* ssd) :ssd(ssd) {
	int size = ssd->blockPerSSD * ssd->pagePerBlock;
	logicalToPhysical.reserve(size);
}

//check=0:already exists, check=1:add a new
void MappingTable::update(int logicalAddress, int physicalAddress, int check) {
	if (check == 0) {
		// Case 0) already exists, just update
		logicalToPhysical[logicalAddress] = physicalAddress;
	}
	else if (check == 1) {
		// Case 1) doesn't exist, add a new
		logicalToPhysical.emplace(make_pair(logicalAddress, physicalAddress));
	}
}


