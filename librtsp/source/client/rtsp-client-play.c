// RFC-2326 10.5 PLAY (p34)
// 1. A PLAY request without a Range header is legal. It starts playing a
//    stream from the beginning unless the stream has been paused. If a
//	  stream has been paused via PAUSE, stream delivery resumes at the pause point.
// 2. If a stream is playing, such a PLAY request causes no
//    further action and can be used by the client to test server liveness.

/*
PLAY rtsp://audio.example.com/audio RTSP/1.0
CSeq: 835
Session: 12345678
Range: npt=10-15

C->S: 
PLAY rtsp://audio.example.com/twister.en RTSP/1.0
CSeq: 833
Session: 12345678
Range: smpte=0:10:20-;time=19970123T153600Z

S->C: 
RTSP/1.0 200 OK
CSeq: 833
Date: 23 Jan 1997 15:35:06 GMT
Range: smpte=0:10:22-;time=19970123T153600Z

C->S: 
PLAY rtsp://audio.example.com/meeting.en RTSP/1.0
CSeq: 835
Session: 12345678
Range: clock=19961108T142300Z-19961108T143520Z

S->C: 
RTSP/1.0 200 OK
CSeq: 835
Date: 23 Jan 1997 15:35:06 GMT
*/

#include "rtsp-client.h"
#include "rtsp-client-internal.h"
#include "rtsp-header-range.h"
#include "rtsp-header-rtp-info.h"
#include <assert.h>

static const char* sc_format = 
	"PLAY %s RTSP/1.0\r\n"
	"CSeq: %u\r\n"
	"Session: %s\r\n"
	"%s" // Range
	"%s" // Speed
	"User-Agent: %s\r\n"
	"\r\n";

static int rtsp_client_media_play(struct rtsp_client_t *rtsp)
{
	int r;
	struct rtsp_media_t* media;

	assert(0 == rtsp->aggregate);
	assert(RTSP_PLAY == rtsp->state);
	assert(rtsp->progress < rtsp->media_count);

	media = rtsp_get_media(rtsp, rtsp->progress);
	assert(media && media->uri && media->session.session[0]);
	r = snprintf(rtsp->req, sizeof(rtsp->req), sc_format, media->uri, media->cseq++, media->session.session, rtsp->range, rtsp->speed, USER_AGENT);
	assert(r > 0 && r < sizeof(rtsp->req));
	return r == rtsp->handler.send(rtsp->param, media->uri, rtsp->req, r) ? 0 : -1;
}

int rtsp_client_play(void* p, const uint64_t *npt, const float *speed)
{
	int r;
	struct rtsp_client_t *rtsp;
	rtsp = (struct rtsp_client_t*)p;
	rtsp->state = RTSP_PLAY;
	rtsp->progress = 0;

	r = snprintf(rtsp->range, sizeof(rtsp->range), npt ? "Range: npt=%" PRIu64 ".%" PRIu64 "-\r\n" : "", npt ? *npt/1000 : 0, npt ? *npt%1000 : 0);
	r = snprintf(rtsp->speed, sizeof(rtsp->speed), speed ? "Speed: %f\r\n" : "", speed ? *speed : 0.0f);
	
	if(rtsp->aggregate)
	{
		assert(rtsp->media_count > 0);
		assert(rtsp->aggregate_uri[0]);
		r = snprintf(rtsp->req, sizeof(rtsp->req), sc_format, rtsp->aggregate_uri, rtsp->cseq++, rtsp->media[0].session.session, rtsp->range, rtsp->speed, USER_AGENT);
		assert(r > 0 && r < sizeof(rtsp->req));
		return r == rtsp->handler.send(rtsp->param, rtsp->aggregate_uri, rtsp->req, r) ? 0 : -1;
	}
	else
	{
		return rtsp_client_media_play(rtsp);
	}
}

static int rtsp_client_media_play_onreply(struct rtsp_client_t* rtsp, void* parser)
{
	int i;
	uint64_t npt0 = (uint64_t)(-1);
	uint64_t npt1 = (uint64_t)(-1);
	double scale = 0.0f;
	const char *prange, *pscale, *prtpinfo;
	struct rtsp_header_range_t range;
	struct rtsp_header_rtp_info_t rtpinfo[N_MEDIA];
	struct rtsp_rtp_info_t rtpInfo[N_MEDIA];

	if (200 != rtsp_get_status_code(parser))
		return -1;

	prange = rtsp_get_header_by_name(parser, "Range");
	pscale = rtsp_get_header_by_name(parser, "Scale");
	prtpinfo = rtsp_get_header_by_name(parser, "RTP-Info");

	if (pscale)
	{
		scale = atof(pscale);
	}

	if (prange && 0 == rtsp_header_range(prange, &range))
	{
		assert(range.from_value == RTSP_RANGE_TIME_NORMAL);
		assert(range.to_value != RTSP_RANGE_TIME_NOW);
		npt0 = range.from;
		npt1 = range.to_value == RTSP_RANGE_TIME_NOVALUE ? -1 : range.to;
	}

	memset(rtpInfo, 0, sizeof(rtpInfo));
	for (i = 0; prtpinfo && i < sizeof(rtpInfo) / sizeof(rtpInfo[0]); i++)
	{
		const char* p1 = strchr(prtpinfo, ',');
		if (0 == rtsp_header_rtp_info(prtpinfo, &rtpinfo[i]))
		{
			rtpInfo[i].uri = rtpinfo[i].url;
			rtpInfo[i].seq = (unsigned int)rtpinfo[i].seq;
			rtpInfo[i].time = (unsigned int)rtpinfo[i].rtptime;
		}
		prtpinfo = p1 ? p1 + 1 : p1;
	}

	rtsp->handler.onplay(rtsp->param, rtsp->progress, (uint64_t)(-1) == npt0 ? NULL : &npt0, (uint64_t)(-1) == npt1 ? NULL : &npt1, pscale ? &scale : NULL, rtpInfo, i);

	if(rtsp->media_count > ++rtsp->progress)
	{
		return rtsp_client_media_play(rtsp);
	}
	return 0;
}

// aggregate control reply
static int rtsp_client_aggregate_play_onreply(struct rtsp_client_t* rtsp, void* parser)
{
	int code;
	assert(RTSP_PLAY == rtsp->state);
	assert(0 == rtsp->progress);
	assert(rtsp->aggregate);
	
	code = rtsp_get_status_code(parser);
	if (459 == code) // 459 Aggregate operation not allowed (p26)
	{
		return rtsp_client_media_play(rtsp);
	}
	else if (200 == code)
	{
		return rtsp_client_media_play_onreply(rtsp, parser);
	}
	else
	{
		return -1;
	}
}

int rtsp_client_play_onreply(struct rtsp_client_t* rtsp, void* parser)
{
	assert(RTSP_PLAY == rtsp->state);
	assert(rtsp->progress < rtsp->media_count);

	if (rtsp->aggregate)
		return rtsp_client_aggregate_play_onreply(rtsp, parser);
	else
		return rtsp_client_media_play_onreply(rtsp, parser);
}
