/*
 * DHD Protocol Module for CDC and BDC.
 *
 * Copyright (C) 1999-2010, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: dhd_cdc.c,v 1.22.4.2.4.7.2.41 2010/06/23 19:58:18 Exp $
 *
 * BDC is like CDC, except it includes a header for data packets to convey
 * packet priority over the bus, and flags (e.g. to indicate checksum status
 * for dongle offload).
 */

#include <typedefs.h>
#include <osl.h>

#include <bcmutils.h>
#include <bcmcdc.h>
#include <bcmendian.h>

#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_proto.h>
#include <dhd_bus.h>
#include <dhd_dbg.h>
<<<<<<< HEAD:drivers/net/wireless/bcm4329/dhd_cdc.c
#include <asm/mach-types.h>
#if defined(IL_BIGENDIAN)
#include <bcmendian.h>
#define htod32(i) (bcmswap32(i))
#define htod16(i) (bcmswap16(i))
#define dtoh32(i) (bcmswap32(i))
#define dtoh16(i) (bcmswap16(i))
#define htodchanspec(i) htod16(i)
#define dtohchanspec(i) dtoh16(i)
#else
#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i
#define htodchanspec(i) i
#define dtohchanspec(i) i
#endif
=======

extern int dhd_preinit_ioctls(dhd_pub_t *dhd);
>>>>>>> master:drivers/net/wireless/bcm4329/dhd_cdc.c

/* Packet alignment for most efficient SDIO (can change based on platform) */
#ifndef DHD_SDALIGN
#define DHD_SDALIGN	32
#endif
#if !ISPOWEROF2(DHD_SDALIGN)
#error DHD_SDALIGN is not a power of 2!
#endif

#define RETRIES 2		/* # of retries to retrieve matching ioctl response */
#define BUS_HEADER_LEN	(16+DHD_SDALIGN)	/* Must be atleast SDPCM_RESERVE
				 * defined in dhd_sdio.c (amount of header tha might be added)
				 * plus any space that might be needed for alignment padding.
				 */
#define ROUND_UP_MARGIN	2048 	/* Biggest SDIO block size possible for
				 * round off at the end of buffer
				 */
extern int wifi_get_dot11n_enable(void);

typedef struct dhd_prot {
	uint16 reqid;
	uint8 pending;
	uint32 lastcmd;
	uint8 bus_header[BUS_HEADER_LEN];
	cdc_ioctl_t msg;
	unsigned char buf[WLC_IOCTL_MAXLEN + ROUND_UP_MARGIN];
} dhd_prot_t;

static int
dhdcdc_msg(dhd_pub_t *dhd)
{
	dhd_prot_t *prot = dhd->prot;
	int len = ltoh32(prot->msg.len) + sizeof(cdc_ioctl_t);
	int ret;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	dhd_os_wake_lock(dhd);

	/* NOTE : cdc->msg.len holds the desired length of the buffer to be
	 *        returned. Only up to CDC_MAX_MSG_SIZE of this buffer area
	 *	  is actually sent to the dongle
	 */
	if (len > CDC_MAX_MSG_SIZE)
		len = CDC_MAX_MSG_SIZE;

	/* Send request */
	ret = dhd_bus_txctl(dhd->bus, (uchar*)&prot->msg, len);
	dhd_os_wake_unlock(dhd);
	return ret;
}

static int
dhdcdc_cmplt(dhd_pub_t *dhd, uint32 id, uint32 len)
{
	int ret;
	dhd_prot_t *prot = dhd->prot;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	do {
		ret = dhd_bus_rxctl(dhd->bus, (uchar*)&prot->msg, len+sizeof(cdc_ioctl_t));
		if (ret < 0)
			break;
	} while (CDC_IOC_ID(ltoh32(prot->msg.flags)) != id);

	return ret;
}

int
dhdcdc_query_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len)
{
	dhd_prot_t *prot = dhd->prot;
	cdc_ioctl_t *msg = &prot->msg;
	void *info;
	int ret = 0, retries = 0;
	uint32 id, flags = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	DHD_CTL(("%s: cmd %d len %d\n", __FUNCTION__, cmd, len));


	/* Respond "bcmerror" and "bcmerrorstr" with local cache */
	if (cmd == WLC_GET_VAR && buf)
	{
		if (!strcmp((char *)buf, "bcmerrorstr"))
		{
			strncpy((char *)buf, bcmerrorstr(dhd->dongle_error), BCME_STRLEN);
			goto done;
		}
		else if (!strcmp((char *)buf, "bcmerror"))
		{
			*(int *)buf = dhd->dongle_error;
			goto done;
		}
	}

	memset(msg, 0, sizeof(cdc_ioctl_t));

	msg->cmd = htol32(cmd);
	msg->len = htol32(len);
	msg->flags = (++prot->reqid << CDCF_IOC_ID_SHIFT);
	CDC_SET_IF_IDX(msg, ifidx);
	msg->flags = htol32(msg->flags);

	if (buf)
		memcpy(prot->buf, buf, len);

	if ((ret = dhdcdc_msg(dhd)) < 0) {
		DHD_ERROR(("dhdcdc_query_ioctl: dhdcdc_msg failed w/status %d\n", ret));
		goto done;
	}

retry:
	/* wait for interrupt and get first fragment */
	if ((ret = dhdcdc_cmplt(dhd, prot->reqid, len)) < 0)
		goto done;

	flags = ltoh32(msg->flags);
	id = (flags & CDCF_IOC_ID_MASK) >> CDCF_IOC_ID_SHIFT;

	if ((id < prot->reqid) && (++retries < RETRIES))
		goto retry;
	if (id != prot->reqid) {
		DHD_ERROR(("%s: %s: unexpected request id %d (expected %d)\n",
		           dhd_ifname(dhd, ifidx), __FUNCTION__, id, prot->reqid));
		ret = -EINVAL;
		goto done;
	}

	/* Check info buffer */
	info = (void*)&msg[1];

	/* Copy info buffer */
	if (buf)
	{
		if (ret < (int)len)
			len = ret;
		memcpy(buf, info, len);
	}

	/* Check the ERROR flag */
	if (flags & CDCF_IOC_ERROR)
	{
		ret = ltoh32(msg->status);
		/* Cache error from dongle */
		dhd->dongle_error = ret;
	}

done:
	return ret;
}

int
dhdcdc_set_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf, uint len)
{
	dhd_prot_t *prot = dhd->prot;
	cdc_ioctl_t *msg = &prot->msg;
	int ret = 0;
	uint32 flags, id;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	DHD_CTL(("%s: cmd %d len %d\n", __FUNCTION__, cmd, len));

	memset(msg, 0, sizeof(cdc_ioctl_t));

	msg->cmd = htol32(cmd);
	msg->len = htol32(len);
	msg->flags = (++prot->reqid << CDCF_IOC_ID_SHIFT) | CDCF_IOC_SET;
	CDC_SET_IF_IDX(msg, ifidx);
	msg->flags = htol32(msg->flags);

	if (buf)
		memcpy(prot->buf, buf, len);

	if ((ret = dhdcdc_msg(dhd)) < 0)
		goto done;

	if ((ret = dhdcdc_cmplt(dhd, prot->reqid, len)) < 0)
		goto done;

	flags = ltoh32(msg->flags);
	id = (flags & CDCF_IOC_ID_MASK) >> CDCF_IOC_ID_SHIFT;

	if (id != prot->reqid) {
		DHD_ERROR(("%s: %s: unexpected request id %d (expected %d)\n",
		           dhd_ifname(dhd, ifidx), __FUNCTION__, id, prot->reqid));
		ret = -EINVAL;
		goto done;
	}

	/* Check the ERROR flag */
	if (flags & CDCF_IOC_ERROR)
	{
		ret = ltoh32(msg->status);
		/* Cache error from dongle */
		dhd->dongle_error = ret;
	}

done:
	return ret;
}

extern int dhd_bus_interface(struct dhd_bus *bus, uint arg, void* arg2);
int
dhd_prot_ioctl(dhd_pub_t *dhd, int ifidx, wl_ioctl_t * ioc, void * buf, int len)
{
	dhd_prot_t *prot = dhd->prot;
	int ret = -1;

	if (dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s : bus is down. we have nothing to do\n", __FUNCTION__));
		return ret;
	}
	dhd_os_proto_block(dhd);

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(len <= WLC_IOCTL_MAXLEN);

	if (len > WLC_IOCTL_MAXLEN)
		goto done;

	if (prot->pending == TRUE) {
		DHD_TRACE(("CDC packet is pending!!!! cmd=0x%x (%lu) lastcmd=0x%x (%lu)\n",
			ioc->cmd, (unsigned long)ioc->cmd, prot->lastcmd,
			(unsigned long)prot->lastcmd));
		if ((ioc->cmd == WLC_SET_VAR) || (ioc->cmd == WLC_GET_VAR)) {
			DHD_TRACE(("iovar cmd=%s\n", (char*)buf));
		}
		goto done;
	}

	prot->pending = TRUE;
	prot->lastcmd = ioc->cmd;
	if (ioc->set)
		ret = dhdcdc_set_ioctl(dhd, ifidx, ioc->cmd, buf, len);
	else {
		ret = dhdcdc_query_ioctl(dhd, ifidx, ioc->cmd, buf, len);
		if (ret > 0)
			ioc->used = ret - sizeof(cdc_ioctl_t);
	}

	/* Too many programs assume ioctl() returns 0 on success */
	if (ret >= 0)
		ret = 0;
	else {
		cdc_ioctl_t *msg = &prot->msg;
		ioc->needed = ltoh32(msg->len); /* len == needed when set/query fails from dongle */
	}

	/* Intercept the wme_dp ioctl here */
	if ((!ret) && (ioc->cmd == WLC_SET_VAR) && (!strcmp(buf, "wme_dp"))) {
		int slen, val = 0;

		slen = strlen("wme_dp") + 1;
		if (len >= (int)(slen + sizeof(int)))
			bcopy(((char *)buf + slen), &val, sizeof(int));
		dhd->wme_dp = (uint8) ltoh32(val);
	}

	prot->pending = FALSE;

done:
	dhd_os_proto_unblock(dhd);

	return ret;
}

int
dhd_prot_iovar_op(dhd_pub_t *dhdp, const char *name,
                  void *params, int plen, void *arg, int len, bool set)
{
	return BCME_UNSUPPORTED;
}

void
dhd_prot_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf)
{
	bcm_bprintf(strbuf, "Protocol CDC: reqid %d\n", dhdp->prot->reqid);
}

#ifdef APSTA_PINGTEST
extern struct ether_addr guest_eas[MAX_GUEST];
#endif

void
dhd_prot_hdrpush(dhd_pub_t *dhd, int ifidx, void *pktbuf)
{
#ifdef BDC
	struct bdc_header *h;
#ifdef APSTA_PINGTEST
	struct	ether_header *eh;
	int i;
#ifdef DHD_DEBUG
	char eabuf1[ETHER_ADDR_STR_LEN];
	char eabuf2[ETHER_ADDR_STR_LEN];
#endif /* DHD_DEBUG */
#endif /* APSTA_PINGTEST */
#endif /* BDC */

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#ifdef BDC
	/* Push BDC header used to convey priority for buses that don't */

#ifdef APSTA_PINGTEST
	eh = (struct ether_header *)PKTDATA(dhd->osh, pktbuf);
#endif

	PKTPUSH(dhd->osh, pktbuf, BDC_HEADER_LEN);

	h = (struct bdc_header *)PKTDATA(dhd->osh, pktbuf);

	h->flags = (BDC_PROTO_VER << BDC_FLAG_VER_SHIFT);
	if (PKTSUMNEEDED(pktbuf))
		h->flags |= BDC_FLAG_SUM_NEEDED;


	h->priority = (PKTPRIO(pktbuf) & BDC_PRIORITY_MASK);
	h->flags2 = 0;
#ifdef APSTA_PINGTEST
	for (i = 0; i < MAX_GUEST; ++i) {
		if (!ETHER_ISNULLADDR(eh->ether_dhost) &&
		    bcmp(eh->ether_dhost, guest_eas[i].octet, ETHER_ADDR_LEN) == 0) {
			DHD_TRACE(("send on if 1; sa %s, da %s\n",
			       bcm_ether_ntoa((struct ether_addr *)(eh->ether_shost), eabuf1),
			       bcm_ether_ntoa((struct ether_addr *)(eh->ether_dhost), eabuf2)));
			/* assume all guest STAs are on interface 1 */
			h->flags2 = 1;
			break;
		}
	}
#endif /* APSTA_PINGTEST */
	h->rssi = 0;
#endif /* BDC */
	BDC_SET_IF_IDX(h, ifidx);
}


bool
dhd_proto_fcinfo(dhd_pub_t *dhd, void *pktbuf, uint8 *fcbits)
{
#ifdef BDC
	struct bdc_header *h;

	if (PKTLEN(dhd->osh, pktbuf) < BDC_HEADER_LEN) {
		DHD_ERROR(("%s: rx data too short (%d < %d)\n",
			__FUNCTION__, PKTLEN(dhd->osh, pktbuf), BDC_HEADER_LEN));
		return BCME_ERROR;
	}

	h = (struct bdc_header *)PKTDATA(dhd->osh, pktbuf);

	*fcbits = h->priority >> BDC_PRIORITY_FC_SHIFT;
	if ((h->flags2 & BDC_FLAG2_FC_FLAG) == BDC_FLAG2_FC_FLAG)
		return TRUE;
#endif
	return FALSE;
}


int
dhd_prot_hdrpull(dhd_pub_t *dhd, int *ifidx, void *pktbuf)
{
#ifdef BDC
	struct bdc_header *h;
#endif

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#ifdef BDC
	/* Pop BDC header used to convey priority for buses that don't */

	if (PKTLEN(dhd->osh, pktbuf) < BDC_HEADER_LEN) {
		DHD_ERROR(("%s: rx data too short (%d < %d)\n", __FUNCTION__,
		           PKTLEN(dhd->osh, pktbuf), BDC_HEADER_LEN));
		return BCME_ERROR;
	}

	h = (struct bdc_header *)PKTDATA(dhd->osh, pktbuf);

	if ((*ifidx = BDC_GET_IF_IDX(h)) >= DHD_MAX_IFS) {
		DHD_ERROR(("%s: rx data ifnum out of range (%d)\n",
		           __FUNCTION__, *ifidx));
		return BCME_ERROR;
	}

	if (((h->flags & BDC_FLAG_VER_MASK) >> BDC_FLAG_VER_SHIFT) != BDC_PROTO_VER) {
		DHD_ERROR(("%s: non-BDC packet received, flags 0x%x\n",
		           dhd_ifname(dhd, *ifidx), h->flags));
		return BCME_ERROR;
	}

	if (h->flags & BDC_FLAG_SUM_GOOD) {
		DHD_INFO(("%s: BDC packet received with good rx-csum, flags 0x%x\n",
		          dhd_ifname(dhd, *ifidx), h->flags));
		PKTSETSUMGOOD(pktbuf, TRUE);
	}

	PKTSETPRIO(pktbuf, (h->priority & BDC_PRIORITY_MASK));

	PKTPULL(dhd->osh, pktbuf, BDC_HEADER_LEN);
#endif /* BDC */

	return 0;
}

int
dhd_prot_attach(dhd_pub_t *dhd)
{
	dhd_prot_t *cdc;

#ifndef DHD_USE_STATIC_BUF
	if (!(cdc = (dhd_prot_t *)MALLOC(dhd->osh, sizeof(dhd_prot_t)))) {
		DHD_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
		goto fail;
	}
#else
	if (!(cdc = (dhd_prot_t *)dhd_os_prealloc(DHD_PREALLOC_PROT, sizeof(dhd_prot_t)))) {
		DHD_ERROR(("%s: kmalloc failed\n", __FUNCTION__));
		goto fail;
	}
#endif /* DHD_USE_STATIC_BUF */
	memset(cdc, 0, sizeof(dhd_prot_t));

	/* ensure that the msg buf directly follows the cdc msg struct */
	if ((uintptr)(&cdc->msg + 1) != (uintptr)cdc->buf) {
		DHD_ERROR(("dhd_prot_t is not correctly defined\n"));
		goto fail;
	}

	dhd->prot = cdc;
#ifdef BDC
	dhd->hdrlen += BDC_HEADER_LEN;
#endif
	dhd->maxctl = WLC_IOCTL_MAXLEN + sizeof(cdc_ioctl_t) + ROUND_UP_MARGIN;
	return 0;

fail:
#ifndef DHD_USE_STATIC_BUF
	if (cdc != NULL)
		MFREE(dhd->osh, cdc, sizeof(dhd_prot_t));
#endif
	return BCME_NOMEM;
}

/* ~NOTE~ What if another thread is waiting on the semaphore?  Holding it? */
void
dhd_prot_detach(dhd_pub_t *dhd)
{
#ifndef DHD_USE_STATIC_BUF
	MFREE(dhd->osh, dhd->prot, sizeof(dhd_prot_t));
#endif
	dhd->prot = NULL;
}

void
dhd_prot_dstats(dhd_pub_t *dhd)
{
	/* No stats from dongle added yet, copy bus stats */
	dhd->dstats.tx_packets = dhd->tx_packets;
	dhd->dstats.tx_errors = dhd->tx_errors;
	dhd->dstats.rx_packets = dhd->rx_packets;
	dhd->dstats.rx_errors = dhd->rx_errors;
	dhd->dstats.rx_dropped = dhd->rx_dropped;
	dhd->dstats.multicast = dhd->rx_multicast;
	return;
}
<<<<<<< HEAD:drivers/net/wireless/bcm4329/dhd_cdc.c
#define htod32(i) i
#define htod16(i) i

#ifdef WLAN_PFN
typedef struct pfn_ssid {
	char		ssid[32];			
	int32		ssid_len;		
	uint32		weight;
} pfn_ssid_t;

typedef struct pfn_ssid_set {
	pfn_ssid_t	pfn_ssids[MAX_PFN_NUMBER];
} pfn_ssid_set_t;

static pfn_ssid_set_t pfn_ssid_set;

int dhd_set_pfn_ssid(char * ssid, int ssid_len)
{
	uint32 i, lightest, weightest, samessid = 0xffff;

	if ((ssid_len < 1) || (ssid_len > 32)) {
		printk("Invaild ssid length!\n");
		return -1;
	}
	printk("pfn: set ssid = %s\n", ssid);
	lightest = 0;
	weightest =	0;
	for (i = 0; i < MAX_PFN_NUMBER; i++)
	{
		if (pfn_ssid_set.pfn_ssids[i].weight < pfn_ssid_set.pfn_ssids[lightest].weight)
			lightest = i;

		if (pfn_ssid_set.pfn_ssids[i].weight > pfn_ssid_set.pfn_ssids[weightest].weight)
			weightest = i;

		if (!strcmp(ssid, pfn_ssid_set.pfn_ssids[i].ssid))
			samessid = i;
	}

	printk("lightest is %d, weightest is %d, samessid = %d\n", lightest, weightest, samessid);

	if (samessid != 0xffff) {
		if (samessid == weightest) {
			printk("connect to latest ssid, ignore!\n");
			return 0;
		}
		pfn_ssid_set.pfn_ssids[samessid].weight = pfn_ssid_set.pfn_ssids[weightest].weight + 1;
		return 0;
	}
	memset(&pfn_ssid_set.pfn_ssids[lightest], 0, sizeof(pfn_ssid_t));

	strncpy(pfn_ssid_set.pfn_ssids[lightest].ssid, ssid, ssid_len);
	pfn_ssid_set.pfn_ssids[lightest].ssid_len = ssid_len;
	pfn_ssid_set.pfn_ssids[lightest].weight = pfn_ssid_set.pfn_ssids[weightest].weight + 1;
	
	return 0;
}

int dhd_del_pfn_ssid(char * ssid, int ssid_len)
{
	uint32 i;

	if ((ssid_len < 1) || (ssid_len > 32)) {
		printk("Invaild ssid length!\n");
		return -1;
	}
	
	for (i = 0; i < MAX_PFN_NUMBER; i++) {
		if (!strcmp(ssid, pfn_ssid_set.pfn_ssids[i].ssid))
			break;
	}

	if (i == MAX_PFN_NUMBER) {
		printk("del ssid [%s] not found!\n", ssid);
		return 0;
	}

	memset(&pfn_ssid_set.pfn_ssids[i], 0, sizeof(pfn_ssid_t));
	printk("del ssid [%s] complete!\n", ssid);

	return 0;
}

static int dhd_set_pfn(dhd_pub_t *dhd, int enabled)
{
	wl_pfn_param_t pfn_param;
	char iovbuf[64];
	int pfn_enabled = 0;
	wl_pfn_t	pfn_element;
	int i;
	int config_network = 0;
	/* Disable pfn */
	bcm_mkiovar("pfn", (char *)&pfn_enabled, 4, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	if (!enabled)
		return 0;
	
	/* clear pfn */
	bcm_mkiovar("pfnclear", NULL, 0, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	/* set pfn parameters */
	pfn_param.version = htod32(PFN_VERSION);
	pfn_param.flags = htod16((PFN_LIST_ORDER << SORT_CRITERIA_BIT));
	/* Scan frequency of 30 sec */
	pfn_param.scan_freq = htod32(PFN_SCAN_FREQ);
	/* RSSI margin of 30 dBm */
	pfn_param.rssi_margin = htod16(30);
	/* Network timeout 60 sec */
	pfn_param.lost_network_timeout = htod32(60);

	bcm_mkiovar("pfn_set", (char *)&pfn_param, sizeof(pfn_param), iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	/* set all pfn ssid */
	for (i = 0; i < MAX_PFN_NUMBER; i++) {
		if (pfn_ssid_set.pfn_ssids[i].ssid[0]) {
			pfn_element.bss_type = htod32(DOT11_BSSTYPE_INFRASTRUCTURE);
			pfn_element.auth = (DOT11_OPEN_SYSTEM);
			pfn_element.wpa_auth = htod32(WPA_AUTH_PFN_ANY);
			pfn_element.wsec = htod32(0);
			pfn_element.infra = htod32(1);

			strncpy((char *)pfn_element.ssid.SSID, pfn_ssid_set.pfn_ssids[i].ssid,
		        sizeof(pfn_element.ssid.SSID));
			pfn_element.ssid.SSID_len = pfn_ssid_set.pfn_ssids[i].ssid_len;

			bcm_mkiovar("pfn_add", (char *)&pfn_element, sizeof(pfn_element), iovbuf, sizeof(iovbuf));
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
			printk("add pfn: %s\n", pfn_ssid_set.pfn_ssids[i].ssid);
			config_network++;
		}
	}

	/* Enable pfn */
	if (config_network) {
		printk("enable pfn!\n");
		pfn_enabled=1;
		bcm_mkiovar("pfn", (char *)&pfn_enabled, 4, iovbuf, sizeof(iovbuf));
		dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
	}

	return 0;
	

}
#endif

void wl_iw_set_screen_off(int off);
static dhd_pub_t *pdhd = NULL;

int dhd_set_suspend(int value, dhd_pub_t *dhd)
{
	int power_mode = PM_MAX;
#if 0
	wl_pkt_filter_enable_t	enable_parm;
#endif
	char iovbuf[32];
	int bcn_li_dtim = 3;


	if (dhd && dhd->up) {
		dhd_os_proto_block(dhd);
		if (value) {
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_PM,
				(char *)&power_mode, sizeof(power_mode));
#if 0
			/* Enable packet filter, only allow unicast packet to send up */
			enable_parm.id = htod32(100);
			enable_parm.enable = htod32(1);
			bcm_mkiovar("pkt_filter_enable", (char *)&enable_parm,
				sizeof(wl_pkt_filter_enable_t), iovbuf, sizeof(iovbuf));
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
#endif
			/* set bcn_li_dtim */
			bcm_mkiovar("bcn_li_dtim", (char *)&bcn_li_dtim,
				4, iovbuf, sizeof(iovbuf));
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
#ifdef WLAN_PFN
			/* set pfn */
			dhd_set_pfn(dhd, 1);
#endif
			/* indicate wl_iw screen off */
			wl_iw_set_screen_off(1);
		} else {
			power_mode = PM_FAST;
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_PM, (char *)&power_mode,
				sizeof(power_mode));
#if 0
			/* disable pkt filter */
			enable_parm.id = htod32(100);
			enable_parm.enable = htod32(0);
			bcm_mkiovar("pkt_filter_enable", (char *)&enable_parm,
				sizeof(wl_pkt_filter_enable_t), iovbuf, sizeof(iovbuf));
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
#endif
			/* set bcn_li_dtim */
			bcn_li_dtim = 0;
			bcm_mkiovar("bcn_li_dtim", (char *)&bcn_li_dtim,
				4, iovbuf, sizeof(iovbuf));
			dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
#ifdef WLAN_PFN
				dhd_set_pfn(dhd, 0);
#endif
			/* indicate wl_iw screen on */
			wl_iw_set_screen_off(0);
		}
		dhd_os_proto_unblock(dhd);
	}

	return 0;
}

#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))

/* Convert user's input in hex pattern to byte-size mask */
static int
wl_pattern_atoh(char *src, char *dst)
{
	int i;
	if (strncmp(src, "0x", 2) != 0 &&
	    strncmp(src, "0X", 2) != 0) {
		printf("Mask invalid format. Needs to start with 0x\n");
		return -1;
	}
	src = src + 2; /* Skip past 0x */
	if (strlen(src) % 2 != 0) {
		printf("Mask invalid format. Needs to be of even length\n");
		return -1;
	}
	for (i = 0; *src != '\0'; i++) {
		char num[3];
		strncpy(num, src, 2);
		num[2] = '\0';
		dst[i] = (uint8)strtoul(num, NULL, 16);
		src += 2;
	}
	return i;
}

int dhd_set_pktfilter(int add, int id, int offset, char *mask, char *pattern)
{
	char 				*str;
	wl_pkt_filter_t		pkt_filter;
	wl_pkt_filter_t		*pkt_filterp;
	int						buf_len;
	int						str_len;
	uint32					mask_size;
	uint32					pattern_size;
	char buf[256];
	int pkt_id = id;
	dhd_pub_t *dhd = pdhd;
	wl_pkt_filter_enable_t	enable_parm;

	printk("Enter set packet filter\n");
	if (!pdhd)
		return -1;

	/* disable pkt filter */
	enable_parm.id = htod32(pkt_id);
	enable_parm.enable = htod32(0);
	bcm_mkiovar("pkt_filter_enable", (char *)&enable_parm,
		sizeof(wl_pkt_filter_enable_t), buf, sizeof(buf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, buf, sizeof(buf));

	/* delete it */
	bcm_mkiovar("pkt_filter_delete", (char *)&pkt_id, 4, buf, sizeof(buf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, buf, sizeof(buf));

	if (!add) {
		return 0;
	}

	printk("start to add pkt filter %d\n", pkt_id);
	memset(buf, 0, sizeof(buf));
	/* add a packet filter pattern */
	str = "pkt_filter_add";
	str_len = strlen(str);
	strncpy(buf, str, str_len);
	buf[ str_len ] = '\0';
	buf_len = str_len + 1;

	pkt_filterp = (wl_pkt_filter_t *) (buf + str_len + 1);

	/* Parse packet filter id. */
	pkt_filter.id = htod32(pkt_id);

	/* Parse filter polarity. */
	pkt_filter.negate_match = htod32(0);

	/* Parse filter type. */
	pkt_filter.type = htod32(0);

	/* Parse pattern filter offset. */
	pkt_filter.u.pattern.offset = htod32(offset);

	/* Parse pattern filter mask. */
	mask_size =	htod32(wl_pattern_atoh(mask,
		(char *) pkt_filterp->u.pattern.mask_and_pattern));

	/* Parse pattern filter pattern. */
	pattern_size = htod32(wl_pattern_atoh(pattern,
		(char *) &pkt_filterp->u.pattern.mask_and_pattern[mask_size]));

	if (mask_size != pattern_size) {
		printk("Mask and pattern not the same size\n");
		return -EINVAL;
	}

	pkt_filter.u.pattern.size_bytes = mask_size;
	buf_len += WL_PKT_FILTER_FIXED_LEN;
	buf_len += (WL_PKT_FILTER_PATTERN_FIXED_LEN + 2 * mask_size);

	memcpy((char *)pkt_filterp, &pkt_filter,
		WL_PKT_FILTER_FIXED_LEN + WL_PKT_FILTER_PATTERN_FIXED_LEN);

	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, buf, buf_len);

	enable_parm.id = htod32(pkt_id);
	enable_parm.enable = htod32(1);
	bcm_mkiovar("pkt_filter_enable", (char *)&enable_parm,
		sizeof(wl_pkt_filter_enable_t), buf, sizeof(buf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, buf, sizeof(buf));

	return 0;
}
=======
>>>>>>> master:drivers/net/wireless/bcm4329/dhd_cdc.c

int
dhd_prot_init(dhd_pub_t *dhd)
{
	int ret = 0;
	char buf[128];

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	dhd_os_proto_block(dhd);
<<<<<<< HEAD:drivers/net/wireless/bcm4329/dhd_cdc.c
#ifdef WLAN_PFN
	/* init pfn data */
	memset(&pfn_ssid_set, 0, sizeof(pfn_ssid_set_t));
#endif
	/* Get the device MAC address */
	strcpy(iovbuf, "cur_etheraddr");
	if ((ret = dhdcdc_query_ioctl(dhd, 0, WLC_GET_VAR, iovbuf, sizeof(iovbuf))) < 0) {
		DHD_ERROR(("%s: can't get MAC address , error=%d\n", __FUNCTION__, ret));
		dhd_os_proto_unblock(dhd);
		return -BCME_NOTUP;
	}
	memcpy(dhd->mac.octet, iovbuf, ETHER_ADDR_LEN);

	/* Set Country code */
	if (dhd->country_code[0] != 0) {
		if (dhdcdc_set_ioctl(dhd, 0, WLC_SET_COUNTRY,
			dhd->country_code, sizeof(dhd->country_code)) < 0) {
			DHD_ERROR(("%s: country code setting failed\n", __FUNCTION__));
		}
	}

	/* Set PowerSave mode */
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_PM, (char *)&power_mode, sizeof(power_mode));

	/* Match Host and Dongle rx alignment */
	bcm_mkiovar("bus:txglomalign", (char *)&dongle_align, 4, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	/* disable glom option per default */
	bcm_mkiovar("bus:txglom", (char *)&glom, 4, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
	/* Setup timeout if Beacons are lost and roam is off to report link down */
	/* if (roamvar) */ {
		bcm_mkiovar("bcn_timeout", (char *)&bcn_timeout, 4, iovbuf, sizeof(iovbuf));
		dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
	}

	/* Enable/Disable build-in roaming to allowed ext supplicant to take of romaing */
	bcm_mkiovar("roam_off", (char *)&roamvar, 4, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	if(!wifi_get_dot11n_enable()) {
		/* Disable nmode as default */
		bcm_mkiovar("nmode", (char *)&nmode, 4, iovbuf, sizeof(iovbuf));
		dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));
		printk("wifi: Disable 802.11n\n");
	}
	/* Force STA UP */
	dhdcdc_set_ioctl(dhd, 0, WLC_UP, (char *)&up, sizeof(up));

	/* Setup event_msgs */
	bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
	dhdcdc_query_ioctl(dhd, 0, WLC_GET_VAR, iovbuf, sizeof(iovbuf));
	bcopy(iovbuf, eventmask, WL_EVENTING_MASK_LEN);

	setbit(eventmask, WLC_E_SET_SSID);
	setbit(eventmask, WLC_E_PRUNE);
	setbit(eventmask, WLC_E_AUTH);
	setbit(eventmask, WLC_E_REASSOC);
	setbit(eventmask, WLC_E_REASSOC_IND);
	setbit(eventmask, WLC_E_DEAUTH_IND);
	setbit(eventmask, WLC_E_DISASSOC_IND);
	setbit(eventmask, WLC_E_DISASSOC);
	setbit(eventmask, WLC_E_JOIN);
	setbit(eventmask, WLC_E_ASSOC_IND);
	setbit(eventmask, WLC_E_PSK_SUP);
	setbit(eventmask, WLC_E_LINK);
	setbit(eventmask, WLC_E_NDIS_LINK);
	setbit(eventmask, WLC_E_MIC_ERROR);
	setbit(eventmask, WLC_E_PMKID_CACHE);
	setbit(eventmask, WLC_E_TXFAIL);
	setbit(eventmask, WLC_E_JOIN_START);
	setbit(eventmask, WLC_E_SCAN_COMPLETE);
	setbit(eventmask, WLC_E_ASSOCREQ_IE);
#ifdef WLAN_PFN
	setbit(eventmask, WLC_E_PFN_NET_FOUND);
#endif

	bcm_mkiovar("event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	dhdcdc_set_ioctl(dhd, 0, WLC_SET_SCAN_CHANNEL_TIME, (char *)&scan_assoc_time,
		sizeof(scan_assoc_time));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_SCAN_UNASSOC_TIME, (char *)&scan_unassoc_time,
		sizeof(scan_unassoc_time));
=======
>>>>>>> master:drivers/net/wireless/bcm4329/dhd_cdc.c

	/* Get the device MAC address */
	strcpy(buf, "cur_etheraddr");
	ret = dhdcdc_query_ioctl(dhd, 0, WLC_GET_VAR, buf, sizeof(buf));
	if (ret < 0) {
		dhd_os_proto_unblock(dhd);
<<<<<<< HEAD:drivers/net/wireless/bcm4329/dhd_cdc.c
		return -EINVAL;
	}

	pkt_filter.u.pattern.size_bytes = mask_size;
	buf_len += WL_PKT_FILTER_FIXED_LEN;
	buf_len += (WL_PKT_FILTER_PATTERN_FIXED_LEN + 2 * mask_size);

	/* Keep-alive attributes are set in local	variable (keep_alive_pkt), and
	** then memcpy'ed into buffer (keep_alive_pktp) since there is no
	** guarantee that the buffer is properly aligned.
	*/
	memcpy((char *)pkt_filterp, &pkt_filter,
		WL_PKT_FILTER_FIXED_LEN + WL_PKT_FILTER_PATTERN_FIXED_LEN);

	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, buf, buf_len);
#endif
	/* put mac address in */
	sprintf( mac_buf, "0x%02x%02x%02x%02x%02x%02x",
	dhd->mac.octet[0], dhd->mac.octet[1], dhd->mac.octet[2],
	dhd->mac.octet[3], dhd->mac.octet[4], dhd->mac.octet[5]
	);

#ifndef CONFIG_MACH_PASSIONC
	/* add a default packet filter pattern */
	dhd_set_pktfilter(1, ALLOW_UNICAST, 0, "0xffffffffffff", mac_buf);
	dhd_set_pktfilter(1, ALLOW_DHCP, 0, "0xffffffffffff000000000000ffff00000000000000000000000000000000000000000000ffff", "0xffffffffffff0000000000000800000000000000000000000000000000000000000000000044");
#endif

	/* set mode to allow pattern */
	bcm_mkiovar("pkt_filter_mode", (char *)&filter_mode, 4, iovbuf, sizeof(iovbuf));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_VAR, iovbuf, sizeof(iovbuf));

	{ /* set roaming parameters */
	int roam_scan_period = 30;
	int roam_trigger = -80;
	int roam_delta = 10;

	dhdcdc_set_ioctl(dhd, 0, WLC_SET_ROAM_SCAN_PERIOD, (char *)&roam_scan_period, sizeof(roam_scan_period));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_ROAM_DELTA, (char *)&roam_delta, sizeof(roam_delta));
	dhdcdc_set_ioctl(dhd, 0, WLC_SET_ROAM_TRIGGER, (char *)&roam_trigger, sizeof(roam_trigger));
=======
		return ret;
>>>>>>> master:drivers/net/wireless/bcm4329/dhd_cdc.c
	}
	memcpy(dhd->mac.octet, buf, ETHER_ADDR_LEN);

	dhd_os_proto_unblock(dhd);

#ifdef EMBEDDED_PLATFORM
	ret = dhd_preinit_ioctls(dhd);
#endif /* EMBEDDED_PLATFORM */

	/* Always assumes wl for now */
	dhd->iswl = TRUE;

	return ret;
}

void
dhd_prot_stop(dhd_pub_t *dhd)
{
	/* Nothing to do for CDC */
}
