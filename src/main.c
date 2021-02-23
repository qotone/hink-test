#include "hi_comm_venc.h"
#include "hi_comm_video.h"
#include "hi_common.h"
#include "hi_math.h"
#include "hi_type.h"
#include "hink_comm.h"
#include "hink_venc.h"
#include "venc.h"
#include "mpp.h"
#include "hink_sys.h"
#include "hink_vi.h"
#include "hink_vpss.h"


#include <asm-generic/errno-base.h>
#include <stddef.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

#include <futils/futils.h>
#include <h264/h264.h>
#include <h265/h265.h>
#include <libpomp.h>
#include <video-streaming/vstrm.h>
#include "vstrm_test_socket.h"

#define ULOG_TAG vstrm_sender_hisi
#include <ulog.h>
ULOG_DECLARE_TAG(vstrm_sender_hisi);

#define DEFAULT_TARGET_PACKET_SIZE 1500
#define MAX_BITRATE 5000000
#define MAX_LATENCY_MS 200
#define MAX_NETWORK_LATENCY_CLASS_0 250
#define MAX_NETWORK_LATENCY_CLASS_1 200
#define MAX_NETWORK_LATENCY_CLASS_2 150
#define MAX_NETWORK_LATENCY_CLASS_3 100



#define DEFAULT_RCVBUF_SIZE 4096
#define DEFAULT_SNDBUF_SIZE 4096
#define DEFAULT_RX_BUFFER_SIZE 65536
#define TOS_CS4 0x80

#define CODEC_STRM_NUM  (2)
#define CODEC_VENC_NUM  (2)
#define CODEC_IPC_CHN   (4)


static mpp_vpss_t stVpss[CODEC_IPC_CHN];

static void nalu_end_cb(struct h264_ctx *ctx,enum h264_nalu_type type,const uint8_t *buf,size_t len,void *userdata);

static void au_end_cb(struct h264_ctx *ctx, void *userdata);
static void goodbye_cb(struct vstrm_sender *stream, const char *reason, void *userdata);

static void video_stats_cb(struct vstrm_sender *stream,const struct vstrm_video_stats *video_stats,const struct vstrm_video_stats_dyn *video_stats_dyn,void *userdata);

static void session_metadata_peer_changed_cb(struct vstrm_sender *stream,const struct vmeta_session *meta,void *userdata);

static int monitor_send_data_ready_cb(struct vstrm_sender *stream,int enable,void *userdata);

static int send_ctrl_cb(struct vstrm_sender *stream,struct pomp_buffer *buf,void *userdata);

static int send_data_cb(struct vstrm_sender *stream,struct pomp_buffer *buf,void *userdata);

struct vstrm_sender_hisi {
    int should_stop;
    struct pomp_loop *loop;
    void *data;
    size_t data_len;
    size_t data_off;
    uint8_t *sps;
    size_t sps_len;
    uint8_t *pps;
    size_t pps_len;
    struct h265_reader *reader;

    struct vstrm_sender *sender;
    struct vstrm_test_socket data_sock;
    struct vstrm_test_socket ctrl_sock;
    struct vstrm_frame *frame;
    uint64_t timestamp;
    void *userdata;

};

static struct vstrm_sender_hisi *s_self = NULL;

mpp_venc_ini_t  venc_ini ={
    .ch_num = 2,
    .st_num = 2
};



/* hink_recv_t st = { */
/*     .s32Cnt = 2, */
/*     .veChn = {0,1}, */
/*     .uargs = NULL, */
/*     .cb = mpp_venc_recv, */
/* }; */
hink_recv_t *pst;




static const struct h264_ctx_cbs h264_cbs = {
    .au_end = &au_end_cb,
    .nalu_end = &nalu_end_cb,
};


static struct vstrm_sender_cbs vstrm_cbs = {
	.send_data = &send_data_cb,
	.send_ctrl = &send_ctrl_cb,
	.monitor_send_data_ready = &monitor_send_data_ready_cb,
	.session_metadata_peer_changed = &session_metadata_peer_changed_cb,
	.video_stats = &video_stats_cb,
	.goodbye = &goodbye_cb,
};


static void frame_dispose(struct vstrm_frame *frame)
{
	return;
}

static void socket_data_cb(int fd, uint32_t events, void *userdata)
{
	int res = 0;
	struct vstrm_sender_hisi *self = userdata;
	ssize_t readlen = 0;

	ULOG_ERRNO_RETURN_IF(self == NULL, EINVAL);

	if ((events & POMP_FD_EVENT_OUT) != 0) {
		/* Notify sender */

		res = vstrm_sender_notify_send_data_ready(self->sender);
		if (res < 0)
			ULOG_ERRNO("vstrm_sender_notify_send_data_ready", -res);
	}

	if ((events & POMP_FD_EVENT_IN) != 0) {
		do {
			/* Read data (and trash them...) */
			readlen = vstrm_test_socket_read(&self->data_sock);
		} while (readlen > 0);
	}
}


static void socket_ctrl_cb(int fd, uint32_t events, void *userdata)
{
	int res = 0;
	struct vstrm_sender_hisi *self = userdata;
	ssize_t readlen = 0;
	struct pomp_buffer *buf = NULL;
	struct timespec ts = {0, 0};

	ULOG_ERRNO_RETURN_IF(self == NULL, EINVAL);

	do {
		/* Read data */
		readlen = vstrm_test_socket_read(&self->ctrl_sock);

		/* Something read ? */
		if (readlen > 0) {
			/* TODO: Avoid copy */
			buf = pomp_buffer_new_with_data(self->ctrl_sock.rxbuf,
                                            readlen);
			res = time_get_monotonic(&ts);
			if (res < 0)
				ULOG_ERRNO("time_get_monotonic", -res);
			res = vstrm_sender_recv_ctrl(self->sender, buf, &ts);
			pomp_buffer_unref(buf);
			buf = NULL;
			if (res < 0)
				ULOG_ERRNO("vstrm_sender_recv_ctrl", -res);
		} else if (readlen == 0) {
			/* TODO: EOF */
		}
	} while (readlen > 0);
}


static int send_data_cb(struct vstrm_sender *stream,
                        struct pomp_buffer *buf,
			void *userdata)
{
	struct vstrm_sender_hisi *self = userdata;
	const void *cdata = NULL;
	size_t len = 0;
	ssize_t writelen = 0;

	ULOG_ERRNO_RETURN_ERR_IF(self == NULL, EINVAL);
	ULOG_ERRNO_RETURN_ERR_IF(buf == NULL, EINVAL);

	/* Write data */
	pomp_buffer_get_cdata(buf, &cdata, &len, NULL);
	writelen = vstrm_test_socket_write(&self->data_sock, cdata, len);
	return writelen >= 0 ? 0 : (int)writelen;
}


static int send_ctrl_cb(struct vstrm_sender *stream,
			struct pomp_buffer *buf,
			void *userdata)
{
	struct vstrm_sender_hisi *self = userdata;
	const void *cdata = NULL;
	size_t len = 0;
	ssize_t writelen = 0;

	ULOG_ERRNO_RETURN_ERR_IF(self == NULL, EINVAL);

	/* Write data */
	pomp_buffer_get_cdata(buf, &cdata, &len, NULL);
	writelen = vstrm_test_socket_write(&self->ctrl_sock, cdata, len);
	return writelen >= 0 ? 0 : (int)writelen;
}


static int monitor_send_data_ready_cb(struct vstrm_sender *stream,
				      int enable,
				      void *userdata)
{
	struct vstrm_sender_hisi *self = userdata;
	uint32_t events = POMP_FD_EVENT_IN | (enable ? POMP_FD_EVENT_OUT : 0);

	ULOG_ERRNO_RETURN_ERR_IF(self == NULL, EINVAL);

	return pomp_loop_update(self->loop, self->data_sock.fd, events);
}


static void session_metadata_peer_changed_cb(struct vstrm_sender *stream,
					     const struct vmeta_session *meta,
					     void *userdata)
{
	ULOGI("%s", __func__);
}


static void video_stats_cb(struct vstrm_sender *stream,
			   const struct vstrm_video_stats *video_stats,
			   const struct vstrm_video_stats_dyn *video_stats_dyn,
			   void *userdata)
{
	ULOGI("%s", __func__);
}


static void goodbye_cb(struct vstrm_sender *stream, const char *reason, void *userdata)
{
	ULOGI("%s", __func__);
}


static void au_end_cb(struct h264_ctx *ctx, void *userdata)
{
	int res;
	struct vstrm_sender_hisi *self = userdata;

	ULOG_ERRNO_RETURN_IF(self == NULL, EINVAL);
	ULOG_ERRNO_RETURN_IF(ctx == NULL, EINVAL);

	if (self->frame != NULL) {
		res = vstrm_sender_send_frame(self->sender, self->frame);
		if (res < 0)
			ULOG_ERRNO("vstrm_sender_send_frame", -res);
		vstrm_frame_unref(self->frame);
		self->frame = NULL;
		res = h264_reader_stop(self->reader);
		if (res < 0)
			ULOG_ERRNO("h264_reader_stop", -res);
	}
}


static void nalu_end_cb(struct h264_ctx *ctx,
                        enum h264_nalu_type type,
                        const uint8_t *buf,
                        size_t len,
                        void *userdata)
{
	int res;
	struct vstrm_sender_hisi *self = userdata;
	struct vstrm_frame_nalu nalu;

	ULOG_ERRNO_RETURN_IF(self == NULL, EINVAL);
	ULOG_ERRNO_RETURN_IF(buf == NULL, EINVAL);

	/* Save the SPS and PPS */
	if ((type == H264_NALU_TYPE_SPS) && (self->sps == NULL)) {
		self->sps_len = len;
		self->sps = malloc(self->sps_len);
		if (self->sps == NULL) {
			ULOG_ERRNO("malloc", ENOMEM);
			return;
		}
		memcpy(self->sps, buf, len);
		ULOGI("SPS found");
	} else if ((type == H264_NALU_TYPE_PPS) && (self->pps == NULL)) {
		self->pps_len = len;
		self->pps = malloc(self->pps_len);
		if (self->pps == NULL) {
			ULOG_ERRNO("malloc", ENOMEM);
			return;
		}
		memcpy(self->pps, buf, len);
		ULOGI("PPS found");
	}

	/* Get a new frame if needed */
	if (self->frame == NULL) {
		struct vstrm_frame_ops ops;
		ops.dispose = &frame_dispose;
		res = vstrm_frame_new(&ops, 0, &self->frame);
		if (res < 0) {
			ULOG_ERRNO("vstrm_frame_new", -res);
			return;
		}
		self->timestamp += (1000000 / 25);//self->frame_interval_us;
		self->frame->timestamps.ntp = self->timestamp;
	}

	/* Add the NALU to the frame */
	memset(&nalu, 0, sizeof(nalu));
	nalu.cdata = buf;
	nalu.len = len;
	res = vstrm_frame_add_nalu(self->frame, &nalu);
	if (res < 0) {
		ULOG_ERRNO("vstrm_frame_add_nalu", -res);
		return;
	}
}


/**
 * Computes the data socket tx size based on bitrate and maximum latency
 * This way the kernel will not bufferize too much and allow us to detect
 * congestion and do some preventive drops.
 * @max_bitrate: max expected bitrate in bits/s
 * @max_latency_ms: max desired latency in ms.
 */
static uint32_t get_socket_data_tx_size(uint32_t max_bitrate,
					uint32_t max_latency_ms)
{
	uint32_t total_size = max_bitrate * max_latency_ms / 1000 / 8;
	uint32_t min_size = max_bitrate * 50 / 1000 / 8;
	uint32_t size = total_size / 4;
	return size > min_size ? size : min_size;
}



static void sig_handler(int signum)
{
	ULOGI("signal %d(%s) received", signum, strsignal(signum));

	if (s_self == NULL)
		return;

	s_self->should_stop = 1;
	if (s_self->loop != NULL)
		pomp_loop_wakeup(s_self->loop);
}


int main(int argc, char *argv[])
{

    int i,j;

    HI_S32 s32Ret  = HI_SUCCESS;
    HI_U32 u32BlkSize = 1920 * 1080 * 2;
    VIDEO_NORM_E enNorm = VIDEO_ENCODING_MODE_AUTO;
    VI_DEV  viDev = 6;
    VI_CHN viChn;
    VI_PARAM_S viParam;
    MPP_CHN_S stSrcChn;
    MPP_CHN_S stDestChn;




    struct vstrm_sender_cfg vstrm_cfg;
    uint32_t tx_size;
    int res;
    char *local_addr="192.168.0.223";
    char *remote_addr = "192.168.0.119";
    uint16_t remote_data_port  = 55004;
    uint16_t remote_ctrl_port = 55005;
    uint16_t local_data_port = 0;
    uint16_t local_ctrl_port = 0;

// ...

    /* Setup signal handlers */
	signal(SIGINT, &sig_handler);
	signal(SIGTERM, &sig_handler);
	signal(SIGPIPE, SIG_IGN);


    s_self = calloc(1,sizeof(*s_self));
    if(s_self == NULL){
        ULOG_ERRNO("calloc", ENOMEM);
        return -ENOMEM;
    }

    s_self->loop = pomp_loop_new();
    if(s_self->loop == NULL){
        ULOG_ERRNO("pomp_loop_new", ENOMEM);
        res = -ENOMEM;
        goto out;
    }

    res = h264_reader_new(&h264_cbs,s_self,&s_self->reader);
    if(res < 0){
        ULOG_ERRNO("h264_reader_new", -res);
        goto out;
    }

/* Setup data socket */
    res = vstrm_test_socket_setup(&s_self->data_sock, local_addr, &local_data_port, remote_addr, remote_data_port, s_self->loop, &socket_data_cb, s_self);
    if(res < 0)
        goto out;

    ULOGI("RTP:address %s->%s port %u->%u",local_addr,remote_addr,local_data_port,remote_data_port);

    tx_size = get_socket_data_tx_size(MAX_BITRATE,MAX_LATENCY_MS);
    vstrm_test_socket_set_rx_size(&s_self->data_sock, DEFAULT_RCVBUF_SIZE);
    vstrm_test_socket_set_tx_size(&s_self->data_sock, tx_size *10);
    vstrm_test_socket_set_class(&s_self->data_sock, TOS_CS4);
    ULOGI("tx_size=%u",tx_size);

    /* Setup control socket */
    res = vstrm_test_socket_setup(&s_self->ctrl_sock, local_addr, &local_ctrl_port, remote_addr, remote_ctrl_port, s_self->loop, &socket_ctrl_cb,s_self);
    if(res)
        goto out;

    ULOGI("RTCP: address %s<->%s port %u<->%u",local_addr,remote_addr,local_ctrl_port,remote_ctrl_port);
    vstrm_test_socket_set_rx_size(&s_self->ctrl_sock, DEFAULT_RCVBUF_SIZE);
    vstrm_test_socket_set_tx_size(&s_self->ctrl_sock, DEFAULT_SNDBUF_SIZE);
    vstrm_test_socket_set_class(&s_self->ctrl_sock, TOS_CS4);

    /* Sender configuration */
    memset(&vstrm_cfg,0, sizeof(vstrm_cfg));
    vstrm_cfg.loop = s_self->loop;
    vstrm_cfg.flags = VSTRM_SENDER_FLAGS_ENABLE_RTP_HEADER_EXT |VSTRM_SENDER_FLAGS_ENABLE_RTCP | VSTRM_SENDER_FLAGS_ENABLE_RTCP_EXT;
    vstrm_cfg.codec = VSTRM_SENDER_CODEC_H264;//VSTRM_SENDER_CODEC_H264;
    vstrm_cfg.dyn.target_packet_size = DEFAULT_TARGET_PACKET_SIZE;
    vstrm_cfg.dyn.max_network_latency_ms[0] = MAX_NETWORK_LATENCY_CLASS_0;
    vstrm_cfg.dyn.max_network_latency_ms[1] = MAX_NETWORK_LATENCY_CLASS_1;
    vstrm_cfg.dyn.max_network_latency_ms[2] = MAX_NETWORK_LATENCY_CLASS_2;
    vstrm_cfg.dyn.max_network_latency_ms[3] = MAX_NETWORK_LATENCY_CLASS_3;

    /* Create sender */
    res = vstrm_sender_new(&vstrm_cfg,&vstrm_cbs,s_self,&s_self->sender);
    if(res < 0){
        ULOG_ERRNO("vstrm_sender_new", -res);
        goto out;
    }


    // hink sys init
    u32BlkSize = hink_sys_calcPicVbBlkSize(enNorm, PIC_HD1080, HINK_PIXEL_FORMAT, HINK_SYS_ALIGN_WIDTH, COMPRESS_MODE_SEG);
    hink_sys_init(u32BlkSize, 18);


    // hink vi init
    hink_vi_getParam(VI_MODE_8_1080P, &viParam);
    viChn = viParam.s32ViChnInterval *viDev;
    s32Ret = hink_vi_init(viDev, viChn, PIC_HD1080);

    // venc
    // vpssGrp: 0 0 1 1
    // vpssChn: 0 1 0 1
    // vencChn: 0 1 2 3
    for(i = 0; i < venc_ini.ch_num; i++){
        for(j = 0; j < venc_ini.st_num; j++){
            hink_venc_t venc = {
                .veChn = CODEC_VENC_NUM * i + j,
                .srcModId = HI_ID_VPSS,
                .vpssGrp = i,
                .vpssChn = (j < venc_ini.st_num) ? j : 0,
                .enPayLoad = PT_H264,
                .enSize = (j > 0) ? PIC_HD720 : PIC_HD1080,
                .enRcMode = HINK_CBR,
                .u32Profile = 0,
                .enGopMode = VENC_GOPMODE_NORMALP,
                .u32FrameRate = 60,
                .u32Gop = 30 * 2,
                .u32BitRate = 3* 1000,
                .enVidNorm = VIDEO_ENCODING_MODE_PAL,//VIDEO_ENCODING_MODE_PAL --> fr=25; VIDEO_ENCODING_MODE_NTSC --> fr= 30;
            };


            hink_venc_start(&venc);
        }
    }




#define VPSS(_i, _g, _ch, _en0, _en1, _sz0, _sz1) do{                   \
        stVpss[_i].vpssGrp=_g; stVpss[_i].viChn=_ch;                    \
        stVpss[_i].enable[0]=_en0;stVpss[_i].enable[1]=_en1;            \
        stVpss[_i].enSize[0]=_sz0;stVpss[_i].enSize[1]=_sz1;}while(0)

    VPSS(0,0,24,1,1,PIC_HD1080,PIC_HD720);

    mpp_vpss_start(&stVpss[0]);

    HINK_MPP_CHN_INIT(stSrcChn, HI_ID_VIU, viDev, viChn);
    HINK_MPP_CHN_INIT(stDestChn, HI_ID_VPSS, stVpss[0].vpssGrp, 0);
    hink_sys_bind(&stSrcChn, &stDestChn);

    HINK_MPP_CHN_INIT(stSrcChn, HI_ID_VPSS, stVpss[0].vpssGrp, 0);
    HINK_MPP_CHN_INIT(stDestChn, HI_ID_VENC, 0, 0);
    hink_sys_bind(&stSrcChn, &stDestChn);

    HINK_MPP_CHN_INIT(stSrcChn, HI_ID_VPSS, stVpss[0].vpssGrp, 1);
    HINK_MPP_CHN_INIT(stDestChn, HI_ID_VENC, 0, 1);
    hink_sys_bind(&stSrcChn, &stDestChn);

    pst = calloc(1, sizeof(*pst));
    if(pst == NULL){
        ULOG_ERRNO("pst calloc failed", ENOMEM);
        return -ENOMEM;
    }

    pst->s32Cnt = 2;
    pst->veChn[0] = 0;
    pst->veChn[1] = 1;
    pst->uargs = s_self->reader;
    pst->cb = mpp_venc_recv;
    hink_venc_recv(pst);

#if 0
    while(1){
        hi_usleep(20);
    }

    return 0;
#else




    while(!s_self->should_stop)
        pomp_loop_wait_and_process(s_self->loop, -1);
out:
    if(res < 0){
        /* vstrm_test_sender_destroy(self); */
        if(s_self != NULL){
            if(s_self->sender != NULL){
                res = vstrm_sender_destroy(s_self->sender);
                if(res < 0)
                    ULOG_ERRNO("vstrm_sender_destroy", -res);
            }

            vstrm_test_socket_cleanup(&s_self->data_sock, s_self->loop);
            vstrm_test_socket_cleanup(&s_self->ctrl_sock, s_self->loop);
            if(s_self->reader!= NULL){
                res = h264_reader_destroy(s_self->reader);
                if(res < 0)
                    ULOG_ERRNO("h264_reader_destroy", -res);
            }

            if(s_self->loop != NULL){
                res = pomp_loop_destroy(s_self->loop);
                if(res < 0)
                    ULOG_ERRNO("pomp_loop_destroy", -res);
            }

            if(s_self->sps)
                free(s_self->sps);
            if(s_self->pps)
                free(s_self->pps);
            free(s_self);
            if(pst)
                free(pst);
        }

        s_self= NULL;
    }
    return res;


#endif



}
