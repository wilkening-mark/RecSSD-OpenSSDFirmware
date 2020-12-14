// Developed by Mark Wilkening
// Harvard University, VLSI-Arch Lab
// 10/2017

#ifndef IA_TRANS_BUFFER_H_
#define IA_TRANS_BUFFER_H_

#include "init_ftl.h"
#include "internal_req.h"
#include "xtime_l.h" // XTime_GetTime()

// Macros for timing statistics
#define TIMEDIFF(t1,t2) (t2 - t1)
#define MICROSECONDS(t) (1000000.0 * t / COUNTS_PER_SECOND)

#define TRANS_BUF_ENTRY_NUM 8

#define TRANS_CONFIG_SIZE SECTOR_SIZE_FTL * 256
#define TRANS_SCRATCHPAD_SIZE (SECTOR_SIZE_FTL * 256)
#define TRANS_BUF_ENTRY_SIZE TRANS_SCRATCHPAD_SIZE

#define MAX_EMBEDDINGS_PER_REQUEST 262144
#define MAX_EMBEDDING_RESULT_PAGES 256

#define TRANS_EMBED_CACHE_ENTRY_NUM 1048576 // 2^20

struct transBufEntry {
	/*
	unsigned int  slba;
	unsigned int  nlb;
	unsigned int  nlbCompleted;
	unsigned int  nlbRequested;
	unsigned int  configured : 1;
	unsigned int  allocated : 1;
	unsigned int  rxDmaExe : 1;
	unsigned int  rxDmaTail : 8;
	unsigned int  reserved1 : 21;
	unsigned int  rxDmaOverFlowCnt;
	unsigned int  prev : 16;
	unsigned int  next : 16;
	unsigned short bytesCompleted[SECTOR_SIZE_FTL];
	unsigned int pagesTranslated;
	*/

	/*
	 * Some request configuration information needs to be reformatted/book-kept to
	 * be partitioned by flash pages.
	 *
	 * This just makes the translation function simpler.
	 */
	unsigned int perPageSLBAs[MAX_EMBEDDINGS_PER_REQUEST];
	unsigned int perPageStartingIndex[MAX_EMBEDDINGS_PER_REQUEST];
	unsigned int perPageInputLength[MAX_EMBEDDINGS_PER_REQUEST];
	unsigned int perResultSectorInputEmbeddings[MAX_EMBEDDING_RESULT_PAGES];
	unsigned int perResultSectorCompletedEmbeddings[MAX_EMBEDDING_RESULT_PAGES];
	/* end reformatted config */

	/* begin dynamic bookkeeping */
	unsigned int  slba;
	unsigned int  requestId;
	unsigned int  nlb;
	unsigned int  nlbRequested;
	unsigned int  nlbCompleted;
	unsigned int  nPages;
	unsigned int  pagesTranslated;

	unsigned int  configured : 1;
	unsigned int  allocated : 1;
	unsigned int  rxDmaExe : 1;
	unsigned int  rxDmaTail : 8;
	unsigned int  reserved1 : 21;
	unsigned int  rxDmaOverFlowCnt;
	unsigned int  prev : 16;
	unsigned int  next : 16;

	// Timing counters TODO: do we need to change/remove these for recsys?

	// Per Translation Request
	XTime configWriteRequested;
	XTime configWritten;
	XTime configProcessed;
	XTime requestCompleted;

	// Need a counter per page because these
	// operations are asynchronous
	// TODO -- too much memory :(

	// Per Flash Page
	XTime flashReadStarted[SECTOR_SIZE_FTL];
	XTime translationStarted[SECTOR_SIZE_FTL];
	XTime translationCompleted[SECTOR_SIZE_FTL];

	// Per Returned Sector
	XTime sectorRequested[SECTOR_SIZE_FTL];
	XTime sectorRequestCompleted[SECTOR_SIZE_FTL];
};

struct transBufArray {
  struct transBufEntry bufEntry[TRANS_BUF_ENTRY_NUM];
};

struct transBufAvailQueue {
  unsigned int head : 16;
  unsigned int tail : 16;
};

struct transEmbedCacheEntry {
	unsigned char valid : 1;
	unsigned int tag : 12;
	unsigned int reserved0 : 19;
	unsigned char embedding_bytes[128]; // Assuming fixed attribute size and embedding vector length
};

struct transEmbedCache {
	struct transEmbedCacheEntry cacheEntry[TRANS_EMBED_CACHE_ENTRY_NUM];
};

struct transConfig {
  /*
   * Configuration for Embedding table lookup.
   *
   * On disk, the embedding table is a simple list of embeddings (rows),
   * which are vectors of attributes. We want to select a few embeddings
   * and MAC them together into a resulting embedding vector. We also
   * want to batch this operation.
   *
   * Ex. IDs = [0, 15, 24, 32] -- sorted list of embedding IDs (row ID)
   *     lengths = [3, 1] -- reduction count for each result embedding
   *
   *     resultEmbeddings = 2
   *     inputEmbeddings = 4
   *     embeddingIDList = [0,0, 0,15, 0,24, 1,32, NULL]
   *
   *     result = ['embeddings 0, 15, 24 MACed', 'embedding 32']
   */
  unsigned int attributeSize;
  unsigned int embeddingLength;
  unsigned int resultEmbeddings;
  unsigned int inputEmbeddings;
  unsigned int tableID;
  struct embeddingIDPair {
	  unsigned int result;
	  unsigned int embeddingID;
  };
  struct embeddingIDPair embeddingIDList[(TRANS_CONFIG_SIZE - 20) / 8];
};

struct transStatistics {
	double requestLatency;
	double configWriteLatency;
	double configProcessLatency;
	double requests;

	double flashReadLatency;
	double translationLatency;
	double pages;
	double totalReadLatency;

	double returnLatency;
	double sectors;

	double cache_hits;
	double cache_misses;
};

extern struct transBufArray* transMap;
extern struct transBufAvailQueue* transAvailQ;
extern struct transStatistics* transStats;

extern struct transEmbedCache* transCache;

void TransBufInit();
unsigned int AllocateTransBufEntry(unsigned int slba, unsigned int requestId);
void DeallocateTransBufEntry(unsigned int entryIdx);
void ConfigureTransBufEntry(unsigned int entryIdx);
unsigned int readTranslatedPagesNonBlocking(unsigned int entryIdx, unsigned int firstSector, unsigned int nextSector,
		unsigned int requestedSectors, unsigned int cmdSlotTag);
int translatePagesNonBlocking(unsigned int entryIdx, unsigned int nextPage);
void translatePage(unsigned int entryIdx, void* devAddr, unsigned int page_idx);
unsigned int readPageToTranslateNonBlocking(unsigned int entryIdx, unsigned int lpa, unsigned int page_idx);
int findTransBufEntry(unsigned int requestId);

#endif /* IA_TRANS_BUFFER_H_ */
