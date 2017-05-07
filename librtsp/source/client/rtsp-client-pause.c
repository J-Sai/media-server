// RFC-2326 10.6 PAUSE (p36)
// 1. A PAUSE request discards all queued PLAY requests. However, the pause
//    point in the media stream MUST be maintained. A subsequent PLAY
//    request without Range header resumes from the pause point. (p36) 
// 2. The PAUSE request may contain a Range header specifying when the
//    stream or presentation is to be halted. (p36) (p45 for more)

/*
C->S: 
PAUSE rtsp://example.com/fizzle/foo RTSP/1.0
CSeq: 834
Session: 12345678

S->C: 
RTSP/1.0 200 OK
CSeq: 834
Date: 23 Jan 1997 15:35:06 GMT
*/

#include "rtsp-client.h"
#include "rtsp-client-internal.h"

static const char* sc_format = 
	"PAUSE %s RTSP/1.0\r\n"
	"CSeq: %u\r\n"
	"Session: %s\r\n"
	"User-Agent: %s\r\n"
	"\r\n";

static int rtsp_client_media_pause(struct rtsp_client_t *rtsp)
{
	int r;
	struct rtsp_media_t* media;

	assert(0 == rtsp->aggregate);
	assert(RTSP_PAUSE == rtsp->state);
	assert(rtsp->progress < rtsp->media_count);

	media = rtsp_get_media(rtsp, rtsp->progress);
	assert(media && media->uri && media->session.session[0]);
	r = snprintf(rtsp->req, sizeof(rtsp->req), sc_format, media->uri, media->cseq++, media->session.session, USER_AGENT);
	assert(r > 0 && r < sizeof(rtsp->req));
	return r == rtsp->handler.send(rtsp->param, media->uri, rtsp->req, r) ? 0 : -1;
}

int rtsp_client_pause(void* p)
{
	int r;
	struct rtsp_client_t *rtsp;
	rtsp = (struct rtsp_client_t*)p;

	assert(RTSP_SETUP == rtsp->state || RTSP_PLAY == rtsp->state || RTSP_PAUSE == rtsp->state);
	rtsp->state = RTSP_PAUSE;
	rtsp->progress = 0;

	if(rtsp->aggregate)
	{
		assert(rtsp->media_count > 0);
		assert(rtsp->aggregate_uri[0]);
		r = snprintf(rtsp->req, sizeof(rtsp->req), sc_format, rtsp->aggregate_uri, rtsp->cseq++, rtsp->media[0].session.session, USER_AGENT);
		assert(r > 0 && r < sizeof(rtsp->req));
		return r == rtsp->handler.send(rtsp->param, rtsp->aggregate_uri, rtsp->req, r) ? 0 : -1;
	}
	else
	{
		return rtsp_client_media_pause(rtsp);
	}
}

static int rtsp_client_media_pause_onreply(struct rtsp_client_t* rtsp, void* parser)
{
	int code;
	assert(rtsp->progress < rtsp->media_count);

	code = rtsp_get_status_code(parser);
	assert(460 != code); // 460 Only aggregate operation allowed (p26)
	if (200 == code)
	{
		if (rtsp->media_count == ++rtsp->progress)
		{
			rtsp->handler.onpause(rtsp->param);
			return 0;
		}
		else
		{
			return rtsp_client_media_pause(rtsp);
		}
	}
	else
	{
		return -1;
	}
}

// aggregate control reply
static int rtsp_client_aggregate_pause_onreply(struct rtsp_client_t* rtsp, void* parser)
{
	int code;

	assert(rtsp->aggregate);
	code = rtsp_get_status_code(parser);
	if (459 == code) // 459 Aggregate operation not allowed (p26)
	{
		return rtsp_client_media_pause(rtsp);
	}
	else if(200 == code)
	{
		rtsp->handler.onpause(rtsp->param);
		return 0;
	}

	return -1;
}

int rtsp_client_pause_onreply(struct rtsp_client_t* rtsp, void* parser)
{
	assert(RTSP_PAUSE == rtsp->state);
	assert(rtsp->progress < rtsp->media_count);

	if (rtsp->aggregate)
		return rtsp_client_aggregate_pause_onreply(rtsp, parser);
	else
		return rtsp_client_media_pause_onreply(rtsp, parser);
}
