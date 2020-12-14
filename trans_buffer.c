// Developed by Mark Wilkening
// Harvard University, VLSI-Arch Lab
// 10/2017

#include	"nvme/debug.h"
#include	"page_map.h"
#include	"lru_buffer.h"
#include	"trans_buffer.h"
#include	"memory_map.h"
#include	"nvme/host_lld.h"
#include	"low_level_scheduler.h"

struct transBufArray* transMap;
struct transBufAvailQueue* transAvailQ;
struct transStatistics* transStats;

struct transEmbedCache* transCache;

void TransBufInit()
{
  transMap = (struct transBufArray*) TRANS_BUF_MAP_ADDR;
  transAvailQ = (struct transBufAvailQueue*) TRANS_AVAIL_Q_ADDR;
  transStats = (struct transStatistics*) TRANS_STATS_ADDR;
  transStats->requestLatency = 0;
  transStats->configWriteLatency = 0;
  transStats->configProcessLatency = 0;
  transStats->requests = 0;
  transStats->flashReadLatency = 0;
  transStats->translationLatency = 0;
  transStats->pages = 0;
  transStats->returnLatency = 0;
  transStats->sectors = 0;
  transStats->cache_hits = 0;
  transStats->cache_misses = 0;

  int i;
  for (i = 0; i < TRANS_BUF_ENTRY_NUM; i++)
  {
    transMap->bufEntry[i].rxDmaExe = 0;
    transMap->bufEntry[i].prev = i == 0 ? 0xffff : i-1;
    transMap->bufEntry[i].next = i == TRANS_BUF_ENTRY_NUM-1 ? 0xffff : i+1;
    transMap->bufEntry[i].allocated = 0;
    transMap->bufEntry[i].configured = 0;
  }

  transAvailQ->head = 0;
  transAvailQ->tail = TRANS_BUF_ENTRY_NUM-1;

  transCache = (struct transEmbedCache*)(TRANS_EMBED_CACHE_ADDR);
  for (i = 0; i < TRANS_EMBED_CACHE_ENTRY_NUM; i++)
  {
	  transCache->cacheEntry[i].valid = 0;
  }
}

unsigned int AllocateTransBufEntry(unsigned int slba, unsigned int requestId)
{
  unsigned int entryIdx = 0xffff;

  if (transAvailQ->head != 0xffff)
  {
    entryIdx = transAvailQ->head;
    if (transAvailQ->head == transAvailQ->tail)
    {
      transAvailQ->head = 0xffff;
      transAvailQ->tail = 0xffff;
    }
    else
    {
      transAvailQ->head = transMap->bufEntry[transAvailQ->head].next;
      transMap->bufEntry[transAvailQ->head].prev = 0xffff;
    }
  }
  else
  {
	  ASSERT(0); // No room for the request! Bahh!
  }

  transMap->bufEntry[entryIdx].slba = slba;
  transMap->bufEntry[entryIdx].requestId = requestId;
  transMap->bufEntry[entryIdx].configured = 0;
  transMap->bufEntry[entryIdx].allocated = 1;
  transMap->bufEntry[entryIdx].nlbRequested = 0;
  transMap->bufEntry[entryIdx].nlbCompleted = 0;
  transMap->bufEntry[entryIdx].pagesTranslated = 0;

  return entryIdx;
}

void DeallocateTransBufEntry(unsigned int entryIdx)
{
  XTime_GetTime(&transMap->bufEntry[entryIdx].requestCompleted);

  transMap->bufEntry[entryIdx].prev = transAvailQ->tail;
  transMap->bufEntry[entryIdx].next = 0xffff;
  transMap->bufEntry[entryIdx].allocated = 0;
  transMap->bufEntry[entryIdx].configured = 0;
  if (transAvailQ->tail == 0xffff)
  {
    transAvailQ->head = entryIdx;
  }
  else
  {
    transMap->bufEntry[transAvailQ->tail].next = entryIdx;
  }
  transAvailQ->tail = entryIdx;

  // Update Aggregate Timing
  transStats->requestLatency += MICROSECONDS(
    (transMap->bufEntry[entryIdx].requestCompleted -
	transMap->bufEntry[entryIdx].configWriteRequested));
  transStats->configWriteLatency += MICROSECONDS(
    (transMap->bufEntry[entryIdx].configWritten -
	transMap->bufEntry[entryIdx].configWriteRequested));
  transStats->configProcessLatency += MICROSECONDS(
      (transMap->bufEntry[entryIdx].configProcessed -
  	transMap->bufEntry[entryIdx].configWritten));
  transStats->requests++;
  int page;
  for (page = 0;
	   page < transMap->bufEntry[entryIdx].pagesTranslated;
	   page++)
  {
	  transStats->flashReadLatency += MICROSECONDS(
			  (transMap->bufEntry[entryIdx].translationStarted[page] -
			  transMap->bufEntry[entryIdx].flashReadStarted[page]));
	  transStats->translationLatency += MICROSECONDS(
	  			  (transMap->bufEntry[entryIdx].translationCompleted[page] -
	  			  transMap->bufEntry[entryIdx].translationStarted[page]));
	  transStats->pages++;
  }
  int sector;
  XTime minRequested = transMap->bufEntry[entryIdx].sectorRequested[0];
  XTime maxCompleted = transMap->bufEntry[entryIdx].sectorRequestCompleted[0];
  for (sector = 0;
	   sector < transMap->bufEntry[entryIdx].nlb;
	   sector++)
  {
	  minRequested =
			  (minRequested < transMap->bufEntry[entryIdx].sectorRequested[sector]) ?
			  minRequested :
			  transMap->bufEntry[entryIdx].sectorRequested[sector];
	  maxCompleted =
			  (maxCompleted > transMap->bufEntry[entryIdx].sectorRequestCompleted[sector]) ?
			  maxCompleted :
			  transMap->bufEntry[entryIdx].sectorRequestCompleted[sector];
	  transStats->returnLatency += MICROSECONDS(
			  (transMap->bufEntry[entryIdx].sectorRequestCompleted[sector] -
			  transMap->bufEntry[entryIdx].sectorRequested[sector]));
	  transStats->sectors++;
  }
  transStats->totalReadLatency = MICROSECONDS((maxCompleted - minRequested));
}

void ConfigureTransBufEntry(unsigned int entryIdx)
{
	XTime_GetTime(&transMap->bufEntry[entryIdx].configWritten);

	struct transConfig* config = (struct transConfig*)(TRANS_CONFIG_ADDR + entryIdx * TRANS_CONFIG_SIZE);

	/* Number of 4k logical blocks being returned. */
	transMap->bufEntry[entryIdx].nlb = (config->resultEmbeddings * config->attributeSize * config->embeddingLength) / SECTOR_SIZE_FTL;
	if ((config->resultEmbeddings * config->attributeSize * config->embeddingLength) % SECTOR_SIZE_FTL != 0) {
		transMap->bufEntry[entryIdx].nlb += 1;
	}
	unsigned i;
	for (i = 0; i < transMap->bufEntry[entryIdx].nlb; i++)
	{
		transMap->bufEntry[entryIdx].perResultSectorCompletedEmbeddings[i] = 0;
		transMap->bufEntry[entryIdx].perResultSectorInputEmbeddings[i] = 0;
	}

	unsigned int embedding_index = 0;
	unsigned int result_sector;
	unsigned int page_index = 0;
	unsigned page_id = ((config->embeddingIDList[0].embeddingID) * config->attributeSize * config->embeddingLength) / PAGE_SIZE;
	unsigned cur_page_id = page_id;
	unsigned cur_page_input_length = 0;
	transMap->bufEntry[entryIdx].perPageSLBAs[page_index] =
						transMap->bufEntry[entryIdx].slba + (cur_page_id * SECTOR_NUM_PER_PAGE);
	transMap->bufEntry[entryIdx].perPageStartingIndex[page_index] = embedding_index;
	for (embedding_index = 0; embedding_index < config->inputEmbeddings; embedding_index++)
	{
		struct embeddingIDPair eID = config->embeddingIDList[embedding_index];
		result_sector = (eID.result * (config->embeddingLength * config->attributeSize)) / SECTOR_SIZE_FTL;

		// Cache FastPath
		unsigned int fullindex = (eID.embeddingID << 5) | config->tableID;
		unsigned int cache_index = fullindex & ((0x1 << 20)-1);
		unsigned int tag = (fullindex >> 20) & ((0x1 << 12)-1);
		//xil_printf("embedding_index (%d), eID.embeddingID (%d), fullindex (%x), cache_index (%x), tag (%x), entryIdx (%d), eID.result (%d).\r\n",
				//embedding_index, eID.embeddingID, fullindex, cache_index, tag, entryIdx, eID.result);
		if (transCache->cacheEntry[cache_index].valid &&
			transCache->cacheEntry[cache_index].tag == tag) {

			float* fromAtr = (float*)transCache->cacheEntry[cache_index].embedding_bytes;
			float* toBase = (float*)(TRANS_BUF_ADDR + entryIdx * TRANS_BUF_ENTRY_SIZE);
			float* toAtr = toBase + (eID.result * config->embeddingLength);
			int k = 0;
	        for (k = 0; k < config->embeddingLength; k++)
			{
				// Assume for now attribute size is 4 and embedding entries are floats
				*((float*)toAtr) += *((float*)fromAtr);
				toAtr++;
				fromAtr++;
			}
	        transStats->cache_hits++;
			continue;
		}
		transStats->cache_misses++;
		// END FastPath -- Make sure embedding is processed from Flash

		transMap->bufEntry[entryIdx].perResultSectorInputEmbeddings[result_sector] += 1;

		cur_page_id = (eID.embeddingID * config->attributeSize * config->embeddingLength) / PAGE_SIZE;
		if (cur_page_id != page_id) {
			page_index++;
			transMap->bufEntry[entryIdx].perPageSLBAs[page_index] =
					transMap->bufEntry[entryIdx].slba + (cur_page_id * SECTOR_NUM_PER_PAGE);
			transMap->bufEntry[entryIdx].perPageStartingIndex[page_index] = embedding_index;
			transMap->bufEntry[entryIdx].perPageInputLength[page_index - 1] = cur_page_input_length;
			cur_page_input_length = 0;
		}
		page_id = cur_page_id;
		cur_page_input_length++;
	}
	transMap->bufEntry[entryIdx].perPageInputLength[page_index] = cur_page_input_length;
	transMap->bufEntry[entryIdx].nPages = page_index + 1;

	/* TODO -- 'Zero' out results pages. Assume floats for now. */
	float *resultsBase = (float*)(TRANS_BUF_ADDR + entryIdx * TRANS_BUF_ENTRY_SIZE);
	for(i = 0; i < config->resultEmbeddings * config->attributeSize * config->embeddingLength; i++)
	{
		*resultsBase = 0;
		resultsBase++;
	}

	transMap->bufEntry[entryIdx].configured = 1;

	XTime_GetTime(&transMap->bufEntry[entryIdx].configProcessed);
}

unsigned int readTranslatedPagesNonBlocking(unsigned int entryIdx, unsigned int firstSector, unsigned int nextSector, unsigned int requestedSectors, unsigned int cmdSlotTag)
{
	unsigned int sectorNum, curSector;
	int nlbRequested = 0;

	if (!transMap->bufEntry[entryIdx].configured) return nlbRequested;

	for (sectorNum = 0;
		 sectorNum < requestedSectors;
		 sectorNum++)
	{
		curSector = nextSector + sectorNum;

		if (transMap->bufEntry[entryIdx].perResultSectorCompletedEmbeddings[curSector] <
				transMap->bufEntry[entryIdx].perResultSectorInputEmbeddings[curSector])
		{
			return nlbRequested;
		}
		else
		{
			transMap->bufEntry[entryIdx].perResultSectorCompletedEmbeddings[curSector] = 0;
			nlbRequested++;
		}

		set_auto_tx_dma(cmdSlotTag, (curSector - firstSector), TRANS_BUF_ADDR + entryIdx * TRANS_BUF_ENTRY_SIZE + curSector * SECTOR_SIZE_FTL);

		XTime_GetTime(&transMap->bufEntry[entryIdx].sectorRequestCompleted[curSector]);

		/* TODO - properly save the txdma tail such that this buffer space isn't allocated again until
		 * all the data has been sent.
		 */

		if (++transMap->bufEntry[entryIdx].nlbCompleted == transMap->bufEntry[entryIdx].nlb)
			DeallocateTransBufEntry(entryIdx);
	}

	return nlbRequested;
}

int translatePagesNonBlocking(unsigned int entryIdx, unsigned int nextPageIdx)
{
  unsigned page, lpa;
  for (page = nextPageIdx;
      page < transMap->bufEntry[entryIdx].nPages;
      page++)
  {
    lpa = transMap->bufEntry[entryIdx].perPageSLBAs[page] / SECTOR_NUM_PER_PAGE;
    //xil_printf("Reading page %d -- lpa %d\r\n", page, lpa);

    /* Read the page from Flash. */
    if (!readPageToTranslateNonBlocking(entryIdx, lpa, page))
    {
    	return page;
    }
  }

  // We're all done
  return -1;
}

void translatePage(unsigned int entryIdx, void* devAddr, unsigned int pageIdx)
{
  XTime_GetTime(&transMap->bufEntry[entryIdx].translationStarted[pageIdx]);

  struct transConfig* config = (struct transConfig*)(TRANS_CONFIG_ADDR + entryIdx * TRANS_CONFIG_SIZE);
  volatile struct attribute
  {
    char bytes[config->attributeSize];
  } *fromPageBase,
    *fromAtr,
    *toBase,
    *toAtr;

  /* Set local helpers from config. */
  toBase = (struct attribute*)(TRANS_BUF_ADDR + entryIdx * TRANS_BUF_ENTRY_SIZE);
  toAtr = toBase;
  fromPageBase = (struct attribute*)devAddr;
  fromAtr = fromPageBase;
  unsigned pair_index = transMap->bufEntry[entryIdx].perPageStartingIndex[pageIdx];
  unsigned n_embeddings = transMap->bufEntry[entryIdx].perPageInputLength[pageIdx];
  unsigned base_embedding_id = ((transMap->bufEntry[entryIdx].perPageSLBAs[pageIdx] - transMap->bufEntry[entryIdx].slba) * SECTOR_SIZE_FTL) /
  		  (config->attributeSize * config->embeddingLength);
  unsigned embedding_offset, embedding_id, result_index, result_sector;

  int i = 0;
  for (i = 0; i < n_embeddings; i++)
  {
	  embedding_id = config->embeddingIDList[pair_index].embeddingID;
	  result_index = config->embeddingIDList[pair_index].result;
	  embedding_offset = embedding_id - base_embedding_id;
	  fromAtr = fromPageBase + (embedding_offset * config->embeddingLength);

	  // Save to Cache - Direct map, overwrites previous entry
	  unsigned int fullindex = (embedding_id << 5) | config->tableID;
	  unsigned int cache_index = fullindex & ((0x1 << 20)-1);
	  unsigned int tag = (fullindex >> 20) & ((0x1 << 12)-1);
	  toAtr = (struct attribute*)transCache->cacheEntry[cache_index].embedding_bytes;
	  int k = 0;
	  for (k = 0; k < config->embeddingLength; k++)
	  {
	  	*toAtr = *fromAtr;
	  	toAtr++;
	  	fromAtr++;
	  }
	  transCache->cacheEntry[cache_index].valid = 1;
	  transCache->cacheEntry[cache_index].tag = tag;
	  // End Cache Save

	  fromAtr = fromPageBase + (embedding_offset * config->embeddingLength);
	  result_sector = (result_index * (config->embeddingLength * config->attributeSize)) / SECTOR_SIZE_FTL;
	  toAtr = toBase + (result_index * config->embeddingLength);

	  /* Perform reduction. SUM */
	  for (k = 0; k < config->embeddingLength; k++)
	  {
		  // Assume for now attribute size is 4 and embedding entries are floats
		  *((float*)toAtr) += *((float*)fromAtr);
		  toAtr++;
		  fromAtr++;
	  }

	  transMap->bufEntry[entryIdx].perResultSectorCompletedEmbeddings[result_sector]++;
	  pair_index++;
  }

  transMap->bufEntry[entryIdx].pagesTranslated++;

  XTime_GetTime(&transMap->bufEntry[entryIdx].translationCompleted[pageIdx]);
}

unsigned int readPageToTranslateNonBlocking(unsigned int entryIdx, unsigned int lpa, unsigned int page_idx)
{
  XTime_GetTime(&transMap->bufEntry[entryIdx].flashReadStarted[page_idx]);

  unsigned int hitEntry = CheckBufHit(lpa);
  if (hitEntry != 0x7fff)
  {
    translatePage(entryIdx, (void*)(BUFFER_ADDR + hitEntry * BUF_ENTRY_SIZE),
              page_idx);

    /* If we have two translation request which use the same page, one could
     * submit a read request and the other could read from the allocated LRU
     * buffer entry while the read request is still pending.
     *
     * We should really enqueue this in
     * the normal scheduling framework so this doesn't happen.
     *
     * TODO This is important and will need to be done :).
     */
  }
  else
  {
    unsigned int dieNo = lpa % DIE_NUM;
    unsigned int dieLpn = lpa / DIE_NUM;

    /* If we don't have room to push this, don't mess up the LRU buffer. */
    if (!CheckReqQueueAvailability(dieNo % CHANNEL_NUM, dieNo / CHANNEL_NUM, 2)) return 0;

    unsigned int bufferEntry = AllocateBufEntry(lpa);
    ASSERT(bufferEntry < BUF_ENTRY_NUM);

    bufMap->bufEntry[bufferEntry].dirty = 0;

	if(bufLruList->bufLruEntry[dieNo].head != 0x7fff)
	{
		bufMap->bufEntry[bufferEntry].prevEntry = 0x7fff;
		bufMap->bufEntry[bufferEntry].nextEntry = bufLruList->bufLruEntry[dieNo].head;
		bufMap->bufEntry[bufLruList->bufLruEntry[dieNo].head].prevEntry = bufferEntry;
		bufLruList->bufLruEntry[dieNo].head = bufferEntry;
	}
	else
	{
		bufMap->bufEntry[bufferEntry].prevEntry = 0x7fff;
		bufMap->bufEntry[bufferEntry].nextEntry = 0x7fff;
		bufLruList->bufLruEntry[dieNo].head = bufferEntry;
		bufLruList->bufLruEntry[dieNo].tail = bufferEntry;
	}
	bufMap->bufEntry[bufferEntry].lpn = lpa;

    if (pageMap->pmEntry[dieNo][dieLpn].ppn != 0xffffffff)
    {
    	LOW_LEVEL_REQ_INFO lowLevelCmd;
    	unsigned int basePage = lpa;
    	unsigned int dieNo = basePage % DIE_NUM;
    	lowLevelCmd.chNo = dieNo % CHANNEL_NUM;
    	lowLevelCmd.wayNo = dieNo / CHANNEL_NUM;
        lowLevelCmd.rowAddr = pageMap->pmEntry[dieNo][dieLpn].ppn;
		lowLevelCmd.spareDataBuf = SPARE_ADDR;
		lowLevelCmd.bufferEntry = bufferEntry;
		lowLevelCmd.translate = 1;
		lowLevelCmd.transBufferEntry = entryIdx;
		lowLevelCmd.transPageIdx = page_idx;
		lowLevelCmd.request = V2FCommand_ReadPageTrigger;

		ASSERT(PushToReqQueueNonBlocking(&lowLevelCmd, 0));
		return 1;
    }
    else
    {
      translatePage(entryIdx, (void*)(BUFFER_ADDR + bufferEntry * BUF_ENTRY_SIZE),
    		  page_idx);
    }
  }

  return 1;
}

int findTransBufEntry(unsigned int requestId)
{
	int foundIdx = -1;
	int entryIdx;
	for (entryIdx = 0; entryIdx < TRANS_BUF_ENTRY_NUM; entryIdx++)
	{
		if (transMap->bufEntry[entryIdx].allocated &&
			transMap->bufEntry[entryIdx].requestId == requestId)
		{
			foundIdx = entryIdx;
			break;
		}
	}
	ASSERT(foundIdx != -1);
	return foundIdx;
}
