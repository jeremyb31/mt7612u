/****************************************************************************
 * Ralink Tech Inc.
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002, Ralink Technology, Inc.
 *
 * All rights reserved. Ralink's source code is an unpublished work and the
 * use of a copyright notice does not imply otherwise. This source code
 * contains confidential trade secret material of Ralink Tech. Any attemp
 * or participation in deciphering, decoding, reverse engineering or in any
 * way altering the source code is stricitly prohibited, unless the prior
 * written consent of Ralink Technology, Inc. is obtained.
 ***************************************************************************/


#define MODULE_WMM_UAPSD
#include "rt_config.h"

#ifdef UAPSD_SUPPORT
#include "uapsd.h"

/*#define UAPSD_DEBUG */

/* used to enable or disable UAPSD power save queue maintain mechanism */
UCHAR gUAPSD_FlgNotQueueMaintain;

#ifdef UAPSD_DEBUG
uint32_t gUAPSD_SP_CloseAbnormalNum;
#endif /* UAPSD_DEBUG */

#ifdef UAPSD_TIMING_RECORD_FUNC
/* all unit: us */

UCHAR  gUAPSD_TimingFlag;
uint32_t gUAPSD_TimingIndexUapsd;
uint32_t gUAPSD_TimingLoopIndex;

/* ISR start timestamp */
uint64_t gUAPSD_TimingIsr[UAPSD_TIMING_RECORD_MAX];

/* Tasklet start timestamp */
uint64_t gUAPSD_TimingTasklet[UAPSD_TIMING_RECORD_MAX];

uint64_t gUAPSD_TimingTrgRcv[UAPSD_TIMING_RECORD_MAX];
uint64_t gUAPSD_TimingMov2Tx[UAPSD_TIMING_RECORD_MAX];
uint64_t gUAPSD_TimingTx2Air[UAPSD_TIMING_RECORD_MAX];

uint32_t gUAPSD_TimingSumIsr2Tasklet;
uint32_t gUAPSD_TimingSumTrig2Txqueue;
uint32_t gUAPSD_TimingSumTxqueue2Air;
#endif /* UAPSD_TIMING_RECORD_FUNC */

/*
========================================================================
Routine Description:
    UAPSD Module Init.

Arguments:
	pAd		Pointer to our adapter

Return Value:
    None

Note:
========================================================================
*/
VOID UAPSD_Init(struct rtmp_adapter *pAd)
{
    /* allocate a lock resource for SMP environment */
	spin_lock_init(&pAd->UAPSDEOSPLock);

#ifdef UAPSD_DEBUG
	DBGPRINT(RT_DEBUG_TRACE, ("uapsd> allocate a spinlock!\n"));
#endif /* UAPSD_DEBUG */

#ifdef UAPSD_DEBUG
	gUAPSD_SP_CloseAbnormalNum = 0;
#endif /* UAPSD_DEBUG */

#ifdef UAPSD_TIMING_RECORD_FUNC
	gUAPSD_TimingFlag = 0; /* default: DISABLE */
	gUAPSD_TimingIndexUapsd = 0;
	gUAPSD_TimingLoopIndex = 0;


	gUAPSD_TimingSumIsr2Tasklet = 0;
	gUAPSD_TimingSumTrig2Txqueue = 0;
	gUAPSD_TimingSumTxqueue2Air = 0;
#endif /* UAPSD_TIMING_RECORD_FUNC */
}


/*
========================================================================
Routine Description:
    UAPSD Module Release.

Arguments:
	pAd		Pointer to our adapter

Return Value:
    None

Note:
========================================================================
*/
VOID UAPSD_Release(struct rtmp_adapter *pAd)
{
    /* free the lock resource for SMP environment */
#ifdef UAPSD_DEBUG
	DBGPRINT(RT_DEBUG_TRACE, ("uapsd> release a spinlock!\n"));
#endif /* UAPSD_DEBUG */
} /* End of UAPSD_Release */


/*
========================================================================
Routine Description:
    Check if ASIC can enter sleep mode. Not software sleep.

Arguments:
	pAd		Pointer to our adapter

Return Value:
    None

Note:
========================================================================
*/
VOID RtmpAsicSleepHandle(struct rtmp_adapter *pAd)
{
#ifdef CONFIG_STA_SUPPORT
	bool FlgCanAsicSleep = true;


	/* finally, check if we can sleep */
	if (FlgCanAsicSleep == true)
	{
		/* just mark the flag to FALSE and wait PeerBeacon() to sleep */
		ASIC_PS_CAN_SLEEP(pAd);
	}
#endif // CONFIG_STA_SUPPORT //
}



/*
========================================================================
Routine Description:
    Close current Service Period.

Arguments:
	pAd				Pointer to our adapter
	pEntry			Close the SP of the entry

Return Value:
    None

Note:
========================================================================
*/
VOID UAPSD_SP_Close(struct rtmp_adapter *pAd, MAC_TABLE_ENTRY *pEntry)
{
	if ((pEntry != NULL) && (pEntry->PsMode == PWR_SAVE))
	{
		RTMP_SEM_LOCK(&pAd->UAPSDEOSPLock);

		if (pEntry->bAPSDFlagSPStart != 0)
		{
			/* SP is started for the station */
#ifdef UAPSD_DEBUG
			DBGPRINT(RT_DEBUG_TRACE, ("uapsd> [3] close SP!\n"));
#endif /* UAPSD_DEBUG */

			if (pEntry->pUAPSDEOSPFrame != NULL)
			{
				/*
				SP will be closed, should not have EOSP frame
				if exists, release it
				*/
#ifdef UAPSD_DEBUG
				DBGPRINT(RT_DEBUG_TRACE, ("uapsd> [3] Free EOSP (UP = %d)!\n",
							RTMP_GET_PACKET_UP(
								QUEUE_ENTRY_TO_PACKET(pEntry->pUAPSDEOSPFrame))));
#endif /* UAPSD_DEBUG */

				RELEASE_NDIS_PACKET(pAd,
								QUEUE_ENTRY_TO_PACKET(pEntry->pUAPSDEOSPFrame),
								NDIS_STATUS_FAILURE);
				pEntry->pUAPSDEOSPFrame = NULL;
			}

			/* re-init SP related parameters */
			pEntry->UAPSDTxNum = 0;
			//pEntry->bAPSDFlagSPStart = 0;
			pEntry->bAPSDFlagEOSPOK = 0;
			pEntry->bAPSDFlagLegacySent = 0;
			UAPSD_SP_END(pAd, pEntry);

#ifdef RTMP_MAC_USB
			pEntry->UAPSDTagOffset[QID_AC_BE] = 0;
			pEntry->UAPSDTagOffset[QID_AC_BK] = 0;
			pEntry->UAPSDTagOffset[QID_AC_VI] = 0;
			pEntry->UAPSDTagOffset[QID_AC_VO] = 0;
#endif /* RTMP_MAC_USB */
		}

		RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);
	}
}


/*
========================================================================
Routine Description:
	Check if the SP for entry is closed.

Arguments:
	pAd				Pointer to our adapter
	pEntry			the peer entry

Return Value:
	None

Note:
========================================================================
*/
bool UAPSD_SP_IsClosed(struct rtmp_adapter *pAd, MAC_TABLE_ENTRY *pEntry)
{
	bool FlgIsSpClosed = true;

	RTMP_SEM_LOCK(&pAd->UAPSDEOSPLock);

	if ((pEntry) &&
		(pEntry->PsMode == PWR_SAVE) &&
		(pEntry->bAPSDFlagSPStart != 0)
	)
		FlgIsSpClosed = FALSE;

	RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);

	return FlgIsSpClosed;
}

/*
========================================================================
Routine Description:
    Deliver all queued packets.

Arguments:
	pAd            Pointer to our adapter
	*pEntry        STATION

Return Value:
    None

Note:
	SMP protection by caller for packet enqueue.
========================================================================
*/
VOID UAPSD_AllPacketDeliver(struct rtmp_adapter *pAd, MAC_TABLE_ENTRY *pEntry)
{
	QUEUE_HEADER *pQueApsd;
	PQUEUE_ENTRY pQueEntry;
	UCHAR QueIdList[WMM_NUM_OF_AC] = { QID_AC_BE, QID_AC_BK,
	                 QID_AC_VI, QID_AC_VO };
	int32_t IdAc, QueId; /* must be signed, can not be unsigned */


	RTMP_SEM_LOCK(&pAd->UAPSDEOSPLock);

	/* check if the EOSP frame is yet transmitted out */
	if (pEntry->pUAPSDEOSPFrame != NULL)
	{
		/* queue the EOSP frame to SW queue to be transmitted */
		QueId = RTMP_GET_PACKET_UAPSD_QUE_ID(
			QUEUE_ENTRY_TO_PACKET(pEntry->pUAPSDEOSPFrame));

		if (QueId > QID_AC_VO)
		{
			/* should not be here, only for sanity */
			QueId = QID_AC_BE;
		}

		UAPSD_INSERT_QUEUE_AC(pAd, pEntry, &pAd->TxSwQueue[QueId],
		pEntry->pUAPSDEOSPFrame);

		pEntry->pUAPSDEOSPFrame = NULL;
		pEntry->UAPSDTxNum = 0;
	}

	/* deliver ALL U-APSD packets from AC3 to AC0 (AC0 to AC3 is also ok) */
	for(IdAc=(WMM_NUM_OF_AC-1); IdAc>=0; IdAc--)
	{
		pQueApsd = &(pEntry->UAPSDQueue[IdAc]);
		QueId = QueIdList[IdAc];

		while(pQueApsd->Head)
		{
			pQueEntry = RemoveHeadQueue(pQueApsd);
			UAPSD_INSERT_QUEUE_AC(pAd, pEntry, &pAd->TxSwQueue[QueId], pQueEntry);
		}
	}

	RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);
}


/*
========================================================================
Routine Description:
    Parse the UAPSD field in WMM element in (re)association request frame.

Arguments:
	pAd				Pointer to our adapter
	*pEntry			STATION
	*pElm			QoS information field
	FlgApsdCapable	true: Support UAPSD

Return Value:
    None

Note:
	No protection is needed.

	1. Association -> TSPEC:
		use static UAPSD settings in Association
		update UAPSD settings in TSPEC

	2. Association -> TSPEC(11r) -> Reassociation:
		update UAPSD settings in TSPEC
		backup static UAPSD settings in Reassociation

	3. Association -> Reassociation:
		update UAPSD settings in TSPEC
		backup static UAPSD settings in Reassociation
========================================================================
*/
VOID UAPSD_AssocParse(
	IN struct rtmp_adapter *pAd,
	IN MAC_TABLE_ENTRY *pEntry,
	IN UCHAR *pElm,
	IN bool FlgApsdCapable)
{
	PQBSS_STA_INFO_PARM  pQosInfo;
	UCHAR UAPSD[4];
	uint32_t IdApsd;


	/* check if the station enables UAPSD function */
	if ((pEntry) && (FlgApsdCapable == true))
	{
		/* backup its UAPSD parameters */
		pQosInfo = (PQBSS_STA_INFO_PARM) pElm;


		UAPSD[QID_AC_BE] = pQosInfo->UAPSD_AC_BE;
		UAPSD[QID_AC_BK] = pQosInfo->UAPSD_AC_BK;
		UAPSD[QID_AC_VI] = pQosInfo->UAPSD_AC_VI;
		UAPSD[QID_AC_VO] = pQosInfo->UAPSD_AC_VO;

		pEntry->MaxSPLength = pQosInfo->MaxSPLength;

		DBGPRINT(RT_DEBUG_TRACE, ("apsd> UAPSD %d %d %d %d!\n",
					pQosInfo->UAPSD_AC_BE, pQosInfo->UAPSD_AC_BK,
					pQosInfo->UAPSD_AC_VI, pQosInfo->UAPSD_AC_VO));
		DBGPRINT(RT_DEBUG_TRACE, ("apsd> MaxSPLength = %d\n",
					pEntry->MaxSPLength));

		/* use static UAPSD setting of association request frame */
		for(IdApsd=0; IdApsd<4; IdApsd++)
		{
			pEntry->bAPSDCapablePerAC[IdApsd] = UAPSD[IdApsd];
			pEntry->bAPSDDeliverEnabledPerAC[IdApsd] = UAPSD[IdApsd];

		}

		if ((pEntry->bAPSDCapablePerAC[QID_AC_BE] == 0) &&
			(pEntry->bAPSDCapablePerAC[QID_AC_BK] == 0) &&
			(pEntry->bAPSDCapablePerAC[QID_AC_VI] == 0) &&
			(pEntry->bAPSDCapablePerAC[QID_AC_VO] == 0))
		{
			CLIENT_STATUS_CLEAR_FLAG(pEntry, fCLIENT_STATUS_APSD_CAPABLE);
		}
		else
		{
			CLIENT_STATUS_SET_FLAG(pEntry, fCLIENT_STATUS_APSD_CAPABLE);
		}

		if ((pEntry->bAPSDCapablePerAC[QID_AC_BE] == 1) &&
			(pEntry->bAPSDCapablePerAC[QID_AC_BK] == 1) &&
			(pEntry->bAPSDCapablePerAC[QID_AC_VI] == 1) &&
			(pEntry->bAPSDCapablePerAC[QID_AC_VO] == 1))
		{
			/* all AC are U-APSD */
			DBGPRINT(RT_DEBUG_TRACE, ("apsd> all AC are UAPSD\n"));
			pEntry->bAPSDAllAC = 1;
		}
		else
		{
			/* at least one AC is not U-APSD */
			DBGPRINT(RT_DEBUG_TRACE, ("apsd> at least one AC is not UAPSD %d %d %d %d\n",
			pEntry->bAPSDCapablePerAC[QID_AC_BE],
			pEntry->bAPSDCapablePerAC[QID_AC_BK],
			pEntry->bAPSDCapablePerAC[QID_AC_VI],
			pEntry->bAPSDCapablePerAC[QID_AC_VO]));
			pEntry->bAPSDAllAC = 0;
		}

		pEntry->VirtualTimeout = 0;

		DBGPRINT(RT_DEBUG_TRACE, ("apsd> MaxSPLength = %d\n", pEntry->MaxSPLength));
	}
}


/*
========================================================================
Routine Description:
    Enqueue a UAPSD packet.

Arguments:
	pAd				Pointer to our adapter
	*pEntry			STATION
	pPacket			UAPSD dnlink packet
	IdAc			UAPSD AC ID (0 ~ 3)

Return Value:
    None

Note:
========================================================================
*/
VOID UAPSD_PacketEnqueue(
	IN	struct rtmp_adapter *	pAd,
	IN	MAC_TABLE_ENTRY		*pEntry,
	IN	struct sk_buff *	pPacket,
	IN	uint32_t 			IdAc)
{
	/*
		1. the STATION is UAPSD STATION;
		2. AC ID is legal;
		3. the AC is UAPSD AC.
		so we queue the packet to its UAPSD queue
	*/

	/* [0] ~ [3], QueIdx base is QID_AC_BE */
	QUEUE_HEADER *pQueUapsd;


	/* check if current queued UAPSD packet number is too much */
	if (pEntry == NULL)
	{
		RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_FAILURE);
		DBGPRINT(RT_DEBUG_TRACE, ("uapsd> pEntry == NULL!\n"));
		return;
	} /* End of if */

	pQueUapsd = &(pEntry->UAPSDQueue[IdAc]);

	if (pQueUapsd->Number >= MAX_PACKETS_IN_UAPSD_QUEUE)
    	{
	        /* too much queued pkts, free (discard) the tx packet */
	        DBGPRINT(RT_DEBUG_TRACE,
                 ("uapsd> many(%d) WCID(%d) AC(%d)\n",
				pQueUapsd->Number,
				RTMP_GET_PACKET_WCID(pPacket),
				IdAc));

		RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_FAILURE);
	}
	else
	{
        	/* queue the tx packet to the U-APSD queue of the AC */
		RTMP_SEM_LOCK(&pAd->UAPSDEOSPLock);
		InsertTailQueue(pQueUapsd, PACKET_TO_QUEUE_ENTRY(pPacket));
		RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);

#ifdef UAPSD_DEBUG
		if (RTMP_GET_PACKET_MGMT_PKT(pPacket) == 1)
		{
			DBGPRINT(RT_DEBUG_TRACE, ("ps> mgmt to uapsd queue...\n"));
		}
		else
		{
			DBGPRINT(RT_DEBUG_TRACE,
					("ps> data (0x%08lx) (AC%d) to uapsd queue (num of pkt = %ld)...\n",
					(ULONG)pPacket, IdAc,
					pQueUapsd->Number));
		}
#endif /* UAPSD_DEBUG */
	}
}




/*
========================================================================
Routine Description:
    Maintenance our UAPSD PS queue.  Release all queued packet if timeout.

Arguments:
	pAd				Pointer to our adapter
	*pEntry			STATION

Return Value:
    None

Note:
	If in RT2870, pEntry can not be removed during UAPSD_QueueMaintenance()
========================================================================
*/
VOID UAPSD_QueueMaintenance(struct rtmp_adapter *pAd, MAC_TABLE_ENTRY *pEntry)
{
	QUEUE_HEADER *pQue;
	uint32_t IdAc;
	bool FlgUapsdPkt, FlgEospPkt;
#ifdef RTMP_MAC_USB
	ULONG IrqFlags;
#endif /* RTMP_MAC_USB */


	if (gUAPSD_FlgNotQueueMaintain)
		return;

	if (pEntry->PsMode != PWR_SAVE)
		return; /* UAPSD packet only for power-save STA, not active STA */

	/* init */
	RTMP_SEM_LOCK(&pAd->UAPSDEOSPLock);

	pQue = pEntry->UAPSDQueue;
	FlgUapsdPkt = 0;
	FlgEospPkt = 0;

	/* check if more than one U-APSD packets exists */
	for(IdAc=0; IdAc<WMM_NUM_OF_AC; IdAc++)
	{
		if (pQue[IdAc].Head != NULL) {
			/*
				At least one U-APSD packets exists so we need to check if
				queued U-APSD packets are timeout.
			*/
			FlgUapsdPkt = 1;
			break;
		}
	}

	if (pEntry->pUAPSDEOSPFrame != NULL)
		FlgEospPkt = 1;

    /* check if any queued UAPSD packet exists */
	if (FlgUapsdPkt || FlgEospPkt)
	{
#ifdef RTMP_MAC_USB
		RTMP_IRQ_LOCK(&pAd->irq_lock, IrqFlags);
#endif /* RTMP_MAC_USB */

		pEntry->UAPSDQIdleCount ++;
		if (pEntry->UAPSDQIdleCount > pAd->MacTab.MsduLifeTime)
		{
			if (FlgUapsdPkt)
			{
				DBGPRINT(RT_DEBUG_TRACE,
						("uapsd> UAPSD queue timeout! clean all queued frames...\n"));
			}

			if (FlgEospPkt)
			{
				DBGPRINT(RT_DEBUG_TRACE,
						("uapsd> UAPSD EOSP timeout! clean the EOSP frame!\n"));
			}

			/* UAPSDQIdleCount will be 0 after trigger frame is received */

			/* clear all U-APSD packets */
			if (FlgUapsdPkt)
			{
				for(IdAc=0; IdAc<WMM_NUM_OF_AC; IdAc++)
					RtmpCleanupPsQueue(pAd, &pQue[IdAc]);
			}

			/* free the EOSP frame */
			pEntry->UAPSDTxNum = 0;

			if (pEntry->pUAPSDEOSPFrame != NULL)
			{
				RELEASE_NDIS_PACKET(pAd,
								QUEUE_ENTRY_TO_PACKET(pEntry->pUAPSDEOSPFrame),
			                        NDIS_STATUS_FAILURE);
				pEntry->pUAPSDEOSPFrame = NULL;
			}

			pEntry->bAPSDFlagEOSPOK = 0;
			//pEntry->bAPSDFlagSPStart = 0;
			pEntry->bAPSDFlagLegacySent = 0;
			UAPSD_SP_END(pAd, pEntry);

			/* clear idle counter */
			pEntry->UAPSDQIdleCount = 0;

#ifdef CONFIG_AP_SUPPORT
			/* check TIM bit */
			if (pEntry->PsQueue.Number == 0)
			{
				WLAN_MR_TIM_BIT_CLEAR(pAd, pEntry->apidx, pEntry->Aid);
			}
#endif /* CONFIG_AP_SUPPORT */
		}

#ifdef RTMP_MAC_USB
		RTMP_IRQ_UNLOCK(&pAd->irq_lock, IrqFlags);
#endif /* RTMP_MAC_USB */
    }
    else
	{
		/* clear idle counter */
		pEntry->UAPSDQIdleCount = 0;
	}

	RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);

	/* virtual timeout handle */
	RTMP_PS_VIRTUAL_TIMEOUT_HANDLE(pEntry);
}


/*
========================================================================
Routine Description:
    Close SP in Tx Done, not Tx DMA Done.

Arguments:
	pAd            Pointer to our adapter
	pEntry			destination entry
	FlgSuccess		0:tx success, 1:tx fail

Return Value:
    None

Note:
	For RT28xx series, for packetID=0 or multicast frame, no statistics
	count can be got, ex: ARP response or DHCP packets, we will use
	low rate to set (CCK, MCS=0=packetID).
	So SP will not be close until UAPSD_EPT_SP_INT timeout.

	So if the tx rate is 1Mbps for a entry, we will use DMA done, not
	use UAPSD_SP_AUE_Handle().
========================================================================
*/
VOID UAPSD_SP_AUE_Handle(
	IN struct rtmp_adapter 	*pAd,
	IN MAC_TABLE_ENTRY	*pEntry,
	IN UCHAR			FlgSuccess)
{
#ifdef UAPSD_SP_ACCURATE
	USHORT QueId;


	if (pEntry == NULL)
		return;

	if (pEntry->PsMode == PWR_ACTIVE)
	{
#ifdef UAPSD_DEBUG
		DBGPRINT(RT_DEBUG_TRACE, ("uapsd> aux: Station actives! Close SP!\n"));
#endif /* UAPSD_DEBUG */
		//pEntry->bAPSDFlagSPStart = 0;
		pEntry->bAPSDFlagEOSPOK = 0;
		UAPSD_SP_END(pAd, pEntry);
		return;
	}

	if (pEntry->PsMode == PWR_SAVE)
	{
		bool FlgEosp;

		RTMP_SEM_LOCK(&pAd->UAPSDEOSPLock);

		if (pEntry->bAPSDFlagSpRoughUse != 0)
		{
			RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);
			return; /* use DMA mechanism, not statistics count mechanism */
		}

#ifdef UAPSD_DEBUG
		DBGPRINT(RT_DEBUG_TRACE, ("uapsd> aux: Tx Num = %d\n", pEntry->UAPSDTxNum));
#endif /* UAPSD_DEBUG */

		FlgEosp = FALSE;

		if (pEntry->bAPSDFlagSPStart == 0)
		{
			/*
				When SP is not started, all packets are from legacy PS queue.
				One downlink packet for one PS-Poll packet.
			*/
			pEntry->bAPSDFlagLegacySent = 0;

#ifdef UAPSD_DEBUG
			DBGPRINT(RT_DEBUG_TRACE, ("uapsd> legacy PS packet is sent!\n"));
#endif /* UAPSD_DEBUG */
		}
		else
		{
#ifdef UAPSD_TIMING_RECORD_FUNC
			UAPSD_TIMING_RECORD(pAd, UAPSD_TIMING_RECORD_TX2AIR);
#endif /* UAPSD_TIMING_RECORD_FUNC */
		}

		/* record current time */
		UAPSD_TIME_GET(pAd, pEntry->UAPSDTimeStampLast);

		/* Note: UAPSDTxNum does NOT include the EOSP packet */
		if (pEntry->UAPSDTxNum > 0)
		{
			/* some UAPSD packets are not yet transmitted */

			if (pEntry->UAPSDTxNum == 1)
			{
				/* this is the last UAPSD packet */
				if (pEntry->pUAPSDEOSPFrame != NULL)
				{
					/* transmit the EOSP frame */
					struct sk_buff *pPkt;


#ifdef UAPSD_DEBUG
					DBGPRINT(RT_DEBUG_TRACE, ("uapsd> aux: send EOSP frame...\n"));
#endif /* UAPSD_DEBUG */

					pPkt = QUEUE_ENTRY_TO_PACKET(pEntry->pUAPSDEOSPFrame);
					QueId = RTMP_GET_PACKET_UAPSD_QUE_ID(pPkt);

					if (QueId > QID_AC_VO)
					{
						/* should not be here, only for sanity */
						QueId = QID_AC_BE;
					}

					UAPSD_INSERT_QUEUE_AC(pAd, pEntry, &pAd->TxSwQueue[QueId],
					pEntry->pUAPSDEOSPFrame);

					pEntry->pUAPSDEOSPFrame = NULL;
					FlgEosp = true;
				}
			}

			/* a UAPSD frame is transmitted so decrease the counter */
			pEntry->UAPSDTxNum --;

			RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);

			/* maybe transmit the EOSP frame */
			if (FlgEosp == true)
			{
				struct os_cookie *pCookie = pAd->OS_Cookie;

				/*
					Too many functions call NICUpdateFifoStaCounters() and
					NICUpdateFifoStaCounters() will call UAPSD_SP_AUE_Handle(),
					if we call RTMPDeQueuePacket() here, double-IRQ LOCK will
					occur. so we need to activate a tasklet to send EOSP frame.

					ex: RTMPDeQueuePacket() --> RTMPFreeTXDUponTxDmaDone() -->
					NICUpdateFifoStaCounters() --> UAPSD_SP_AUE_Handle() -->
					RTMPDeQueuePacket() ERROR! or

					RTMPHandleTxRingDmaDoneInterrupt() -->
					RTMP_IRQ_LOCK() -->
					RTMPFreeTXDUponTxDmaDone() -->
					NICUpdateFifoStaCounters() -->
					UAPSD_SP_AUE_Handle() -->
					RTMPDeQueuePacket() -->
					DEQUEUE_LOCK() -->
					RTMP_IRQ_LOCK() ERROR!
				*/
				RTMP_OS_TASKLET_SCHE(&pCookie->uapsd_eosp_sent_task);
			}
			/* must return here; Or double unlock UAPSDEOSPLock */
			return;
		}
		else
		{
			/* UAPSDTxNum == 0 so the packet is the EOSP packet */

			if (pAd->bAPSDFlagSPSuspend == 1)
			{
#ifdef UAPSD_DEBUG
				DBGPRINT(RT_DEBUG_TRACE, ("uapsd> aux: SP is suspend, keep SP if exists!\n"));
#endif /* UAPSD_DEBUG */

				/* keep SP, not to close SP */
				pEntry->bAPSDFlagEOSPOK = 1;
			}

			if ((pEntry->bAPSDFlagSPStart != 0) &&
				(pAd->bAPSDFlagSPSuspend == 0))
			{
				//pEntry->bAPSDFlagSPStart = 0;
				pEntry->bAPSDFlagEOSPOK = 0;
				UAPSD_SP_END(pAd, pEntry);

#ifdef UAPSD_DEBUG
				DBGPRINT(RT_DEBUG_TRACE, ("uapsd> aux: close a SP.\n\n\n"));
#endif /* UAPSD_DEBUG */
			}
		}

		RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);
	}
#endif /* UAPSD_SP_ACCURATE */
}


/*
========================================================================
Routine Description:
    Close current Service Period.

Arguments:
	pAd				Pointer to our adapter

Return Value:
    None

Note:
    When we receive EOSP frame tx done interrupt and a uplink packet
    from the station simultaneously, we will regard it as a new trigger
    frame because the packet is received when EOSP frame tx done interrupt.

    We can not sure the uplink packet is sent after old SP or in the old SP.
    So we must close the old SP in receive done ISR to avoid the problem.
========================================================================
*/
VOID UAPSD_SP_CloseInRVDone(struct rtmp_adapter *pAd)
{
	uint32_t IdEntry;
	int FirstWcid = 0;


	if (pAd->MacTab.fAnyStationInPsm == FALSE)
		return; /* no any station is in power save mode */


	/* check for all CLIENT's UAPSD Service Period */
	for(IdEntry = FirstWcid; IdEntry < MAX_LEN_OF_MAC_TABLE; IdEntry++)
	{
		MAC_TABLE_ENTRY *pEntry = &pAd->MacTab.Content[IdEntry];

		RTMP_SEM_LOCK(&pAd->UAPSDEOSPLock);

		/* check if SP is started and EOSP is transmitted ok */
		if ((pEntry->bAPSDFlagSPStart != 0) &&
			(pEntry->bAPSDFlagEOSPOK != 0))
		{
			/*
				1. SP is started;
				2. EOSP frame is sent ok.
			*/

			/*
				We close current SP for the STATION so we can receive new
				trigger frame from the STATION again.
			*/
#ifdef UAPSD_DEBUG
			DBGPRINT(RT_DEBUG_TRACE,("uapsd> close SP in %s()!\n",
						__FUNCTION__));
#endif /* UAPSD_DEBUG */

			//pEntry->bAPSDFlagSPStart = 0;
			pEntry->bAPSDFlagEOSPOK = 0;
			pEntry->bAPSDFlagLegacySent = 0;
			UAPSD_SP_END(pAd, pEntry);
		}

		RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);
	}
}


#ifdef UAPSD_TIMING_RECORD_FUNC
/*
========================================================================
Routine Description:
	Enable/Disable Timing Record Function.

Arguments:
	pAd				Pointer to our adapter
	Flag			1 (Enable) or 0 (Disable)

Return Value:
	None

Note:
========================================================================
*/
VOID UAPSD_TimingRecordCtrl(uint32_t Flag)
{
	if (gUAPSD_TimingFlag == UAPSD_TIMING_CTRL_SUSPEND)
		return;

	gUAPSD_TimingFlag = Flag;
}


/*
========================================================================
Routine Description:
	Record some timings.

Arguments:
	pAd				Pointer to our adapter
	Type			The timing is for what type

Return Value:
	None

Note:
	UAPSD_TIMING_RECORD_ISR
	UAPSD_TIMING_RECORD_TASKLET
	UAPSD_TIMING_RECORD_TRG_RCV
	UAPSD_TIMING_RECORD_MOVE2TX
	UAPSD_TIMING_RECORD_TX2AIR
========================================================================
*/
VOID UAPSD_TimingRecord(struct rtmp_adapter *pAd, uint32_t Type)
{
	uint32_t Index;

	if (gUAPSD_TimingFlag == UAPSD_TIMING_CTRL_STOP)
		return;

	if ((gUAPSD_TimingFlag == UAPSD_TIMING_CTRL_SUSPEND) &&
		(Type != UAPSD_TIMING_RECORD_TX2AIR))
	{
		return;
	}

	Index = gUAPSD_TimingIndexUapsd;

	switch(Type)
	{
		case UAPSD_TIMING_RECORD_ISR:
			/* start to record the timing */
			UAPSD_TIMESTAMP_GET(pAd, gUAPSD_TimingIsr[Index]);
			break;

		case UAPSD_TIMING_RECORD_TASKLET:
			UAPSD_TIMESTAMP_GET(pAd, gUAPSD_TimingTasklet[Index]);
			break;

		case UAPSD_TIMING_RECORD_TRG_RCV:
			if (gUAPSD_TimingLoopIndex == 0)
			{
				/*
					The trigger frame is the first received frame.
					The received time will be the time recorded in ISR.
				*/
				gUAPSD_TimingTrgRcv[Index] = gUAPSD_TimingIsr[Index];
			}
			else
			{
				/*
					Some packets are handled before the trigger frame so
					we record next one.
				*/
				UAPSD_TIMING_RECORD_STOP();
			}
			break;

		case UAPSD_TIMING_RECORD_MOVE2TX:
			UAPSD_TIMESTAMP_GET(pAd, gUAPSD_TimingMov2Tx[Index]);

			/* prepare to wait for tx done */
			UAPSD_TimingRecordCtrl(UAPSD_TIMING_CTRL_SUSPEND);
			break;

		case UAPSD_TIMING_RECORD_TX2AIR:
			UAPSD_TIMESTAMP_GET(pAd, gUAPSD_TimingTx2Air[Index]);

			/* sum the delay */
			gUAPSD_TimingSumIsr2Tasklet += \
				(uint32_t)(gUAPSD_TimingTasklet[Index] - gUAPSD_TimingIsr[Index]);
			gUAPSD_TimingSumTrig2Txqueue += \
				(uint32_t)(gUAPSD_TimingMov2Tx[Index] - gUAPSD_TimingTrgRcv[Index]);
			gUAPSD_TimingSumTxqueue2Air += \
				(uint32_t)(gUAPSD_TimingTx2Air[Index] - gUAPSD_TimingMov2Tx[Index]);

			/* display average delay */
			if ((Index % UAPSD_TIMING_RECORD_DISPLAY_TIMES) == 0)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("uapsd> Isr2Tasklet=%d, Trig2Queue=%d, Queue2Air=%d micro seconds\n",
						gUAPSD_TimingSumIsr2Tasklet/
											UAPSD_TIMING_RECORD_DISPLAY_TIMES,
						gUAPSD_TimingSumTrig2Txqueue/
											UAPSD_TIMING_RECORD_DISPLAY_TIMES,
						gUAPSD_TimingSumTxqueue2Air/
											UAPSD_TIMING_RECORD_DISPLAY_TIMES));
				gUAPSD_TimingSumIsr2Tasklet = 0;
				gUAPSD_TimingSumTrig2Txqueue = 0;
				gUAPSD_TimingSumTxqueue2Air = 0;
			}

			/* ok, a record is finished; prepare to record the next one */
			gUAPSD_TimingIndexUapsd ++;

			if (gUAPSD_TimingIndexUapsd >= UAPSD_TIMING_RECORD_MAX)
				gUAPSD_TimingIndexUapsd = 0;

			/* stop the record */
			gUAPSD_TimingFlag = UAPSD_TIMING_CTRL_STOP;

			DBGPRINT(RT_DEBUG_TRACE, ("sam> Isr->Tasklet:%d, Trig->TxQueue:%d, TxQueue->TxDone:%d\n",
				(uint32_t)(gUAPSD_TimingTasklet[Index] - gUAPSD_TimingIsr[Index]),
				(uint32_t)(gUAPSD_TimingMov2Tx[Index] - gUAPSD_TimingTrgRcv[Index]),
				(uint32_t)(gUAPSD_TimingTx2Air[Index] - gUAPSD_TimingMov2Tx[Index])));
			break;
	}
}


/*
========================================================================
Routine Description:
	Record the loop index for received packet handle.

Arguments:
	pAd				Pointer to our adapter
	LoopIndex		The RxProcessed in rtmp_rx_done_handle()

Return Value:
	None

Note:
========================================================================
*/
VOID UAPSD_TimeingRecordLoopIndex(uint32_t LoopIndex)
{
	gUAPSD_TimingLoopIndex = LoopIndex;
}
#endif /* UAPSD_TIMING_RECORD_FUNC */


/*
========================================================================
Routine Description:
    Handle PS-Poll Frame.

Arguments:
	pAd				Pointer to our adapter
	*pEntry			the source STATION

Return Value:
    true			Handle OK
	FALSE			Handle FAIL

Note:
========================================================================
*/
bool UAPSD_PsPollHandle(struct rtmp_adapter *pAd, MAC_TABLE_ENTRY *pEntry)
{
	QUEUE_HEADER	*pAcPsQue;
	QUEUE_HEADER	*pAcSwQue;
	PQUEUE_ENTRY	pQuedEntry;
	struct sk_buff *pQuedPkt;
	uint32_t AcQueId;
	/*
		AC ID          = VO > VI > BK > BE
		so we need to change BE & BK
		=> AC priority = VO > VI > BE > BK
	*/
	uint32_t AcPriority[WMM_NUM_OF_AC] = { 1, 0, 2, 3 };
	UCHAR	QueIdList[WMM_NUM_OF_AC] = { QID_AC_BE, QID_AC_BK,
                                            QID_AC_VI, QID_AC_VO };
	bool	FlgQueEmpty;
	int32_t IdAc; /* must be signed, can not use unsigned */
	uint32_t Aid, QueId;


	if (pEntry == NULL)
		return FALSE;

	FlgQueEmpty = true;
	pAcSwQue = NULL;
	pQuedPkt = NULL;

	RTMP_SEM_LOCK(&pAd->UAPSDEOSPLock);

	if (pEntry->bAPSDAllAC == 0)
	{
		RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);
		return FALSE; /* not all AC are delivery-enabled */
	}

	if (pEntry->bAPSDFlagSPStart != 0)
	{
		RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);
		return FALSE; /* its service period is not yet ended */
	}

	/* from highest priority AC3 --> AC2 --> AC0 --> lowest priority AC1 */
	for (IdAc=(WMM_NUM_OF_AC-1); IdAc>=0; IdAc--)
	{
		AcQueId = AcPriority[IdAc];

		/*
			NOTE: get U-APSD queue pointer here to speed up, do NOT use
			pEntry->UAPSDQueue[AcQueId] throughout codes because
			compiler will compile it to many assembly codes.
		*/
		pAcPsQue = &pEntry->UAPSDQueue[AcQueId];

		/* check if any U-APSD packet is queued for the AC */
		if (pAcPsQue->Head == NULL)
			continue;

		/* at least one U-APSD packet exists here */

		/* put U-APSD packets to the AC software queue */
		if ((pAcPsQue->Head != NULL) && (pQuedPkt == NULL))
		{
			/* get AC software queue */
			QueId = QueIdList[AcQueId];
			pAcSwQue = &pAd->TxSwQueue[QueId];

			/* get the U-APSD packet */
			pQuedEntry = RemoveHeadQueue(pAcPsQue);
			pQuedPkt = QUEUE_ENTRY_TO_PACKET(pQuedEntry);

			if (pQuedPkt != NULL)
			{
				/*
					WMM Specification V1.1 3.6.1.7
                       The More Data bit (b13) of the directed MSDU or MMPDU
                       associated with delivery-enabled ACs and destined for
                       that WMM STA indicates that more frames are buffered for
					the delivery-enabled ACs.
				*/
				RTMP_SET_PACKET_MOREDATA(pQuedPkt, true);

				/* set U-APSD flag & its software queue ID */
				RTMP_SET_PACKET_UAPSD(pQuedPkt, true, QueId);
			}
		}

		if (pAcPsQue->Head != NULL)
		{
			/* still have packets in queue */
			FlgQueEmpty = FALSE;
			break;
		}
	}

	if (pQuedPkt != NULL)
	{
		if (FlgQueEmpty == true)
		{
			/*
				No any more queued U-APSD packet so clear More Data bit of
				the last frame.
			*/
			RTMP_SET_PACKET_MOREDATA(pQuedPkt, FALSE);
		}

		UAPSD_INSERT_QUEUE_AC(pAd, pEntry, pAcSwQue, pQuedPkt);
	}

	/* clear corresponding TIM bit */
	/* get its AID for the station */
	Aid = pEntry->Aid;
	if ((pEntry->bAPSDAllAC == 1) && (FlgQueEmpty == true))
	{
		/* all AC are U-APSD and no any U-APSD packet is queued, set TIM */
#ifdef CONFIG_AP_SUPPORT
		/* clear TIM bit */
		if ((Aid > 0) && (Aid < MAX_LEN_OF_MAC_TABLE))
		{
			WLAN_MR_TIM_BIT_CLEAR(pAd, pEntry->apidx, Aid);
		}
#endif /* CONFIG_AP_SUPPORT */
	}

	/* reset idle timeout here whenever a trigger frame is received */
	pEntry->UAPSDQIdleCount = 0;

	RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);

	/* Dequeue outgoing frames from TxSwQueue0..3 queue and process it */
	RTMPDeQueuePacket(pAd, FALSE, NUM_OF_TX_RING, MAX_TX_PROCESS);
	return true;
}


/*
========================================================================
Routine Description:
    Get the queue status for delivery-enabled AC.

Arguments:
	pAd					Pointer to our adapter
	pEntry				the peer entry
	pFlgIsAnyPktForBK	true: At lease a BK packet is queued
	pFlgIsAnyPktForBE	true: At lease a BE packet is queued
	pFlgIsAnyPktForVI	true: At lease a VI packet is queued
	pFlgIsAnyPktForVO	true: At lease a VO packet is queued

Return Value:
    None

Note:
========================================================================
*/
VOID UAPSD_QueueStatusGet(
	IN	struct rtmp_adapter *	pAd,
	IN	MAC_TABLE_ENTRY		*pEntry,
	OUT	bool				*pFlgIsAnyPktForBK,
	OUT bool				*pFlgIsAnyPktForBE,
	OUT bool				*pFlgIsAnyPktForVI,
	OUT bool				*pFlgIsAnyPktForVO)
{
	*pFlgIsAnyPktForBK = FALSE;
	*pFlgIsAnyPktForBE = FALSE;
	*pFlgIsAnyPktForVI = FALSE;
	*pFlgIsAnyPktForVO = FALSE;

	if (pEntry == NULL)
		return;

	/* get queue status */
	RTMP_SEM_LOCK(&pAd->UAPSDEOSPLock);
	if (pEntry->UAPSDQueue[QID_AC_BK].Head != NULL)
		*pFlgIsAnyPktForBK = true;
	if (pEntry->UAPSDQueue[QID_AC_BE].Head != NULL)
		*pFlgIsAnyPktForBE = true;
	if (pEntry->UAPSDQueue[QID_AC_VI].Head != NULL)
		*pFlgIsAnyPktForVI = true;
	if (pEntry->UAPSDQueue[QID_AC_VO].Head != NULL)
		*pFlgIsAnyPktForVO = true;
	RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);
}


/*
========================================================================
Routine Description:
    Handle UAPSD Trigger Frame.

Arguments:
	pAd				Pointer to our adapter
	*pEntry			the source STATION
	UpOfFrame		the UP of the trigger frame

Return Value:
    None

Note:
========================================================================
*/
VOID UAPSD_TriggerFrameHandle(struct rtmp_adapter *pAd, MAC_TABLE_ENTRY *pEntry, UCHAR UpOfFrame)
{
	QUEUE_HEADER	*pAcPsQue;
	QUEUE_HEADER	*pAcSwQue, *pLastAcSwQue;
	PQUEUE_ENTRY	pQuedEntry;
	struct sk_buff *pQuedPkt;

	uint32_t AcQueId;
	uint32_t TxPktNum, SpMaxLen;
    /*
		AC ID          = VO > VI > BK > BE
		so we need to change BE & BK
		=> AC priority = VO > VI > BE > BK
	*/
	uint32_t AcPriority[WMM_NUM_OF_AC] = { 1, 0, 2, 3 };
	/* 0: deliver all U-APSD packets */
	uint32_t SpLenMap[WMM_NUM_OF_AC] = { 0, 2, 4, 6 };
	UCHAR	QueIdList[WMM_NUM_OF_AC] = { QID_AC_BE, QID_AC_BK,
                                            QID_AC_VI, QID_AC_VO };
	bool	FlgQueEmpty;
	bool	FlgNullSnd;
	bool	FlgMgmtFrame;
	uint32_t Aid, QueId;
	int32_t IdAc; /* must be signed, can not use unsigned */
/*	ULONG    FlgIrq; */

#ifdef UAPSD_SP_ACCURATE
	ULONG	TimeNow;
#endif /* UAPSD_SP_ACCURATE */


	/* sanity check for Service Period of the STATION */
	RTMP_SEM_LOCK(&pAd->UAPSDEOSPLock);

#ifdef UAPSD_DEBUG
	DBGPRINT(RT_DEBUG_TRACE, ("\nuapsd> bAPSDFlagLegacySent = %d!\n",
			pEntry->bAPSDFlagLegacySent));
#endif /* UAPSD_DEBUG */

	if (pEntry->bAPSDFlagSPStart != 0)
	{
		/*
			reset ContinueTxFailCnt
		*/
		pEntry->ContinueTxFailCnt = 0;

		/*
			WMM Specification V1.1 3.6.1.5
               A Trigger Frame received by the WMM AP from a WMM STA that
               already has an USP underway shall not trigger the start of a new
			USP.
		*/

		/*
			Current SP for the STATION is not yet ended so the packet is
			normal DATA packet.
		*/

#ifdef UAPSD_DEBUG
		DBGPRINT(RT_DEBUG_TRACE, ("uapsd> sorry! SP is not yet closed!\n"));
#endif /* UAPSD_DEBUG */

#ifdef UAPSD_SP_ACCURATE
		/*
			The interval between the data frame from QSTA and last confirmed
			packet from QAP in UAPSD_SP_AUE_Handle() is too large so maybe
			we suffer the worse case.

			Currently, if we send any packet with 1Mbps in 2.4GHz and 6Mbps
			in 5GHz, no any statistics count for the packet so the SP can
			not be closed.
		*/
		UAPSD_TIME_GET(pAd, TimeNow);

		if ((TimeNow - pEntry->UAPSDTimeStampLast) >= UAPSD_EPT_SP_INT)
		{
#ifdef UAPSD_DEBUG
			DBGPRINT(RT_DEBUG_TRACE, ("uapsd> SP period is too large so SP is closed first!"
						" (%lu %lu %lu)!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n",
						TimeNow, pEntry->UAPSDTimeStampLast,
						(TimeNow - pEntry->UAPSDTimeStampLast)));

			gUAPSD_SP_CloseAbnormalNum ++;
#endif /* UAPSD_DEBUG */

			RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);
			UAPSD_SP_Close(pAd, pEntry);
			RTMP_SEM_LOCK(&pAd->UAPSDEOSPLock);
		}
		else
		{
#endif /* UAPSD_SP_ACCURATE */

			RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);
            return;

#ifdef UAPSD_SP_ACCURATE
		}
#endif /* UAPSD_SP_ACCURATE */
	}

#ifdef UAPSD_TIMING_RECORD_FUNC
	UAPSD_TIMING_RECORD(pAd, UAPSD_TIMING_RECORD_TRG_RCV);
#endif /* UAPSD_TIMING_RECORD_FUNC */

#ifdef UAPSD_DEBUG
	if (pEntry->pUAPSDEOSPFrame != NULL)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("uapsd> EOSP is not NULL!\n"));
		RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);
		return;
	}
#endif /* UAPSD_DEBUG */

	if (pEntry->MaxSPLength >= 4)
	{
		/* fatal error, should be 0 ~ 3 so reset it to 0 */
		DBGPRINT(RT_DEBUG_TRACE,
					("uapsd> MaxSPLength >= 4 (%d)!\n", pEntry->MaxSPLength));
		pEntry->MaxSPLength = 0;
	}


#ifdef UAPSD_SP_ACCURATE
	/* mark the start time for the SP */
	UAPSD_TIME_GET(pAd, pEntry->UAPSDTimeStampLast);


	/* check if current rate of the entry is 1Mbps (2.4GHz) or 6Mbps (5GHz) */

#ifdef RTMP_MAC_USB
	/* always use rough mechanism */
	pEntry->bAPSDFlagSpRoughUse = 1;
#endif /* RTMP_MAC_USB */
#else

	pEntry->bAPSDFlagSpRoughUse = 1;
#endif /* UAPSD_SP_ACCURATE */

	/* sanity Check for UAPSD condition */
	if (UpOfFrame >= 8)
		UpOfFrame = 1; /* shout not be here */

	/* get the AC ID of incoming packet */
	AcQueId = WMM_UP2AC_MAP[UpOfFrame];

	/* check whether the AC is trigger-enabled AC */
	if (pEntry->bAPSDCapablePerAC[AcQueId] == 0)
	{
		/*
			WMM Specification V1.1 Page 4
               Trigger Frame: A QoS Data or QoS Null frame from a WMM STA in
               Power Save Mode associated with an AC the WMM STA has configured
               to be a trigger-enabled AC.

               A QoS Data or QoS Null frame that indicates transition to/from
               Power Save Mode is not considered to be a Trigger Frame and the
			AP shall not respond with a QoS Null frame.
		*/

		/*
			ERROR! the AC does not belong to a trigger-enabled AC or
			the ACM of the AC is set.
		*/
		RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);
		return;
	}


	/* enqueue U-APSD packets to AC software queues */

	/*
		Protect TxSwQueue0 & McastPsQueue because use them in
		interrupt context.
	*/
/*	RTMP_IRQ_LOCK(FlgIrq); */

	/* init */
	FlgQueEmpty = true;
	TxPktNum = 0;
	SpMaxLen = SpLenMap[pEntry->MaxSPLength];
	pAcSwQue = NULL;
	pLastAcSwQue = NULL;
	pQuedPkt = NULL;
	FlgMgmtFrame = 0;

	/* from highest priority AC3 --> AC2 --> AC0 --> lowest priority AC1 */
	for (IdAc=(WMM_NUM_OF_AC-1); IdAc>=0; IdAc--)
	{
		AcQueId = AcPriority[IdAc];

		/* check if the AC is delivery-enable AC */
		if (pEntry->bAPSDDeliverEnabledPerAC[AcQueId] == 0)
			continue;

		/*
			NOTE: get U-APSD queue pointer here to speed up, do NOT use
			pEntry->UAPSDQueue[AcQueId] throughout codes because
			compiler will compile it to many assembly codes.
		*/
		pAcPsQue = &pEntry->UAPSDQueue[AcQueId];

		/* check if any U-APSD packet is queued for the AC */
		if (pAcPsQue->Head == NULL)
			continue;

		/* at least one U-APSD packet exists here */

		/* get AC software queue */
		QueId = QueIdList[AcQueId];
		pAcSwQue = &pAd->TxSwQueue[QueId];

		/* put U-APSD packets to the AC software queue */
		while(pAcPsQue->Head)
		{
			/* check if Max SP Length != 0 */
			if (SpMaxLen != 0)
			{
				/*
					WMM Specification V1.1 3.6.1.7
                       At each USP for a WMM STA, the WMM AP shall attempt to
                       transmit at least one MSDU or MMPDU, but no more than the
                       value encoded in the Max SP Length field in the QoS Info
                       Field of a WMM Information Element from delivery-enabled
					ACs, that are destined for the WMM STA.
				*/
				if (TxPktNum >= SpMaxLen)
				{
					/*
						Some queued U-APSD packets still exists so we will
						not clear MoreData bit of the packet.
					*/
					FlgQueEmpty = FALSE;
					break;
				}
			}

			/* count U-APSD packet number */
			TxPktNum ++;

			/* queue last U-APSD packet */
			if (pQuedPkt != NULL)
			{
				/*
					enqueue U-APSD packet to tx software queue

					WMM Specification V1.1 3.6.1.7:
					Each buffered frame shall be delivered using the access
					parameters of its AC.
				*/
				UAPSD_INSERT_QUEUE_AC(pAd, pEntry, pLastAcSwQue, pQuedPkt);
			}

			/* get the U-APSD packet */
			pQuedEntry = RemoveHeadQueue(pAcPsQue);
			pQuedPkt = QUEUE_ENTRY_TO_PACKET(pQuedEntry);
			if (pQuedPkt != NULL)
			{
				if (RTMP_GET_PACKET_MGMT_PKT(pQuedPkt) == 1)
					FlgMgmtFrame = 1;

				/*
					WMM Specification V1.1 3.6.1.7
                       The More Data bit (b13) of the directed MSDU or MMPDU
                       associated with delivery-enabled ACs and destined for
                       that WMM STA indicates that more frames are buffered for
					the delivery-enabled ACs.
				*/
				RTMP_SET_PACKET_MOREDATA(pQuedPkt, true);

				/* set U-APSD flag & its software queue ID */
				RTMP_SET_PACKET_UAPSD(pQuedPkt, true, QueId);
			}

			/* backup its software queue pointer */
			pLastAcSwQue = pAcSwQue;
		}

		if (FlgQueEmpty == FALSE)
		{
			/* FlgQueEmpty will be FALSE only when TxPktNum >= SpMaxLen */
			break;
		}
	}

	/*
		For any mamagement UAPSD frame, we use DMA to do SP check
		because no any FIFO statistics for management frame.
	*/
	if (FlgMgmtFrame)
		pEntry->bAPSDFlagSpRoughUse = 1;

	/*
		No need to protect EOSP handle code because we will be here
		only when last SP is ended.
	*/
	FlgNullSnd = FALSE;

	if (TxPktNum >= 1)
	{
		if (FlgQueEmpty == true)
		{
			/*
				No any more queued U-APSD packet so clear More Data bit of
				the last frame.
			*/
			RTMP_SET_PACKET_MOREDATA(pQuedPkt, FALSE);
		}
	}

	//pEntry->bAPSDFlagSPStart = 1; /* set the SP start flag */
	UAPSD_SP_START(pAd, pEntry); /* set the SP start flag */
	pEntry->bAPSDFlagEOSPOK = 0;

#ifdef UAPSD_DEBUG
{
	ULONG DebugTimeNow;

	UAPSD_TIME_GET(pAd, DebugTimeNow);

	DBGPRINT(RT_DEBUG_TRACE, ("uapsd> start a SP (Tx Num = %d) (Rough SP = %d) "
			"(Has Any Mgmt = %d) (Abnormal = %d) (Time = %lu)\n",
			TxPktNum, pEntry->bAPSDFlagSpRoughUse, FlgMgmtFrame,
			gUAPSD_SP_CloseAbnormalNum, DebugTimeNow));
}
#endif /* UAPSD_DEBUG */

	if (TxPktNum <= 1)
	{
		/* if no data needs to tx, respond with QosNull for the trigger frame */
		pEntry->pUAPSDEOSPFrame = NULL;
		pEntry->UAPSDTxNum = 0;

		if (TxPktNum <= 0)
		{
			FlgNullSnd = true;

#ifdef UAPSD_DEBUG
			DBGPRINT(RT_DEBUG_TRACE,
					 ("uapsd> No data, send a Qos-Null frame with ESOP bit on and "
					  "UP=%d to end USP\n", UpOfFrame));
#endif /* RELEASE_EXCLUDE */
		}
		else
		{
			/* only one packet so send it directly */
			RTMP_SET_PACKET_EOSP(pQuedPkt, true);
			UAPSD_INSERT_QUEUE_AC(pAd, pEntry, pLastAcSwQue, pQuedPkt);

#ifdef UAPSD_DEBUG
			DBGPRINT(RT_DEBUG_TRACE,
					("uapsd> Only one packet with UP = %d\n",
					RTMP_GET_PACKET_UP(pQuedPkt)));
#endif /* RELEASE_EXCLUDE */
		}

		/*
			We will send the QoS Null frame below and we will hande the
			QoS Null tx done in RTMPFreeTXDUponTxDmaDone().
		*/
	}
	else
	{
		/* more than two U-APSD packets */

		/*
			NOTE: EOSP bit != !MoreData bit because Max SP Length,
			we can not use MoreData bit to decide EOSP bit.
		*/

		/*
			Backup the EOSP frame and
			we will transmit the EOSP frame in RTMPFreeTXDUponTxDmaDone().
		*/
		RTMP_SET_PACKET_EOSP(pQuedPkt, true);

		pEntry->pUAPSDEOSPFrame = (PQUEUE_ENTRY)pQuedPkt;
		pEntry->UAPSDTxNum = TxPktNum-1; /* skip the EOSP frame */
	}

#ifdef UAPSD_DEBUG
	if ((pEntry->pUAPSDEOSPFrame != NULL) &&
		(RTMP_GET_PACKET_MGMT_PKT(pEntry->pUAPSDEOSPFrame) == 1))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("uapsd> The EOSP frame is a management frame.\n"));
	}
#endif /* UAPSD_DEBUG */


#ifdef UAPSD_SP_ACCURATE
	/* count for legacy PS packet */

	/*
		Note: A worse case for mix mode (UAPSD + legacy PS):
			PS-Poll --> legacy ps packet --> trigger frame --> QoS Null frame
			(QSTA)		(QAP)				 (QSTA)			   (QAP)

		where statistics handler is NICUpdateFifoStaCounters().

		If we receive the trigger frame before the legacy ps packet is sent to
		the air, when we call statistics handler in tx done, it maybe the
		legacy ps statistics, not the QoS Null frame statistics, so we will
		do UAPSD counting fail.

		We need to count the legacy PS here if it is not yet sent to the air.
	*/

	/*
		Note: in addition, only one legacy PS need to count because one legacy
		packet for one PS-Poll packet; if we receive a trigger frame from a
		station, it means that only one legacy ps packet is possible not sent
		to the air, it is impossible more than 2 legacy packets are not yet
		sent to the air.
	*/
	if ((pEntry->bAPSDFlagSpRoughUse == 0) &&
		(pEntry->bAPSDFlagLegacySent != 0))
	{
		pEntry->UAPSDTxNum ++;

#ifdef UAPSD_DEBUG
		DBGPRINT(RT_DEBUG_TRACE, ("uapsd> A legacy PS is sent! UAPSDTxNum = %d\n",
				pEntry->UAPSDTxNum));
#endif /* UAPSD_DEBUG */
	}
#endif /* UAPSD_SP_ACCURATE */


	/* clear corresponding TIM bit */

	/* get its AID for the station */
	Aid = pEntry->Aid;

	if ((pEntry->bAPSDAllAC == 1) && (FlgQueEmpty == 1))
	{
		/* all AC are U-APSD and no any U-APSD packet is queued, set TIM */

#ifdef CONFIG_AP_SUPPORT
		/* clear TIM bit */
		if ((Aid > 0) && (Aid < MAX_LEN_OF_MAC_TABLE))
		{
			WLAN_MR_TIM_BIT_CLEAR(pAd, pEntry->apidx, Aid);
		}
#endif /* CONFIG_AP_SUPPORT */
	}

	/* reset idle timeout here whenever a trigger frame is received */
	pEntry->UAPSDQIdleCount = 0;

	RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);


	/* check if NULL Frame is needed to be transmitted */

	/* it will be crashed, when spin locked in kernel 2.6 */
	if (FlgNullSnd)
	{
		/* bQosNull = bEOSP = true = 1 */

		/*
			Use management queue to tx QoS Null frame to avoid delay so
			us_of_frame is not used.
		*/
		RtmpEnqueueNullFrame(pAd, pEntry->Addr, pEntry->CurrTxRate,
							Aid, pEntry->apidx, true, true, UpOfFrame);

#ifdef UAPSD_DEBUG
		DBGPRINT(RT_DEBUG_TRACE, ("uapsd> end a SP by a QoS Null frame!\n"));
#endif /* UAPSD_DEBUG */
	}

#ifdef UAPSD_TIMING_RECORD_FUNC
	UAPSD_TIMING_RECORD(pAd, UAPSD_TIMING_RECORD_MOVE2TX);
#endif /* UAPSD_TIMING_RECORD_FUNC */

	/* Dequeue outgoing frames from TxSwQueue0..3 queue and process it */
	RTMPDeQueuePacket(pAd, FALSE, NUM_OF_TX_RING, MAX_TX_PROCESS);
}


#ifdef RTMP_MAC_USB
/*
========================================================================
Routine Description:
    Tag current offset of the AC in USB URB tx buffer.

Arguments:
	pAd				Pointer to our adapter
	*pPkt			the tx packet
    Wcid			destination entry id
	PktOffset		USB tx buffer offset

Return Value:
    None

Note:
	Only for RT2870.
========================================================================
*/
VOID UAPSD_TagFrame(
	IN	struct rtmp_adapter 	*pAd,
	IN	struct sk_buff		*pPkt,
	IN	UCHAR				Wcid,
	IN	uint32_t 			PktOffset)
{
	MAC_TABLE_ENTRY *pEntry;
	UCHAR AcQueId;


	if ((Wcid == MCAST_WCID) || (Wcid >= MAX_LEN_OF_MAC_TABLE))
		return; /* the frame is broadcast/multicast frame */

	pEntry = &pAd->MacTab.Content[Wcid];

	if (pEntry->bAPSDFlagSPStart == 0)
		return; /* SP is not started */

#ifdef UAPSD_DEBUG
	DBGPRINT(RT_DEBUG_TRACE, ("uapsd> prepare to tag the frame...\n"));
#endif /* UAPSD_DEBUG */

	if (RTMP_GET_PACKET_UAPSD_Flag(pPkt) == true)
	{
		/* mark the latest USB tx buffer offset for the priority */
		AcQueId = RTMP_GET_PACKET_UAPSD_QUE_ID(pPkt);
		pEntry->UAPSDTagOffset[AcQueId] = PktOffset;

#ifdef UAPSD_DEBUG
		DBGPRINT(RT_DEBUG_TRACE, ("uapsd> tag offset = %d\n", PktOffset));
#endif /* UAPSD_DEBUG */
	}
}


/*
========================================================================
Routine Description:
    Check if UAPSD packets are tx ok.

Arguments:
	pAd				Pointer to our adapter
	AcQueId			TX completion for the AC (0 ~ 3)
	bulkStartPos
	bulkEnPos

Return Value:
    None

Note:
	Only for RT2870.
========================================================================
*/
VOID UAPSD_UnTagFrame(
	IN	struct rtmp_adapter *pAd,
	IN	UCHAR			AcQueId,
	IN	uint32_t 		bulkStartPos,
	IN	uint32_t 		bulkEnPos)
{
	MAC_TABLE_ENTRY *pEntry;
	uint32_t IdEntry;
	uint32_t TxPktTagOffset;
	uint16_t QueId;
	int		FirstWcid = 1;


	RTMP_SEM_LOCK(&pAd->UAPSDEOSPLock);

	/* loop for all entries to check whether we need to close their SP */
	for(IdEntry = FirstWcid; IdEntry < MAX_LEN_OF_MAC_TABLE; IdEntry++)
	{
		pEntry = &pAd->MacTab.Content[IdEntry];

		if ((IS_ENTRY_CLIENT(pEntry)
			)
			 && (pEntry->bAPSDFlagSPStart == 1) &&
			(pEntry->UAPSDTagOffset[AcQueId] != 0))
		{
#ifdef UAPSD_DEBUG
			DBGPRINT(RT_DEBUG_ERROR, ("uapsd> bulkStartPos = %d\n", bulkStartPos));
			DBGPRINT(RT_DEBUG_ERROR, ("uapsd> bulkEnPos = %d\n", bulkEnPos));
			DBGPRINT(RT_DEBUG_ERROR, ("uapsd> record offset = %d\n", pEntry->UAPSDTagOffset[AcQueId]));
#endif /* UAPSD_DEBUG */

			/*
				1. tx tag is in [bulkStartPos, bulkEnPos];
				2. when bulkEnPos < bulkStartPos
			*/
			TxPktTagOffset = pEntry->UAPSDTagOffset[AcQueId];

			if (((TxPktTagOffset >= bulkStartPos) &&
					(TxPktTagOffset <= bulkEnPos)) ||
				((bulkEnPos < bulkStartPos) &&
					(TxPktTagOffset >= bulkStartPos)) ||
				((bulkEnPos < bulkStartPos) &&
					(TxPktTagOffset <= bulkEnPos)))
			{
				/* ok, some UAPSD frames of the AC are transmitted */
				pEntry->UAPSDTagOffset[AcQueId] = 0;

				if (pEntry->UAPSDTxNum == 0)
				{
					/* ok, all UAPSD frames are transmitted */
					//pEntry->bAPSDFlagSPStart = 0;
					pEntry->bAPSDFlagEOSPOK = 0;
					UAPSD_SP_END(pAd, pEntry);

					if (pEntry->pUAPSDEOSPFrame != NULL)
		            {
						/* should not be here */
						RELEASE_NDIS_PACKET(pAd,
								QUEUE_ENTRY_TO_PACKET(pEntry->pUAPSDEOSPFrame),
		                                    NDIS_STATUS_FAILURE);
						pEntry->pUAPSDEOSPFrame = NULL;
		            }

#ifdef UAPSD_DEBUG
					DBGPRINT(RT_DEBUG_ERROR, ("uapsd> [1] close SP (%d)!\n", AcQueId));
#endif /* UAPSD_DEBUG */
					continue; /* check next station */
				}

				if ((pEntry->UAPSDTagOffset[QID_AC_BE] == 0) &&
					(pEntry->UAPSDTagOffset[QID_AC_BK] == 0) &&
					(pEntry->UAPSDTagOffset[QID_AC_VI] == 0) &&
					(pEntry->UAPSDTagOffset[QID_AC_VO] == 0))
				{
					/*
						OK, UAPSD frames of all AC for the entry are transmitted
						except the EOSP frame.
					*/

					if (pEntry->pUAPSDEOSPFrame != NULL)
	                {
	                    /* transmit the EOSP frame */
						struct sk_buff *pPkt;

						pPkt = QUEUE_ENTRY_TO_PACKET(pEntry->pUAPSDEOSPFrame);
						QueId = RTMP_GET_PACKET_UAPSD_QUE_ID(pPkt);

						if (QueId > QID_AC_VO)
	                    {
	                        /* should not be here, only for sanity */
							QueId = QID_AC_BE;
	                    }

#ifdef UAPSD_DEBUG
				DBGPRINT(RT_DEBUG_ERROR, ("uapsd> enqueue the EOSP frame...\n"));
#endif /* UAPSD_DEBUG */

						UAPSD_INSERT_QUEUE_AC(pAd, pEntry,
										&pAd->TxSwQueue[QueId],
										pEntry->pUAPSDEOSPFrame);

						pEntry->pUAPSDEOSPFrame = NULL;

						/*
							The EOSP frame will be put into ASIC to tx
							in RTMPHandleTxRingDmaDoneInterrupt(),
							not the function.
						*/

						/* de-queue packet here to speed up EOSP frame response */
						printk("%s:: ----> RTMPDeQueuePacket\n", __FUNCTION__);
						RTMPDeQueuePacket(pAd, FALSE, NUM_OF_TX_RING, MAX_TX_PROCESS);
					}
					else
					{
						/* only when 1 data frame with EOSP = 1 is transmitted */
						//pEntry->bAPSDFlagSPStart = 0;
						pEntry->bAPSDFlagEOSPOK = 0;
						UAPSD_SP_END(pAd, pEntry);

#ifdef UAPSD_DEBUG
						DBGPRINT(RT_DEBUG_ERROR, ("uapsd> [2] close SP (%d)!\n", AcQueId));
#endif /* UAPSD_DEBUG */
					}

					/* no any EOSP frames are queued and prepare to close the SP */
					pEntry->UAPSDTxNum = 0;
				}
			}
		}
	}

	RTMP_SEM_UNLOCK(&pAd->UAPSDEOSPLock);
}
#endif /* RTMP_MAC_USB */

#endif /* UAPSD_SUPPORT */

